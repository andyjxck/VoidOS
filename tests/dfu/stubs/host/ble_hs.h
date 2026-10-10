/* Minimal ble_hs for the host DFU test. */
#ifndef VOIDOS_TEST_BLE_HS_H
#define VOIDOS_TEST_BLE_HS_H

#include <stdint.h>
#include "os/os_mbuf.h"

struct os_mbuf *ble_hs_mbuf_from_flat(const void *buf, uint16_t len);
int ble_gatts_notify_custom(uint16_t conn_handle, uint16_t chr_val_handle,
                            struct os_mbuf *om);
int ble_gatts_count_cfg(const void *svcs);
int ble_gatts_add_svcs(const void *svcs);

#endif
