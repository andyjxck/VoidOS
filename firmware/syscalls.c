/* newlib-nano syscall stubs: VoidOS never touches files or a heap on the
   watch, but snprintf drags the reentrant I/O layer in. Keep them tiny. */
#include <errno.h>
#include <sys/stat.h>

int _close(int fd) { (void)fd; return -1; }
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd) { (void)fd; return 1; }
int _lseek(int fd, int off, int whence) { (void)fd; (void)off; (void)whence; return 0; }
int _read(int fd, char *buf, int len) { (void)fd; (void)buf; (void)len; return 0; }
int _write(int fd, const char *buf, int len) { (void)fd; (void)buf; return len; }
/* NimBLE's GATT registration grows arrays with realloc; the host needs a
   small real heap. Cap it at 4 KB so the ISR/main stack keeps the rest. */
extern char end, _stack_top;
void *_sbrk(int incr)
{
    static char *brk;
    char *prev;
    if (!brk)
        brk = &end;
    prev = brk;
    uintptr_t nxt = (uintptr_t)brk + incr;
    if (incr < 0 || nxt > (uintptr_t)&end + 4096 ||
        nxt > (uintptr_t)&_stack_top - 6144) {
        errno = ENOMEM;
        return (void *)-1;
    }
    brk = (char *)nxt;
    return prev;
}
void _exit(int code) { (void)code; for (;;) ; }
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
int _getpid(void) { return 1; }
