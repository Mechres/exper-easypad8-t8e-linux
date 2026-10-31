// SPDX-License-Identifier: MIT
/*
 * On-screen KMS + GLES2 demo using the stock Android 4.4 EGL/GLES/gralloc
 * stack (bionic, build with the NDK for API 19) on mainline Linux 7.2
 * sun4i-drm (/dev/dri/card0) + open-source pvrsrvkm (/dev/pvrsrvkm,
 * /dev/pvr_sync) + ion_compat (/dev/ion).
 *
 * Allocates scanout-capable CMA buffers via gralloc's "gpu0" device, imports
 * their ION dma-buf fds into sun4i-drm via DRM_IOCTL_PRIME_FD_TO_HANDLE +
 * DRM_IOCTL_MODE_ADDFB2, wraps them in an Android ANativeWindow, renders an
 * animated rotating triangle in OpenGL ES 2.0, waits on each frame's
 * /dev/pvr_sync fence fd in queueBuffer(), and page-flips on the 1024x768
 * LVDS panel.
 *
 * Usage: stock-kms-gles [seconds] [layers] [/dev/dri/card0]
 */
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
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
	HAL_PIXEL_FORMAT_BGRA_8888 = 5,
};

enum {
	GRALLOC_USAGE_HW_RENDER = 0x00000200,
	GRALLOC_USAGE_HW_FB     = 0x00001000,
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
	struct android_native_base_t common;
	int width;
	int height;
	int stride;
	int format;
	int usage;
	void *reserved[2];
	buffer_handle_t handle;
	void *reserved_proc[8];
} ANativeWindowBuffer_t;

typedef struct ANativeWindow {
	struct android_native_base_t common;
	uint32_t flags;
	int minSwapInterval;
	int maxSwapInterval;
	float xdpi;
	float ydpi;
	intptr_t oem[4];
	int (*setSwapInterval)(struct ANativeWindow *window, int interval);
	int (*dequeueBuffer_DEPRECATED)(struct ANativeWindow *window, struct ANativeWindowBuffer **buffer);
	int (*lockBuffer_DEPRECATED)(struct ANativeWindow *window, struct ANativeWindowBuffer *buffer);
	int (*queueBuffer_DEPRECATED)(struct ANativeWindow *window, struct ANativeWindowBuffer *buffer);
	int (*query)(const struct ANativeWindow *window, int what, int *value);
	int (*perform)(struct ANativeWindow *window, int operation, ...);
	int (*cancelBuffer_DEPRECATED)(struct ANativeWindow *window, struct ANativeWindowBuffer *buffer);
	int (*dequeueBuffer)(struct ANativeWindow *window, struct ANativeWindowBuffer **buffer, int *fenceFd);
	int (*queueBuffer)(struct ANativeWindow *window, struct ANativeWindowBuffer *buffer, int fenceFd);
	int (*cancelBuffer)(struct ANativeWindow *window, struct ANativeWindowBuffer *buffer, int fenceFd);
	void *reserved_proc[9];
} ANativeWindow_t;

enum {
	NATIVE_WINDOW_WIDTH          = 0,
	NATIVE_WINDOW_HEIGHT         = 1,
	NATIVE_WINDOW_FORMAT         = 2,
	NATIVE_WINDOW_MIN_UNDEQUEUED = 3,
	NATIVE_WINDOW_DEFAULT_WIDTH  = 6,
	NATIVE_WINDOW_DEFAULT_HEIGHT = 7,
	NATIVE_WINDOW_TRANSFORM_HINT = 8,
};

#define NUM_BUFS 2

static struct alloc_device_t *g_alloc;
static ANativeWindowBuffer_t g_bufs[NUM_BUFS];
static uint32_t g_gem_handles[NUM_BUFS];
static uint32_t g_fb_ids[NUM_BUFS];
static int g_cur_buf;
static int g_width = 1024;
static int g_height = 768;
static int g_format = HAL_PIXEL_FORMAT_BGRA_8888;
static int g_usage = GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_FB;

static int g_drm_fd = -1;
static uint32_t g_conn_id;
static uint32_t g_crtc_id;
static struct drm_mode_modeinfo g_mode;
static struct drm_mode_crtc g_saved_crtc;
static int g_frames;

static __attribute__((noreturn)) void die(const char *what)
{
	fprintf(stderr, "FAIL: %s (errno=%d %s, egl=0x%x, gl=0x%x)\n",
		what, errno, strerror(errno), eglGetError(), glGetError());
	exit(1);
}

static double now_sec(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void base_inc_ref(struct android_native_base_t *base) { (void)base; }
static void base_dec_ref(struct android_native_base_t *base) { (void)base; }

static int win_set_swap_interval(struct ANativeWindow *win, int interval)
{
	(void)win;
	(void)interval;
	return 0;
}

static int win_query(const struct ANativeWindow *win, int what, int *value)
{
	(void)win;
	switch (what) {
	case NATIVE_WINDOW_WIDTH:
	case NATIVE_WINDOW_DEFAULT_WIDTH:
		*value = g_width;
		break;
	case NATIVE_WINDOW_HEIGHT:
	case NATIVE_WINDOW_DEFAULT_HEIGHT:
		*value = g_height;
		break;
	case NATIVE_WINDOW_FORMAT:
		*value = g_format;
		break;
	case NATIVE_WINDOW_MIN_UNDEQUEUED:
		*value = 1;
		break;
	case NATIVE_WINDOW_TRANSFORM_HINT:
	default:
		*value = 0;
		break;
	}
	return 0;
}

static int win_perform(struct ANativeWindow *win, int operation, ...)
{
	(void)win;
	(void)operation;
	return 0;
}

static int win_dequeue_buffer(struct ANativeWindow *win, struct ANativeWindowBuffer **buffer, int *fenceFd)
{
	(void)win;
	*buffer = &g_bufs[g_cur_buf];
	g_cur_buf = (g_cur_buf + 1) % NUM_BUFS;
	*fenceFd = -1;
	return 0;
}

static void drm_wait_page_flip(void)
{
	struct pollfd pfd = { .fd = g_drm_fd, .events = POLLIN };
	uint8_t buf[256];

	if (poll(&pfd, 1, 1000) <= 0)
		die("KMS page flip timeout");
	if (read(g_drm_fd, buf, sizeof(buf)) < 0)
		die("KMS read event");
}

static int win_queue_buffer(struct ANativeWindow *win, struct ANativeWindowBuffer *buffer, int fenceFd)
{
	int idx = (int)(buffer - g_bufs);
	(void)win;

	/* Wait for SGX render + transfer fence before scanning out */
	if (fenceFd >= 0) {
		struct pollfd pfd = { .fd = fenceFd, .events = POLLIN };
		if (poll(&pfd, 1, 1000) <= 0)
			die("pvr_sync fence timeout");
		close(fenceFd);
	}

	if (g_frames == 0) {
		struct drm_mode_crtc crtc = {
			.crtc_id = g_crtc_id,
			.fb_id = g_fb_ids[idx],
			.set_connectors_ptr = (uintptr_t)&g_conn_id,
			.count_connectors = 1,
			.mode = g_mode,
			.mode_valid = 1,
		};
		if (ioctl(g_drm_fd, DRM_IOCTL_MODE_SETCRTC, &crtc) < 0)
			die("DRM_IOCTL_MODE_SETCRTC");
	} else {
		struct drm_mode_crtc_page_flip flip = {
			.crtc_id = g_crtc_id,
			.fb_id = g_fb_ids[idx],
			.flags = DRM_MODE_PAGE_FLIP_EVENT,
			.user_data = 0,
		};
		if (ioctl(g_drm_fd, DRM_IOCTL_MODE_PAGE_FLIP, &flip) < 0)
			die("DRM_IOCTL_MODE_PAGE_FLIP");
		drm_wait_page_flip();
	}
	g_frames++;
	return 0;
}

static int win_cancel_buffer(struct ANativeWindow *win, struct ANativeWindowBuffer *buffer, int fenceFd)
{
	(void)win;
	(void)buffer;
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

static void kms_init(const char *path)
{
	struct drm_mode_card_res res = { 0 };
	uint32_t conn_ids[16] = { 0 }, crtc_ids[16] = { 0 }, enc_ids[16] = { 0 };
	uint32_t i;

	g_drm_fd = open(path, O_RDWR | O_CLOEXEC);
	if (g_drm_fd < 0)
		die("open /dev/dri/card0");

	if (ioctl(g_drm_fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0)
		die("DRM_IOCTL_MODE_GETRESOURCES count");

	res.connector_id_ptr = (uintptr_t)conn_ids;
	res.crtc_id_ptr = (uintptr_t)crtc_ids;
	res.encoder_id_ptr = (uintptr_t)enc_ids;
	res.fb_id_ptr = 0;
	res.count_fbs = 0;
	if (ioctl(g_drm_fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0)
		die("DRM_IOCTL_MODE_GETRESOURCES");

	for (i = 0; i < res.count_connectors; i++) {
		struct drm_mode_get_connector conn = { .connector_id = conn_ids[i] };
		struct drm_mode_modeinfo modes[32];
		uint32_t props[32], prop_vals[32], encoders[16];

		if (ioctl(g_drm_fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0)
			continue;
		if (conn.connection != 1 /* DRM_MODE_CONNECTED */ || conn.count_modes == 0)
			continue;

		conn.modes_ptr = (uintptr_t)modes;
		conn.props_ptr = (uintptr_t)props;
		conn.prop_values_ptr = (uintptr_t)prop_vals;
		conn.encoders_ptr = (uintptr_t)encoders;
		if (ioctl(g_drm_fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0)
			continue;

		g_conn_id = conn.connector_id;
		g_mode = modes[0];
		g_width = g_mode.hdisplay;
		g_height = g_mode.vdisplay;

		if (conn.encoder_id) {
			struct drm_mode_get_encoder enc = { .encoder_id = conn.encoder_id };
			if (ioctl(g_drm_fd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0 && enc.crtc_id)
				g_crtc_id = enc.crtc_id;
		}
		if (!g_crtc_id && conn.count_encoders > 0) {
			struct drm_mode_get_encoder enc = { .encoder_id = encoders[0] };
			if (ioctl(g_drm_fd, DRM_IOCTL_MODE_GETENCODER, &enc) == 0) {
				uint32_t c;
				for (c = 0; c < res.count_crtcs; c++) {
					if (enc.possible_crtcs & (1u << c)) {
						g_crtc_id = crtc_ids[c];
						break;
					}
				}
			}
		}
		break;
	}

	if (!g_conn_id || !g_crtc_id)
		die("no connected KMS connector/CRTC found");

	g_saved_crtc.crtc_id = g_crtc_id;
	ioctl(g_drm_fd, DRM_IOCTL_MODE_GETCRTC, &g_saved_crtc);

	printf("KMS connector %u, mode %s %dx%d@%uHz, crtc %u\n",
	       g_conn_id, g_mode.name, g_mode.hdisplay, g_mode.vdisplay,
	       g_mode.vrefresh, g_crtc_id);
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

int main(int argc, char **argv)
{
	double seconds = argc > 1 ? atof(argv[1]) : 5.0;
	int layers = argc > 2 ? atoi(argv[2]) : 1;
	const char *drm_path = argc > 3 ? argv[3] : "/dev/dri/card0";
	const struct hw_module_t *mod = NULL;
	struct hw_device_t *dev = NULL;
	ANativeWindow_t win;
	int i, k, ret;

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	kms_init(drm_path);

	ret = hw_get_module("gralloc", &mod);
	if (ret || !mod)
		die("hw_get_module(gralloc)");
	ret = mod->methods->open(mod, "gpu0", &dev);
	if (ret || !dev)
		die("gralloc->open(gpu0)");
	g_alloc = (struct alloc_device_t *)dev;

	for (i = 0; i < NUM_BUFS; i++) {
		ANativeWindowBuffer_t *b = &g_bufs[i];
		struct drm_prime_handle prime = { 0 };
		struct drm_mode_fb_cmd2 fb = { 0 };

		memset(b, 0, sizeof(*b));
		b->common.magic = ANDROID_NATIVE_BUFFER_MAGIC;
		b->common.version = sizeof(android_native_base_t);
		b->common.incRef = base_inc_ref;
		b->common.decRef = base_dec_ref;
		b->width = g_width;
		b->height = g_height;
		b->format = g_format;
		b->usage = g_usage;
		ret = g_alloc->alloc(g_alloc, g_width, g_height, g_format, g_usage,
				     &b->handle, &b->stride);
		if (ret || !b->handle)
			die("gralloc->alloc");

		prime.fd = b->handle->data[0];
		if (ioctl(g_drm_fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &prime) < 0)
			die("DRM_IOCTL_PRIME_FD_TO_HANDLE");
		g_gem_handles[i] = prime.handle;

		fb.width = (uint32_t)g_width;
		fb.height = (uint32_t)g_height;
		fb.pixel_format = DRM_FORMAT_XRGB8888;
		fb.handles[0] = g_gem_handles[i];
		fb.pitches[0] = (uint32_t)(b->stride * 4);
		fb.offsets[0] = 0;
		if (ioctl(g_drm_fd, DRM_IOCTL_MODE_ADDFB2, &fb) < 0)
			die("DRM_IOCTL_MODE_ADDFB2");
		g_fb_ids[i] = fb.fb_id;

		printf("buf[%d]: dma_buf_fd=%d gem=%u fb_id=%u stride=%d\n",
		       i, prime.fd, g_gem_handles[i], g_fb_ids[i], b->stride);
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
	static const GLfloat tri[] = {
		 0.0f,  0.8f, 1.0f, 0.0f, 0.0f,
		-0.7f, -0.5f, 0.0f, 1.0f, 0.0f,
		 0.7f, -0.5f, 0.0f, 0.0f, 1.0f,
	};

	EGLDisplay dpy;
	EGLConfig cfg;
	EGLSurface surf;
	EGLContext ctx;
	EGLint major, minor, n;
	GLuint prog;
	GLint u_rot, u_scale;

	dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor))
		die("eglInitialize");
	if (!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n) || n < 1)
		die("eglChooseConfig");
	surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)&win, NULL);
	if (surf == EGL_NO_SURFACE)
		die("eglCreateWindowSurface");
	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	if (ctx == EGL_NO_CONTEXT)
		die("eglCreateContext");
	if (!eglMakeCurrent(dpy, surf, surf, ctx))
		die("eglMakeCurrent");
	printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));

	prog = glCreateProgram();
	glAttachShader(prog, shader(GL_VERTEX_SHADER,
		"uniform float rot; uniform float aspect; uniform float scale;\n"
		"attribute vec2 pos; attribute vec3 col; varying vec3 v;\n"
		"void main() { float c = cos(rot) * scale, s = sin(rot) * scale;\n"
		"  gl_Position = vec4((c * pos.x - s * pos.y) / aspect, s * pos.x + c * pos.y, 0.0, 1.0);\n"
		"  v = col; }"));
	glAttachShader(prog, shader(GL_FRAGMENT_SHADER,
		"precision mediump float; varying vec3 v;\n"
		"void main() { gl_FragColor = vec4(v, 0.5); }"));
	glBindAttribLocation(prog, 0, "pos");
	glBindAttribLocation(prog, 1, "col");
	glLinkProgram(prog);
	glUseProgram(prog);

	u_rot = glGetUniformLocation(prog, "rot");
	u_scale = glGetUniformLocation(prog, "scale");
	glUniform1f(u_scale, 1.0f);
	glUniform1f(glGetUniformLocation(prog, "aspect"), (float)g_width / (float)g_height);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), tri);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), tri + 2);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glViewport(0, 0, g_width, g_height);

	double t0 = now_sec(), t;
	do {
		t = now_sec() - t0;
		glClearColor(0.1f + 0.1f * sinf((float)t),
			     0.1f,
			     0.2f + 0.1f * cosf((float)t),
			     1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		if (layers > 1) {
			glEnable(GL_BLEND);
			glUniform1f(u_scale, 3.0f);
			for (k = 1; k < layers; k++) {
				glUniform1f(u_rot, (float)(-t * (1.0 + 0.1 * k)));
				glDrawArrays(GL_TRIANGLES, 0, 3);
			}
			glDisable(GL_BLEND);
			glUniform1f(u_scale, 1.0f);
		}
		glUniform1f(u_rot, (float)t);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		if (!eglSwapBuffers(dpy, surf))
			die("eglSwapBuffers");
	} while (t < seconds);
	t = now_sec() - t0;
	printf("%d frames in %.2f s = %.1f fps (%d layers)\n",
	       g_frames, t, g_frames / t, layers);

	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(dpy, ctx);
	eglDestroySurface(dpy, surf);
	eglTerminate(dpy);

	if (g_saved_crtc.mode_valid) {
		g_saved_crtc.set_connectors_ptr = (uintptr_t)&g_conn_id;
		g_saved_crtc.count_connectors = 1;
		ioctl(g_drm_fd, DRM_IOCTL_MODE_SETCRTC, &g_saved_crtc);
	}

	for (i = 0; i < NUM_BUFS; i++) {
		struct drm_gem_close gem_close = { .handle = g_gem_handles[i] };
		ioctl(g_drm_fd, DRM_IOCTL_MODE_RMFB, &g_fb_ids[i]);
		ioctl(g_drm_fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
		g_alloc->free(g_alloc, g_bufs[i].handle);
	}
	dev->close(dev);
	close(g_drm_fd);
	return 0;
}
