// SPDX-License-Identifier: MIT
/*
 * Preload for libhybris programs on the stock Android 4.4 libraries: libhybris loads the stock
 * bionic libc.so but never runs its start-up code, so bionic's property area pointer stays NULL
 * and any property lookup made *inside* libc (not through the hooked entry points) crashes.
 * Call bionic's own __system_properties_init(), which maps /dev/__properties__.
 *
 * Android code addresses per-thread slots straight from the TLS register (tpidruro[slot], slot 3
 * is the GL dispatch table, 4 the GL context, 2 errno). Under glibc, tpidruro points to an 8-byte
 * thread control block that is directly followed by the static TLS of the first module with TLS,
 * so those writes would land in somebody's thread-local variables. This library carries a TLS block
 * meant to be that first module (preload it, the program itself must have no TLS) and checks it.
 */
#include <dlfcn.h>
#include <stdio.h>

void *android_dlopen(const char *filename, int flag);
void *android_dlsym(void *handle, const char *symbol);

#define ANDROID_TLS_SLOTS 64	/* BIONIC_TLS_SLOTS in Android 4.4 */

__thread void *hybris_android_tls[ANDROID_TLS_SLOTS - 2] __attribute__((aligned(8), tls_model("initial-exec")));

__attribute__((constructor)) static void hybris_bionic_init(void)
{
	void **tp;

	asm volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(tp));
	if ((void *)&hybris_android_tls[0] != (void *)&tp[2])
		fprintf(stderr, "hybris-bionic-init: Android TLS slots are not reserved (padding at %p, slot 2 at %p):"
			" preload this library first, before any library with TLS\n", (void *)hybris_android_tls,
			(void *)&tp[2]);

	void *libc = android_dlopen("libc.so", RTLD_NOW);
	int (*props_init)(void) = libc ? (int (*)(void))android_dlsym(libc, "__system_properties_init") : NULL;

	if (!props_init || props_init() != 0)
		fprintf(stderr, "hybris-bionic-init: bionic __system_properties_init failed (is /dev/__properties__ there?)\n");
}
