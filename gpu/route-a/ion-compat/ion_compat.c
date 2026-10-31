// SPDX-License-Identifier: GPL-2.0
/*
 * Legacy Android ION (/dev/ion) for the stock Allwinner A31 Android 4.4 user space on
 * mainline Linux, which removed ION in 5.11.
 *
 * Implements the Android 3.4-era ION ABI the stock gralloc uses (ioctl type 'I'):
 *   0 ALLOC  struct { size_t len, align; u32 heap_mask, flags; handle }   (20 bytes on arm)
 *   1 FREE   struct { handle }
 *   2 MAP, 4 SHARE  struct { handle; int fd } -> dma-buf fd of the buffer
 *   5 IMPORT struct { handle; int fd } <- dma-buf fd, returns a handle
 *   6 CUSTOM struct { u32 cmd; unsigned long arg }  (logged, -ENOTTY for now)
 *   7 SYNC   struct { handle; int fd }   (buffers are write-combined: nothing to do)
 * Handles are small per-client integers. Every buffer is a dma-buf backed by physically
 * contiguous write-combined memory from the CMA area, whatever heap is asked for.
 * (/dev/pvr_sync, the PowerVR native fence device, is provided by pvrsrvkm.)
 */
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

MODULE_IMPORT_NS("DMA_BUF");

#define ION_IOC_MAGIC	'I'

struct ion_allocation_data {
	__u32 len;		/* size_t on 32-bit arm */
	__u32 align;
	__u32 heap_mask;
	__u32 flags;
	__s32 handle;
};

struct ion_fd_data {
	__s32 handle;
	__s32 fd;
};

struct ion_handle_data {
	__s32 handle;
};

struct ion_custom_data {
	__u32 cmd;
	__u32 arg;
};

#define ION_IOC_ALLOC	_IOWR(ION_IOC_MAGIC, 0, struct ion_allocation_data)
#define ION_IOC_FREE	_IOWR(ION_IOC_MAGIC, 1, struct ion_handle_data)
#define ION_IOC_MAP	_IOWR(ION_IOC_MAGIC, 2, struct ion_fd_data)
#define ION_IOC_SHARE	_IOWR(ION_IOC_MAGIC, 4, struct ion_fd_data)
#define ION_IOC_IMPORT	_IOWR(ION_IOC_MAGIC, 5, struct ion_fd_data)
#define ION_IOC_CUSTOM	_IOWR(ION_IOC_MAGIC, 6, struct ion_custom_data)
#define ION_IOC_SYNC	_IOWR(ION_IOC_MAGIC, 7, struct ion_fd_data)

static struct miscdevice ion_compat_dev;

struct ion_compat_buffer {
	size_t size;
	void *vaddr;
	dma_addr_t dma;
};

struct ion_compat_client {
	struct mutex lock;
	struct idr handles;	/* handle -> struct dma_buf * (one reference each) */
};

/* --- dma-buf exporter ---------------------------------------------------------------- */

static struct device *ion_dev(void)
{
	return ion_compat_dev.this_device;
}

static struct sg_table *ion_compat_map_dma_buf(struct dma_buf_attachment *attach,
					       enum dma_data_direction dir)
{
	struct ion_compat_buffer *buf = attach->dmabuf->priv;
	struct sg_table *sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	int ret;

	if (!sgt)
		return ERR_PTR(-ENOMEM);
	ret = dma_get_sgtable(ion_dev(), sgt, buf->vaddr, buf->dma, buf->size);
	if (!ret)
		ret = dma_map_sgtable(attach->dev, sgt, dir, DMA_ATTR_SKIP_CPU_SYNC);
	if (ret) {
		sg_free_table(sgt);
		kfree(sgt);
		return ERR_PTR(ret);
	}
	return sgt;
}

static void ion_compat_unmap_dma_buf(struct dma_buf_attachment *attach, struct sg_table *sgt,
				     enum dma_data_direction dir)
{
	dma_unmap_sgtable(attach->dev, sgt, dir, DMA_ATTR_SKIP_CPU_SYNC);
	sg_free_table(sgt);
	kfree(sgt);
}

static void ion_compat_release(struct dma_buf *dmabuf)
{
	struct ion_compat_buffer *buf = dmabuf->priv;

	dma_free_wc(ion_dev(), buf->size, buf->vaddr, buf->dma);
	kfree(buf);
}

static int ion_compat_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct ion_compat_buffer *buf = dmabuf->priv;

	return dma_mmap_wc(ion_dev(), vma, buf->vaddr, buf->dma, buf->size);
}

static int ion_compat_vmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct ion_compat_buffer *buf = dmabuf->priv;

	iosys_map_set_vaddr(map, buf->vaddr);
	return 0;
}

static const struct dma_buf_ops ion_compat_dmabuf_ops = {
	.map_dma_buf	= ion_compat_map_dma_buf,
	.unmap_dma_buf	= ion_compat_unmap_dma_buf,
	.release	= ion_compat_release,
	.mmap		= ion_compat_mmap,
	.vmap		= ion_compat_vmap,
};

static struct dma_buf *ion_compat_alloc(size_t len)
{
	DEFINE_DMA_BUF_EXPORT_INFO(info);
	struct ion_compat_buffer *buf;
	struct dma_buf *dmabuf;

	len = PAGE_ALIGN(len);
	if (!len)
		return ERR_PTR(-EINVAL);
	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);
	buf->size = len;
	buf->vaddr = dma_alloc_wc(ion_dev(), len, &buf->dma, GFP_KERNEL);
	if (!buf->vaddr) {
		kfree(buf);
		return ERR_PTR(-ENOMEM);
	}
	info.ops = &ion_compat_dmabuf_ops;
	info.size = len;
	info.flags = O_RDWR;
	info.priv = buf;
	dmabuf = dma_buf_export(&info);
	if (IS_ERR(dmabuf)) {
		dma_free_wc(ion_dev(), len, buf->vaddr, buf->dma);
		kfree(buf);
	}
	return dmabuf;
}

/* --- clients and handles --------------------------------------------------------------- */

static int ion_compat_add_handle(struct ion_compat_client *c, struct dma_buf *dmabuf)
{
	int id;

	mutex_lock(&c->lock);
	id = idr_alloc(&c->handles, dmabuf, 1, 0, GFP_KERNEL);
	mutex_unlock(&c->lock);
	return id;
}

static struct dma_buf *ion_compat_get_handle(struct ion_compat_client *c, int id)
{
	struct dma_buf *dmabuf;

	mutex_lock(&c->lock);
	dmabuf = idr_find(&c->handles, id);
	if (dmabuf)
		get_dma_buf(dmabuf);
	mutex_unlock(&c->lock);
	return dmabuf;
}

static long ion_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ion_compat_client *c = file->private_data;
	void __user *uarg = (void __user *)arg;
	struct dma_buf *dmabuf;

	switch (cmd) {
	case ION_IOC_ALLOC: {
		struct ion_allocation_data d;
		int id;

		if (copy_from_user(&d, uarg, sizeof(d)))
			return -EFAULT;
		dmabuf = ion_compat_alloc(d.len);
		if (IS_ERR(dmabuf))
			return PTR_ERR(dmabuf);
		id = ion_compat_add_handle(c, dmabuf);
		if (id < 0) {
			dma_buf_put(dmabuf);
			return id;
		}
		d.handle = id;
		return copy_to_user(uarg, &d, sizeof(d)) ? -EFAULT : 0;
	}
	case ION_IOC_FREE: {
		struct ion_handle_data d;

		if (copy_from_user(&d, uarg, sizeof(d)))
			return -EFAULT;
		mutex_lock(&c->lock);
		dmabuf = idr_remove(&c->handles, d.handle);
		mutex_unlock(&c->lock);
		if (!dmabuf)
			return -EINVAL;
		dma_buf_put(dmabuf);
		return 0;
	}
	case ION_IOC_MAP:
	case ION_IOC_SHARE: {
		struct ion_fd_data d;

		if (copy_from_user(&d, uarg, sizeof(d)))
			return -EFAULT;
		dmabuf = ion_compat_get_handle(c, d.handle);
		if (!dmabuf)
			return -EINVAL;
		d.fd = dma_buf_fd(dmabuf, O_CLOEXEC);
		if (d.fd < 0) {
			dma_buf_put(dmabuf);
			return d.fd;
		}
		return copy_to_user(uarg, &d, sizeof(d)) ? -EFAULT : 0;
	}
	case ION_IOC_IMPORT: {
		struct ion_fd_data d;
		int id;

		if (copy_from_user(&d, uarg, sizeof(d)))
			return -EFAULT;
		dmabuf = dma_buf_get(d.fd);
		if (IS_ERR(dmabuf))
			return PTR_ERR(dmabuf);
		id = ion_compat_add_handle(c, dmabuf);
		if (id < 0) {
			dma_buf_put(dmabuf);
			return id;
		}
		d.handle = id;
		return copy_to_user(uarg, &d, sizeof(d)) ? -EFAULT : 0;
	}
	case ION_IOC_SYNC:
		return 0;
	default: {
		u32 words[4] = { 0 };

		if ((_IOC_DIR(cmd) & _IOC_WRITE) && _IOC_SIZE(cmd))
			if (copy_from_user(words, uarg, min_t(unsigned int, _IOC_SIZE(cmd), sizeof(words))))
				return -EFAULT;
		pr_info("ion-compat: unhandled ioctl %08x (nr %u size %u) args %08x %08x %08x %08x\n",
			cmd, _IOC_NR(cmd), _IOC_SIZE(cmd), words[0], words[1], words[2], words[3]);
		return -ENOTTY;
	}
	}
}

static int ion_compat_open(struct inode *inode, struct file *file)
{
	struct ion_compat_client *c = kzalloc(sizeof(*c), GFP_KERNEL);

	if (!c)
		return -ENOMEM;
	mutex_init(&c->lock);
	idr_init(&c->handles);
	file->private_data = c;
	return 0;
}

static int ion_compat_put_one(int id, void *p, void *data)
{
	dma_buf_put(p);
	return 0;
}

static int ion_compat_close(struct inode *inode, struct file *file)
{
	struct ion_compat_client *c = file->private_data;

	idr_for_each(&c->handles, ion_compat_put_one, NULL);
	idr_destroy(&c->handles);
	kfree(c);
	return 0;
}

static const struct file_operations ion_compat_fops = {
	.owner		= THIS_MODULE,
	.open		= ion_compat_open,
	.release	= ion_compat_close,
	.unlocked_ioctl	= ion_compat_ioctl,
};

static struct miscdevice ion_compat_dev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "ion",
	.fops	= &ion_compat_fops,
	.mode	= 0666,
};

static int __init ion_compat_init(void)
{
	int ret = misc_register(&ion_compat_dev);

	if (ret)
		return ret;
	/* the misc device allocates the buffers: give it DMA capability (no IOMMU on the A31) */
	ret = dma_coerce_mask_and_coherent(ion_compat_dev.this_device, DMA_BIT_MASK(32));
	if (ret)
		misc_deregister(&ion_compat_dev);
	return ret;
}

static void __exit ion_compat_exit(void)
{
	misc_deregister(&ion_compat_dev);
}

module_init(ion_compat_init);
module_exit(ion_compat_exit);
MODULE_DESCRIPTION("Legacy Android ION interface for the stock Allwinner A31 user space");
MODULE_LICENSE("GPL");
