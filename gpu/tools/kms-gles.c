// SPDX-License-Identifier: MIT
/*
 * On-screen GPU test: GBM surface + EGL + GLES2, shown through KMS page flips.
 * Draws a rotating colour triangle on a slowly changing background for the
 * given number of seconds on the first connected connector, then reports fps.
 * With layers > 1 each frame also draws that many large blended triangles
 * (fill-rate load for stability testing).
 * usage: kms-gles [seconds] [layers] [/dev/dri/card0]
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <fcntl.h>
#include <gbm.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

static void die(const char *what)
{
	fprintf(stderr, "FAIL: %s (egl 0x%x, gl 0x%x)\n", what, eglGetError(), glGetError());
	exit(1);
}

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
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

static void bo_destroy(struct gbm_bo *bo, void *data)
{
	int fd = gbm_device_get_fd(gbm_bo_get_device(bo));
	uint32_t fb = (uint32_t)(uintptr_t)data;

	if (fb)
		drmModeRmFB(fd, fb);
}

static uint32_t bo_fb(int fd, struct gbm_bo *bo)
{
	uint32_t fb = (uint32_t)(uintptr_t)gbm_bo_get_user_data(bo);
	uint32_t handles[4] = { gbm_bo_get_handle(bo).u32 };
	uint32_t pitches[4] = { gbm_bo_get_stride(bo) };
	uint32_t offsets[4] = { 0 };

	if (fb)
		return fb;
	if (drmModeAddFB2(fd, gbm_bo_get_width(bo), gbm_bo_get_height(bo),
			  gbm_bo_get_format(bo), handles, pitches, offsets, &fb, 0))
		die("drmModeAddFB2");
	gbm_bo_set_user_data(bo, (void *)(uintptr_t)fb, bo_destroy);
	return fb;
}

static void flip_done(int fd, unsigned int seq, unsigned int sec, unsigned int usec, void *data)
{
	*(int *)data = 0;
}

int main(int argc, char **argv)
{
	double seconds = argc > 1 ? atof(argv[1]) : 10;
	int layers = argc > 2 ? atoi(argv[2]) : 1;
	const char *path = argc > 3 ? argv[3] : "/dev/dri/card0";
	static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	static const EGLint cfg_attr[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
	static const GLfloat tri[] = {
		 0.0f,  0.8f, 1, 0, 0,
		-0.7f, -0.5f, 0, 1, 0,
		 0.7f, -0.5f, 0, 0, 1,
	};
	PFNEGLGETPLATFORMDISPLAYEXTPROC get_display;
	PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC create_surface;
	drmEventContext ev = { .version = 2, .page_flip_handler = flip_done };
	drmModeRes *res;
	drmModeConnector *conn = NULL;
	drmModeEncoder *enc;
	drmModeModeInfo mode;
	drmModeCrtc *saved;
	uint32_t crtc_id, formats[] = { GBM_FORMAT_XRGB8888, GBM_FORMAT_RGB565 };
	struct gbm_device *gbm;
	struct gbm_surface *gs = NULL;
	struct gbm_bo *bo, *prev = NULL;
	EGLDisplay dpy;
	EGLConfig cfgs[64], cfg = NULL;
	EGLContext ctx;
	EGLSurface surf;
	EGLint n, i, vid;
	GLuint prog;
	GLint u_rot, u_scale;
	int k;
	int fd, f, frames = 0, pending;
	double t0, t;

	fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		die("open device");
	res = drmModeGetResources(fd);
	if (!res)
		die("drmModeGetResources");
	for (i = 0; i < res->count_connectors; i++) {
		conn = drmModeGetConnector(fd, res->connectors[i]);
		if (conn && conn->connection == DRM_MODE_CONNECTED && conn->count_modes)
			break;
		drmModeFreeConnector(conn);
		conn = NULL;
	}
	if (!conn)
		die("no connected connector");
	mode = conn->modes[0];
	enc = conn->encoder_id ? drmModeGetEncoder(fd, conn->encoder_id) : NULL;
	if (enc && enc->crtc_id)
		crtc_id = enc->crtc_id;
	else {
		enc = drmModeGetEncoder(fd, conn->encoders[0]);
		for (i = 0; i < res->count_crtcs; i++)
			if (enc->possible_crtcs & (1 << i))
				break;
		crtc_id = res->crtcs[i];
	}
	saved = drmModeGetCrtc(fd, crtc_id);
	printf("connector %u, mode %s %dx%d@%d, crtc %u\n", conn->connector_id, mode.name,
	       mode.hdisplay, mode.vdisplay, mode.vrefresh, crtc_id);

	gbm = gbm_create_device(fd);
	if (!gbm)
		die("gbm_create_device");
	get_display = (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
	create_surface = (void *)eglGetProcAddress("eglCreatePlatformWindowSurfaceEXT");
	dpy = get_display(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL))
		die("eglInitialize");
	eglBindAPI(EGL_OPENGL_ES_API);
	if (!eglChooseConfig(dpy, cfg_attr, cfgs, 64, &n))
		die("eglChooseConfig");
	/* pick the first format both EGL and the display take */
	for (f = 0; f < 2 && !cfg; f++)
		for (i = 0; i < n; i++)
			if (eglGetConfigAttrib(dpy, cfgs[i], EGL_NATIVE_VISUAL_ID, &vid) &&
			    (uint32_t)vid == formats[f]) {
				cfg = cfgs[i];
				break;
			}
	if (!cfg)
		die("no EGL config with an XRGB8888/RGB565 visual");
	f--;
	printf("format %.4s\n", (char *)&formats[f]);
	gs = gbm_surface_create(gbm, mode.hdisplay, mode.vdisplay, formats[f],
				GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	if (!gs)
		die("gbm_surface_create");
	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	surf = create_surface(dpy, cfg, gs, NULL);
	if (ctx == EGL_NO_CONTEXT || surf == EGL_NO_SURFACE)
		die("context/surface");
	if (!eglMakeCurrent(dpy, surf, surf, ctx))
		die("eglMakeCurrent");
	printf("GL_RENDERER %s\n", glGetString(GL_RENDERER));

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
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glUniform1f(glGetUniformLocation(prog, "aspect"), (float)mode.hdisplay / mode.vdisplay);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), tri);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), tri + 2);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glViewport(0, 0, mode.hdisplay, mode.vdisplay);

	t0 = now();
	do {
		t = now() - t0;
		glClearColor(0.1f + 0.1f * sinf(t), 0.1f, 0.2f + 0.1f * cosf(t), 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		if (layers > 1) {
			glEnable(GL_BLEND);
			glUniform1f(u_scale, 3.0f);
			for (k = 1; k < layers; k++) {
				glUniform1f(u_rot, -t * (1.0f + 0.1f * k));
				glDrawArrays(GL_TRIANGLES, 0, 3);
			}
			glDisable(GL_BLEND);
			glUniform1f(u_scale, 1.0f);
		}
		glUniform1f(u_rot, t);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		if (!eglSwapBuffers(dpy, surf))
			die("eglSwapBuffers");
		bo = gbm_surface_lock_front_buffer(gs);
		if (!bo)
			die("gbm_surface_lock_front_buffer");
		if (!frames) {
			if (drmModeSetCrtc(fd, crtc_id, bo_fb(fd, bo), 0, 0, &conn->connector_id, 1, &mode))
				die("drmModeSetCrtc");
		} else {
			pending = 1;
			if (drmModePageFlip(fd, crtc_id, bo_fb(fd, bo), DRM_MODE_PAGE_FLIP_EVENT, &pending))
				die("drmModePageFlip");
			while (pending) {
				struct pollfd p = { .fd = fd, .events = POLLIN };

				if (poll(&p, 1, 1000) <= 0)
					die("page flip timeout");
				drmHandleEvent(fd, &ev);
			}
		}
		if (prev)
			gbm_surface_release_buffer(gs, prev);
		prev = bo;
		frames++;
	} while (t < seconds);
	t = now() - t0;
	printf("%d frames in %.1f s = %.1f fps (%d layers)\n", frames, t, frames / t, layers);

	if (saved)
		drmModeSetCrtc(fd, saved->crtc_id, saved->buffer_id, saved->x, saved->y,
			       &conn->connector_id, 1, &saved->mode);
	return 0;
}
