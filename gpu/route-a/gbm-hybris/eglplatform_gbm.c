// SPDX-License-Identifier: MIT
/*
 * libhybris EGL platform "gbm" (HYBRIS_EGLPLATFORM=gbm): the native display is a gbm_device and
 * the native window a gbm_surface from gbm-hybris; the window handed to the stock EGL is the
 * Android native window behind the surface.
 */
#include <stdlib.h>
#include <EGL/egl.h>

#include <ws.h>
#include <eglplatformcommon.h>
#include <hybris/gralloc/gralloc.h>

#include "gbm.h"

static void gbmws_init_module(struct ws_egl_interface *egl_iface)
{
	hybris_gralloc_initialize(0);
	eglplatformcommon_init(egl_iface);
}

static struct _EGLDisplay *gbmws_GetDisplay(EGLNativeDisplayType display)
{
	(void)display;
	return calloc(1, sizeof(struct _EGLDisplay));
}

static void gbmws_Terminate(struct _EGLDisplay *dpy)
{
	free(dpy);
}

static EGLNativeWindowType gbmws_CreateWindow(EGLNativeWindowType win, struct _EGLDisplay *display)
{
	(void)display;
	return (EGLNativeWindowType)gbm_hybris_surface_get_native_window((struct gbm_surface *)win);
}

static void gbmws_DestroyWindow(EGLNativeWindowType win)
{
	(void)win;	/* the gbm_surface owns the window */
}

struct ws_module ws_module_info = {
	gbmws_init_module,
	gbmws_GetDisplay,
	gbmws_Terminate,
	gbmws_CreateWindow,
	gbmws_DestroyWindow,
	eglplatformcommon_eglGetProcAddress,
	eglplatformcommon_passthroughImageKHR,
	eglplatformcommon_eglQueryString,
};
