#include <math.h>
#include "lvgl.h"
#include "voidos/hal.h"
#include "board.h"

extern const lv_font_t kenney9, kenney16, kenney24, kenney56;
int sensors_bpm(void);
int sensors_steps(void);
int sensors_ppg(int i, int *out_y100);
int sensors_hrs_ok(void);
int hal_clock_synced(void);
void hal_clock_date(int *y, int *mo, int *d, int *dow);

static lv_obj_t *lbl_hour, *lbl_min, *lbl_colon, *lbl_sec;
static lv_obj_t *lbl_bpm, *lbl_steps, *lbl_tag, *ecg_obj, *heart_obj;
static lv_obj_t *lbl_diag, *lbl_date;
static uint32_t frame_ms;
static int lap_minute = -1;

static void draw_line(lv_layer_t *layer, int x0, int y0, int x1, int y1,
                      lv_color_t color, int width)
{
    const lv_area_t *clip = &layer->_clip_area;
    if ((x0 < clip->x1 - width && x1 < clip->x1 - width) ||
        (x0 > clip->x2 + width && x1 > clip->x2 + width) ||
        (y0 < clip->y1 - width && y1 < clip->y1 - width) ||
        (y0 > clip->y2 + width && y1 > clip->y2 + width))
        return;
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.p1.x = x0;
    dsc.p1.y = y0;
    dsc.p2.x = x1;
    dsc.p2.y = y1;
    dsc.color = color;
    dsc.width = width;
    lv_draw_line(layer, &dsc);
}

static void ecg_draw(lv_event_t *ev)
{
    static const int8_t waveform[24] = {
        0, 0, -1, -2, -1, 0, 2, -8, 15, -5, 2, 0,
        1, 3, 4, 3, 1, 0, 0, 0, 0, 0, 0, 0
    };
    lv_layer_t *layer = lv_event_get_layer(ev);
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(ev), &area);
    int x0 = area.x1 + 2, x1 = area.x2 - 2, mid = area.y1 + 19;
    int y100;
    if (sensors_ppg(0, &y100)) {
        /* real PPG: 128-sample buffer (~2.5 s) stretched across the strip */
        int prev_y = mid;
        for (int x = x0; x <= x1; x++) {
            int i = (x - x0) * 128 / (x1 - x0);
            int y = y100;
            if (x > x0)
                sensors_ppg(i, &y);
            int py = mid - y * 14 / 100;
            if (x > x0)
                draw_line(layer, x - 1, prev_y, x, py,
                          lv_color_hex(0x5AC8D2), 1);
            prev_y = py;
        }
        return;
    }
    int phase = (int)(frame_ms / 42) % 24;
    int prev_y = mid;
    for (int x = x0; x <= x1; x++) {
        int y = mid - waveform[(x / 3 + phase) % 24];
        if (x > x0)
            draw_line(layer, x - 1, prev_y, x, y, lv_color_hex(0x5AC8D2), 1);
        prev_y = y;
    }
}

static void heart_draw(lv_event_t *ev)
{
    static const char shape[6][8] = {
        ".XX.XX.", "XXXXXXX", "XXXXXXX", ".XXXXX.", "..XXX..", "...X..."
    };
    lv_layer_t *layer = lv_event_get_layer(ev);
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(ev), &area);
    int intensity = (int)(frame_ms / 110) % 8;
    lv_draw_fill_dsc_t fill;
    lv_draw_fill_dsc_init(&fill);
    fill.color = lv_color_hex(intensity < 2 ? 0xFF9898 : 0xE63C3C);
    for (int y = 0; y < 6; y++)
        for (int x = 0; x < 7; x++) {
            if (shape[y][x] != 'X') continue;
            lv_area_t pixel = {area.x1 + x * 2, area.y1 + y * 2,
                               area.x1 + x * 2 + 1, area.y1 + y * 2 + 1};
            lv_draw_fill(layer, &fill, &pixel);
        }
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font,
                       uint32_t color, int x, int y, int width, const char *value)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_text(obj, value);
    return obj;
}

static lv_obj_t *card(lv_obj_t *p, int x, int y, int w, int h)
{
    lv_obj_t *c = lv_obj_create(p);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_radius(c, 14, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(0x0A0E15), 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_shadow_width(c, 0, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

void ui_face_build(lv_obj_t *scr)
{
    /* true black: the dead bezel disappears into the background */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    lbl_hour = label(scr, &kenney56, 0xFFFFFF, 32, 48, LV_SIZE_CONTENT, "--");
    lbl_colon = label(scr, &kenney56, 0xFFFFFF, 109, 48, LV_SIZE_CONTENT, ":");
    lbl_min = label(scr, &kenney56, 0xFFFFFF, 130, 48, LV_SIZE_CONTENT, "--");
    lbl_sec = label(scr, &kenney24, 0x5AC8D2, 91, 106, 58, "--");

    /* ECG strip card */
    card(scr, 14, 134, 212, 40);
    ecg_obj = lv_obj_create(scr);
    lv_obj_set_pos(ecg_obj, 16, 136);
    lv_obj_set_size(ecg_obj, 208, 36);
    lv_obj_set_style_bg_opa(ecg_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ecg_obj, 0, 0);
    lv_obj_set_style_pad_all(ecg_obj, 0, 0);
    lv_obj_remove_flag(ecg_obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(ecg_obj, ecg_draw, LV_EVENT_DRAW_MAIN, NULL);

    /* HR card: heart + bpm */
    card(scr, 14, 182, 104, 40);
    heart_obj = lv_obj_create(scr);
    lv_obj_set_pos(heart_obj, 26, 196);
    lv_obj_set_size(heart_obj, 14, 12);
    lv_obj_set_style_bg_opa(heart_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(heart_obj, 0, 0);
    lv_obj_set_style_pad_all(heart_obj, 0, 0);
    lv_obj_remove_flag(heart_obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(heart_obj, heart_draw, LV_EVENT_DRAW_MAIN, NULL);
    lbl_bpm = label(scr, &kenney16, 0xFFFFFF, 46, 192, 72, "-- BPM");
    lv_obj_set_style_text_align(lbl_bpm, LV_TEXT_ALIGN_LEFT, 0);
    lbl_tag = label(scr, &kenney9, 0x8890A0, 46, 208, 72, "");
    lv_obj_set_style_text_align(lbl_tag, LV_TEXT_ALIGN_LEFT, 0);

    /* steps card: value + tag, same layout as the HR card. The old
       "STEPS n" string overflowed the card past 9,999 and clipped
       against its right edge */
    card(scr, 122, 182, 104, 40);
    lbl_steps = label(scr, &kenney16, 0xFFFFFF, 130, 190, 88, "--");
    lv_obj_set_style_text_align(lbl_steps, LV_TEXT_ALIGN_LEFT, 0);
    label(scr, &kenney9, 0x8890A0, 130, 206, 80, "STEPS");

    lbl_date = label(scr, &kenney9, 0x59647B, 60, 226, 120, "TIME UNSYNCED");
    lbl_diag = label(scr, &kenney16, 0xFF8040, 4, 2, 232, "");
    (void)lbl_steps;
}

void ui_face_tick(void)
{
    static uint32_t last_anim = UINT32_MAX;
    static int last_hour = -1, last_bpm = -1, last_blink = -1,
               last_steps = -1, lap_second = -1;
    frame_ms = hal_millis();
    int second = hal_clock_second();
    int minute = hal_clock_minute();
    int hour = hal_clock_hour();
    char buf[12];
    int time_changed = 0;
    if (hour != last_hour) {
        lv_snprintf(buf, sizeof buf, "%02d", hour);
        lv_label_set_text(lbl_hour, buf);
        last_hour = hour;
        time_changed = 1;
    }
    if (minute != lap_minute) {
        lv_snprintf(buf, sizeof buf, "%02d", minute);
        lv_label_set_text(lbl_min, buf);
        lap_minute = minute;
        time_changed = 1;
    }
    if (time_changed) {
        lv_obj_update_layout(lbl_hour);
        lv_obj_update_layout(lbl_colon);
        lv_obj_update_layout(lbl_min);
        int hw = lv_obj_get_width(lbl_hour);
        int cw = lv_obj_get_width(lbl_colon);
        int mw = lv_obj_get_width(lbl_min);
        int x = (VOIDOS_WIDTH - hw - cw - mw) / 2;
        lv_obj_set_x(lbl_hour, x);
        lv_obj_set_x(lbl_colon, x + hw);
        lv_obj_set_x(lbl_min, x + hw + cw);
    }
    if (second != lap_second) {
        lv_snprintf(buf, sizeof buf, "%02d", second);
        lv_label_set_text(lbl_sec, buf);
        lap_second = second;
    }
    int blink = (frame_ms / 500) % 2;
    if (blink != last_blink) {
        lv_obj_set_style_text_opa(lbl_colon, blink ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
        last_blink = blink;
    }
    int bpm = sensors_bpm();
    if (bpm != last_bpm) {
        if (bpm > 0)
            lv_snprintf(buf, sizeof buf, "%d BPM", bpm);
        else
            lv_snprintf(buf, sizeof buf, "-- BPM");
        lv_label_set_text(lbl_bpm, buf);
        lv_label_set_text(lbl_tag, bpm > 0 ? "LIVE" :
                          bpm == 0 ? "SENSING" : "NO HRS");
        last_bpm = bpm;
    }
    {
        static int last_synced = -1;
        int synced = hal_clock_synced();
        if (synced != last_synced) {
            if (synced) {
                static const char *dn[8] = {"", "MON", "TUE", "WED",
                                          "THU", "FRI", "SAT", "SUN"};
                static const char *mn[13] = {"", "JAN", "FEB", "MAR",
                                             "APR", "MAY", "JUN", "JUL",
                                             "AUG", "SEP", "OCT", "NOV",
                                             "DEC"};
                int y, mo, d, dow;
                char db[20];
                hal_clock_date(&y, &mo, &d, &dow);
                lv_snprintf(db, sizeof db, "%s %d %s",
                            dow >= 1 && dow <= 7 ? dn[dow] : "???", d,
                            mo >= 1 && mo <= 12 ? mn[mo] : "???");
                lv_label_set_text(lbl_date, db);
            } else {
                lv_label_set_text(lbl_date, "TIME UNSYNCED");
            }
            last_synced = synced;
        }
    }
    int steps = sensors_steps();
    if (steps != last_steps) {
        if (steps >= 0)
            lv_snprintf(buf, sizeof buf, "%d", steps);
        else
            lv_snprintf(buf, sizeof buf, "--");
        lv_label_set_text(lbl_steps, buf);
        last_steps = steps;
    }
    if (frame_ms / 120 != last_anim) {
        lv_obj_invalidate(ecg_obj);
        lv_obj_invalidate(heart_obj);
        last_anim = frame_ms / 120;
    }
    {
        int s, ac, rc, ge, at;
        extern void ble_diag(int *, int *, int *, int *, int *);
        ble_diag(&s, &ac, &rc, &ge, &at);
        extern int nimble_last_stage(void);
        extern int boot_last_stage, boot_last_fault;
        int died = (boot_last_stage >= 0 && boot_last_stage < 99)
                   || (boot_last_fault >= 1 && boot_last_fault <= 4);
        char dbuf[64];
        if (died) {
            lv_snprintf(dbuf, sizeof dbuf, "DIED L%d F%X",
                        boot_last_stage, boot_last_fault);
        } else {
            int ds, dc, dp, nr; unsigned db;
            extern void ble_dfu_diag(int *, int *, int *, int *, unsigned *);
            ble_dfu_diag(&ds, &dc, &dp, &nr, &db);
            lv_snprintf(dbuf, sizeof dbuf,
                        "S%d T%u F%d %02X%02X%02X C%X W%u\nI:%02X,%02X,%02X D%dC%dP%dN%d",
                        s, touch_diag.reads, touch_diag.last_fingers,
                        touch_diag.raw[1], touch_diag.raw[2],
                        touch_diag.raw[3],
                        touch_diag.chip_id,
                        touch_diag.twi_err + touch_diag.twi_timeout,
                        touch_diag.scan[0], touch_diag.scan[1],
                        touch_diag.scan[2],
                        ds, dc, dp, nr);
        }
        lv_label_set_text(lbl_diag, dbuf);
    }
}
