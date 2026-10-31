/* SPDX-License-Identifier: MIT */
/*
 * The part of Mesa's GBM API (gbm.h, ABI-compatible) that weston's DRM backend and GL renderer use,
 * implemented on Android gralloc buffers through libhybris (gbm-hybris.c).
 */
#ifndef T8E_GBM_H
#define T8E_GBM_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define __gbm_fourcc_code(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#define GBM_FORMAT_XRGB8888	__gbm_fourcc_code('X', 'R', '2', '4')
#define GBM_FORMAT_ARGB8888	__gbm_fourcc_code('A', 'R', '2', '4')
#define GBM_FORMAT_XBGR8888	__gbm_fourcc_code('X', 'B', '2', '4')
#define GBM_FORMAT_ABGR8888	__gbm_fourcc_code('A', 'B', '2', '4')
#define GBM_FORMAT_RGB565	__gbm_fourcc_code('R', 'G', '1', '6')

struct gbm_device;
struct gbm_bo;
struct gbm_surface;

union gbm_bo_handle {
	void *ptr;
	int32_t s32;
	uint32_t u32;
	int64_t s64;
	uint64_t u64;
};

enum gbm_bo_flags {
	GBM_BO_USE_SCANOUT	= (1 << 0),
	GBM_BO_USE_CURSOR	= (1 << 1),
	GBM_BO_USE_RENDERING	= (1 << 2),
	GBM_BO_USE_WRITE	= (1 << 3),
	GBM_BO_USE_LINEAR	= (1 << 4),
};

struct gbm_device *gbm_create_device(int fd);
void gbm_device_destroy(struct gbm_device *gbm);
int gbm_device_get_fd(struct gbm_device *gbm);
const char *gbm_device_get_backend_name(struct gbm_device *gbm);
int gbm_device_is_format_supported(struct gbm_device *gbm, uint32_t format, uint32_t flags);

struct gbm_bo *gbm_bo_create(struct gbm_device *gbm, uint32_t width, uint32_t height, uint32_t format, uint32_t flags);
struct gbm_bo *gbm_bo_import(struct gbm_device *gbm, uint32_t type, void *buffer, uint32_t flags);
uint32_t gbm_bo_get_width(struct gbm_bo *bo);
uint32_t gbm_bo_get_height(struct gbm_bo *bo);
uint32_t gbm_bo_get_stride(struct gbm_bo *bo);
uint32_t gbm_bo_get_stride_for_plane(struct gbm_bo *bo, int plane);
uint32_t gbm_bo_get_format(struct gbm_bo *bo);
uint32_t gbm_bo_get_bpp(struct gbm_bo *bo);
uint32_t gbm_bo_get_offset(struct gbm_bo *bo, int plane);
struct gbm_device *gbm_bo_get_device(struct gbm_bo *bo);
union gbm_bo_handle gbm_bo_get_handle(struct gbm_bo *bo);
union gbm_bo_handle gbm_bo_get_handle_for_plane(struct gbm_bo *bo, int plane);
int gbm_bo_get_fd(struct gbm_bo *bo);
uint64_t gbm_bo_get_modifier(struct gbm_bo *bo);
int gbm_bo_get_plane_count(struct gbm_bo *bo);
int gbm_bo_write(struct gbm_bo *bo, const void *buf, size_t count);
void gbm_bo_set_user_data(struct gbm_bo *bo, void *data, void (*destroy_user_data)(struct gbm_bo *, void *));
void *gbm_bo_get_user_data(struct gbm_bo *bo);
void gbm_bo_destroy(struct gbm_bo *bo);

struct gbm_surface *gbm_surface_create(struct gbm_device *gbm, uint32_t width, uint32_t height, uint32_t format,
				       uint32_t flags);
struct gbm_surface *gbm_surface_create_with_modifiers(struct gbm_device *gbm, uint32_t width, uint32_t height,
						      uint32_t format, const uint64_t *modifiers, const unsigned int count);
struct gbm_bo *gbm_surface_lock_front_buffer(struct gbm_surface *surface);
void gbm_surface_release_buffer(struct gbm_surface *surface, struct gbm_bo *bo);
int gbm_surface_has_free_buffers(struct gbm_surface *surface);
void gbm_surface_destroy(struct gbm_surface *surface);

/* not in Mesa: the Android native window behind a surface, for the libhybris "gbm" EGL platform */
void *gbm_hybris_surface_get_native_window(struct gbm_surface *surface);

#ifdef __cplusplus
}
#endif
#endif
