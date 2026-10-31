// SPDX-License-Identifier: MIT
/*
 * Minimal GPU rendering check: GBM -> EGL -> surfaceless GLES2 context ->
 * offscreen FBO. Clears to blue, draws a red triangle over the centre and
 * reads pixels back. No display output.
 * usage: gles-probe [/dev/dri/card0 | /dev/dri/renderD128]
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define W 64
#define H 64

static void die(const char *what)
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

static void pixel(const char *name, int x, int y)
{
	unsigned char p[4];

	glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
	printf("pixel %-7s (%2d,%2d) = %3d %3d %3d %3d\n", name, x, y, p[0], p[1], p[2], p[3]);
}

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/dev/dri/card0";
	static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	static const EGLint cfg_attr[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, 0, EGL_NONE };
	static const GLfloat tri[] = { -0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f };
	PFNEGLGETPLATFORMDISPLAYEXTPROC get_display;
	struct gbm_device *gbm;
	EGLDisplay dpy;
	EGLConfig cfg;
	EGLContext ctx;
	EGLint major, minor, n;
	GLuint fbo, rb, prog;
	int fd;

	fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		die("open device");
	gbm = gbm_create_device(fd);
	if (!gbm)
		die("gbm_create_device");
	printf("gbm backend: %s\n", gbm_device_get_backend_name(gbm));

	get_display = (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
	dpy = get_display ? get_display(EGL_PLATFORM_GBM_KHR, gbm, NULL) : EGL_NO_DISPLAY;
	if (dpy == EGL_NO_DISPLAY)
		die("eglGetPlatformDisplayEXT");
	if (!eglInitialize(dpy, &major, &minor))
		die("eglInitialize");
	printf("EGL %d.%d vendor '%s' version '%s'\n", major, minor,
	       eglQueryString(dpy, EGL_VENDOR), eglQueryString(dpy, EGL_VERSION));
	if (!eglBindAPI(EGL_OPENGL_ES_API))
		die("eglBindAPI");
	if (!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n) || n < 1)
		die("eglChooseConfig");
	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	if (ctx == EGL_NO_CONTEXT)
		die("eglCreateContext");
	if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx))
		die("eglMakeCurrent (surfaceless)");
	printf("GL_VENDOR   %s\nGL_RENDERER %s\nGL_VERSION  %s\n",
	       glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));

	glGenFramebuffers(1, &fbo);
	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB565, W, H);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		die("framebuffer incomplete");

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
	glFinish();

	pixel("corner", 2, 2);
	pixel("centre", W / 2, H / 2);
	printf("glGetError 0x%x\n", glGetError());

	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(dpy, ctx);
	eglTerminate(dpy);
	gbm_device_destroy(gbm);
	close(fd);
	return 0;
}
