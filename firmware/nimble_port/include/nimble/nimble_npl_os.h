/* Bare-metal NPL for VoidOS: single-threaded, cooperative.
   The LL and host event queues are drained from the main loop. */
#ifndef _NIMBLE_NPL_OS_H_
#define _NIMBLE_NPL_OS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_NPL_OS_ALIGNMENT    4
#define BLE_NPL_TIME_FOREVER    UINT32_MAX

typedef uint32_t ble_npl_time_t;
typedef int32_t ble_npl_stime_t;

struct ble_npl_event;

struct ble_npl_event {
    void (*fn)(struct ble_npl_event *ev);
    void *arg;
    struct ble_npl_event *next;
    uint8_t queued;
};

struct ble_npl_eventq {
    struct ble_npl_event *head;
};

struct ble_npl_callout {
    struct ble_npl_event ev;
    struct ble_npl_eventq *evq;
    uint32_t ticks;
    struct ble_npl_callout *next;
    uint8_t on_list;
};

struct ble_npl_mutex {
    uint8_t locked;
};

struct ble_npl_sem {
    volatile uint16_t tokens;
};

#ifdef __cplusplus
}
#endif

#endif  /* _NIMBLE_NPL_OS_H_ */
