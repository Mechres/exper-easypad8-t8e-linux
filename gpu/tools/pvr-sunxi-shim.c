// SPDX-License-Identifier: MIT
/*
 * TI's SGX user space (libsrv_um) only accepts a display device whose DRM
 * driver is called "omapdrm". Preload this to present sun4i-drm under that
 * name. Mesa's loader then picks omapdrm_dri.so, which is the same sgx driver.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <xf86drm.h>

drmVersionPtr drmGetVersion(int fd)
{
	static drmVersionPtr (*real)(int);
	drmVersionPtr v;
	char *name;

	if (!real)
		real = (drmVersionPtr (*)(int))dlsym(RTLD_NEXT, "drmGetVersion");
	v = real(fd);
	if (v && v->name && !strcmp(v->name, "sun4i-drm") && !getenv("PVR_SHIM_OFF")) {
		name = strdup("omapdrm");
		if (name) {
			free(v->name);	/* drmFreeVersion() frees it with free() too */
			v->name = name;
			v->name_len = strlen(name);
		}
	}
	return v;
}
