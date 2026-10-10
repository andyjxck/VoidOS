#ifndef H_OS_UTIL_
#define H_OS_UTIL_

#define POINTER_TO_UINT(p) ((unsigned int) ((uintptr_t) (p)))
#define UINT_TO_POINTER(u) ((void *) ((uintptr_t) (u)))
#define POINTER_TO_INT(p) ((int) ((intptr_t) (p)))
#define INT_TO_POINTER(u) ((void *) ((intptr_t) (u)))

#define CONTAINER_OF(ptr, type, field) \
        ((type *)(((char *)(ptr)) - offsetof(type, field)))

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) \
        (sizeof(array) / sizeof((array)[0]))
#endif

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

#endif
