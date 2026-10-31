// SPDX-License-Identifier: MIT
/*
 * libgbm on Android gralloc (through libhybris) for weston's DRM backend on the stock Allwinner
 * DDK 1.12 GPU libraries of the Exper T8E.
 *
 * A gbm_surface is an Android native window whose buffers are gralloc "gpu0" allocations. The
 * stock gralloc backs them with dma-bufs (native_handle data[0]) that are imported into the KMS
 * device with PRIME, so a buffer the GPU rendered can be scanned out directly:
 *   eglSwapBuffers -> queueBuffer (waits for the render fence) -> the buffer becomes the front
 *   buffer -> gbm_surface_lock_front_buffer hands it to weston -> gbm_surface_release_buffer.
 * gbm_bo_create makes KMS dumb buffers (cursor planes). Importing foreign buffers is not supported.
 *
 * The stock EGL reports Android HAL pixel formats as EGL_NATIVE_VISUAL_ID, while GBM users look
 * for DRM fourccs: eglGetConfigAttrib is interposed to translate, and EGL_PLATFORM_GBM_KHR displays
 * become displays of the libhybris "gbm" platform (preload this library).
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>
#include <hardware/gralloc.h>
#include <system/window.h>
#include <hybris/gralloc/gralloc.h>

#include "gbm.h"

void *android_dlopen(const char *filename, int flag);
void *android_dlsym(void *handle, const char *symbol);

#define NUM_BUFFERS	4

struct gbm_device {
	int fd;
};

struct gbm_bo {
	struct gbm_device *dev;
	uint32_t width, height, stride, format, handle;
	void *user_data;
	void (*destroy_user_data)(struct gbm_bo *, void *);
	/* dumb buffers only */
	uint64_t size;
};

enum buf_state { BUF_FREE, BUF_DEQUEUED, BUF_QUEUED, BUF_LOCKED };

struct surface_buffer {
	ANativeWindowBuffer_t anb;	/* first: the window hands out &anb */
	struct gbm_surface *surf;
	enum buf_state state;
	unsigned int seq;		/* queue order */
	struct gbm_bo bo;		/* bo.handle == 0: not allocated */
};

struct gbm_surface {
	ANativeWindow win;		/* first: libhybris gets &win */
	struct gbm_device *dev;
	pthread_mutex_t lock;
	uint32_t width, height, drm_format;
	int hal_format, usage;
	unsigned int seq;
	struct surface_buffer bufs[NUM_BUFFERS];
};

static void log_err(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fputs("gbm-hybris: ", stderr);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

/* DRM fourcc <-> Android HAL pixel format (byte order in memory, little endian) */
static int drm_to_hal(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_ARGB8888:
		return HAL_PIXEL_FORMAT_BGRA_8888;
	case DRM_FORMAT_XBGR8888:
		return HAL_PIXEL_FORMAT_RGBX_8888;
	case DRM_FORMAT_ABGR8888:
		return HAL_PIXEL_FORMAT_RGBA_8888;
	case DRM_FORMAT_RGB565:
		return HAL_PIXEL_FORMAT_RGB_565;
	}
	return -1;
}

static uint32_t hal_to_drm(int hal, uint32_t wanted)
{
	switch (hal) {
	case HAL_PIXEL_FORMAT_BGRA_8888:
		return wanted == DRM_FORMAT_ARGB8888 ? DRM_FORMAT_ARGB8888 : DRM_FORMAT_XRGB8888;
	case HAL_PIXEL_FORMAT_RGBA_8888:
		return wanted == DRM_FORMAT_ABGR8888 ? DRM_FORMAT_ABGR8888 : DRM_FORMAT_XBGR8888;
	case HAL_PIXEL_FORMAT_RGBX_8888:
		return DRM_FORMAT_XBGR8888;
	case HAL_PIXEL_FORMAT_RGB_565:
		return DRM_FORMAT_RGB565;
	}
	return 0;
}

static void gem_close(int fd, uint32_t handle)
{
	struct drm_gem_close c = { .handle = handle };

	if (handle)
		ioctl(fd, DRM_IOCTL_GEM_CLOSE, &c);
}

/* ---- device ---- */

struct gbm_device *gbm_create_device(int fd)
{
	struct gbm_device *dev = calloc(1, sizeof(*dev));

	if (!dev)
		return NULL;
	dev->fd = fd;
	hybris_gralloc_initialize(0);
	return dev;
}

void gbm_device_destroy(struct gbm_device *dev)
{
	free(dev);
}

int gbm_device_get_fd(struct gbm_device *dev)
{
	return dev->fd;
}

const char *gbm_device_get_backend_name(struct gbm_device *dev)
{
	(void)dev;
	return "hybris";
}

int gbm_device_is_format_supported(struct gbm_device *dev, uint32_t format, uint32_t flags)
{
	(void)dev;
	(void)flags;
	return drm_to_hal(format) >= 0;
}

/* ---- buffer objects ---- */

struct gbm_bo *gbm_bo_create(struct gbm_device *dev, uint32_t width, uint32_t height, uint32_t format,
			     uint32_t flags)
{
	struct drm_mode_create_dumb c = { .width = width, .height = height, .bpp = 32 };
	struct gbm_bo *bo;

	(void)flags;
	if (format == DRM_FORMAT_RGB565)
		c.bpp = 16;
	else if (drm_to_hal(format) < 0) {
		errno = EINVAL;
		return NULL;
	}
	if (ioctl(dev->fd, DRM_IOCTL_MODE_CREATE_DUMB, &c) < 0)
		return NULL;
	bo = calloc(1, sizeof(*bo));
	if (!bo) {
		gem_close(dev->fd, c.handle);
		return NULL;
	}
	bo->dev = dev;
	bo->width = width;
	bo->height = height;
	bo->stride = c.pitch;
	bo->format = format;
	bo->handle = c.handle;
	bo->size = c.size;
	return bo;
}

struct gbm_bo *gbm_bo_import(struct gbm_device *dev, uint32_t type, void *buffer, uint32_t flags)
{
	(void)dev;
	(void)type;
	(void)buffer;
	(void)flags;
	errno = ENOSYS;
	return NULL;
}

uint32_t gbm_bo_get_width(struct gbm_bo *bo) { return bo->width; }
uint32_t gbm_bo_get_height(struct gbm_bo *bo) { return bo->height; }
uint32_t gbm_bo_get_stride(struct gbm_bo *bo) { return bo->stride; }
uint32_t gbm_bo_get_format(struct gbm_bo *bo) { return bo->format; }
uint32_t gbm_bo_get_bpp(struct gbm_bo *bo) { return bo->format == DRM_FORMAT_RGB565 ? 16 : 32; }
uint32_t gbm_bo_get_offset(struct gbm_bo *bo, int plane) { (void)bo; (void)plane; return 0; }
struct gbm_device *gbm_bo_get_device(struct gbm_bo *bo) { return bo->dev; }
int gbm_bo_get_plane_count(struct gbm_bo *bo) { (void)bo; return 1; }
uint64_t gbm_bo_get_modifier(struct gbm_bo *bo) { (void)bo; return DRM_FORMAT_MOD_INVALID; }

uint32_t gbm_bo_get_stride_for_plane(struct gbm_bo *bo, int plane)
{
	return plane == 0 ? bo->stride : 0;
}

union gbm_bo_handle gbm_bo_get_handle(struct gbm_bo *bo)
{
	union gbm_bo_handle h = { .u64 = 0 };

	h.u32 = bo->handle;
	return h;
}

union gbm_bo_handle gbm_bo_get_handle_for_plane(struct gbm_bo *bo, int plane)
{
	union gbm_bo_handle h = { .u64 = 0 };

	if (plane == 0)
		h.u32 = bo->handle;
	return h;
}

int gbm_bo_get_fd(struct gbm_bo *bo)
{
	struct drm_prime_handle p = { .handle = bo->handle, .flags = DRM_CLOEXEC | DRM_RDWR };

	return ioctl(bo->dev->fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &p) < 0 ? -1 : p.fd;
}

int gbm_bo_write(struct gbm_bo *bo, const void *buf, size_t count)
{
	struct drm_mode_map_dumb m = { .handle = bo->handle };
	void *p;

	if (!bo->size || count > bo->size) {
		errno = EINVAL;
		return -1;
	}
	if (ioctl(bo->dev->fd, DRM_IOCTL_MODE_MAP_DUMB, &m) < 0)
		return -1;
	p = mmap(NULL, bo->size, PROT_WRITE, MAP_SHARED, bo->dev->fd, m.offset);
	if (p == MAP_FAILED)
		return -1;
	memcpy(p, buf, count);
	munmap(p, bo->size);
	return 0;
}

void gbm_bo_set_user_data(struct gbm_bo *bo, void *data, void (*destroy_user_data)(struct gbm_bo *, void *))
{
	bo->user_data = data;
	bo->destroy_user_data = destroy_user_data;
}

void *gbm_bo_get_user_data(struct gbm_bo *bo)
{
	return bo->user_data;
}

static void bo_drop_user_data(struct gbm_bo *bo)
{
	if (bo->destroy_user_data)
		bo->destroy_user_data(bo, bo->user_data);
	bo->user_data = NULL;
	bo->destroy_user_data = NULL;
}

void gbm_bo_destroy(struct gbm_bo *bo)
{
	struct drm_mode_destroy_dumb d = { .handle = bo->handle };

	if (!bo->size) {
		log_err("gbm_bo_destroy on a surface buffer ignored\n");
		return;
	}
	bo_drop_user_data(bo);
	ioctl(bo->dev->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
	free(bo);
}

/* ---- surface buffers ---- */

static void buffer_free(struct surface_buffer *b)
{
	if (!b->bo.handle)
		return;
	bo_drop_user_data(&b->bo);
	gem_close(b->surf->dev->fd, b->bo.handle);
	hybris_gralloc_release(b->anb.handle, 1);
	b->bo.handle = 0;
	b->anb.handle = NULL;
}

static int buffer_alloc(struct gbm_surface *s, struct surface_buffer *b)
{
	struct drm_prime_handle p = { 0 };
	buffer_handle_t handle = NULL;
	uint32_t stride = 0;
	int usage = s->usage | GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_FB;

	if (hybris_gralloc_allocate(s->width, s->height, s->hal_format, usage, &handle, &stride) || !handle) {
		log_err("gralloc allocation %ux%u format %d failed\n", s->width, s->height, s->hal_format);
		return -ENOMEM;
	}
	/* the stock gralloc puts the buffer's dma-buf first */
	p.fd = handle->numFds > 0 ? handle->data[0] : -1;
	if (p.fd < 0 || ioctl(s->dev->fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &p) < 0) {
		log_err("PRIME import of the gralloc buffer failed: %s\n", strerror(errno));
		hybris_gralloc_release(handle, 1);
		return -EINVAL;
	}
	b->anb.width = s->width;
	b->anb.height = s->height;
	b->anb.stride = stride;
	b->anb.format = s->hal_format;
	b->anb.usage = usage;
	b->anb.handle = handle;
	b->bo.dev = s->dev;
	b->bo.width = s->width;
	b->bo.height = s->height;
	b->bo.stride = stride * (s->hal_format == HAL_PIXEL_FORMAT_RGB_565 ? 2 : 4);
	b->bo.format = hal_to_drm(s->hal_format, s->drm_format);
	b->bo.handle = p.handle;
	if (getenv("GBM_HYBRIS_DEBUG"))
		log_err("buffer %ux%u hal format %d (%.4s) stride %u px usage %#x gem %u\n", s->width, s->height,
			s->hal_format, (char *)&b->bo.format, stride, usage, p.handle);
	return 0;
}

static struct gbm_surface *win_surface(const struct ANativeWindow *win)
{
	return (struct gbm_surface *)win;
}

static void base_ref(struct android_native_base_t *base) { (void)base; }

static int win_set_swap_interval(struct ANativeWindow *win, int interval)
{
	(void)win;
	(void)interval;
	return 0;
}

static int win_dequeue(struct ANativeWindow *win, struct ANativeWindowBuffer **out, int *fence)
{
	struct gbm_surface *s = win_surface(win);
	struct surface_buffer *b = NULL;
	int i, ret = 0;

	pthread_mutex_lock(&s->lock);
	for (i = 0; i < NUM_BUFFERS && !b; i++)
		if (s->bufs[i].state == BUF_FREE)
			b = &s->bufs[i];
	if (!b) {
		log_err("no free buffer (all queued or held by the compositor)\n");
		ret = -EBUSY;
		goto out;
	}
	/* (re)allocate on first use or after a format/size/usage change */
	if (b->bo.handle && (b->anb.format != s->hal_format || b->anb.width != (int)s->width ||
			     b->anb.height != (int)s->height ||
			     (b->anb.usage & s->usage) != s->usage))
		buffer_free(b);
	if (!b->bo.handle && (ret = buffer_alloc(s, b)))
		goto out;
	b->state = BUF_DEQUEUED;
	*out = &b->anb;
	*fence = -1;
out:
	pthread_mutex_unlock(&s->lock);
	return ret;
}

static int win_queue(struct ANativeWindow *win, struct ANativeWindowBuffer *anb, int fence)
{
	struct gbm_surface *s = win_surface(win);
	struct surface_buffer *b = (struct surface_buffer *)anb;

	/* scan-out must not start before the GPU is done */
	if (fence >= 0) {
		struct pollfd pfd = { .fd = fence, .events = POLLIN };

		if (poll(&pfd, 1, 3000) <= 0)
			log_err("render fence did not signal\n");
		close(fence);
	}
	pthread_mutex_lock(&s->lock);
	b->state = BUF_QUEUED;
	b->seq = ++s->seq;
	pthread_mutex_unlock(&s->lock);
	return 0;
}

static int win_cancel(struct ANativeWindow *win, struct ANativeWindowBuffer *anb, int fence)
{
	struct gbm_surface *s = win_surface(win);

	if (fence >= 0)
		close(fence);
	pthread_mutex_lock(&s->lock);
	((struct surface_buffer *)anb)->state = BUF_FREE;
	pthread_mutex_unlock(&s->lock);
	return 0;
}

static int win_dequeue_dep(struct ANativeWindow *win, struct ANativeWindowBuffer **out)
{
	int fence;

	return win_dequeue(win, out, &fence);
}

static int win_lock_dep(struct ANativeWindow *win, struct ANativeWindowBuffer *anb)
{
	(void)win;
	(void)anb;
	return 0;
}

static int win_queue_dep(struct ANativeWindow *win, struct ANativeWindowBuffer *anb)
{
	return win_queue(win, anb, -1);
}

static int win_cancel_dep(struct ANativeWindow *win, struct ANativeWindowBuffer *anb)
{
	return win_cancel(win, anb, -1);
}

static int win_query(const struct ANativeWindow *win, int what, int *value)
{
	struct gbm_surface *s = win_surface(win);

	switch (what) {
	case NATIVE_WINDOW_WIDTH:
	case NATIVE_WINDOW_DEFAULT_WIDTH:
		*value = s->width;
		return 0;
	case NATIVE_WINDOW_HEIGHT:
	case NATIVE_WINDOW_DEFAULT_HEIGHT:
		*value = s->height;
		return 0;
	case NATIVE_WINDOW_FORMAT:
		*value = s->hal_format;
		return 0;
	case NATIVE_WINDOW_MIN_UNDEQUEUED_BUFFERS:
		*value = 1;
		return 0;
	case NATIVE_WINDOW_CONCRETE_TYPE:
		*value = NATIVE_WINDOW_SURFACE;
		return 0;
	case NATIVE_WINDOW_QUEUES_TO_WINDOW_COMPOSER:
	case NATIVE_WINDOW_TRANSFORM_HINT:
	case NATIVE_WINDOW_CONSUMER_RUNNING_BEHIND:
		*value = 0;
		return 0;
	}
	return -EINVAL;
}

static int win_perform(struct ANativeWindow *win, int op, ...)
{
	struct gbm_surface *s = win_surface(win);
	va_list ap;
	int v;

	va_start(ap, op);
	switch (op) {
	case NATIVE_WINDOW_SET_USAGE:
		s->usage = va_arg(ap, int);
		break;
	case NATIVE_WINDOW_SET_BUFFERS_FORMAT:
		/*
		 * Keep the format the surface was created with: the stock EGL asks for RGBA_8888 even for
		 * BGRA configs, and the display may not scan out the byte order it implies (sun4i has no
		 * XBGR8888). The GPU renders into whatever layout the buffer has.
		 */
		v = va_arg(ap, int);
		(void)v;
		break;
	default:
		break;	/* geometry, crop, transform, timestamps, connect: nothing to do */
	}
	va_end(ap);
	return 0;
}

struct gbm_surface *gbm_surface_create(struct gbm_device *dev, uint32_t width, uint32_t height, uint32_t format,
				       uint32_t flags)
{
	struct gbm_surface *s;
	int hal = drm_to_hal(format), i;

	(void)flags;
	if (hal < 0) {
		errno = EINVAL;
		return NULL;
	}
	s = calloc(1, sizeof(*s));
	if (!s)
		return NULL;
	s->dev = dev;
	s->width = width;
	s->height = height;
	s->drm_format = format;
	s->hal_format = hal;
	pthread_mutex_init(&s->lock, NULL);

	s->win.common.magic = ANDROID_NATIVE_WINDOW_MAGIC;
	s->win.common.version = sizeof(ANativeWindow);
	s->win.common.incRef = base_ref;
	s->win.common.decRef = base_ref;
	/* const members in the Android header */
	*(int *)&s->win.minSwapInterval = 0;
	*(int *)&s->win.maxSwapInterval = 1;
	*(float *)&s->win.xdpi = *(float *)&s->win.ydpi = 160.0f;
	s->win.setSwapInterval = win_set_swap_interval;
	s->win.dequeueBuffer_DEPRECATED = win_dequeue_dep;
	s->win.lockBuffer_DEPRECATED = win_lock_dep;
	s->win.queueBuffer_DEPRECATED = win_queue_dep;
	s->win.query = win_query;
	s->win.perform = win_perform;
	s->win.cancelBuffer_DEPRECATED = win_cancel_dep;
	s->win.dequeueBuffer = win_dequeue;
	s->win.queueBuffer = win_queue;
	s->win.cancelBuffer = win_cancel;

	for (i = 0; i < NUM_BUFFERS; i++) {
		struct surface_buffer *b = &s->bufs[i];

		b->surf = s;
		b->anb.common.magic = ANDROID_NATIVE_BUFFER_MAGIC;
		b->anb.common.version = sizeof(ANativeWindowBuffer_t);
		b->anb.common.incRef = base_ref;
		b->anb.common.decRef = base_ref;
	}
	return s;
}

struct gbm_surface *gbm_surface_create_with_modifiers(struct gbm_device *dev, uint32_t width, uint32_t height,
						      uint32_t format, const uint64_t *modifiers, const unsigned int count)
{
	unsigned int i;

	/* only linear buffers; weston falls back to gbm_surface_create when this fails */
	for (i = 0; i < count; i++)
		if (modifiers[i] == DRM_FORMAT_MOD_LINEAR)
			return gbm_surface_create(dev, width, height, format, 0);
	errno = ENOSYS;
	return NULL;
}

void *gbm_hybris_surface_get_native_window(struct gbm_surface *s)
{
	return &s->win;
}

struct gbm_bo *gbm_surface_lock_front_buffer(struct gbm_surface *s)
{
	struct surface_buffer *front = NULL;
	int i;

	pthread_mutex_lock(&s->lock);
	for (i = 0; i < NUM_BUFFERS; i++) {
		struct surface_buffer *b = &s->bufs[i];

		if (b->state == BUF_QUEUED && (!front || b->seq > front->seq))
			front = b;
	}
	/* older queued frames were never shown: recycle them */
	for (i = 0; i < NUM_BUFFERS; i++)
		if (s->bufs[i].state == BUF_QUEUED && &s->bufs[i] != front)
			s->bufs[i].state = BUF_FREE;
	if (front)
		front->state = BUF_LOCKED;
	pthread_mutex_unlock(&s->lock);
	return front ? &front->bo : NULL;
}

void gbm_surface_release_buffer(struct gbm_surface *s, struct gbm_bo *bo)
{
	struct surface_buffer *b = (struct surface_buffer *)((char *)bo - offsetof(struct surface_buffer, bo));

	pthread_mutex_lock(&s->lock);
	if (b->state == BUF_LOCKED)
		b->state = BUF_FREE;
	pthread_mutex_unlock(&s->lock);
}

int gbm_surface_has_free_buffers(struct gbm_surface *s)
{
	int i, n = 0;

	pthread_mutex_lock(&s->lock);
	for (i = 0; i < NUM_BUFFERS; i++)
		n += s->bufs[i].state == BUF_FREE || s->bufs[i].state == BUF_QUEUED;
	pthread_mutex_unlock(&s->lock);
	return n > 0;
}

void gbm_surface_destroy(struct gbm_surface *s)
{
	int i;

	for (i = 0; i < NUM_BUFFERS; i++)
		buffer_free(&s->bufs[i]);
	pthread_mutex_destroy(&s->lock);
	free(s);
}

/* ---- EGL interposers (this library is preloaded, libhybris' libEGL comes later) ---- */

/* the real EGL entry point: libEGL may sit in a dlopen()ed module's local scope, out of RTLD_NEXT's reach */
static void *egl_sym(const char *name)
{
	static void *egl;
	void *f = dlsym(RTLD_NEXT, name);

	if (!f) {
		if (!egl)
			egl = dlopen("libEGL.so.1", RTLD_LAZY);
		if (egl)
			f = dlsym(egl, name);
	}
	return f;
}

/* ---- EGL_NATIVE_VISUAL_ID: Android HAL format -> DRM fourcc ---- */

EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint *value)
{
	static EGLBoolean (*real)(EGLDisplay, EGLConfig, EGLint, EGLint *);
	EGLBoolean ret;

	if (!real)
		real = (EGLBoolean (*)(EGLDisplay, EGLConfig, EGLint, EGLint *))egl_sym("eglGetConfigAttrib");
	if (!real)
		return EGL_FALSE;
	ret = real(dpy, config, attribute, value);
	if (ret && attribute == EGL_NATIVE_VISUAL_ID) {
		uint32_t f = hal_to_drm(*value, 0);

		if (f)
			*value = (EGLint)f;
	}
	return ret;
}

/* ---- EGL_PLATFORM_GBM_KHR: libhybris only knows its own platforms; a GBM display is our "gbm" one ---- */

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

static EGLDisplay gbm_display(void *native)
{
	static EGLDisplay (*get_display)(EGLNativeDisplayType);

	if (!get_display)
		get_display = (EGLDisplay (*)(EGLNativeDisplayType))egl_sym("eglGetDisplay");
	return get_display ? get_display((EGLNativeDisplayType)native) : EGL_NO_DISPLAY;
}

static EGLDisplay get_platform_display_ext(EGLenum platform, void *native, const EGLint *attrib)
{
	static EGLDisplay (*real)(EGLenum, void *, const EGLint *);

	if (platform == EGL_PLATFORM_GBM_KHR)
		return gbm_display(native);
	if (!real) {
		__eglMustCastToProperFunctionPointerType (*gpa)(const char *) =
			(__eglMustCastToProperFunctionPointerType (*)(const char *))egl_sym("eglGetProcAddress");
		real = gpa ? (EGLDisplay (*)(EGLenum, void *, const EGLint *))gpa("eglGetPlatformDisplayEXT") : NULL;
	}
	return real ? real(platform, native, attrib) : EGL_NO_DISPLAY;
}

EGLDisplay eglGetPlatformDisplay(EGLenum platform, void *native, const EGLAttrib *attrib)
{
	static EGLDisplay (*real)(EGLenum, void *, const EGLAttrib *);

	if (platform == EGL_PLATFORM_GBM_KHR)
		return gbm_display(native);
	if (!real)
		real = (EGLDisplay (*)(EGLenum, void *, const EGLAttrib *))egl_sym("eglGetPlatformDisplay");
	return real ? real(platform, native, attrib) : EGL_NO_DISPLAY;
}

__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *name)
{
	static __eglMustCastToProperFunctionPointerType (*real)(const char *);
	__eglMustCastToProperFunctionPointerType f;

	if (!strcmp(name, "eglGetPlatformDisplayEXT"))
		return (__eglMustCastToProperFunctionPointerType)get_platform_display_ext;
	if (!strcmp(name, "eglGetPlatformDisplay"))
		return (__eglMustCastToProperFunctionPointerType)eglGetPlatformDisplay;
	if (!real)
		real = (__eglMustCastToProperFunctionPointerType (*)(const char *))egl_sym("eglGetProcAddress");
	f = real ? real(name) : NULL;
	/* extensions the stock EGL advertises but libhybris does not wrap (eglWaitSyncKHR, ...) */
	if (!f && !strncmp(name, "egl", 3)) {
		static void *stock_egl;

		if (!stock_egl)
			stock_egl = android_dlopen("libEGL.so", RTLD_LAZY);
		if (stock_egl)
			f = (__eglMustCastToProperFunctionPointerType)android_dlsym(stock_egl, name);
	}
	return f;
}
