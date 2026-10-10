/* Minimal SLIST subset for the host DFU test (replaces sys/queue.h). */
#ifndef VOIDOS_TEST_QUEUE_H
#define VOIDOS_TEST_QUEUE_H

#define SLIST_ENTRY(type) struct { struct type *sle_next; }
#define SLIST_HEAD(name, type) struct name { struct type *slh_first; }
#define SLIST_FIRST(head) ((head)->slh_first)
#define SLIST_NEXT(elm, field) ((elm)->field.sle_next)
#define SLIST_EMPTY(head) (SLIST_FIRST(head) == NULL)
#define SLIST_INIT(head) ((head)->slh_first = NULL)
#define SLIST_INSERT_HEAD(head, elm, field) do { \
    (elm)->field.sle_next = (head)->slh_first; \
    (head)->slh_first = (elm); \
} while (0)
#define SLIST_FOREACH(var, head, field) \
    for ((var) = SLIST_FIRST(head); (var); (var) = SLIST_NEXT(var, field))

#endif
