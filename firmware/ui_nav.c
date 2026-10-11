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
int hal_clock_hour(void);
int hal_clock_minute(void);
void hal_clock_set(int y, int mo, int d, int dow, int h, int mi, int s);
void hal_clock_date(int *y, int *mo, int *d, int *dow);
extern const lv_font_t kenney9, kenney16, kenney24, kenney56;

/* ---- state -------------------------------------------------------------- */

typedef enum {
    SCR_FACE, SCR_LAUNCHER, SCR_CC, SCR_WORKOUT, SCR_W_RUN, SCR_W_DONE,
    SCR_SLEEP, SCR_S_DONE, SCR_WATER, SCR_CLOCK, SCR_SETTINGS, SCR_STATS,
    SCR_SETTIME, SCR_NOTIF, SCR_CARDIO
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
int ble_status(void);
void ble_diag(int *syncs, int *adv_calls, int *adv_rc, int *gap_events,
              int *addr_type);

/* radio follows BT toggle AND airplane mode, from every entry point */
static void apply_radio(void)
{
    ble_set_enabled(bt_on && !airplane_on);
}

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
static uint32_t motor_off_at;
static int resting_bpm;         /* lowest BPM seen this session */
static int alarm_on = 1;

/* navigation */
static scr_t back_target = SCR_FACE;

/* ---- persistence: settings + step history on external SPI flash ---- */
void spi_flash_init(void);
void spi_flash_read(uint32_t, uint8_t *, unsigned);
void spi_flash_write(uint32_t, const uint8_t *, unsigned);
void spi_flash_sector_erase(uint32_t);
int sensors_steps(void);
int sensors_wrist_raise(void);
int sensors_ppg(int i, int *out_y100);
int hal_battery_pct(void);
void hal_motor(int);

#define PERSIST_ADDR 0x3F0000u   /* top sector, clear of the DFU region */
struct persist {
    uint32_t magic;              /* 0x564F4944 = "VOID" */
    uint8_t ver;
    uint8_t brightness, dnd, airplane, water_lock, bt_on;
    uint8_t cc_hide, water_ml, alarm_on;
    uint32_t steps_total, chip_at_save;
};
static struct persist P;

int ui_steps(void)
{
    int chip = sensors_steps();
    if (chip < 0)
        return -1;
    uint32_t c = (uint32_t)chip;
    uint32_t tot = P.steps_total;
    tot += c >= P.chip_at_save ? c - P.chip_at_save : c;
    return (int)tot;
}

static void persist_save(void)
{
    P.magic = 0x564F4944u;
    P.ver = 1;
    P.brightness = (uint8_t)brightness;
    P.dnd = (uint8_t)dnd_on;
    P.airplane = (uint8_t)airplane_on;
    P.water_lock = (uint8_t)water_lock;
    P.bt_on = (uint8_t)bt_on;
    P.cc_hide = (uint8_t)cc_hide;
    P.water_ml = (uint8_t)(water_ml / 250);
    P.alarm_on = (uint8_t)alarm_on;
    P.steps_total = (uint32_t)ui_steps();
    P.chip_at_save = (uint32_t)sensors_steps();
    spi_flash_init();
    spi_flash_sector_erase(PERSIST_ADDR);
    spi_flash_write(PERSIST_ADDR, (const uint8_t *)&P, sizeof P);
}

static void persist_load(void)
{
    spi_flash_init();
    spi_flash_read(PERSIST_ADDR, (uint8_t *)&P, sizeof P);
    if (P.magic != 0x564F4944u || P.ver != 1) {
        memset(&P, 0, sizeof P);
        return;
    }
    brightness = P.brightness;
    dnd_on = P.dnd;
    airplane_on = P.airplane;
    water_lock = P.water_lock;
    bt_on = P.bt_on;
    cc_hide = P.cc_hide;
    water_ml = P.water_ml * 250;
    alarm_on = P.alarm_on;
}

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
    lv_obj_set_style_radius(b, 4, 0);   /* sharp: the 240 panel has a bezel,
                                         not the bevel-less glass the
                                         squircle look was designed for */
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

/* CST816S gesture register IDs: 1=down 2=up 3=left(R->L) 4=right(L->R).
   Five-way face map, edge-aware on the down swipe:
     down on the left half  -> control centre
     down on the right half -> apps
     up                     -> stats
     L->R                   -> notifications
     R->L                   -> cardio
   Each panel exits with the opposite swipe; inner screens keep
   L->R (dir 4) as back like before. */
static void dir_nav(int cst_dir, int start_x)
{
    if (water_lock)
        return;
    if (cur == SCR_FACE) {
        if (cst_dir == 1)
            nav_to(start_x < 120 ? SCR_CC : SCR_LAUNCHER);
        else if (cst_dir == 2)
            nav_to(SCR_STATS);
        else if (cst_dir == 4)
            nav_to(SCR_NOTIF);
        else if (cst_dir == 3)
            nav_to(SCR_CARDIO);
    } else if (cur == SCR_CC || cur == SCR_LAUNCHER) {
        if (cst_dir == 2)
            nav_to(SCR_FACE);
    } else if (cur == SCR_STATS) {
        if (cst_dir == 1)
            nav_to(SCR_FACE);
    } else if (cur == SCR_NOTIF) {
        if (cst_dir == 3)
            nav_to(SCR_FACE);
    } else if (cur == SCR_CARDIO) {
        if (cst_dir == 4)
            nav_to(SCR_FACE);
    } else {
        if (cst_dir == 4)
            nav_to(back_target);
    }
}

static uint32_t last_swipe_ms;

void ui_swipe(int cst_dir, int start_x)
{
    last_swipe_ms = hal_millis();
    dir_nav(cst_dir, start_x);
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

/* taps never navigate — the five-way swipe map is the only way off the
   face, so there is no tap_cb. Root stays clickable only so the CST816S
   events still flow through the indev to child buttons. */

/* ---- baked material icons (assets/fonts PNGs -> icon_alpha[][]) ----------- */

#include "assets.h"

static lv_image_dsc_t icon_dsc[15];
static int icon_dsc_ready;

/* icon_alpha order (tools/gen_assets.py ICONS):
   0 dnd_on 1 flight 2 bt 3 torch_on 4 phone_vibrate 5 water_drop
   6 settings 7 light_mode 8 battery 9 dnd_off 10 airplane_off
   11 bt_off 12 torch_off 13 cloud 14 footprint */
static void icon_dsc_init(void)
{
    for (int i = 0; i < 15; i++) {
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
static const uint8_t tile_icon[7][2] = {
    {9, 0},   /* DND:      off -> dnd_off,      on -> dnd_on   */
    {10, 1},  /* AIR:      off -> airplane_off, on -> flight   */
    {11, 2},  /* BT:       off -> bt_off,       on -> bt       */
    {12, 3},  /* TORCH:    off -> torch_off,    on -> torch_on */
    {4, 4},   /* PHONE:    mobile_vibrate both ways             */
    {5, 5},   /* LOCK:     water_drop both ways                 */
    {6, 6},   /* SETTINGS                                     */
};

/* positional icon by raw icon_alpha index — for capsules and stats */
static lv_obj_t *mk_icon_at(lv_obj_t *parent, int idx, int on, int x, int y)
{
    if (!icon_dsc_ready)
        icon_dsc_init();
    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, &icon_dsc[idx]);
    lv_obj_set_style_image_recolor(img, on ? C_ACCENT : C_MUTED, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_set_pos(img, x, y);
    return img;
}

/* ---- control centre ------------------------------------------------------ */

static int cc_state(int i)
{
    switch (i) {
    case 0: return dnd_on;
    case 1: return airplane_on;
    case 2: return bt_on;
    case 3: return torch_on;
    case 4: return ble_connected();
    case 5: return water_lock;
    }
    return 0;
}

/* active tint per tile — the simulator honeycomb colours */
static lv_color_t cc_tint(int i)
{
    switch (i) {
    case 0: return lv_color_hex(0x7040C8);   /* dnd: purple   */
    case 1: return lv_color_hex(0xDC821E);   /* air: orange   */
    case 2: return lv_color_hex(0x2870DC);   /* bt: blue      */
    case 3: return lv_color_hex(0xC8A01E);   /* torch: amber  */
    case 4: return lv_color_hex(0x2870DC);   /* phone: blue   */
    case 5: return lv_color_hex(0x1E96BE);   /* lock: teal    */
    }
    return lv_color_hex(0x28303C);           /* settings grey */
}

/* honeycomb layout: cx, cy, tile — staggered like the simulator comb
   (int16: 149/177 overflow an int8 and wrap the hexes off-screen) */
static const int16_t cc_hexpos[7][3] = {
    { 91,  78, 0}, {149,  78, 1},
    { 63, 128, 2}, {120, 128, 3}, {177, 128, 4},
    { 91, 178, 5}, {149, 178, 6}
};

/* pointy-top hexagon (apex at top and bottom), the simulator shape:
   full width for the middle half, tapering to a point at ±R */
static void hex_fill(lv_layer_t *layer, int cx, int cy, int r,
                     lv_color_t c)
{
    int hw_r = r * 866 / 1000;            /* apothem, R * 0.866 */
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = c;
    dsc.opa = LV_OPA_COVER;
    for (int dy = -r + 1; dy <= r - 1; dy++) {
        int ay = dy < 0 ? -dy + 1 : dy + 1;
        int hw = ay <= r / 2 ? hw_r : hw_r * (2 * r - 2 * ay) / r;
        if (hw < 1)
            continue;
        dsc.p1.x = cx - hw;
        dsc.p2.x = cx + hw;
        dsc.p1.y = dsc.p2.y = cy + dy;
        lv_draw_line(layer, &dsc);
    }
}

/* vertical capsule with rounded ends; inner fill rises from the
   bottom by pct — the simulator's battery/brightness pills */
static void vcap_fill(lv_layer_t *layer, int cx, int y0, int w, int h,
                      int pct, lv_color_t fc)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = w / 2;
    dsc.bg_opa = LV_OPA_COVER;
    lv_area_t a = {cx - w / 2, y0, cx + w / 2 - 1, y0 + h - 1};
    dsc.bg_color = lv_color_hex(0x464E5A);          /* shell */
    lv_draw_rect(layer, &dsc, &a);
    lv_area_t in = {a.x1 + 2, a.y1 + 2, a.x2 - 2, a.y2 - 2};
    dsc.radius = (w - 4) / 2;
    dsc.bg_color = lv_color_hex(0x0C0F16);          /* recessed slot */
    lv_draw_rect(layer, &dsc, &in);
    int fh = (h - 6) * pct / 100;
    lv_area_t fill = {in.x1 + 1, in.y2 - fh - 1, in.x2 - 1, in.y2 - 1};
    dsc.radius = (w - 6) / 2;
    dsc.bg_color = fc;
    lv_draw_rect(layer, &dsc, &fill);
}

/* one draw pass paints the whole control centre — capsules, comb —
   the same proven pattern as the face's dial_draw */
static void cc_draw(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    int bat = hal_battery_pct();
    vcap_fill(layer, 46, 22, 16, 72, bat,
              bat > 30 ? C_GOOD : C_WARN);
    vcap_fill(layer, 210, 22, 16, 72, brightness, C_ACCENT);
    for (int i = 0; i < 7; i++) {
        int t = cc_hexpos[i][2];
        if ((t == 3 && (cc_hide & 1)) || (t == 4 && (cc_hide & 4)) ||
            (t == 5 && (cc_hide & 2)))
            continue;
        hex_fill(layer, cc_hexpos[i][0], cc_hexpos[i][1], 30,
                 cc_state(t) ? cc_tint(t) : lv_color_hex(0x232B3B));
    }
}

static void cc_cb(lv_event_t *e)
{
    if (lv_event_get_user_data(e)) {   /* brightness capsule: tap steps */
        brightness = brightness >= 100 ? 25 : brightness + 25;
        if (!torch_on)
            display_backlight(1 + brightness * 2 / 100);
        persist_save();
        rebuild();
        return;
    }
    int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    switch (i) {
    case 0: dnd_on ^= 1; break;
    case 1:
        airplane_on ^= 1;
        apply_radio();
        break;
    case 3:
        torch_on ^= 1;
        display_backlight(torch_on ? 3 : 1 + brightness * 2 / 100);
        break;
    case 2:
        bt_on ^= 1;
        if (bt_on)
            airplane_on = 0;     /* explicitly enabling BT leaves airplane */
        apply_radio();
        break;
    case 4:
        nav_to(SCR_NOTIF);
        return;                     /* phone tile = notifications   */
    case 5: water_lock ^= 1; break;
    case 6:
        nav_to(SCR_SETTINGS);
        return;
    default: break;
    }
    persist_save();
    rebuild();
}

static void build_cc(lv_obj_t *r)
{
    page_bg(r);
    mk_label_c(r, &kenney16, C_MUTED, 120, 16, "CONTROL");

    /* one canvas paints capsules + the whole comb, like the face's
       dial layer — per-hex draw objects proved fragile */
    lv_obj_t *bg = lv_obj_create(r);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_size(bg, 240, 240);
    lv_obj_set_style_bg_opa(bg, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_set_style_pad_all(bg, 0, 0);
    lv_obj_remove_flag(bg, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(bg, cc_draw, LV_EVENT_DRAW_MAIN, NULL);

    char bb[8];
    lv_snprintf(bb, sizeof bb, "%d%%", hal_battery_pct());
    mk_icon_at(r, 8, 1, 56, 30);         /* battery icon beside capsule */
    mk_label(r, &kenney9, C_MUTED, 84, 38, bb);
    mk_icon_at(r, 7, 1, 168, 30);        /* light icon beside capsule */

    /* invisible tap targets over the capsule and each hexagon;
       icons sit on the bg layer so hits never paint over art */
    lv_obj_t *btap = lv_obj_create(r);
    lv_obj_set_pos(btap, 196, 16);
    lv_obj_set_size(btap, 32, 84);
    lv_obj_set_style_bg_opa(btap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btap, 0, 0);
    lv_obj_set_style_pad_all(btap, 0, 0);
    lv_obj_remove_flag(btap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btap, cc_cb, LV_EVENT_CLICKED, (void *)1);

    for (int i = 0; i < 7; i++) {
        int t = cc_hexpos[i][2];
        if ((t == 3 && (cc_hide & 1)) || (t == 4 && (cc_hide & 4)) ||
            (t == 5 && (cc_hide & 2)))
            continue;
        mk_icon_at(r, tile_icon[t][cc_state(t) != 0], 0,
                   cc_hexpos[i][0] - 12, cc_hexpos[i][1] - 12);
        lv_obj_t *hx = lv_obj_create(r);
        lv_obj_set_pos(hx, cc_hexpos[i][0] - 26, cc_hexpos[i][1] - 28);
        lv_obj_set_size(hx, 52, 56);
        lv_obj_set_style_bg_opa(hx, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(hx, 0, 0);
        lv_obj_set_style_pad_all(hx, 0, 0);
        lv_obj_remove_flag(hx, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(hx, (void *)(intptr_t)t);
        lv_obj_add_event_cb(hx, cc_cb, LV_EVENT_CLICKED, NULL);
    }
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
    /* no gesture hint label — keep the panel clean */
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
    static const char *rows[10] = {
        "DND", "AIRPLANE", "BLUETOOTH", "TORCH", "WATER LOCK",
        "BRIGHTNESS", "CC: TORCH", "CC: WATER", "CC: PHONE", "SET TIME"
    };
    int vals[10] = {dnd_on, airplane_on, bt_on, torch_on, water_lock,
                    brightness, !(cc_hide & 1), !(cc_hide & 2),
                    !(cc_hide & 4), 1};
    for (int i = 0; i < 10; i++) {
        int y = 42 + i * 19;
        mk_label(r, &kenney9, C_WHITE, 18, y, rows[i]);
        char v[8];
        if (i == 5)
            lv_snprintf(v, sizeof v, "%d%%", brightness);
        else
            lv_snprintf(v, sizeof v, "%s", vals[i] ? "ON" : "OFF");
        lv_obj_t *b = mk_btn(r, i == 9 ? ">" : v, 158, y - 6, 64, 22,
                             vals[i] ? C_ACCENT : C_FILL, settings_cb);
        lv_obj_set_style_text_color(lv_obj_get_child(b, 0),
                                    vals[i] ? C_BG : C_MUTED, 0);
        lv_obj_set_user_data(b, (void *)(intptr_t)i);
    }
    static const char *bs[5] = {"BLE OFF", "BLE STARTING", "BLE VISIBLE",
                                "BLE CONNECTED", "BLE RETRYING"};
    int st = ble_status();
    mk_label_c(r, &kenney9, st == 2 || st == 3 ? C_GOOD : C_MUTED, 120,
               230, bs[st < 0 || st > 4 ? 4 : st]);
}

/* manual time set, like the reference watches: +/- on hour/minute,
   DONE writes it and marks the clock synced */
static int set_h, set_m;

static void settime_cb(lv_event_t *e)
{
    int act = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (act == 0) set_h = (set_h + 23) % 24;
    if (act == 1) set_h = (set_h + 1) % 24;
    if (act == 2) set_m = (set_m + 59) % 60;
    if (act == 3) set_m = (set_m + 1) % 60;
    if (act == 4) {
        int y, mo, d, dow;
        hal_clock_date(&y, &mo, &d, &dow);
        if (!y) { y = 2025; mo = 1; d = 1; dow = 3; }
        hal_clock_set(y, mo, d, dow, set_h, set_m, 0);
        nav_to(SCR_SETTINGS);
        return;
    }
    if (act == 5) {
        nav_to(SCR_SETTINGS);
        return;
    }
    rebuild();
}

static void build_settime(lv_obj_t *r)
{
    page_bg(r);
    mk_header(r, "SET TIME", 1);
    if (!set_h && !set_m) {
        set_h = hal_clock_hour();
        set_m = hal_clock_minute();
    }
    char buf[8];
    lv_snprintf(buf, sizeof buf, "%02d:%02d", set_h, set_m);
    mk_label_c(r, &kenney24, C_WHITE, 120, 96, buf);
    lv_obj_set_user_data(mk_btn(r, "-", 42, 140, 40, 32, C_FILL, settime_cb),
                         (void *)(intptr_t)0);
    lv_obj_set_user_data(mk_btn(r, "+", 88, 140, 40, 32, C_FILL, settime_cb),
                         (void *)(intptr_t)1);
    lv_obj_set_user_data(mk_btn(r, "-", 112, 140, 40, 32, C_FILL, settime_cb),
                         (void *)(intptr_t)2);
    lv_obj_set_user_data(mk_btn(r, "+", 158, 140, 40, 32, C_FILL, settime_cb),
                         (void *)(intptr_t)3);
    lv_obj_set_user_data(mk_btn(r, "DONE", 70, 186, 100, 30, C_ACCENT,
                                settime_cb), (void *)(intptr_t)4);
    mk_label_c(r, &kenney9, C_MUTED, 120, 224, "24H · SAVED ON DONE");
}

static void settings_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    switch (i) {
    case 0: dnd_on ^= 1; break;
    case 1: airplane_on ^= 1; apply_radio(); break;
    case 2:
        bt_on ^= 1;
        if (bt_on)
            airplane_on = 0;
        apply_radio();
        break;
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
    case 9:
        set_h = hal_clock_hour();
        set_m = hal_clock_minute();
        nav_to(SCR_SETTIME);
        return;
    }
    rebuild();
    persist_save();
}

/* ---- stats ------------------------------------------------------------------- */

static void ppg_draw(lv_event_t *ev)
{
    lv_layer_t *layer = lv_event_get_layer(ev);
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target(ev), &area);
    int x0 = area.x1, x1 = area.x2;
    int mid = (area.y1 + area.y2) / 2;
    int half = (area.y2 - area.y1) / 2 - 2;
    int y100;
    if (!sensors_ppg(0, &y100))
        return;
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = lv_color_hex(0x5AC8D2);
    dsc.width = 2;
    int prev_y = mid - y100 * half / 100;
    for (int x = x0 + 1; x <= x1; x++) {
        sensors_ppg((x - x0) * 128 / (x1 - x0), &y100);
        int y = mid - y100 * half / 100;
        dsc.p1.x = x - 1;
        dsc.p1.y = prev_y;
        dsc.p2.x = x;
        dsc.p2.y = y;
        lv_draw_line(layer, &dsc);
        prev_y = y;
    }
}

/* one simulator stat card: sharp-cornered fill, small muted label
   top-left, coloured value right-aligned */
static void stat_card(lv_obj_t *r, int x, int y, const char *lab,
                      const char *val, lv_color_t vc, int icon)
{
    lv_obj_t *c = lv_obj_create(r);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, 96, 44);
    lv_obj_set_style_bg_color(c, lv_color_hex(0x1A2230), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 4, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    if (icon >= 0)
        mk_icon_at(c, icon, 0, 6, 10);
    mk_label(c, &kenney9, C_MUTED, icon >= 0 ? 34 : 8, 7, lab);
    mk_label(c, &kenney16, vc, icon >= 0 ? 34 : 8, 20, val);
}

static void build_stats(lv_obj_t *r)
{
    page_bg(r);
    mk_label_c(r, &kenney16, C_MUTED, 120, 12, "STATS");
    char buf[24];
    int steps = ui_steps();
    int bpm = sensors_bpm();

    lv_snprintf(buf, sizeof buf, "%d", steps >= 0 ? steps : 0);
    stat_card(r, 18, 40, "STEPS", steps >= 0 ? buf : "--", C_WHITE, 14);
    lv_snprintf(buf, sizeof buf, "%d BPM", bpm > 0 ? bpm : 0);
    stat_card(r, 126, 40, "HEART",
              bpm > 0 ? buf : (bpm == 0 ? "SENS" : "--"), C_ACCENT, -1);

    lv_snprintf(buf, sizeof buf, "%d", resting_bpm);
    stat_card(r, 18, 90, "RESTING", resting_bpm ? buf : "--",
              C_WHITE, -1);
    stat_card(r, 126, 90, "HRV", "--", C_ACCENT, -1);

    if (s_in_bed) {
        lv_snprintf(buf, sizeof buf, "IN BED");
    } else if (s_last_ms) {
        unsigned sm = s_last_ms / 60000;
        lv_snprintf(buf, sizeof buf, "%uH %uM", sm / 60, sm % 60);
    } else {
        lv_snprintf(buf, sizeof buf, "--");
    }
    stat_card(r, 18, 140, "SLEEP", buf, C_WHITE, -1);
    lv_snprintf(buf, sizeof buf, "%d ML", water_ml);
    stat_card(r, 126, 140, "WATER", buf, lv_color_hex(0x1E96BE), 5);

    /* workout status strip — same flat bar as the simulator */
    lv_obj_t *bar = lv_obj_create(r);
    lv_obj_set_pos(bar, 18, 196);
    lv_obj_set_size(bar, 204, 26);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x1A2230), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 4, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    mk_label(bar, &kenney9, C_MUTED, 8, 9,
             w_active ? "WORKOUT LIVE" : "WORKOUT");
    if (w_active) {
        lv_snprintf(buf, sizeof buf, "%d SETS", w_sets);
        mk_label(bar, &kenney9, C_ACCENT, 140, 9, buf);
    } else {
        mk_label(bar, &kenney9, C_MUTED, 152, 9, "NO SAVE");
    }
}

/* ---- notifications + cardio ------------------------------------------------ */

/* ---- notifications -------------------------------------------------------- */

int ancs_ready(void), ancs_state(void), ancs_count(void);
int ancs_take_dirty(void), ancs_take_new(void);
struct ancs_notif_view {
    uint32_t uid; uint8_t flags; uint8_t has_pos, has_neg;
    const char *app, *title, *msg; uint32_t rx_ms;
};
const struct ancs_notif_view *ancs_get(int i);
void ancs_dismiss(uint32_t uid);
void ancs_positive(uint32_t uid);

static int notif_sel = -1;         /* -1 = list, >=0 = detail view */
static int notif_reply;            /* 1 = quick-reply picker open */

static void notif_item_cb(lv_event_t *e)
{
    notif_sel = (int)(intptr_t)lv_obj_get_user_data(
        lv_event_get_target(e));
    rebuild();
}

static void notif_back_cb(lv_event_t *e)
{
    (void)e;
    notif_sel = -1;
    notif_reply = 0;
    rebuild();
}

static void notif_dismiss_cb(lv_event_t *e)
{
    (void)e;
    const struct ancs_notif_view *v = ancs_get(notif_sel);
    if (v)
        ancs_dismiss(v->uid);
    notif_sel = -1;
    notif_reply = 0;
    rebuild();
}

static void notif_reply_cb(lv_event_t *e)
{
    (void)e;
    notif_reply = 1;
    rebuild();
}

static void notif_send_cb(lv_event_t *e)
{
    (void)e;
    /* ActionIDPositive fires the notification's reply action on the
       phone — for Messages that hands off to iOS to handle the text */
    const struct ancs_notif_view *v = ancs_get(notif_sel);
    if (v)
        ancs_positive(v->uid);
    notif_sel = -1;
    notif_reply = 0;
    rebuild();
}

static void build_notif(lv_obj_t *r)
{
    page_bg(r);

    if (notif_sel >= 0) {
        /* detail: app + title + body, actions at the bottom */
        const struct ancs_notif_view *v = ancs_get(notif_sel);
        if (!v) {
            notif_sel = -1;
            rebuild();
            return;
        }
        mk_btn(r, "<", 12, 10, 36, 26, C_FILL, notif_back_cb);
        mk_label(r, &kenney9, C_MUTED, 56, 16,
                 v->app[0] ? v->app : "NOTIFICATION");
        lv_obj_t *t = mk_label(r, &kenney16, C_WHITE, 16, 44,
                               v->title[0] ? v->title : "(NO TITLE)");
        lv_obj_set_width(t, 208);
        lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
        lv_obj_t *m = mk_label(r, &kenney9, C_MUTED, 16, 78, v->msg);
        lv_obj_set_width(m, 208);
        lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);

        if (notif_reply) {
            /* canned replies — sent as the notification's positive
               action; real typed replies need a companion relay */
            static const char *qr[4] = {"OK", "YES", "NO", "CALL YOU"};
            for (int i = 0; i < 4; i++) {
                lv_obj_t *b = mk_btn(r, qr[i], 16 + (i % 2) * 106,
                                     152 + (i / 2) * 40, 100, 34,
                                     C_FILL, notif_send_cb);
                lv_obj_set_user_data(b, (void *)(intptr_t)i);
            }
        } else {
            mk_btn(r, "REPLY", 16, 186, 100, 34, C_FILL, notif_reply_cb);
            mk_btn(r, "DISMISS", 124, 186, 100, 34, C_FILL,
                   notif_dismiss_cb);
        }
        return;
    }

    mk_label_c(r, &kenney16, C_MUTED, 120, 14, "NOTIFICATIONS");

    if (!ble_connected()) {
        mk_label_c(r, &kenney9, C_WARN, 120, 110, "PHONE NOT CONNECTED");
        mk_label_c(r, &kenney9, C_MUTED, 120, 130,
                   "PAIR FOR NOTIFICATIONS");
        return;
    }
    if (ancs_state() < 6 /* ST_LIVE */) {
        mk_label_c(r, &kenney9, C_MUTED, 120, 110, "SYNCING...");
        mk_label_c(r, &kenney9, C_MUTED, 120, 130,
                   "ANCS HANDSHAKE");
        return;
    }
    if (!ancs_count()) {
        mk_icon_at(r, 13, 0, 108, 80);   /* cloud */
        mk_label_c(r, &kenney16, C_WHITE, 120, 118, "INBOX ZERO");
        mk_label_c(r, &kenney9, C_MUTED, 120, 140, "ALL CAUGHT UP");
        return;
    }

    for (int i = 0; i < ancs_count() && i < 4; i++) {
        const struct ancs_notif_view *v = ancs_get(i);
        if (!v)
            continue;
        int y = 40 + i * 48;
        lv_obj_t *row = lv_obj_create(r);
        lv_obj_set_pos(row, 12, y);
        lv_obj_set_size(row, 216, 44);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1A2230), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_user_data(row, (void *)(intptr_t)i);
        lv_obj_add_event_cb(row, notif_item_cb, LV_EVENT_CLICKED, NULL);
        /* unread marker */
        lv_obj_t *dot = lv_obj_create(row);
        lv_obj_set_pos(dot, 8, 18);
        lv_obj_set_size(dot, 7, 7);
        lv_obj_set_style_bg_color(dot, C_ACCENT, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(dot, 4, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_remove_flag(dot,
                           LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        mk_label(row, &kenney9, C_MUTED, 24, 6,
                 v->app[0] ? v->app : "APP");
        lv_obj_t *tl = mk_label(row, &kenney16, C_WHITE, 24, 20,
                                v->title[0] ? v->title : v->msg);
        lv_obj_set_width(tl, 184);
        lv_label_set_long_mode(tl, LV_LABEL_LONG_DOT);
    }
}

static lv_obj_t *cardio_trace;
static lv_obj_t *cardio_bpm;

static void build_cardio(lv_obj_t *r)
{
    page_bg(r);
    mk_label_c(r, &kenney16, C_MUTED, 120, 12, "CARDIO");

    /* big live BPM — the number is the point */
    int bpm = sensors_bpm();
    char buf[16];
    if (bpm > 0)
        lv_snprintf(buf, sizeof buf, "%d", bpm);
    else
        lv_snprintf(buf, sizeof buf, bpm == 0 ? "--" : "OFF");
    cardio_bpm = mk_label_c(r, &kenney56, C_ACCENT, 120, 58, buf);
    mk_label_c(r, &kenney9, C_MUTED, 120, 108,
               bpm > 0 ? "BPM" : bpm == 0 ? "SENSING" : "NO HRS");

    /* full-width live PPG — the real optical signal */
    lv_obj_t *t = lv_obj_create(r);
    lv_obj_set_pos(t, 8, 124);
    lv_obj_set_size(t, 224, 90);
    lv_obj_set_style_bg_color(t, lv_color_hex(0x0B0E14), 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(t, 4, 0);
    lv_obj_set_style_border_width(t, 0, 0);
    lv_obj_set_style_pad_all(t, 0, 0);
    lv_obj_remove_flag(t, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(t, ppg_draw, LV_EVENT_DRAW_MAIN, NULL);
    cardio_trace = t;

    mk_label_c(r, &kenney9, C_MUTED, 120, 220, "HRS3300 OPTICAL");
}

/* ---- dispatch ------------------------------------------------------------------ */

static void rebuild(void)
{
    lv_obj_clean(root);
    lbl_time_big = NULL;
    cardio_trace = NULL;
    cardio_bpm = NULL;
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
    case SCR_SETTIME:   build_settime(root); break;
    case SCR_NOTIF:     build_notif(root); break;
    case SCR_CARDIO:    build_cardio(root); break;
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
    /* no LV_EVENT_GESTURE or CLICKED handler: the CST816S detects
       gestures in hardware (ui_swipe) and taps never navigate */
    persist_load();
    apply_radio();
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
    /* idle -> screen off (never mid-DFU) */
    if (!asleep && last_input_ms && now - last_input_ms > SLEEP_AFTER_MS &&
        !ble_dfu_busy()) {
        asleep = 1;
        display_backlight(0);
        persist_save();
    }
    /* raise-to-wake: the Bosch engine's wrist-wear flag is the primary
       detector; if the engine never loaded, fall back to a sustained
       tilt change (>0.5 g-equivalent held for ~8 ticks) with slow
       drift tracking so a resting wrist can't keep re-firing it */
    if (asleep) {
        static int tilt_streak;
        static int32_t sleep_az = 0x7FFFFFFF;
        if (sensors_wrist_raise()) {
            ui_wake();
            tilt_streak = 0;
            sleep_az = 0x7FFFFFFF;
        } else {
            int z = sensors_bma_z();
            if (sleep_az == 0x7FFFFFFF)
                sleep_az = z;
            int32_t dz = z - sleep_az;
            if (dz < 0)
                dz = -dz;
            if (dz > 600) {
                if (++tilt_streak >= 8) {
                    ui_wake();
                    tilt_streak = 0;
                    sleep_az = 0x7FFFFFFF;
                }
            } else {
                tilt_streak = 0;
                sleep_az += (z - sleep_az) >> 4;
            }
        }
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
        hal_motor(1);
        motor_off_at = now + 400;
        if (asleep)
            ui_wake();
        if (cur == SCR_CLOCK)
            rebuild();
    }
    if (motor_off_at && now >= motor_off_at) {
        motor_off_at = 0;
        hal_motor(0);
    }
    /* incoming notification: buzz + wake unless DND is on */
    if (ancs_take_new() && !dnd_on) {
        hal_motor(1);
        motor_off_at = now + 250;
        if (asleep)
            ui_wake();
    }
    if (cur == SCR_NOTIF && ancs_take_dirty())
        rebuild();
    /* resting HR = lowest sustained reading this session */
    {
        int b = sensors_bpm();
        if (b > 0 && (resting_bpm == 0 || b < resting_bpm))
            resting_bpm = b;
    }
    switch (cur) {
    case SCR_FACE:
        ui_face_tick();
        break;
    case SCR_CARDIO:
        if (cardio_trace) {
            static uint32_t last_tr;
            if (now - last_tr >= 100) {
                last_tr = now;
                lv_obj_invalidate(cardio_trace);
            }
        }
        if (cardio_bpm) {
            static int last_bpm = -2;
            int bpm = sensors_bpm();
            if (bpm != last_bpm) {
                last_bpm = bpm;
                char buf[16];
                lv_snprintf(buf, sizeof buf, "%d", bpm > 0 ? bpm : 0);
                lv_label_set_text(cardio_bpm, bpm > 0 ? buf : "--");
            }
        }
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
