/* Minimal os_mbuf replacement for the host DFU test.
   Single-buffer mbufs chained with SLIST; om_omp carries the pool record
   so OS_MBUF_TRAILINGSPACE can report a real capacity. */
#ifndef VOIDOS_TEST_OS_MBUF_H
#define VOIDOS_TEST_OS_MBUF_H

#include <stdint.h>
#include "sys/queue.h"

struct os_mbuf_pool {
    uint16_t omp_databuf_len;
};

struct os_mbuf_pkthdr {
    uint16_t omp_len;
};

struct os_mbuf {
    struct os_mbuf_pkthdr om_pkthdr;
    SLIST_ENTRY(os_mbuf) om_next;
    struct os_mbuf_pool *om_omp;
    uint8_t *om_data;
    uint16_t om_len;
    uint16_t om_cap;
    uint8_t *om_alloc;
};

#define OS_MBUF_PKTHDR(__om) (&(__om)->om_pkthdr)
#define OS_MBUF_PKTLEN(__om) (OS_MBUF_PKTHDR(__om)->omp_len)
#define OS_MBUF_TRAILINGSPACE(__om) \
    ((uint16_t)((__om)->om_cap - (__om)->om_len))

int os_mbuf_copydata(const struct os_mbuf *m, int off, int len, void *dst);
int os_mbuf_append(struct os_mbuf *m, const void *data, uint16_t len);
struct os_mbuf *os_mbuf_get_pkthdr(uint8_t *buf, uint16_t cap);
void os_mbuf_free_chain(struct os_mbuf *m);
int os_msys_num_free(void);

#endif
