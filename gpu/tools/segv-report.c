// SPDX-License-Identifier: MIT
/*
 * Preload crash reporter for glibc programs on the tablet (no gdb there):
 * on SIGSEGV/SIGBUS/SIGILL/SIGABRT print the fault address, PC and LR from
 * the signal context as library+offset, plus a backtrace(), then re-raise.
 * Addresses in libraries glibc does not know (e.g. loaded by libhybris' bionic
 * linker) are looked up in /proc/self/maps instead.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

static int from_maps(const char *what, unsigned long addr)
{
	unsigned long start, end, off;
	char line[512], path[256];
	FILE *f = fopen("/proc/self/maps", "r");
	int found = 0;

	if (!f)
		return 0;
	while (!found && fgets(line, sizeof(line), f)) {
		path[0] = '\0';
		if (sscanf(line, "%lx-%lx %*s %lx %*s %*s %255s", &start, &end, &off, path) >= 3 &&
		    addr >= start && addr < end) {
			fprintf(stderr, "segv-report: %s %#lx = %s file offset %#lx (maps)\n", what, addr,
				path[0] ? path : "[anon]", addr - start + off);
			found = 1;
		}
	}
	fclose(f);
	return found;
}

static void where(const char *what, unsigned long addr)
{
	Dl_info info;

	if (dladdr((void *)addr, &info) && info.dli_fname)
		fprintf(stderr, "segv-report: %s %#lx = %s+%#lx (%s)\n", what, addr, info.dli_fname,
			addr - (unsigned long)info.dli_fbase, info.dli_sname ? info.dli_sname : "?");
	else if (!from_maps(what, addr))
		fprintf(stderr, "segv-report: %s %#lx (unknown)\n", what, addr);
}

static void handler(int sig, siginfo_t *si, void *ctx)
{
	ucontext_t *uc = ctx;
	void *bt[32];
	int n;

	fprintf(stderr, "segv-report: signal %d (%s), fault address %p\n", sig, strsignal(sig), si->si_addr);
	where("pc", uc->uc_mcontext.arm_pc);
	where("lr", uc->uc_mcontext.arm_lr);
	n = backtrace(bt, 32);
	backtrace_symbols_fd(bt, n, 2);
	signal(sig, SIG_DFL);
	raise(sig);
}

__attribute__((constructor)) static void install(void)
{
	struct sigaction sa = { .sa_sigaction = handler, .sa_flags = SA_SIGINFO };

	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);
	sigaction(SIGILL, &sa, NULL);
	sigaction(SIGABRT, &sa, NULL);
}
