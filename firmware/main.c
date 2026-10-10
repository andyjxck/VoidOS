/* VoidOS on PineTime, LVGL build. */
#include <stdint.h>
#include "lvgl.h"
#include "voidos/hal.h"
#include "board.h"

void lvgl_port_init(void);
void ui_init(void);
void ui_tick(void);
uint32_t button_held_ms(void);
int ble_init(void);
void ble_poll(void);
extern int nimble_last_stage(void);
extern int nimble_last_fault(void);
extern void nimble_stage(int);
extern void nimble_fault(int);

int boot_last_stage, boot_last_fault;

int main(void)
{
    wdt_kick();
    boot_last_stage = nimble_last_stage();  /* furthest stage of last run */
    boot_last_fault = nimble_last_fault();
    nimble_stage(0);                        /* 0 = reached app entry */
    nimble_fault(0);                        /* clear stale fault record */
    hal_init();
    lvgl_port_init();
    ui_init();
    ble_init();
    nimble_stage(99);                       /* 99 = init survived */

    for (;;) {
        ui_tick();
        lv_timer_handler();   /* render before ble_poll: if the BLE path
                                 hangs, the face + crash banner stay up */
        wdt_kick();
        ble_poll();
        if (button_held_ms() >= 3000)
            SCB_AIRCR = 0x05FA0004;      /* reset -> revert to InfiniTime */
        hal_delay_ms(5);
    }
}
