/* Distributed under the terms of the GNU General Public License v2 */
/* Link-time stand-in for bionic's libc.so (the real one is in Android). */
int poll(void *f, unsigned long n, int t) { return 0; }
int *__errno(void) { return 0; }
int fstat(int fd, void *st) { return -1; }
int clock_gettime(int c, void *ts) { return -1; }
void *memcpy(void *d, const void *s, unsigned long n) { return d; }
int pthread_create(void *t, const void *a, void *(*f)(void *), void *arg) { return -1; }
int pthread_mutex_lock(void *m) { return 0; }
int pthread_mutex_unlock(void *m) { return 0; }
int pthread_cond_wait(void *c, void *m) { return 0; }
int pthread_cond_signal(void *c) { return 0; }
int dup(int fd) { return -1; }
