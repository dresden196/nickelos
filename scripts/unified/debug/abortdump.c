/* Test only: init (PID 1) aborts before it can log, and its stderr goes to
 * /dev/null. Keep stderr on /dev/kmsg (the serial console): dup2/dup3 onto
 * fd 2 are skipped. glibc's fatal message then shows. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
static int (*real_dup2)(int, int);
static int (*real_dup3)(int, int, int);
int dup2(int a, int b) {
	if (!real_dup2) real_dup2 = dlsym(RTLD_NEXT, "dup2");
	if (b == 2) return 2;
	return real_dup2(a, b);
}
int dup3(int a, int b, int f) {
	if (!real_dup3) real_dup3 = dlsym(RTLD_NEXT, "dup3");
	if (b == 2) return 2;
	return real_dup3(a, b, f);
}
__attribute__((constructor)) static void init(void) {
	int fd = open("/dev/kmsg", O_WRONLY);
	if (fd >= 0) {
		int (*d2)(int, int) = dlsym(RTLD_NEXT, "dup2");
		d2(fd, 2);
		if (write(2, "abortdump: stderr on kmsg\n", 26) < 0) {}
	}
}
