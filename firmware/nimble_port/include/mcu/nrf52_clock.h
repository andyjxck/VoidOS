#ifndef _MCU_NRF52_CLOCK_H
#define _MCU_NRF52_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

/* HFXO arbitration for the BLE PHY. Single-threaded system, so these
   just start/stop the crystal with a refcount to survive nesting. */
int nrf52_clock_hfxo_request(void);
int nrf52_clock_hfxo_release(void);

#ifdef __cplusplus
}
#endif

#endif
