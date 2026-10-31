// SPDX-License-Identifier: MIT
/*
 * Window-surface GLES2 probe against the stock Android 4.4 EGL/GLES/gralloc
 * stack (bionic, build with the NDK for API 19).
 *
 * Implements a minimal ANativeWindow + ANativeWindowBuffer backed by gralloc's
 * "gpu0" allocator device, passes it to eglCreateWindowSurface(), renders a
 * blue background + red triangle, and calls eglSwapBuffers().
 */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- Android HAL / gralloc ABI (32-bit ARM, API 19) ---- */

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

int hw_get_module(const char *id, const struct hw_module_t **module);

enum {
	HAL_PIXEL_FORMAT_RGBA_8888 = 1,
	HAL_PIXEL_FORMAT_RGBX_8888 = 2,
	HAL_PIXEL_FORMAT_RGB_565   = 4,
	HAL_PIXEL_FORMAT_BGRA_8888 = 5,
};

enum {
	GRALLOC_USAGE_SW_READ_OFTEN  = 0x00000003,
	GRALLOC_USAGE_HW_TEXTURE     = 0x00000100,
	GRALLOC_USAGE_HW_RENDER      = 0x00000200,
	GRALLOC_USAGE_HW_COMPOSER    = 0x00000800,
	GRALLOC_USAGE_HW_FB          = 0x00001000,
};

/* ---- Android ANativeWindow / ANativeWindowBuffer ABI (32-bit ARM, API 19) ---- */

#define ANDROID_NATIVE_WINDOW_MAGIC 0x5f776e64 /* "_wnd" */
#define ANDROID_NATIVE_BUFFER_MAGIC 0x5f626672 /* "_bfr" */

typedef struct android_native_base_t {
	int magic;
	int version;
	void *reserved[4];
	void (*incRef)(struct android_native_base_t *base);
	void (*decRef)(struct android_native_base_t *base);
} android_native_base_t;

typedef struct ANativeWindowBuffer {
	struct android_native_base_t common; /* 0..31 */
	int width;                           /* 32 */
	int height;                          /* 36 */
	int stride;                          /* 40 */
	int format;                          /* 44 */
	int usage;                           /* 48 */
	void *reserved[2];                   /* 52..59 */
	buffer_handle_t handle;              /* 60 */
	void *reserved_proc[8];              /* 64..95 */
} ANativeWindowBuffer_t;

typedef struct ANativeWindow {
	struct android_native_base_t common; /* 0..31 */
	uint32_t flags;                      /* 32 */
	int minSwapInterval;                 /* 36 */
	int maxSwapInterval;                 /* 40 */
	float xdpi;                          /* 44 */
	float ydpi;                          /* 48 */
	intptr_t oem[4];                     /* 52..67 */
	int (*setSwapInterval)(struct ANativeWindow *window, int interval); /* 68 */
	int (*dequeueBuffer_DEPRECATED)(struct ANativeWindow *window, struct ANativeWindowBuffer **buffer); /* 72 */
	int (*lockBuffer_DEPRECATED)(struct ANativeWindow *window, struct ANativeWindowBuffer *buffer); /* 76 */
	int (*queueBuffer_DEPRECATED)(struct ANativeWindow *window, struct ANativeWindowBuffer *buffer); /* 80 */
	int (*query)(const struct ANativeWindow *window, int what, int *value); /* 84 */
	int (*perform)(struct ANativeWindow *window, int operation, ...); /* 88 */
	int (*cancelBuffer_DEPRECATED)(struct ANativeWindow *window, struct ANativeWindowBuffer *buffer); /* 92 */
	int (*dequeueBuffer)(struct ANativeWindow *window, struct ANativeWindowBuffer **buffer, int *fenceFd); /* 96 */
	int (*queueBuffer)(struct ANativeWindow *window, struct ANativeWindowBuffer *buffer, int fenceFd); /* 100 */
	int (*cancelBuffer)(struct ANativeWindow *window, struct ANativeWindowBuffer *buffer, int fenceFd); /* 104 */
	void *reserved_proc[9];
} ANativeWindow_t;

enum {
	NATIVE_WINDOW_WIDTH             = 0,
	NATIVE_WINDOW_HEIGHT            = 1,
	NATIVE_WINDOW_FORMAT            = 2,
	NATIVE_WINDOW_MIN_UNDEQUEUED    = 3,
	NATIVE_WINDOW_DEFAULT_WIDTH     = 6,
	NATIVE_WINDOW_DEFAULT_HEIGHT    = 7,
	NATIVE_WINDOW_TRANSFORM_HINT    = 8,
};

enum {
	NATIVE_WINDOW_SET_USAGE              = 0,
	NATIVE_WINDOW_CONNECT                = 1,
	NATIVE_WINDOW_DISCONNECT             = 2,
	NATIVE_WINDOW_SET_BUFFERS_DIMENSIONS = 8,
	NATIVE_WINDOW_SET_BUFFERS_FORMAT     = 9,
	NATIVE_WINDOW_SET_BUFFERS_TRANSFORM  = 6,
};

#define W 1024
#define H 768
#define NUM_BUFS 2

static const struct gralloc_module_t *g_gralloc;
static struct alloc_device_t *g_alloc;
static ANativeWindowBuffer_t g_bufs[NUM_BUFS];
static int g_cur_buf;
static int g_usage = GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_FB | GRALLOC_USAGE_SW_READ_OFTEN;
static int g_format = HAL_PIXEL_FORMAT_BGRA_8888;

static void base_inc_ref(struct android_native_base_t *base)
{
	(void)base;
}

static void base_dec_ref(struct android_native_base_t *base)
{
	(void)base;
}

static int win_set_swap_interval(struct ANativeWindow *win, int interval)
{
	(void)win;
	printf("[win] setSwapInterval(%d)\n", interval);
	return 0;
}

static int win_query(const struct ANativeWindow *win, int what, int *value)
{
	(void)win;
	switch (what) {
	case NATIVE_WINDOW_WIDTH:
	case NATIVE_WINDOW_DEFAULT_WIDTH:
		*value = W;
		break;
	case NATIVE_WINDOW_HEIGHT:
	case NATIVE_WINDOW_DEFAULT_HEIGHT:
		*value = H;
		break;
	case NATIVE_WINDOW_FORMAT:
		*value = g_format;
		break;
	case NATIVE_WINDOW_MIN_UNDEQUEUED:
		*value = 1;
		break;
	case NATIVE_WINDOW_TRANSFORM_HINT:
		*value = 0;
		break;
	default:
		printf("[win] query(unknown=%d)\n", what);
		*value = 0;
		break;
	}
	printf("[win] query(%d) -> %d\n", what, *value);
	return 0;
}

static int win_perform(struct ANativeWindow *win, int operation, ...)
{
	va_list ap;
	(void)win;
	va_start(ap, operation);
	switch (operation) {
	case NATIVE_WINDOW_SET_USAGE: {
		int u = va_arg(ap, int);
		printf("[win] perform(SET_USAGE, 0x%x)\n", u);
		break;
	}
	case NATIVE_WINDOW_SET_BUFFERS_DIMENSIONS: {
		int w = va_arg(ap, int);
		int h = va_arg(ap, int);
		printf("[win] perform(SET_BUFFERS_DIMENSIONS, %dx%d)\n", w, h);
		break;
	}
	case NATIVE_WINDOW_SET_BUFFERS_FORMAT: {
		int f = va_arg(ap, int);
		printf("[win] perform(SET_BUFFERS_FORMAT, %d)\n", f);
		break;
	}
	case NATIVE_WINDOW_SET_BUFFERS_TRANSFORM: {
		int t = va_arg(ap, int);
		printf("[win] perform(SET_BUFFERS_TRANSFORM, %d)\n", t);
		break;
	}
	default:
		printf("[win] perform(op=%d)\n", operation);
		break;
	}
	va_end(ap);
	return 0;
}

static int win_dequeue_buffer(struct ANativeWindow *win, struct ANativeWindowBuffer **buffer, int *fenceFd)
{
	(void)win;
	ANativeWindowBuffer_t *b = &g_bufs[g_cur_buf];
	g_cur_buf = (g_cur_buf + 1) % NUM_BUFS;
	*buffer = b;
	*fenceFd = -1;
	printf("[win] dequeueBuffer() -> buf=%p (handle=%p)\n", (void *)b, (const void *)b->handle);
	return 0;
}

static int win_queue_buffer(struct ANativeWindow *win, struct ANativeWindowBuffer *buffer, int fenceFd)
{
	(void)win;
	if (fenceFd >= 0) {
		struct pollfd pfd = { .fd = fenceFd, .events = POLLIN };
		int r = poll(&pfd, 1, 1000);
		printf("[win] queueBuffer(buf=%p, fenceFd=%d) poll=%d revents=0x%x\n",
		       (void *)buffer, fenceFd, r, pfd.revents);
		close(fenceFd);
	} else {
		printf("[win] queueBuffer(buf=%p, fenceFd=%d)\n", (void *)buffer, fenceFd);
	}
	return 0;
}

static int win_cancel_buffer(struct ANativeWindow *win, struct ANativeWindowBuffer *buffer, int fenceFd)
{
	(void)win;
	printf("[win] cancelBuffer(buf=%p, fenceFd=%d)\n", (void *)buffer, fenceFd);
	if (fenceFd >= 0)
		close(fenceFd);
	return 0;
}

static int win_lock_buffer_dep(struct ANativeWindow *win, struct ANativeWindowBuffer *buffer)
{
	(void)win;
	(void)buffer;
	return 0;
}

static __attribute__((noreturn)) void die(const char *what)
{
	fprintf(stderr, "FAIL: %s (egl 0x%x, gl 0x%x)\n", what, eglGetError(), glGetError());
	exit(1);
}

static GLuint shader(GLenum type, const char *src)
{
	GLuint s = glCreateShader(type);
	GLint ok;
	char log[512];

	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		fprintf(stderr, "shader: %s\n", log);
		die("shader compile");
	}
	return s;
}

int main(void)
{
	const struct hw_module_t *mod = NULL;
	struct hw_device_t *dev = NULL;
	ANativeWindow_t win;
	int i, ret;

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	ret = hw_get_module("gralloc", &mod);
	if (ret || !mod) {
		fprintf(stderr, "hw_get_module(gralloc) failed: %d\n", ret);
		return 1;
	}
	g_gralloc = (const struct gralloc_module_t *)mod;
	ret = mod->methods->open(mod, "gpu0", &dev);
	if (ret || !dev) {
		fprintf(stderr, "open(gpu0) failed: %d\n", ret);
		return 1;
	}
	g_alloc = (struct alloc_device_t *)dev;

	for (i = 0; i < NUM_BUFS; i++) {
		ANativeWindowBuffer_t *b = &g_bufs[i];
		memset(b, 0, sizeof(*b));
		b->common.magic = ANDROID_NATIVE_BUFFER_MAGIC;
		b->common.version = sizeof(android_native_base_t);
		b->common.incRef = base_inc_ref;
		b->common.decRef = base_dec_ref;
		b->width = W;
		b->height = H;
		b->format = g_format;
		b->usage = g_usage;
		ret = g_alloc->alloc(g_alloc, W, H, g_format, g_usage, &b->handle, &b->stride);
		if (ret || !b->handle) {
			fprintf(stderr, "alloc buffer %d failed: %d\n", i, ret);
			return 1;
		}
		printf("allocated buf[%d]: handle=%p fd=%d stride=%d\n",
		       i, (const void *)b->handle, b->handle->data[0], b->stride);
	}

	memset(&win, 0, sizeof(win));
	win.common.magic = ANDROID_NATIVE_WINDOW_MAGIC;
	win.common.version = sizeof(android_native_base_t);
	win.common.incRef = base_inc_ref;
	win.common.decRef = base_dec_ref;
	win.minSwapInterval = 1;
	win.maxSwapInterval = 1;
	win.xdpi = 160.0f;
	win.ydpi = 160.0f;
	win.setSwapInterval = win_set_swap_interval;
	win.lockBuffer_DEPRECATED = win_lock_buffer_dep;
	win.query = win_query;
	win.perform = win_perform;
	win.dequeueBuffer = win_dequeue_buffer;
	win.queueBuffer = win_queue_buffer;
	win.cancelBuffer = win_cancel_buffer;

	static const EGLint cfg_attr[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_NONE
	};
	static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	static const GLfloat tri[] = { -0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f };

	EGLDisplay dpy;
	EGLConfig cfg;
	EGLSurface surf;
	EGLContext ctx;
	EGLint major, minor, n;
	GLuint prog;

	dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor))
		die("eglInitialize");
	printf("EGL %d.%d vendor '%s' version '%s'\n", major, minor,
	       eglQueryString(dpy, EGL_VENDOR), eglQueryString(dpy, EGL_VERSION));
	if (!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n) || n < 1)
		die("eglChooseConfig");

	printf("calling eglCreateWindowSurface...\n");
	surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)&win, NULL);
	if (surf == EGL_NO_SURFACE)
		die("eglCreateWindowSurface");
	printf("eglCreateWindowSurface succeeded: %p\n", surf);

	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	if (ctx == EGL_NO_CONTEXT)
		die("eglCreateContext");
	if (!eglMakeCurrent(dpy, surf, surf, ctx))
		die("eglMakeCurrent");
	printf("GL_RENDERER %s\n", glGetString(GL_RENDERER));

	prog = glCreateProgram();
	glAttachShader(prog, shader(GL_VERTEX_SHADER,
		"attribute vec2 pos; void main() { gl_Position = vec4(pos, 0.0, 1.0); }"));
	glAttachShader(prog, shader(GL_FRAGMENT_SHADER,
		"precision mediump float; void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }"));
	glBindAttribLocation(prog, 0, "pos");
	glLinkProgram(prog);
	glUseProgram(prog);

	glViewport(0, 0, W, H);
	glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, tri);
	glEnableVertexAttribArray(0);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	printf("calling eglSwapBuffers...\n");
	if (!eglSwapBuffers(dpy, surf))
		die("eglSwapBuffers");
	printf("eglSwapBuffers succeeded!\n");

	/* Lock buffer 0 via gralloc and inspect corner + centre pixels */
	void *vaddr = NULL;
	ret = g_gralloc->lock(g_gralloc, g_bufs[0].handle, GRALLOC_USAGE_SW_READ_OFTEN,
			      0, 0, W, H, &vaddr);
	printf("gralloc->lock(buf[0]) = %d, vaddr=%p\n", ret, vaddr);
	if (!ret && vaddr) {
		uint8_t *p = (uint8_t *)vaddr;
		uint8_t *corner = p + (2 * g_bufs[0].stride + 2) * 4;
		uint8_t *centre = p + ((H / 2) * g_bufs[0].stride + (W / 2)) * 4;
		printf("buf[0] BGRA corner (2,2)       = %3u %3u %3u %3u\n",
		       corner[0], corner[1], corner[2], corner[3]);
		printf("buf[0] BGRA centre (%d,%d) = %3u %3u %3u %3u\n",
		       W / 2, H / 2, centre[0], centre[1], centre[2], centre[3]);
		g_gralloc->unlock(g_gralloc, g_bufs[0].handle);
	}

	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(dpy, ctx);
	eglDestroySurface(dpy, surf);
	eglTerminate(dpy);
	for (i = 0; i < NUM_BUFS; i++)
		g_alloc->free(g_alloc, g_bufs[i].handle);
	dev->close(dev);
	return 0;
}
