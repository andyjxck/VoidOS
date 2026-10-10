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

/* Analog face: black field bleeding into the bezel, edge ticks,
   three hands with a smooth sub-second second sweep. Small
   complications: brand + steps under 12, date at 3, HR subdial at 6. */
#define CX 120
#define CY 120

static lv_obj_t *dial_obj;
static lv_obj_t *lbl_bpm, *lbl_steps, *lbl_date, *heart_obj;
static uint32_t frame_ms;

static void draw_line(lv_layer_t *layer, int x0, int y0, int x1, int y1,
                      lv_color_t color, int width, int round_end)
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
    dsc.round_end = round_end;
    lv_draw_line(layer, &dsc);
}

/* hand helper: angle in degrees, 0 = 12 o'clock */
static void hand(lv_layer_t *layer, float deg, int len, int tail,
                 int width, uint32_t color)
{
    float a = (deg - 90.0f) * 0.0174533f;
    int x1 = CX + (int)(cosf(a) * len);
    int y1 = CY + (int)(sinf(a) * len);
    int x0 = CX - (int)(cosf(a) * tail);
    int y0 = CY - (int)(sinf(a) * tail);
    draw_line(layer, x0, y0, x1, y1, lv_color_hex(color), width, 1);
}

static void dial_draw(lv_event_t *ev)
{
    lv_layer_t *layer = lv_event_get_layer(ev);

    /* ticks out to the bezel edge: thin dim minutes, long bright hours */
    for (int i = 0; i < 60; i++) {
        float a = i * 6.0f * 0.0174533f - 1.5707963f;
        int hour_tick = i % 5 == 0;
        int r1 = hour_tick ? 98 : 109;
        draw_line(layer,
                  CX + (int)(cosf(a) * r1), CY + (int)(sinf(a) * r1),
                  CX + (int)(cosf(a) * 118), CY + (int)(sinf(a) * 118),
                  lv_color_hex(hour_tick ? 0xB8C4D8 : 0x2A3344),
                  hour_tick ? 4 : 2, hour_tick);
    }

    int h = hal_clock_hour() % 12;
    int m = hal_clock_minute();
    int s = hal_clock_second();
    int ms = hal_clock_millis();

    /* second behind, then minute, then hour on top */
    hand(layer, (s + ms / 1000.0f) * 6.0f, 92, 18, 2, 0x5AC8D2);
    hand(layer, (m + s / 60.0f) * 6.0f, 84, 12, 6, 0xEAF0F8);
    hand(layer, (h + m / 60.0f) * 30.0f, 56, 12, 9, 0xFFFFFF);

    /* pivot cap */
    lv_draw_fill_dsc_t fill;
    lv_draw_fill_dsc_init(&fill);
    lv_area_t cap = {CX - 7, CY - 7, CX + 7, CY + 7};
    fill.color = lv_color_hex(0x5AC8D2);
    fill.radius = 7;
    lv_draw_fill(layer, &fill, &cap);
    lv_area_t dot = {CX - 3, CY - 3, CX + 3, CY + 3};
    fill.color = lv_color_hex(0x0A0E15);
    fill.radius = 3;
    lv_draw_fill(layer, &fill, &dot);

    /* live PPG trace inside the dial — real signal, no fake waveform */
    int y100;
    if (sensors_ppg(0, &y100)) {
        int mid = 186, x0 = 76, x1 = 164;
        int prev_y = mid;
        for (int x = x0; x <= x1; x++) {
            int i = (x - x0) * 128 / (x1 - x0);
            int y = mid - y100 * 7 / 100;
            if (x > x0) {
                sensors_ppg(i, &y100);
                y = mid - y100 * 7 / 100;
                draw_line(layer, x - 1, prev_y, x, y,
                          lv_color_hex(0x2E5A66), 1, 0);
            }
            prev_y = y;
        }
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
    /* pulse at the measured rate; shimmer when no lock */
    int rate = sensors_bpm();
    int lit;
    if (rate > 0) {
        int period = 60000 / rate;
        lit = (int)(frame_ms % (uint32_t)period) < 220;
    } else {
        lit = ((int)(frame_ms / 110) % 8) < 2;
    }
    lv_draw_fill_dsc_t fill;
    lv_draw_fill_dsc_init(&fill);
    fill.color = lv_color_hex(lit ? 0xFF9898 : 0xE63C3C);
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

void ui_face_build(lv_obj_t *scr)
{
    /* true black: the dead bezel disappears into the dial edge */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    /* dial layer under everything: ticks, hands, PPG trace */
    dial_obj = lv_obj_create(scr);
    lv_obj_set_pos(dial_obj, 0, 0);
    lv_obj_set_size(dial_obj, 240, 240);
    lv_obj_set_style_bg_opa(dial_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dial_obj, 0, 0);
    lv_obj_set_style_pad_all(dial_obj, 0, 0);
    lv_obj_remove_flag(dial_obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(dial_obj, dial_draw, LV_EVENT_DRAW_MAIN, NULL);

    /* hour numerals at r=80; 3 replaced by the date window */
    for (int i = 1; i <= 12; i++) {
        if (i == 3)
            continue;
        float a = i * 30.0f * 0.0174533f - 1.5707963f;
        int x = CX + (int)(cosf(a) * 80) - 14;
        int y = CY + (int)(sinf(a) * 80) - 10;
        char num[3];
        lv_snprintf(num, sizeof num, "%d", i);
        label(scr, &kenney16, 0xD8E0EC, x, y, 28, num);
    }

    /* complications */
    label(scr, &kenney9, 0x59647B, 90, 50, 60, "VOIDOS");
    lbl_steps = label(scr, &kenney9, 0x8890A0, 80, 66, 80, "STEPS --");
    lbl_date = label(scr, &kenney9, 0x8890A0, 160, 112, 56, "");

    heart_obj = lv_obj_create(scr);
    lv_obj_set_pos(heart_obj, 96, 172);
    lv_obj_set_size(heart_obj, 14, 12);
    lv_obj_set_style_bg_opa(heart_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(heart_obj, 0, 0);
    lv_obj_set_style_pad_all(heart_obj, 0, 0);
    lv_obj_remove_flag(heart_obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(heart_obj, heart_draw, LV_EVENT_DRAW_MAIN, NULL);
    lbl_bpm = label(scr, &kenney9, 0xD8E0EC, 114, 173, 46, "--");
    lv_obj_set_style_text_align(lbl_bpm, LV_TEXT_ALIGN_LEFT, 0);
}

void ui_face_tick(void)
{
    static int last_bpm = -1, last_steps = -1, last_synced = -1;
    static int last_dom = -1;
    frame_ms = hal_millis();
    char buf[24];

    int bpm = sensors_bpm();
    if (bpm != last_bpm) {
        lv_snprintf(buf, sizeof buf, "%d", bpm > 0 ? bpm : 0);
        lv_label_set_text(lbl_bpm, bpm > 0 ? buf : "--");
        last_bpm = bpm;
    }
    int steps = sensors_steps();
    if (steps != last_steps) {
        lv_snprintf(buf, sizeof buf, "STEPS %d", steps >= 0 ? steps : 0);
        lv_label_set_text(lbl_steps, buf);
        last_steps = steps;
    }
    int synced = hal_clock_synced();
    int y, mo, d, dow;
    hal_clock_date(&y, &mo, &d, &dow);
    if (synced != last_synced || d != last_dom) {
        if (synced) {
            static const char *dn[8] = {"", "MON", "TUE", "WED", "THU",
                                        "FRI", "SAT", "SUN"};
            lv_snprintf(buf, sizeof buf, "%s %d",
                        dow >= 1 && dow <= 7 ? dn[dow] : "???", d);
            lv_label_set_text(lbl_date, buf);
        } else {
            lv_label_set_text(lbl_date, "");
        }
        last_synced = synced;
        last_dom = d;
    }
    /* hands sweep every frame — the whole point of an analog face */
    lv_obj_invalidate(dial_obj);
    lv_obj_invalidate(heart_obj);
}
