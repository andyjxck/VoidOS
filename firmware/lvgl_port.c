/* LVGL <-> PineTime glue: display flush, touch read, tick source. */
#include "lvgl.h"
#include "voidos/hal.h"
#include "board.h"

void display_init(void);
void display_send(int x0, int y0, int x1, int y1, const uint16_t *px);
int touch_sample(int *x, int *y);
int touch_gesture(void);
uint32_t hal_millis_raw(void);
int ui_asleep(void);
void ui_wake(void);
void ui_swipe(int cst_dir);

/* partial-render buffer: LVGL renders into this, we push it to the LCD */
static uint16_t draw_buf[240 * 32];

unsigned lvgl_flushes;

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    display_send(area->x1, area->y1, area->x2, area->y2,
                 (const uint16_t *)px);
    lvgl_flushes++;
    lv_display_flush_ready(disp);
}

/* The CST816S detects swipes in hardware: a gesture ID navigates
   directly via ui_swipe(), bypassing LVGL's gesture event routing.
   Afterwards eat_touch keeps reporting RELEASED until the finger lifts
   so the tail of the swipe can't click a control on the new screen. */
static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    static lv_point_t last;
    static int eat_touch;
    int x, y;

    if (ui_asleep()) {
        /* first touch wakes the screen; swallow it so it can't press */
        int g = touch_gesture();
        int x2, y2;
        if ((g >= 1 && g <= 4) || touch_sample(&x2, &y2))
            ui_wake();
        data->state = LV_INDEV_STATE_RELEASED;
        data->point = last;
        return;
    }

    int g = touch_gesture();
    if (g >= 1 && g <= 4) {
        ui_swipe(g);
        eat_touch = 1;
    }
    if (eat_touch) {
        if (!touch_sample(&x, &y))
            eat_touch = 0;
        data->state = LV_INDEV_STATE_RELEASED;
        data->point = last;
        return;
    }

    if (touch_sample(&x, &y)) {
        last.x = x;
        last.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
    data->point = last;
}

void lvgl_port_init(void)
{
    lv_init();
    lv_tick_set_cb(hal_millis_raw);
    /* display_init() already ran in hal_init(); running it again here
       switched the backlight off */

    lv_display_t *disp = lv_display_create(VOIDOS_WIDTH, VOIDOS_HEIGHT);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, draw_buf, NULL, sizeof draw_buf,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev, disp);
    lv_indev_set_read_cb(indev, read_cb);
}
