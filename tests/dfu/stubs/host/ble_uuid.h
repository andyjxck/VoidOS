/* Minimal ble_uuid for the host DFU test. */
#ifndef VOIDOS_TEST_BLE_UUID_H
#define VOIDOS_TEST_BLE_UUID_H

#include <stdint.h>

#define BLE_UUID_TYPE_16  16
#define BLE_UUID_TYPE_128 128

typedef struct ble_uuid {
    uint8_t type;
} ble_uuid_t;

typedef struct {
    ble_uuid_t u;
    uint16_t value;
} ble_uuid16_t;

typedef struct {
    ble_uuid_t u;
    uint8_t value[16];
} ble_uuid128_t;

#define BLE_UUID16_INIT(v) \
    { .u = { .type = BLE_UUID_TYPE_16 }, .value = (v) }
#define BLE_UUID128_INIT(...) \
    { .u = { .type = BLE_UUID_TYPE_128 }, .value = { __VA_ARGS__ } }

#endif
