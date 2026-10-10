/* VoidOS device UI: navigation and screens on LVGL.
   One root container; screens rebuild inside it. Only wired-up controls
   exist — everything a button shows is real. */
#include <math.h>
#include <string.h>
#include "lvgl.h"
#include "voidos/hal.h"
#include "board.h"

void display_backlight(int level);
void ui_face_build(lv_obj_t *root);
void ui_face_tick(void);
void sensors_tick(uint32_t now);
int sensors_bma_ok(void);
int sensors_bma_z(void);
int sensors_hrs_ok(void);
int sensors_steps(void);
int sensors_bpm(void);
extern const lv_font_t kenney9, kenney16, kenney24, kenney56;

/* ---- state -------------------------------------------------------------- */

typedef enum {
    SCR_FACE, SCR_LAUNCHER, SCR_CC, SCR_WORKOUT, SCR_W_RUN, SCR_W_DONE,
    SCR_SLEEP, SCR_S_DONE, SCR_WATER, SCR_CLOCK, SCR_SETTINGS, SCR_STATS
} scr_t;

static scr_t cur = SCR_FACE;
static lv_obj_t *root;

/* control centre */
static int brightness = 75;    /* 0-100 */
static int torch_on, water_lock;
static int dnd_on, airplane_on;
static unsigned cc_hide;          /* 1=torch 2=water lock 4=find phone */
static int bt_on = 1;

int ble_connected(void);
void ble_set_enabled(int on);
void ble_diag(int *syncs, int *adv_calls, int *adv_rc, int *gap_events,
              int *addr_type);

/* workout */
static int w_active, w_paused, w_sets;
static uint32_t w_start_ms, w_pause_ms, w_paused_total, w_last_ms;
static int w_routine = -1;

/* sleep */
static int s_track = 1, s_in_bed;
static uint32_t s_start_ms, s_last_ms;

/* water */
static int water_ml;

/* clock: 0 stopwatch, 1 timer, 2 alarm */
static int clock_tab;
static int sw_running;
static uint32_t sw_t0, sw_acc;
static uint32_t timer_end_ms;
static int timer_done;
static int alarm_on = 1;

/* navigation */
static scr_t back_target = SCR_FACE;

/* ---- helpers ------------------------------------------------------------ */

#define C_BG     lv_color_hex(0x000000)
#define C_PANEL  lv_color_hex(0x0B0E14)
#define C_EDGE   lv_color_hex(0x304568)
#define C_FILL   lv_color_hex(0x151B26)
#define C_WHITE  lv_color_white()
#define C_MUTED  lv_color_hex(0x8890A0)
#define C_ACCENT lv_color_hex(0x5AC8D2)
#define C_WARN   lv_color_hex(0xEBC828)
#define C_GOOD   lv_color_hex(0x3CC850)

static lv_obj_t *mk_label(lv_obj_t *parent, const lv_font_t *font,
                          lv_color_t c, int x, int y, const char *s)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, s);
    return l;
}

static lv_obj_t *mk_label_c(lv_obj_t *parent, const lv_font_t *font,
                            lv_color_t c, int x, int y, const char *s)
{
    lv_obj_t *l = mk_label(parent, font, c, x, y, s);
    lv_obj_update_layout(l);
    lv_obj_set_x(l, x - lv_obj_get_width(l) / 2);
    return l;
}

static lv_obj_t *mk_btn(lv_obj_t *parent, const char *txt,
                        int x, int y, int w, int h, lv_color_t fill,
                        lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, fill, 0);
    lv_obj_set_style_radius(b, h / 2, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, C_EDGE, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    if (cb)
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_obj_set_style_text_font(l, &kenney16, 0);
    lv_obj_set_style_text_color(l, C_WHITE, 0);
    lv_label_set_text(l, txt);
    lv_obj_center(l);
    return b;
}

static lv_obj_t *mk_toggle(lv_obj_t *parent, const char *txt,
                           int x, int y, int w, int on, lv_event_cb_t cb)
{
    lv_obj_t *b = mk_btn(parent, txt, x, y, w, 30, C_FILL, cb);
    lv_obj_set_style_bg_color(b, on ? C_ACCENT : C_FILL, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(b, 0),
                                on ? C_BG : C_WHITE, 0);
    return b;
}

static void back_cb(lv_event_t *e);
static void nav_to(scr_t s);

static void mk_header(lv_obj_t *parent, const char *title, int with_back)
{
    if (with_back)
        mk_btn(parent, "<", 14, 10, 36, 26, C_FILL, back_cb);
    mk_label_c(parent, &kenney16, C_MUTED, 120, 14, title);
    /* thin accent divider under the title */
    lv_obj_t *line = lv_obj_create(parent);
    lv_obj_set_pos(line, 20, 34);
    lv_obj_set_size(line, 200, 1);
    lv_obj_set_style_bg_color(line, C_EDGE, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_pad_all(line, 0, 0);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

/* dynamic labels refreshed by per-screen ticks */
static lv_obj_t *lbl_time_big;

static void page_bg(lv_obj_t *parent)
{
    /* full-bleed black: bezel and background are indistinguishable */
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(parent, 0, 0);
}

/* ---- navigation ---------------------------------------------------------- */

static void rebuild(void);

static void nav_to(scr_t s)
{
    if (s == SCR_LAUNCHER)
        back_target = SCR_FACE;
    else if (s != SCR_FACE && s != SCR_CC)
        back_target = SCR_LAUNCHER;
    cur = s;
    rebuild();
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    nav_to(back_target);
}

/* cst: 1=up 2=down 3=left 4=right — control centre sits above the face
   (swipe up reveals it), apps sit below (swipe down), right goes back */
static void dir_nav(int cst_dir)
{
    if (water_lock)
        return;
    if (cur == SCR_FACE) {
        if (cst_dir == 1)
            nav_to(SCR_CC);
        else if (cst_dir == 2)
            nav_to(SCR_LAUNCHER);
        else if (cst_dir == 3)
            nav_to(SCR_STATS);
    } else if (cur == SCR_CC) {
        if (cst_dir == 2)
            nav_to(SCR_FACE);
    } else {
        if (cst_dir == 4)
            nav_to(back_target);
        else if (cst_dir == 3 && cur == SCR_LAUNCHER)
            nav_to(SCR_STATS);
    }
}

static uint32_t last_swipe_ms;

void ui_swipe(int cst_dir)
{
    last_swipe_ms = hal_millis();
    dir_nav(cst_dir);
}

static void gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = lv_indev_active();
    if (!indev)
        return;
    static const int cst[5] = {0, 1, 2, 3, 4};
    lv_dir_t d = lv_indev_get_gesture_dir(indev);
    int c = (d == LV_DIR_TOP)    ? cst[1] :
            (d == LV_DIR_BOTTOM) ? cst[2] :
            (d == LV_DIR_LEFT)   ? cst[3] :
            (d == LV_DIR_RIGHT)  ? cst[4] : 0;
    if (c)
        dir_nav(c);
}

static uint32_t ui_start_ms;

/* ---- display sleep ------------------------------------------------------ */
static int asleep;
static uint32_t last_input_ms;
#define SLEEP_AFTER_MS 60000

int ui_asleep(void) { return asleep; }

static int bl_level(void) { return torch_on ? 3 : 1 + brightness * 2 / 100; }

void ui_wake(void)
{
    if (asleep) {
        asleep = 0;
        display_backlight(bl_level());
    }
    last_input_ms = hal_millis();
}

static void tap_cb(lv_event_t *e)
{
    (void)e;
    /* swallow a phantom tap latched by the touch chip across power-on;
       the window is short and only blocks the face->launcher tap */
    if (hal_millis() - ui_start_ms < 2500)
        return;
    /* a click that lands right after a swipe is the swipe's tail, not a
       real tap — otherwise face->CC would bounce straight to launcher */
    if (hal_millis() - last_swipe_ms < 400)
        return;
    if (cur == SCR_FACE && !water_lock)
        nav_to(SCR_LAUNCHER);
}

/* ---- baked material icons (assets/fonts PNGs -> icon_alpha[][]) ----------- */

#include "assets.h"

static lv_image_dsc_t icon_dsc[14];
static int icon_dsc_ready;

/* icon_alpha order (tools/gen_assets.py ICONS):
   0 dnd_on 1 flight 2 bt 3 torch_on 4 phone_vibrate 5 water_drop
   6 settings 7 light_mode 8 battery 9 dnd_off 10 airplane_off
   11 bt_off 12 torch_off 13 cloud */
static void icon_dsc_init(void)
{
    for (int i = 0; i < 14; i++) {
        icon_dsc[i].header.magic = LV_IMAGE_HEADER_MAGIC;
        icon_dsc[i].header.w = 24;
        icon_dsc[i].header.h = 24;
        icon_dsc[i].header.cf = LV_COLOR_FORMAT_A8;
        icon_dsc[i].header.stride = 24;
        icon_dsc[i].data = icon_alpha[i];
        icon_dsc[i].data_size = sizeof icon_alpha[i];
    }
    icon_dsc_ready = 1;
}

/* per-tile icon index for on/off states */
static const uint8_t tile_icon[6][2] = {
    {9, 0},   /* DND:      off -> dnd_off,      on -> dnd_on   */
    {10, 1},  /* AIR:      off -> airplane_off, on -> flight   */
    {11, 2},  /* BT:       off -> bt_off,       on -> bt       */
    {12, 3},  /* TORCH:    off -> torch_off,    on -> torch_on */
    {4, 4},   /* PHONE:    mobile_vibrate both ways             */
    {5, 5},   /* LOCK:     water_drop both ways                 */
};

static lv_obj_t *mk_icon(lv_obj_t *parent, int tile, int on)
{
    if (!icon_dsc_ready)
        icon_dsc_init();
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, &icon_dsc[tile_icon[tile][on != 0]]);
    lv_obj_set_style_image_recolor(img, on ? C_BG : C_ACCENT, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_center(img);
    return img;
}

/* ---- control centre ------------------------------------------------------ */

static void cc_cb(lv_event_t *e)
{
    if (lv_event_get_user_data(e)) {   /* brightness slider */
        lv_obj_t *sl = lv_event_get_target(e);
        brightness = (int)lv_slider_get_value(sl);
        if (!torch_on)
            display_backlight(1 + brightness * 2 / 100);
        return;
    }
    int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    switch (i) {
    case 0: dnd_on ^= 1; break;
    case 1: airplane_on ^= 1; break;
    case 3:
        torch_on ^= 1;
        display_backlight(torch_on ? 3 : 1 + brightness * 2 / 100);
        break;
    case 2:
        bt_on ^= 1;
        ble_set_enabled(bt_on);
        break;
    case 5: water_lock ^= 1; break;
    default: break;   /* PHONE toggle arrives with BLE data services */
    }
    rebuild();
}

static void build_cc(lv_obj_t *r)
{
    page_bg(r);
    mk_header(r, "CONTROL", 0);

    mk_label(r, &kenney9, C_MUTED, 176, 30, "SUN");
    lv_obj_t *sl = lv_slider_create(r);
    lv_obj_set_pos(sl, 214, 40);
    lv_obj_set_size(sl, 12, 160);
    lv_slider_set_range(sl, 0, 100);
    lv_slider_set_value(sl, brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(sl, cc_cb, LV_EVENT_VALUE_CHANGED, (void *)1);

    static const char *tt[6] = {"DND", "AIR", "BT", "TORCH", "PHONE", "LOCK"};
    for (int i = 0; i < 6; i++) {
        if ((i == 3 && (cc_hide & 1)) || (i == 4 && (cc_hide & 4)) ||
            (i == 5 && (cc_hide & 2)))
            continue;
        int x = 42 + (i % 3) * 70, y = 66 + (i / 3) * 70;
        int on = (i == 0) ? dnd_on : (i == 1) ? airplane_on :
                 (i == 2) ? bt_on : (i == 3) ? torch_on :
                 (i == 4) ? ble_connected() : (i == 5) ? water_lock : 0;
        lv_obj_t *b = mk_btn(r, tt[i], x - 29, y - 29, 58, 58,
                             on ? C_ACCENT : C_FILL, cc_cb);
        lv_obj_set_style_radius(b, 18, 0);
        lv_obj_t *txt = lv_obj_get_child(b, 0);
        lv_obj_set_style_text_font(txt, &kenney9, 0);
        lv_obj_set_style_text_color(txt, on ? C_BG : C_MUTED, 0);
        lv_obj_align(txt, LV_ALIGN_BOTTOM_MID, 0, -6);
        lv_obj_t *ic = mk_icon(b, i, on);
        lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 6);
        lv_obj_set_user_data(b, (void *)(intptr_t)i);
    }
    mk_label_c(r, &kenney9, C_MUTED, 120, 216, "SWIPE DOWN FOR FACE");
}

/* ---- launcher ------------------------------------------------------------- */

static void launcher_cb(lv_event_t *e)
{
    nav_to((scr_t)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e)));
}

static void build_launcher(lv_obj_t *r)
{
    page_bg(r);
    mk_header(r, "APPS", 0);
    static const char *names[6] = {
        "WORKOUT", "SLEEP", "WATER", "STATS", "SETTINGS", "CLOCK"
    };
    static const scr_t scrs[6] = {
        SCR_WORKOUT, SCR_SLEEP, SCR_WATER, SCR_STATS, SCR_SETTINGS, SCR_CLOCK
    };
    for (int i = 0; i < 6; i++) {
        int x = 14 + (i % 2) * 112, y = 46 + (i / 2) * 62;
        lv_obj_t *b = mk_btn(r, names[i], x, y, 100, 56, C_FILL, launcher_cb);
        lv_obj_set_style_radius(b, 14, 0);
        lv_obj_set_user_data(b, (void *)(intptr_t)scrs[i]);
    }
    mk_label_c(r, &kenney9, C_MUTED, 120, 218, "SWIPE RIGHT FOR BACK");
}

/* ---- workout -------------------------------------------------------------- */

static void fmt_ms(uint32_t ms, char *out)
{
    unsigned s = ms / 1000;
    lv_snprintf(out, 12, "%02d:%02d", s / 60, s % 60);
}

static void start_workout(void)
{
    w_active = 1;
    w_paused = 0;
    w_sets = 0;
    w_paused_total = 0;
    w_start_ms = hal_millis();
    nav_to(SCR_W_RUN);
}

static void workout_cb(lv_event_t *e)
{
    int code = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    uint32_t now = hal_millis();
    switch (code) {
    case 0: w_routine = -1; start_workout(); break;
    case 1:                     /* pause / resume */
        if (w_paused) {
            w_paused_total += now - w_pause_ms;
            w_paused = 0;
        } else {
            w_pause_ms = now;
            w_paused = 1;
        }
        rebuild();
        break;
    case 2:                     /* end */
        w_last_ms = now - w_start_ms - w_paused_total;
        w_active = 0;
        nav_to(SCR_W_DONE);
        break;
    case 3:
        if (!w_paused) {
            w_sets++;
            rebuild();
        }
        break;
    }
}

static void routine_cb(lv_event_t *e)
{
    w_routine = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    start_workout();
}

static void build_workout(lv_obj_t *r)
{
    page_bg(r);
    mk_header(r, "WORKOUT", 1);
    static const char *rt[3] = {"PUSH DAY", "PULL DAY", "LEG DAY"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = mk_btn(r, rt[i], 20, 48 + i * 32, 200, 28, C_FILL,
                             routine_cb);
        lv_obj_set_user_data(b, (void *)(intptr_t)i);
    }
    lv_obj_t *b = mk_btn(r, "QUICK SESSION", 20, 150, 200, 48, C_ACCENT,
                         workout_cb);
    lv_obj_set_user_data(b, (void *)0);
}

static void build_w_run(lv_obj_t *r)
{
    page_bg(r);
    static const char *rt[3] = {"PUSH DAY", "PULL DAY", "LEG DAY"};
    mk_header(r, w_routine >= 0 ? rt[w_routine] : "QUICK SESSION", 0);
    char buf[16];
    fmt_ms(hal_millis() - w_start_ms - w_paused_total, buf);
    lbl_time_big = mk_label_c(r, &kenney24, C_WHITE, 120, 80, buf);
    char sbuf[24];
    lv_snprintf(sbuf, sizeof sbuf, "SET %d OF 3", w_sets);
    mk_label_c(r, &kenney16, C_MUTED, 120, 116, sbuf);
    lv_obj_t *b;
    b = mk_btn(r, "SET", 20, 138, 200, 32, C_FILL, workout_cb);
    lv_obj_set_user_data(b, (void *)(intptr_t)3);
    b = mk_btn(r, w_paused ? "RESUME" : "PAUSE", 20, 178, 97, 42, C_FILL,
               workout_cb);
    lv_obj_set_user_data(b, (void *)(intptr_t)1);
    b = mk_btn(r, "END", 123, 178, 97, 42, C_WARN, workout_cb);
    lv_obj_set_user_data(b, (void *)(intptr_t)2);
}

static void build_w_done(lv_obj_t *r)
{
    page_bg(r);
    mk_header(r, "SUMMARY", 1);
    char buf[16];
    fmt_ms(w_last_ms, buf);
    mk_label_c(r, &kenney24, C_WHITE, 120, 90, buf);
    char sbuf[24];
    lv_snprintf(sbuf, sizeof sbuf, "%d SETS DONE", w_sets);
    mk_label_c(r, &kenney16, C_ACCENT, 120, 128, sbuf);
    mk_btn(r, "DONE", 20, 176, 200, 40, C_FILL, back_cb);
}

/* ---- sleep ----------------------------------------------------------------- */

static void sleep_cb(lv_event_t *e)
{
    int code = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (code == 2) {
        s_track ^= 1;
        if (!s_track)
            s_in_bed = 0;
        rebuild();
        return;
    }
    if (!s_track)
        return;
    if (s_in_bed) {
        s_last_ms = hal_millis() - s_start_ms;
        s_in_bed = 0;
        nav_to(SCR_S_DONE);
    } else {
        s_in_bed = 1;
        s_start_ms = hal_millis();
        rebuild();
    }
}

static void build_sleep(lv_obj_t *r)
{
    page_bg(r);
    mk_header(r, "SLEEP", 1);
    mk_label_c(r, &kenney9, C_MUTED, 120, 46, s_in_bed ? "IN BED" : "LAST NIGHT");
    char buf[24];
    if (s_in_bed)
        fmt_ms(hal_millis() - s_start_ms, buf);
    else
        fmt_ms(s_last_ms, buf);
    lbl_time_big = mk_label_c(r, &kenney24, C_WHITE, 120, 66, buf);
    lv_obj_t *b = mk_btn(r, s_in_bed ? "WAKE UP" : "BEDTIME", 20, 108, 200, 52,
                         s_in_bed ? C_WARN : C_ACCENT, sleep_cb);
    lv_obj_set_user_data(b, (void *)(intptr_t)1);
    lv_obj_t *t = mk_toggle(r, s_track ? "TRACKING ON" : "TRACKING OFF",
                            20, 178, 200, s_track, sleep_cb);
    lv_obj_set_user_data(t, (void *)(intptr_t)2);
}

static void build_s_done(lv_obj_t *r)
{
    page_bg(r);
    mk_header(r, "NIGHT", 1);
    mk_label_c(r, &kenney9, C_MUTED, 120, 76, "TIME IN BED");
    char buf[16];
    fmt_ms(s_last_ms, buf);
    mk_label_c(r, &kenney24, C_WHITE, 120, 98, buf);
    mk_btn(r, "DONE", 20, 176, 200, 40, C_FILL, back_cb);
}

/* ---- water ----------------------------------------------------------------- */

static void water_cb(lv_event_t *e)
{
    water_ml += (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    rebuild();
}

static void build_water(lv_obj_t *r)
{
    page_bg(r);
    mk_header(r, "WATER", 1);
    char buf[24];
    lv_snprintf(buf, sizeof buf, "%d ML", water_ml);
    mk_label_c(r, &kenney24, C_ACCENT, 120, 82, buf);
    mk_label_c(r, &kenney9, C_MUTED, 120, 118, "GOAL 2000 ML");
    lv_obj_t *bar = lv_bar_create(r);
    lv_obj_set_pos(bar, 30, 138);
    lv_obj_set_size(bar, 180, 16);
    lv_bar_set_range(bar, 0, 2000);
    lv_bar_set_value(bar, water_ml > 2000 ? 2000 : water_ml, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, C_FILL, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_t *b;
    b = mk_btn(r, "+250", 20, 178, 97, 42, C_FILL, water_cb);
    lv_obj_set_user_data(b, (void *)(intptr_t)250);
    b = mk_btn(r, "+500", 123, 178, 97, 42, C_FILL, water_cb);
    lv_obj_set_user_data(b, (void *)(intptr_t)500);
}

/* ---- clock ----------------------------------------------------------------- */

static void clock_cb(lv_event_t *e);

static void build_clock(lv_obj_t *r)
{
    page_bg(r);
    mk_header(r, "CLOCK", 1);
    static const char *tab[3] = {"WATCH", "TIMER", "ALARM"};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = mk_btn(r, tab[i], 14 + i * 74, 40, 70, 28,
                             clock_tab == i ? C_ACCENT : C_FILL, clock_cb);
        lv_obj_set_user_data(b, (void *)(intptr_t)(20 + i));
        lv_obj_set_style_text_color(lv_obj_get_child(b, 0),
                                    clock_tab == i ? C_BG : C_WHITE, 0);
    }
    if (clock_tab == 0) {
        char buf[16];
        fmt_ms(sw_acc + (sw_running ? hal_millis() - sw_t0 : 0), buf);
        lbl_time_big = mk_label_c(r, &kenney24, C_WHITE, 120, 96, buf);
        lv_obj_t *b;
        b = mk_btn(r, sw_running ? "STOP" : "START", 20, 140, 97, 40,
                   C_ACCENT, clock_cb);
        lv_obj_set_user_data(b, (void *)(intptr_t)10);
        b = mk_btn(r, "RESET", 123, 140, 97, 40, C_FILL, clock_cb);
        lv_obj_set_user_data(b, (void *)(intptr_t)11);
    } else if (clock_tab == 1) {
        if (timer_done) {
            mk_label_c(r, &kenney16, C_WARN, 120, 96, "TIME'S UP");
            lv_obj_t *b = mk_btn(r, "DISMISS", 20, 140, 200, 40, C_FILL,
                                 clock_cb);
            lv_obj_set_user_data(b, (void *)(intptr_t)12);
        } else if (timer_end_ms) {
            char buf[16];
            fmt_ms(timer_end_ms - hal_millis(), buf);
            lbl_time_big = mk_label_c(r, &kenney24, C_WARN, 120, 96, buf);
            lv_obj_t *b = mk_btn(r, "CANCEL", 20, 140, 200, 40, C_FILL,
                                 clock_cb);
            lv_obj_set_user_data(b, (void *)(intptr_t)13);
        } else {
            static const char *p[3] = {"1 MIN", "5 MIN", "10 MIN"};
            for (int i = 0; i < 3; i++) {
                lv_obj_t *b = mk_btn(r, p[i], 14 + i * 74, 100, 68, 40, C_FILL,
                                     clock_cb);
                lv_obj_set_user_data(b, (void *)(intptr_t)(30 + i));
            }
        }
    } else {
        mk_label_c(r, &kenney24, C_WHITE, 120, 88, "07:00");
        lv_obj_t *b = mk_toggle(r, alarm_on ? "ALARM ON" : "ALARM OFF",
                                20, 130, 200, alarm_on, clock_cb);
        lv_obj_set_user_data(b, (void *)(intptr_t)14);
        mk_label_c(r, &kenney9, C_MUTED, 120, 186, "VIBRATES UNTIL BUTTON");
    }
}

static void clock_cb(lv_event_t *e)
{
    int v = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (v >= 20) {
        clock_tab = v - 20;
        rebuild();
        return;
    }
    uint32_t now = hal_millis();
    switch (v) {
    case 10:
        if (sw_running) { sw_acc += now - sw_t0; sw_running = 0; }
        else { sw_t0 = now; sw_running = 1; }
        break;
    case 11: sw_running = 0; sw_acc = 0; break;
    case 12: timer_done = 0; break;
    case 13: timer_end_ms = 0; break;
    case 14: alarm_on ^= 1; break;
    case 30: timer_end_ms = now + 60000; break;
    case 31: timer_end_ms = now + 300000; break;
    case 32: timer_end_ms = now + 600000; break;
    }
    rebuild();
}

/* ---- settings --------------------------------------------------------------- */

static void settings_cb(lv_event_t *e);

static void build_settings(lv_obj_t *r)
{
    page_bg(r);
    mk_header(r, "SETTINGS", 1);
    static const char *rows[9] = {
        "DND", "AIRPLANE", "BLUETOOTH", "TORCH", "WATER LOCK",
        "BRIGHTNESS", "CC: TORCH", "CC: WATER", "CC: PHONE"
    };
    int vals[9] = {dnd_on, airplane_on, 0, torch_on, water_lock,
                   brightness, !(cc_hide & 1), !(cc_hide & 2), !(cc_hide & 4)};
    for (int i = 0; i < 9; i++) {
        int y = 44 + i * 20;
        mk_label(r, &kenney9, C_WHITE, 18, y, rows[i]);
        char v[8];
        if (i == 5)
            lv_snprintf(v, sizeof v, "%d%%", brightness);
        else
            lv_snprintf(v, sizeof v, "%s", vals[i] ? "ON" : "OFF");
        lv_obj_t *b = mk_btn(r, v, 158, y - 6, 64, 22,
                             vals[i] ? C_ACCENT : C_FILL, settings_cb);
        lv_obj_set_style_text_color(lv_obj_get_child(b, 0),
                                    vals[i] ? C_BG : C_MUTED, 0);
        lv_obj_set_user_data(b, (void *)(intptr_t)i);
    }
    mk_label_c(r, &kenney9, C_MUTED, 120, 224, "HOLD BUTTON 3S = RESET");
}

static void settings_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    switch (i) {
    case 0: dnd_on ^= 1; break;
    case 1: airplane_on ^= 1; break;
    case 3:
        torch_on ^= 1;
        display_backlight(torch_on ? 3 : 1 + brightness * 2 / 100);
        break;
    case 4: water_lock ^= 1; break;
    case 5:
        brightness = brightness >= 100 ? 25 : brightness + 25;
        if (!torch_on)
            display_backlight(1 + brightness * 2 / 100);
        break;
    case 6: cc_hide ^= 1; break;
    case 7: cc_hide ^= 2; break;
    case 8: cc_hide ^= 4; break;
    }
    rebuild();
}

/* ---- stats ------------------------------------------------------------------- */

static void build_stats(lv_obj_t *r)
{
    page_bg(r);
    mk_header(r, "STATS", 1);
    char buf[32];
    int steps = sensors_steps();
    lv_snprintf(buf, sizeof buf, "STEPS  %s", steps >= 0 ? "" : "--");
    if (steps >= 0)
        lv_snprintf(buf, sizeof buf, "STEPS  %d", steps);
    mk_label_c(r, &kenney16, C_WHITE, 120, 64, buf);
    int bpm = sensors_bpm();
    lv_snprintf(buf, sizeof buf, "BPM    %s",
                bpm > 0 ? "" : bpm == 0 ? "SENSING" : "--");
    if (bpm > 0)
        lv_snprintf(buf, sizeof buf, "BPM    %d", bpm);
    mk_label_c(r, &kenney16, C_WHITE, 120, 92, buf);
    lv_snprintf(buf, sizeof buf, "WATER  %d ML", water_ml);
    mk_label_c(r, &kenney16, C_ACCENT, 120, 120, buf);
    lv_snprintf(buf, sizeof buf, "SETS   %d", w_sets);
    mk_label_c(r, &kenney16, C_WHITE, 120, 148, buf);
    if (!sensors_bma_ok())
        mk_label_c(r, &kenney9, C_WARN, 120, 200, "ACCEL OFFLINE");
    if (!sensors_hrs_ok())
        mk_label_c(r, &kenney9, C_WARN, 120, 214, "HR SENSOR OFFLINE");
    int s, ac, rc, ge, at;
    ble_diag(&s, &ac, &rc, &ge, &at);
    lv_snprintf(buf, sizeof buf, "BLE S%d A%d E%d", s, ac, rc);
    mk_label_c(r, &kenney9, C_MUTED, 120, 172, buf);
    lv_snprintf(buf, sizeof buf, "TCH R%u F%d E%d", touch_diag.reads,
                touch_diag.last_fingers, touch_diag.last_event);
    mk_label_c(r, &kenney9, C_MUTED, 120, 186, buf);
}

/* ---- dispatch ------------------------------------------------------------------ */

static void rebuild(void)
{
    lv_obj_clean(root);
    lbl_time_big = NULL;
    switch (cur) {
    case SCR_FACE:      ui_face_build(root); break;
    case SCR_LAUNCHER:  build_launcher(root); break;
    case SCR_CC:        build_cc(root); break;
    case SCR_WORKOUT:   build_workout(root); break;
    case SCR_W_RUN:     build_w_run(root); break;
    case SCR_W_DONE:    build_w_done(root); break;
    case SCR_SLEEP:     build_sleep(root); break;
    case SCR_S_DONE:    build_s_done(root); break;
    case SCR_WATER:     build_water(root); break;
    case SCR_CLOCK:     build_clock(root); break;
    case SCR_SETTINGS:  build_settings(root); break;
    case SCR_STATS:     build_stats(root); break;
    }
}

void ui_init(void)
{
    root = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, 240, 240);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root, gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(root, tap_cb, LV_EVENT_CLICKED, NULL);
    display_backlight(3);
    last_input_ms = hal_millis();
    nav_to(SCR_FACE);
}

uint32_t button_held_ms(void);

void ui_tick(void)
{
    uint32_t now = hal_millis();
    if (!ui_start_ms)
        ui_start_ms = now;   /* first loop iter, after ble_init's boot marks */
    extern int ble_dfu_busy(void);
    static int sleep_az = 0x4000;      /* sentinel: no baseline yet */
    /* idle -> screen off (never mid-DFU) */
    if (!asleep && last_input_ms && now - last_input_ms > SLEEP_AFTER_MS &&
        !ble_dfu_busy()) {
        asleep = 1;
        display_backlight(0);
        sleep_az = sensors_bma_z();    /* orientation at sleep: raise-to-
                                          wake watches for a ~40° tilt */
    }
    /* raise-to-wake: a wrist raise rotates the Z axis ~1g (±1024 in
       12-bit counts). Edge-triggered on the sleep-time baseline so a
       watch left face-up on a desk doesn't keep waking. */
    if (asleep && sleep_az != 0x4000) {
        int dz = sensors_bma_z() - sleep_az;
        if (dz > 700 || dz < -700)
            ui_wake();
    }
    /* side button as nav fallback while touch is unreliable: a release
       before the 3 s reset threshold steps through screens */
    static int was_held;
    uint32_t held = button_held_ms();
    if (held)
        last_input_ms = now;
    if (was_held && !held) {
        if (asleep)
            ui_wake();
        else if (water_lock)
            ; /* ignore */
        else if (cur == SCR_FACE)
            nav_to(SCR_LAUNCHER);
        else if (cur == SCR_LAUNCHER)
            nav_to(SCR_CC);
        else
            nav_to(back_target);
    }
    was_held = held != 0;
    sensors_tick(now);
    if (timer_end_ms && now >= timer_end_ms) {
        timer_end_ms = 0;
        timer_done = 1;
        if (cur == SCR_CLOCK)
            rebuild();
    }
    switch (cur) {
    case SCR_FACE:
        ui_face_tick();
        break;
    case SCR_W_RUN: {
        static uint32_t last;
        if (!w_paused && now - last >= 500) {
            last = now;
            if (lbl_time_big) {
                char buf[16];
                fmt_ms(now - w_start_ms - w_paused_total, buf);
                lv_label_set_text(lbl_time_big, buf);
            }
        }
        break;
    }
    case SCR_SLEEP:
    case SCR_CLOCK: {
        static uint32_t last;
        if (now - last >= 500) {
            last = now;
            if (lbl_time_big) {
                char buf[16];
                if (cur == SCR_SLEEP && s_in_bed)
                    fmt_ms(now - s_start_ms, buf);
                else if (clock_tab == 0)
                    fmt_ms(sw_acc + (sw_running ? now - sw_t0 : 0), buf);
                else if (timer_end_ms)
                    fmt_ms(timer_end_ms - now, buf);
                else
                    break;
                lv_label_set_text(lbl_time_big, buf);
            }
        }
        break;
    }
    default:
        break;
    }
}
