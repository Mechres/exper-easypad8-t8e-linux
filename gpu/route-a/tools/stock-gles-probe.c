// SPDX-License-Identifier: MIT
/*
 * Offscreen GLES2 check against the stock Android 4.4 EGL/GLES libraries (bionic,
 * build with the NDK for API 19): EGL default display, 64x64 pbuffer, clear to
 * blue, a red triangle over the centre, glReadPixels.
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>

#define W 64
#define H 64

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

static void pixel(const char *name, int x, int y)
{
	unsigned char p[4];

	glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
	printf("pixel %-7s (%2d,%2d) = %3d %3d %3d %3d\n", name, x, y, p[0], p[1], p[2], p[3]);
}

int main(void)
{
	static const EGLint cfg_attr[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 5, EGL_NONE };
	static const EGLint pb_attr[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
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
	surf = eglCreatePbufferSurface(dpy, cfg, pb_attr);
	if (surf == EGL_NO_SURFACE)
		die("eglCreatePbufferSurface");
	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
	if (ctx == EGL_NO_CONTEXT)
		die("eglCreateContext");
	if (!eglMakeCurrent(dpy, surf, surf, ctx))
		die("eglMakeCurrent");
	printf("GL_VENDOR   %s\nGL_RENDERER %s\nGL_VERSION  %s\n",
	       glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));

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
	eglDestroySurface(dpy, surf);
	eglTerminate(dpy);
	return 0;
}
