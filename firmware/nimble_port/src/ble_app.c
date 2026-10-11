/* VoidOS BLE: NimBLE host + controller on the bare-metal port.
   One cooperative pump (ble_poll) drains both event queues; the main
   loop calls it. Currently: GAP peripheral named "VoidOS", re-advertising
   on disconnect. DFU/data services layer on top of this. */
#include <string.h>
#include "os/os.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "nimble/nimble_port.h"
#include "controller/ble_ll.h"
#include "controller/ble_phy.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

extern void nrf52_clock_hfxo_request(void);
extern void npl_callout_tick(void);
extern void nimble_mark(int stage);

static int ble_up;
static int ble_adv_on;
static int ble_want = 1;        /* desired radio state, valid before init */
static int ble_synced;
static uint32_t adv_check_ms;
static uint16_t conn_handle;
static uint8_t own_addr_type = BLE_OWN_ADDR_PUBLIC;
/* diagnostics visible on the stats screen while BLE is being brought up */
static int d_syncs, d_adv_calls, d_adv_rc = -1, d_gap_events;
int d_subscribed, d_mtu_events;   /* exposed to the DFU diag char */
static int d_ll_evs, d_hs_evs;
int d_ll_cmds, d_hs_evt_tx;   /* patched into ble_ll_hci.c */
int d_st1, d_st2, d_st2rc = -9, d_startgo, d_resetrc = -9; /* ble_hs startup */

static void ble_advertise(void);
int ble_connected(void);

void ancs_on_connect(uint16_t conn);
void ancs_on_disconnect(void);
void ancs_on_enc_ok(void);
void ancs_on_notify(uint16_t attr_handle, struct os_mbuf *om);
void ancs_tick(void);
int ble_store_ram_init(void);

static int gap_cb(struct ble_gap_event *ev, void *arg)
{
    (void)arg;
    d_gap_events++;
    switch (ev->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (ev->connect.status == 0) {
            conn_handle = ev->connect.conn_handle;
            /* ANCS discovery kicks off as soon as the link settles */
            ancs_on_connect(conn_handle);
        } else {
            ble_advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ancs_on_disconnect();
        ble_advertise();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        d_subscribed++;
        break;
    case BLE_GAP_EVENT_NOTIFY_RX:
        /* incoming GATT notifications on client handles (ANCS NS/DS) */
        ancs_on_notify(ev->notify_rx.attr_handle, ev->notify_rx.om);
        break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (ev->enc_change.status == 0)
            ancs_on_enc_ok();
        break;
    case BLE_GAP_EVENT_MTU:
        d_mtu_events++;
        break;
    default:
        break;
    }
    return 0;
}

static void ble_advertise(void)
{
    if (!ble_adv_on)
        return;
    struct ble_hs_adv_fields f;
    memset(&f, 0, sizeof f);
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.name = (uint8_t *)"VoidOS";
    f.name_len = 6;
    f.name_is_complete = 1;
    ble_gap_adv_set_fields(&f);

    struct ble_gap_adv_params p;
    memset(&p, 0, sizeof p);
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    d_adv_calls++;
    d_adv_rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                                 &p, gap_cb, NULL);
}

static void on_sync(void)
{
    /* public addr if burnt in, else the FICR static random */
    d_syncs++;
    ble_synced = 1;
    ble_hs_id_infer_auto(0, &own_addr_type);
    ble_adv_on = ble_want;
    ble_advertise();
}

static void on_reset(int reason)
{
    (void)reason;
}

int ble_init(void)
{
    if (ble_up)
        return 1;
    nrf52_clock_hfxo_request();
    nimble_port_init();
    nimble_mark(10);
    ble_svc_gap_init();
    ble_svc_gatt_init();
    extern void ble_dfu_init(void);
    ble_dfu_init();
    nimble_mark(11);
    ble_svc_gap_device_name_set("VoidOS");
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    /* ANCS requires an encrypted link; a screenless watch can only do
       Just Works, which iOS accepts */
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_HS_KEY_DIST_ENC_KEY |
                                 BLE_HS_KEY_DIST_ID_KEY;
    ble_hs_cfg.sm_their_key_dist = BLE_HS_KEY_DIST_ENC_KEY |
                                   BLE_HS_KEY_DIST_ID_KEY;
    ble_store_ram_init();            /* bonds live for the session */
    ble_up = 1;
    return 1;
}

/* Pop and run one LL-queue event. Used by ble_poll and by ble_npl_sem_pend
   (blocking HCI calls only complete when the LL queue is serviced). */
void ble_port_pump_llq(void)
{
    struct ble_npl_event *ev = ble_npl_eventq_get(&g_ble_ll_data.ll_evq, 0);
    if (ev) {
        d_ll_evs++;
        ble_npl_event_run(ev);
    }
}

/* drain the LL + host event queues; called from the main loop.
   Bounded so a busy queue can never starve the watchdog. */
void ble_poll(void)
{
    if (!ble_up)
        return;
    /* Cooperative replacement for the first half of ble_ll_task(): the
       blocking task loop doesn't exist on bare metal, so run its one-time
       PHY/tx-power preamble here before draining the LL queue. */
    static int ll_started;
    if (!ll_started) {
        extern int ble_ll_tx_power_round(int);
        extern void ble_ll_tx_power_set(int);
        extern int8_t g_ble_ll_tx_power;
        extern void nimble_stage(int);
        ll_started = 1;
        nimble_stage(12);
        ble_phy_init();
        nimble_stage(13);
        g_ble_ll_tx_power = ble_ll_tx_power_round(
            MYNEWT_VAL(BLE_LL_TX_PWR_DBM) < MYNEWT_VAL(BLE_LL_TX_PWR_MAX_DBM)
                ? MYNEWT_VAL(BLE_LL_TX_PWR_DBM)
                : MYNEWT_VAL(BLE_LL_TX_PWR_MAX_DBM));
        ble_ll_tx_power_set(g_ble_ll_tx_power);
        nimble_stage(14);
    }
    npl_callout_tick();
    ancs_tick();
    /* advertising watchdog: any failed/interrupted adv start (disconnect
       races, failed pairing) would otherwise leave the watch invisible */
    {
        extern uint32_t hal_millis(void);
        uint32_t now = hal_millis();
        if (ble_synced && ble_adv_on && now - adv_check_ms > 2000) {
            adv_check_ms = now;
            if (!ble_gap_adv_active() && !ble_connected())
                ble_advertise();
        }
    }
    for (int i = 0; i < 32; i++) {
        extern void nimble_stage(int);
        struct ble_npl_event *ev =
            ble_npl_eventq_get(&g_ble_ll_data.ll_evq, 0);
        if (!ev)
            break;
        d_ll_evs++;
        if (d_ll_evs < 10)
            nimble_stage(30 + d_ll_evs);   /* 31..39 = about to run LL ev N */
        ble_npl_event_run(ev);
        if (d_ll_evs < 10)
            nimble_stage(40 + d_ll_evs);   /* 41..49 = LL ev N completed */
    }
    for (int i = 0; i < 32; i++) {
        struct ble_npl_event *ev =
            ble_npl_eventq_get(nimble_port_get_dflt_eventq(), 0);
        if (!ev)
            break;
        d_hs_evs++;
        ble_npl_event_run(ev);
    }
}

int ble_connected(void)
{
    struct ble_gap_conn_desc d;
    return ble_up && ble_gap_conn_find(conn_handle, &d) == 0;
}

void ble_diag(int *syncs, int *adv_calls, int *adv_rc, int *gap_events,
              int *addr_type)
{
    extern uint8_t ble_hs_sync_state;
    /* diag packing: syncs=state, adv_calls=st1*10+st2, adv_rc=hs_start
       rc (-9 = never ran), gap_events=reset cmd rc (-9 = never ran) */
    extern uint8_t ble_hs_sync_state;
    if (syncs)      *syncs = ble_hs_sync_state;   /* 0 bad 1 bringup 2 good */
    if (adv_calls)  *adv_calls = d_st1 * 10 + d_st2;
    if (adv_rc)     *adv_rc = d_st2rc;
    if (gap_events) *gap_events = d_resetrc;
    if (gap_events) *gap_events = d_gap_events;
    if (addr_type)  *addr_type = own_addr_type;
}

void ble_set_enabled(int on)
{
    ble_want = on != 0;
    if (!ble_up || !ble_synced)
        return;                      /* on_sync applies ble_want */
    ble_adv_on = ble_want;
    if (on) {
        ble_advertise();
    } else {
        ble_gap_adv_stop();
        if (ble_connected())
            ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

/* 0 off, 1 waiting for sync, 2 advertising, 3 connected, 4 idle/retrying */
int ble_status(void)
{
    if (!ble_want)
        return 0;
    if (!ble_synced)
        return 1;
    if (ble_connected())
        return 3;
    return ble_gap_adv_active() ? 2 : 4;
}
