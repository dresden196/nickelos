/* Distributed under the terms of the GNU General Public License v2 */
/* Link-time stand-in for bionic's libdl.so (the real one is in Android). */
int dladdr(const void *a, void *i) { return 0; }
void *dlopen(const char *f, int fl) { return 0; }
void *dlsym(void *h, const char *s) { return 0; }
