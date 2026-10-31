// SPDX-License-Identifier: MIT
/*
 * Emulate GL_EXT_unpack_subimage on top of a GLES 2.0 driver that lacks it
 * (PowerVR SGX, TI DDK 1.17). weston >= 10 refuses to start its GL renderer
 * on GLES 2.0 without the extension. Preload this library:
 *   - glGetString(GL_EXTENSIONS) also reports GL_EXT_unpack_subimage
 *   - glPixelStorei(GL_UNPACK_ROW_LENGTH/SKIP_ROWS/SKIP_PIXELS) is recorded
 *     instead of being passed to the driver
 *   - glTexImage2D/glTexSubImage2D repack such uploads into tight rows first
 */
#define _GNU_SOURCE
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXT_NAME "GL_EXT_unpack_subimage"

static GLint row_length, skip_rows, skip_pixels, alignment = 4;

/*
 * weston loads libGLESv2 through a dlopen()ed module with local scope, which
 * RTLD_NEXT does not search, so fall back to a handle on the library itself.
 */
static void *real_sym(const char *name)
{
	static void *gles;
	void *sym = dlsym(RTLD_NEXT, name);

	if (sym)
		return sym;
	if (!gles)
		gles = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_LOCAL);
	return gles ? dlsym(gles, name) : NULL;
}

#define REAL(ret, name, args)						\
	static ret (*real_##name) args;					\
	if (!real_##name)						\
		real_##name = (ret (*) args)real_sym(#name)

static int bytes_per_pixel(GLenum format, GLenum type)
{
	int comps;

	switch (type) {
	case GL_UNSIGNED_SHORT_5_6_5:
	case GL_UNSIGNED_SHORT_4_4_4_4:
	case GL_UNSIGNED_SHORT_5_5_5_1:
		return 2;
	case GL_UNSIGNED_INT_2_10_10_10_REV_EXT:
		return 4;
	}
	switch (format) {
	case GL_ALPHA:
	case GL_LUMINANCE:
	case GL_RED_EXT:
		comps = 1;
		break;
	case GL_LUMINANCE_ALPHA:
	case GL_RG_EXT:
		comps = 2;
		break;
	case GL_RGB:
		comps = 3;
		break;
	default:	/* GL_RGBA, GL_BGRA_EXT */
		comps = 4;
	}
	switch (type) {
	case GL_UNSIGNED_SHORT:
	case GL_HALF_FLOAT_OES:
		return comps * 2;
	case GL_FLOAT:
	case GL_UNSIGNED_INT:
		return comps * 4;
	default:
		return comps;
	}
}

/*
 * Returns the pointer to hand to the driver: the caller's, or a tightly
 * packed copy (*copy, free it afterwards) when row length or skips are set.
 * *packed tells whether GL_UNPACK_ALIGNMENT must be 1 for the call.
 */
static const void *repack(GLsizei w, GLsizei h, GLenum format, GLenum type,
			  const void *pixels, void **copy, int *packed)
{
	int bpp = bytes_per_pixel(format, type);
	size_t row = (size_t)w * bpp, stride;
	const uint8_t *src;
	uint8_t *dst;
	GLsizei y;

	*copy = NULL;
	*packed = 0;
	if (!pixels || (!row_length && !skip_rows && !skip_pixels))
		return pixels;
	stride = (size_t)(row_length ? row_length : w) * bpp;
	stride = (stride + alignment - 1) / alignment * alignment;
	src = (const uint8_t *)pixels + skip_rows * stride + (size_t)skip_pixels * bpp;
	dst = malloc(row * h);
	if (!dst)
		return src;
	for (y = 0; y < h; y++)
		memcpy(dst + y * row, src + y * stride, row);
	*copy = dst;
	*packed = 1;
	return dst;
}

const GLubyte *glGetString(GLenum name)
{
	static char *ext;
	const GLubyte *s;
	REAL(const GLubyte *, glGetString, (GLenum));

	s = real_glGetString(name);
	if (name != GL_EXTENSIONS || !s || strstr((const char *)s, EXT_NAME))
		return s;
	if (!ext && asprintf(&ext, "%s %s", (const char *)s, EXT_NAME) < 0)
		ext = NULL;
	return ext ? (const GLubyte *)ext : s;
}

void glPixelStorei(GLenum pname, GLint param)
{
	REAL(void, glPixelStorei, (GLenum, GLint));

	switch (pname) {
	case GL_UNPACK_ROW_LENGTH_EXT:
		row_length = param;
		return;
	case GL_UNPACK_SKIP_ROWS_EXT:
		skip_rows = param;
		return;
	case GL_UNPACK_SKIP_PIXELS_EXT:
		skip_pixels = param;
		return;
	case GL_UNPACK_ALIGNMENT:
		alignment = param;
		break;
	}
	real_glPixelStorei(pname, param);
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width,
		  GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels)
{
	void *copy;
	int packed;
	const void *p;
	REAL(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *));
	REAL(void, glPixelStorei, (GLenum, GLint));

	p = repack(width, height, format, type, pixels, &copy, &packed);
	if (packed)
		real_glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	real_glTexImage2D(target, level, internalformat, width, height, border, format, type, p);
	if (packed)
		real_glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
	free(copy);
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
		     GLsizei height, GLenum format, GLenum type, const void *pixels)
{
	void *copy;
	int packed;
	const void *p;
	REAL(void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *));
	REAL(void, glPixelStorei, (GLenum, GLint));

	p = repack(width, height, format, type, pixels, &copy, &packed);
	if (packed)
		real_glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	real_glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, p);
	if (packed)
		real_glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
	free(copy);
}
