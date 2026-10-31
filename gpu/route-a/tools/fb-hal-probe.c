// SPDX-License-Identifier: MIT
/*
 * Probe the stock gralloc HAL's allocator device ("gpu0"): allocate a 1024x768
 * BGRA_8888 (format 5) or RGBA_8888 (format 1) buffer with HW_RENDER | HW_FB,
 * inspect the returned native_handle_t (fds and ints), lock/unlock it, and free it.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

struct hw_module_t;
struct hw_device_t;

struct hw_module_methods_t {
	int (*open)(const struct hw_module_t *module, const char *id, struct hw_device_t **device);
};

struct hw_module_t {
	uint32_t tag;
	uint16_t module_api_version;
	uint16_t hal_api_version;
	const char *id;
	const char *name;
	const char *author;
	struct hw_module_methods_t *methods;
	void *dso;
	uint32_t reserved[32 - 7];
};

struct hw_device_t {
	uint32_t tag;
	uint32_t version;
	struct hw_module_t *module;
	uint32_t reserved[12];
	int (*close)(struct hw_device_t *device);
};

typedef struct native_handle {
	int version;
	int numFds;
	int numInts;
	int data[];
} native_handle_t;

typedef const native_handle_t *buffer_handle_t;

struct gralloc_module_t {
	struct hw_module_t common;
	int (*registerBuffer)(const struct gralloc_module_t *module, buffer_handle_t handle);
	int (*unregisterBuffer)(const struct gralloc_module_t *module, buffer_handle_t handle);
	int (*lock)(const struct gralloc_module_t *module, buffer_handle_t handle, int usage,
		    int l, int t, int w, int h, void **vaddr);
	int (*unlock)(const struct gralloc_module_t *module, buffer_handle_t handle);
	int (*perform)(const struct gralloc_module_t *module, int operation, ...);
	void *reserved_proc[7];
};

struct alloc_device_t {
	struct hw_device_t common;
	int (*alloc)(struct alloc_device_t *dev, int w, int h, int format, int usage,
		     buffer_handle_t *handle, int *stride);
	int (*free)(struct alloc_device_t *dev, buffer_handle_t handle);
	void (*dump)(struct alloc_device_t *dev, char *buff, int buff_len);
	void *reserved_proc[7];
};

enum {
	HAL_PIXEL_FORMAT_RGBA_8888 = 1,
	HAL_PIXEL_FORMAT_RGBX_8888 = 2,
	HAL_PIXEL_FORMAT_RGB_565   = 4,
	HAL_PIXEL_FORMAT_BGRA_8888 = 5,
};

enum {
	GRALLOC_USAGE_SW_READ_OFTEN  = 0x00000003,
	GRALLOC_USAGE_SW_WRITE_OFTEN = 0x00000030,
	GRALLOC_USAGE_HW_TEXTURE     = 0x00000100,
	GRALLOC_USAGE_HW_RENDER      = 0x00000200,
	GRALLOC_USAGE_HW_COMPOSER    = 0x00000800,
	GRALLOC_USAGE_HW_FB          = 0x00001000,
};

int hw_get_module(const char *id, const struct hw_module_t **module);

int main(void)
{
	const struct hw_module_t *mod = NULL;
	const struct gralloc_module_t *gmod;
	struct hw_device_t *dev = NULL;
	struct alloc_device_t *alloc;
	buffer_handle_t handle = NULL;
	int stride = 0, ret, i;
	void *vaddr = NULL;

	setvbuf(stdout, NULL, _IONBF, 0);
	ret = hw_get_module("gralloc", &mod);
	if (ret) {
		printf("hw_get_module(gralloc) failed: %d\n", ret);
		return 1;
	}
	gmod = (const struct gralloc_module_t *)mod;
	printf("gralloc module: id '%s' name '%s' author '%s'\n", mod->id, mod->name, mod->author);

	ret = mod->methods->open(mod, "gpu0", &dev);
	if (ret || !dev) {
		printf("open(gpu0) failed: %d dev=%p\n", ret, (void *)dev);
		return 1;
	}
	alloc = (struct alloc_device_t *)dev;

	ret = alloc->alloc(alloc, 1024, 768, HAL_PIXEL_FORMAT_BGRA_8888,
			   GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_FB | GRALLOC_USAGE_SW_READ_OFTEN,
			   &handle, &stride);
	printf("alloc(1024x768 BGRA_8888) = %d, handle=%p stride=%d\n", ret, (const void *)handle, stride);
	if (ret || !handle)
		return 1;

	printf("native_handle: version=%d numFds=%d numInts=%d\n",
	       handle->version, handle->numFds, handle->numInts);
	for (i = 0; i < handle->numFds; i++) {
		char path[64], target[128];
		ssize_t n;
		snprintf(path, sizeof(path), "/proc/self/fd/%d", handle->data[i]);
		n = readlink(path, target, sizeof(target) - 1);
		target[n > 0 ? n : 0] = '\0';
		printf("  fd[%d] = %d -> %s\n", i, handle->data[i], target);
	}
	for (i = 0; i < handle->numInts; i++)
		printf("  int[%2d] = 0x%08x (%d)\n", i,
		       (unsigned)handle->data[handle->numFds + i], handle->data[handle->numFds + i]);

	ret = gmod->lock(gmod, handle, GRALLOC_USAGE_SW_READ_OFTEN, 0, 0, 1024, 768, &vaddr);
	printf("lock() = %d, vaddr=%p\n", ret, vaddr);
	if (!ret) {
		ret = gmod->unlock(gmod, handle);
		printf("unlock() = %d\n", ret);
	}

	ret = alloc->free(alloc, handle);
	printf("free() = %d\n", ret);
	dev->close(dev);
	return 0;
}
