/* VoidOS bare-metal NPL implementation for Apache NimBLE.
   Single-threaded: event queues are drained cooperatively from the main
   loop (ble_poll), ISRs only ever *put* events. Callouts are ms-based and
   processed by npl_callout_tick() from the same loop. */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "nimble/nimble_npl.h"
#include "syscfg/syscfg.h"

extern uint32_t hal_millis_raw(void);
extern void wdt_kick(void);
extern void hal_delay_ms(unsigned ms);

/* Defined in ble_app.c: drains one pending LL event (blocking HCI callers
   pend on sems whose release happens in LL command processing). */
extern void ble_port_pump_llq(void);

#define SCB_VTOR  (*(volatile uint32_t *)0xE000ED08)
#define NVIC_ISER(n) (*(volatile uint32_t *)(0xE000E100 + (n) * 4))

/* ---- misc -------------------------------------------------------------- */

bool ble_npl_os_started(void) { return true; }
void *ble_npl_get_current_task_id(void) { return NULL; }

/* ---- critical sections -------------------------------------------------- */

uint32_t ble_npl_hw_enter_critical(void)
{
    uint32_t ctx;
    __asm volatile("mrs %0, primask; cpsid i" : "=r"(ctx));
    return ctx;
}

void ble_npl_hw_exit_critical(uint32_t ctx)
{
    __asm volatile("msr primask, %0" :: "r"(ctx));
}

bool ble_npl_hw_is_in_critical(void)
{
    uint32_t ctx;
    __asm volatile("mrs %0, primask" : "=r"(ctx));
    return (ctx & 1) != 0;
}

/* ---- events -------------------------------------------------------------- */

void ble_npl_eventq_init(struct ble_npl_eventq *evq)
{
    evq->head = NULL;
}

struct ble_npl_event *ble_npl_eventq_get(struct ble_npl_eventq *evq,
                                         ble_npl_time_t tmo)
{
    struct ble_npl_event *ev;
    uint32_t ctx = ble_npl_hw_enter_critical();
    if (evq->head) {
        ev = evq->head;
        evq->head = ev->next;
        ev->next = NULL;
        ev->queued = 0;
    } else if (tmo == BLE_NPL_TIME_FOREVER || tmo > 0) {
        /* blocking wait: poll until an event lands; kick the watchdog
           so a stalled queue doesn't reset us mid-wait */
        uint32_t start = hal_millis_raw();
        for (;;) {
            if (evq->head) {
                ev = evq->head;
                evq->head = ev->next;
                ev->next = NULL;
                ev->queued = 0;
                break;
            }
            /* producers run from ISRs: let them in between polls or
               this spins forever with PRIMASK set */
            ble_npl_hw_exit_critical(ctx);
            wdt_kick();
            ctx = ble_npl_hw_enter_critical();
            if (tmo != BLE_NPL_TIME_FOREVER &&
                hal_millis_raw() - start >= tmo) {
                ev = NULL;
                break;
            }
        }
    } else {
        ev = NULL;
    }
    ble_npl_hw_exit_critical(ctx);
    return ev;
}

void ble_npl_eventq_put(struct ble_npl_eventq *evq, struct ble_npl_event *ev)
{
    uint32_t ctx = ble_npl_hw_enter_critical();
    /* re-linking an already-queued event would corrupt the list into a
       cycle; mynewt asserts on this, we just refuse */
    if (ev->queued) {
        ble_npl_hw_exit_critical(ctx);
        return;
    }
    ev->next = NULL;
    ev->queued = 1;
    if (!evq->head) {
        evq->head = ev;
    } else {
        struct ble_npl_event *p = evq->head;
        while (p->next)
            p = p->next;
        p->next = ev;
    }
    ble_npl_hw_exit_critical(ctx);
}

void ble_npl_eventq_remove(struct ble_npl_eventq *evq,
                           struct ble_npl_event *ev)
{
    uint32_t ctx = ble_npl_hw_enter_critical();
    struct ble_npl_event **pp = &evq->head;
    while (*pp) {
        if (*pp == ev) {
            *pp = ev->next;
            ev->next = NULL;
            ev->queued = 0;
            break;
        }
        pp = &(*pp)->next;
    }
    ble_npl_hw_exit_critical(ctx);
}

void ble_npl_event_init(struct ble_npl_event *ev, ble_npl_event_fn *fn,
                        void *arg)
{
    ev->fn = fn;
    ev->arg = arg;
    ev->next = NULL;
    ev->queued = 0;
}

bool ble_npl_event_is_queued(struct ble_npl_event *ev)
{
    return ev->queued != 0;
}

void *ble_npl_event_get_arg(struct ble_npl_event *ev) { return ev->arg; }
void ble_npl_event_set_arg(struct ble_npl_event *ev, void *arg)
{
    ev->arg = arg;
}

bool ble_npl_eventq_is_empty(struct ble_npl_eventq *evq)
{
    return evq->head == NULL;
}

void ble_npl_event_run(struct ble_npl_event *ev)
{
    ev->fn(ev);
}

/* ---- mutex / sem ---------------------------------------------------------- */

ble_npl_error_t ble_npl_mutex_init(struct ble_npl_mutex *mu)
{
    mu->locked = 0;
    return BLE_NPL_OK;
}

ble_npl_error_t ble_npl_mutex_pend(struct ble_npl_mutex *mu,
                                   ble_npl_time_t timeout)
{
    (void)timeout;
    mu->locked = 1;
    return BLE_NPL_OK;
}

ble_npl_error_t ble_npl_mutex_release(struct ble_npl_mutex *mu)
{
    mu->locked = 0;
    return BLE_NPL_OK;
}

ble_npl_error_t ble_npl_sem_init(struct ble_npl_sem *sem, uint16_t tokens)
{
    sem->tokens = tokens;
    return BLE_NPL_OK;
}

ble_npl_error_t ble_npl_sem_pend(struct ble_npl_sem *sem,
                                 ble_npl_time_t timeout)
{
    uint32_t start = hal_millis_raw();
    for (;;) {
        uint32_t ctx = ble_npl_hw_enter_critical();
        if (sem->tokens) {
            sem->tokens--;
            ble_npl_hw_exit_critical(ctx);
            return BLE_NPL_OK;
        }
        ble_npl_hw_exit_critical(ctx);
        if (timeout == 0)
            return BLE_NPL_TIMEOUT;
        if (timeout != BLE_NPL_TIME_FOREVER &&
            hal_millis_raw() - start >= timeout)
            return BLE_NPL_TIMEOUT;
        /* Single-threaded port: the sem release we are waiting on is
           produced by LL-queue work that can't run while we spin here —
           drain it cooperatively or the wait is a guaranteed timeout. */
        ble_port_pump_llq();
        wdt_kick();
    }
}

ble_npl_error_t ble_npl_sem_release(struct ble_npl_sem *sem)
{
    uint32_t ctx = ble_npl_hw_enter_critical();
    if (sem->tokens < 0xFFFF)
        sem->tokens++;
    ble_npl_hw_exit_critical(ctx);
    return BLE_NPL_OK;
}

uint16_t ble_npl_sem_get_count(struct ble_npl_sem *sem)
{
    return sem->tokens;
}

/* ---- callouts -------------------------------------------------------------- */

static struct ble_npl_callout *callouts;

void ble_npl_callout_init(struct ble_npl_callout *co,
                          struct ble_npl_eventq *evq,
                          ble_npl_event_fn *ev_cb, void *ev_arg)
{
    memset(co, 0, sizeof *co);
    ble_npl_event_init(&co->ev, ev_cb, ev_arg);
    co->evq = evq;
}

ble_npl_error_t ble_npl_callout_reset(struct ble_npl_callout *co,
                                      ble_npl_time_t ticks)
{
    uint32_t ctx = ble_npl_hw_enter_critical();
    co->ticks = hal_millis_raw() + ticks;
    if (!co->on_list && !co->ev.queued) {
        co->next = callouts;
        callouts = co;
        co->on_list = 1;
    }
    ble_npl_hw_exit_critical(ctx);
    return BLE_NPL_OK;
}

void ble_npl_callout_stop(struct ble_npl_callout *co)
{
    uint32_t ctx = ble_npl_hw_enter_critical();
    struct ble_npl_callout **pp = &callouts;
    while (*pp) {
        if (*pp == co) {
            *pp = co->next;
            co->next = NULL;
            co->on_list = 0;
            break;
        }
        pp = &(*pp)->next;
    }
    ble_npl_hw_exit_critical(ctx);
}

bool ble_npl_callout_is_active(struct ble_npl_callout *co)
{
    return co->on_list || co->ev.queued;
}

ble_npl_time_t ble_npl_callout_get_ticks(struct ble_npl_callout *co)
{
    return co->ticks;
}

ble_npl_time_t ble_npl_callout_remaining_ticks(struct ble_npl_callout *co,
                                               ble_npl_time_t time)
{
    return (ble_npl_time_t)(co->ticks - time);
}

void ble_npl_callout_set_arg(struct ble_npl_callout *co, void *arg)
{
    co->ev.arg = arg;
}

/* called each main-loop iteration: fires due callouts onto their queues */
void npl_callout_tick(void)
{
    uint32_t now = hal_millis_raw();
    for (;;) {
        uint32_t ctx = ble_npl_hw_enter_critical();
        struct ble_npl_callout **pp = &callouts;
        struct ble_npl_callout *due = NULL;
        while (*pp) {
            if ((int32_t)(now - (*pp)->ticks) >= 0) {
                due = *pp;
                *pp = due->next;
                due->next = NULL;
                due->on_list = 0;
                break;
            }
            pp = &(*pp)->next;
        }
        ble_npl_hw_exit_critical(ctx);
        if (!due)
            return;
        if (due->evq)
            ble_npl_eventq_put(due->evq, &due->ev);
        else
            ble_npl_event_run(&due->ev);
    }
}

/* ---- time ----------------------------------------------------------------- */

ble_npl_time_t ble_npl_time_get(void)
{
    return hal_millis_raw();
}

ble_npl_error_t ble_npl_time_ms_to_ticks(uint32_t ms, ble_npl_time_t *out)
{
    *out = ms;
    return BLE_NPL_OK;
}

ble_npl_error_t ble_npl_time_ticks_to_ms(ble_npl_time_t ticks, uint32_t *out)
{
    *out = ticks;
    return BLE_NPL_OK;
}

ble_npl_time_t ble_npl_time_ms_to_ticks32(uint32_t ms) { return ms; }
uint32_t ble_npl_time_ticks_to_ms32(ble_npl_time_t ticks) { return ticks; }

void ble_npl_time_delay(ble_npl_time_t ticks)
{
    hal_delay_ms(ticks);
}

/* ---- ISR wiring -------------------------------------------------------------- */

void ble_npl_hw_set_isr(int irqn, void (*addr)(void))
{
    /* MCUBoot is supposed to relocate our table to RAM, but if VTOR still
       points at flash a write here silently does nothing and the IRQ
       lands in Default_Handler. Relocate once ourselves if needed. */
    static uint32_t vt_ram[16 + 39] __attribute__((aligned(256)));
    static int relocated;
    if (SCB_VTOR < 0x20000000 && !relocated) {
        const volatile uint32_t *src = (const volatile uint32_t *)SCB_VTOR;
        for (int i = 0; i < 16 + 39; i++)
            vt_ram[i] = src[i];
        __asm volatile("dsb");
        SCB_VTOR = (uint32_t)vt_ram;
        __asm volatile("dsb; isb");
        relocated = 1;
    }
    volatile uint32_t *vt = (volatile uint32_t *)SCB_VTOR;
    vt[16 + irqn] = (uint32_t)addr;
    NVIC_ISER(irqn >> 5) = 1u << (irqn & 31);
}
