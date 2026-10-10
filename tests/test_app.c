#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "voidos/app.h"

static uint32_t g_now;
static int g_draw_calls;
static int g_present_calls;
static int g_hour = 7, g_min = 30, g_sec = 0;

#define MAX_RECTS 40000
static int g_rect_x[MAX_RECTS];
static int g_rect_y[MAX_RECTS];
static int g_rect_w[MAX_RECTS];
static int g_rect_h[MAX_RECTS];
static int g_rect_c[MAX_RECTS];
static int g_rect_n;

int hal_init(void) { return 1; }
void hal_shutdown(void) {}
int hal_poll_event(hal_event_t *ev) { (void)ev; return 0; }
void hal_draw_rect(int x, int y, int w, int h, hal_color_t c)
{
    if (g_rect_n < MAX_RECTS) {
        g_rect_x[g_rect_n] = x;
        g_rect_y[g_rect_n] = y;
        g_rect_w[g_rect_n] = w;
        g_rect_h[g_rect_n] = h;
        g_rect_c[g_rect_n] = c;
        g_rect_n++;
    }
    g_draw_calls++;
}
void hal_draw_rect_a(int x, int y, int w, int h, hal_color_t c,
                     unsigned alpha)
{
    /* approximate alpha blending onto black so colour-based checks hold */
    unsigned r = (((c >> 8) & 0xF8) * alpha) / 255;
    unsigned g = (((c >> 3) & 0xFC) * alpha) / 255;
    unsigned b = (((c << 3) & 0xF8) * alpha) / 255;
    /* clip to the screen like the real HAL does */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > VOIDOS_WIDTH) w = VOIDOS_WIDTH - x;
    if (y + h > VOIDOS_HEIGHT) h = VOIDOS_HEIGHT - y;
    hal_draw_rect(x, y, w, h, HAL_RGB565(r, g, b));
}
void hal_present(void) { g_present_calls++; }
void hal_delay_ms(unsigned ms) { (void)ms; }
uint32_t hal_millis(void) { return g_now; }
int hal_clock_hour(void) { return g_hour; }
int hal_clock_minute(void) { return g_min; }
int hal_clock_second(void) { return g_sec; }
int hal_clock_millis(void) { return (int)(g_now % 1000); }

static int g_face_text_calls;
static int g_face_text_px[64];
static char g_face_text_str[64][16];
static int g_face_text_x[64];
static int g_face_text_y[64];

void hal_face_text_size(const char *text, int px, int *w, int *h)
{
    int n = 0;
    while (text[n])
        n++;
    *w = n * px / 2;
    *h = px;
}

void hal_draw_icon(hal_icon_t icon, int x, int y, int size,
                   unsigned alpha)
{
    (void)icon; (void)x; (void)y; (void)size; (void)alpha;
}
void hal_mark_dirty(int x, int y, int w, int h)
{
    (void)x; (void)y; (void)w; (void)h;
}
void hal_mark_all_dirty(void) {}
void hal_mark_screen(void) {}
int hal_render_static(void) { return 1; }
void hal_draw_logo(int x, int y, int w, int h, unsigned alpha,
                   int shimmer_col)
{
    (void)x; (void)y; (void)w; (void)h; (void)alpha; (void)shimmer_col;
}

void hal_face_draw_text(int x, int y, const char *text,
                        hal_color_t color, int px)
{
    (void)color;
    if (g_face_text_calls < 64) {
        g_face_text_px[g_face_text_calls] = px;
        g_face_text_x[g_face_text_calls] = x;
        g_face_text_y[g_face_text_calls] = y;
        int i = 0;
        while (text[i] && i < 15) {
            g_face_text_str[g_face_text_calls][i] = text[i];
            i++;
        }
        g_face_text_str[g_face_text_calls][i] = 0;
    }
    g_face_text_calls++;
}

static int rect_covers(int x, int y)
{
    for (int i = 0; i < g_rect_n; i++) {
        if (g_rect_w[i] >= VOIDOS_WIDTH && g_rect_h[i] >= VOIDOS_HEIGHT)
            continue;   /* full-screen clear / dim overlay */
        if (g_rect_w[i] >= 220 && g_rect_h[i] <= 2)
            continue;   /* squircle background row */
        if (x >= g_rect_x[i] && x < g_rect_x[i] + g_rect_w[i] &&
            y >= g_rect_y[i] && y < g_rect_y[i] + g_rect_h[i])
            return 1;
    }
    return 0;
}

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static void ev(app_t *a, hal_event_type_t t, int x, int y)
{
    hal_event_t e = {t, x, y};
    app_handle_event(a, &e);
}

static void tap(app_t *a, int x, int y)
{
    ev(a, HAL_EV_TOUCH_DOWN, x, y);
    ev(a, HAL_EV_TOUCH_UP, x, y);
}

static void swipe(app_t *a, int x0, int y0, int x1, int y1)
{
    ev(a, HAL_EV_TOUCH_DOWN, x0, y0);
    ev(a, HAL_EV_TOUCH_MOVE, (x0 + x1) / 2, (y0 + y1) / 2);
    ev(a, HAL_EV_TOUCH_UP, x1, y1);
}

static void tile_center(int idx, int *x, int *y)
{
    *x = (((idx / 2) & 1) ? 96 : 70) + (idx % 2) * 80;
    *y = 64 + (idx / 2) * 54;
}

static void open_launcher(app_t *a)
{
    tap(a, 120, 120);
}

static void test_swipe_nav(void)
{
    app_t a;
    app_init(&a);
    swipe(&a, 120, 200, 120, 100);
    CHECK(a.screen == SCR_STATS);
    app_init(&a);
    swipe(&a, 120, 60, 120, 180);
    CHECK(a.screen == SCR_QUICK);
    app_init(&a);
    swipe(&a, 190, 120, 70, 120);
    CHECK(a.screen == SCR_NUTRITION);
    app_init(&a);
    swipe(&a, 60, 120, 190, 120);
    CHECK(a.screen == SCR_NOTIF);
}

static void test_face_app_access(void)
{
    app_t a;
    app_init(&a);
    tap(&a, 200, 40);
    CHECK(a.screen == SCR_LAUNCHER);
    app_init(&a);
    ev(&a, HAL_EV_BUTTON, 0, 0);
    CHECK(a.screen == SCR_LAUNCHER);
    ev(&a, HAL_EV_BUTTON, 0, 0);
    CHECK(a.screen == SCR_FACE);
}

static void test_ambiguous_drag_ignored(void)
{
    app_t a;
    app_init(&a);
    swipe(&a, 120, 120, 150, 155);
    CHECK(a.screen == SCR_FACE);
    swipe(&a, 120, 120, 150, 100);
    CHECK(a.screen == SCR_FACE);
}

static void test_launcher_to_workout_summary(void)
{
    app_t a;
    app_init(&a);
    int x, y;
    open_launcher(&a);
    CHECK(a.screen == SCR_LAUNCHER);
    tile_center(0, &x, &y);
    tap(&a, x, y);
    CHECK(a.screen == SCR_WORKOUT);
    tap(&a, 120, 130);
    CHECK(a.screen == SCR_WORKOUT_RUN);
    CHECK(a.sets_done == 0);
    for (int i = 0; i < 3; i++) {
        tap(&a, 120, 127);
        CHECK(a.sets_done == i + 1);
        CHECK(a.screen == SCR_WORKOUT_RUN);
    }
    tap(&a, 120, 127);
    CHECK(a.screen == SCR_WORKOUT_DONE);
    CHECK(a.sets_done == 3);
}

static void test_rest_and_pause(void)
{
    app_t a;
    app_init(&a);
    g_now = 10000;
    open_launcher(&a);
    int x, y;
    tile_center(0, &x, &y);
    tap(&a, x, y);
    tap(&a, 120, 130);
    tap(&a, 120, 127);
    CHECK(a.rest_end_ms == g_now + APP_REST_MS);
    tap(&a, 60, 192);
    CHECK(a.workout_paused == 1);
    g_now += 30000;
    tap(&a, 120, 127);
    CHECK(a.sets_done == 1);
    tap(&a, 60, 192);
    CHECK(a.workout_paused == 0);
    CHECK(a.paused_total_ms == 30000);
    CHECK(a.rest_end_ms == 10000 + APP_REST_MS + 30000);
    tap(&a, 120, 127);
    CHECK(a.sets_done == 2);
    tap(&a, 60, 192);
    tap(&a, 120, 127);
    CHECK(a.sets_done == 2);
    tap(&a, 60, 192);
    tap(&a, 120, 127);
    CHECK(a.sets_done == 3);
    tap(&a, 120, 127);
    CHECK(a.screen == SCR_WORKOUT_DONE);
    CHECK(a.workout_active == 0);
}

static void test_workout_leave_reenter(void)
{
    app_t a;
    app_init(&a);
    g_now = 50000;
    open_launcher(&a);
    int x, y;
    tile_center(0, &x, &y);
    tap(&a, x, y);
    tap(&a, 120, 130);
    CHECK(a.workout_active == 1);
    tap(&a, 120, 127);
    CHECK(a.sets_done == 1);
    uint32_t rest = a.rest_end_ms;
    ev(&a, HAL_EV_BUTTON, 0, 0);
    CHECK(a.screen == SCR_LAUNCHER);
    CHECK(a.sets_done == 1);
    CHECK(a.rest_end_ms == rest);
    tile_center(0, &x, &y);
    tap(&a, x, y);
    CHECK(a.screen == SCR_WORKOUT_RUN);
    CHECK(a.sets_done == 1);
    tap(&a, 120, 127);
    tap(&a, 120, 127);
    tap(&a, 120, 127);
    CHECK(a.screen == SCR_WORKOUT_DONE);
    CHECK(a.workout_active == 0);
}

static void test_drag_back_to_start_not_tap(void)
{
    app_t a;
    app_init(&a);
    open_launcher(&a);
    int x, y;
    tile_center(2, &x, &y);
    tap(&a, x, y);
    CHECK(a.screen == SCR_WATER);
    ev(&a, HAL_EV_TOUCH_DOWN, 65, 145);
    ev(&a, HAL_EV_TOUCH_MOVE, 65, 100);
    ev(&a, HAL_EV_TOUCH_MOVE, 65, 145);
    ev(&a, HAL_EV_TOUCH_UP, 65, 145);
    CHECK(a.water_ml == 0);
}

static void test_intermediate_drag_not_start(void)
{
    app_t a;
    app_init(&a);
    open_launcher(&a);
    int x, y;
    tile_center(0, &x, &y);
    tap(&a, x, y);
    CHECK(a.screen == SCR_WORKOUT);
    ev(&a, HAL_EV_TOUCH_DOWN, 120, 130);
    ev(&a, HAL_EV_TOUCH_MOVE, 120, 105);
    ev(&a, HAL_EV_TOUCH_UP, 120, 130);
    CHECK(a.screen == SCR_WORKOUT);
    CHECK(a.workout_active == 0);
}

static void test_sleep_time_in_bed(void)
{
    app_t a;
    app_init(&a);
    g_now = 100000;
    open_launcher(&a);
    int x, y;
    tile_center(1, &x, &y);
    tap(&a, x, y);
    CHECK(a.screen == SCR_SLEEP);
    tap(&a, 120, 137);
    CHECK(a.in_bed == 1);
    g_now += 8u * 3600u * 1000u;
    tap(&a, 120, 137);
    CHECK(a.in_bed == 0);
    CHECK(a.screen == SCR_SLEEP_DONE);
    CHECK(a.last_in_bed_ms == 8u * 3600u * 1000u);
}

static void test_water_adds(void)
{
    app_t a;
    app_init(&a);
    open_launcher(&a);
    int x, y;
    tile_center(2, &x, &y);
    tap(&a, x, y);
    CHECK(a.screen == SCR_WATER);
    tap(&a, 65, 214);
    tap(&a, 65, 214);
    tap(&a, 175, 214);
    CHECK(a.water_ml == 1000);
}

static void test_button_back(void)
{
    app_t a;
    app_init(&a);
    open_launcher(&a);
    CHECK(a.screen == SCR_LAUNCHER);
    ev(&a, HAL_EV_BUTTON, 0, 0);
    CHECK(a.screen == SCR_FACE);
    open_launcher(&a);
    int x, y;
    tile_center(4, &x, &y);
    tap(&a, x, y);
    CHECK(a.screen == SCR_SETTINGS);
    ev(&a, HAL_EV_BUTTON, 0, 0);
    CHECK(a.screen == SCR_LAUNCHER);
    ev(&a, HAL_EV_BUTTON, 0, 0);
    CHECK(a.screen == SCR_FACE);
}

static void test_onscreen_back(void)
{
    app_t a;
    app_init(&a);
    swipe(&a, 120, 60, 120, 180);
    CHECK(a.screen == SCR_QUICK);
    swipe(&a, 120, 190, 120, 80);   /* swipe up dismisses control centre */
    CHECK(a.screen == SCR_FACE);
}

static void test_render_and_quit(void)
{
    app_t a;
    app_init(&a);
    app_render(&a);
    CHECK(g_draw_calls > 0);
    ev(&a, HAL_EV_QUIT, 0, 0);
    CHECK(a.quit == 1);
}

static void test_face_hands(void)
{
    app_t a;
    int x, y;
    app_init(&a);
    a.analog_face = 1;

    g_hour = 12; g_min = 0; g_sec = 0;
    g_rect_n = 0;
    app_render(&a);
    app_dial_point(0, 88, &x, &y);
    CHECK(rect_covers(x, y));
    app_dial_point(0, 58, &x, &y);
    CHECK(rect_covers(x, y));
    app_dial_point(0, 100, &x, &y);
    CHECK(rect_covers(x, y));

    g_hour = 3; g_min = 15; g_sec = 30;
    g_rect_n = 0;
    app_render(&a);
    /* minute hand sweeps with seconds: 15 + 30/60 = 15.5 */
    app_dial_point_f(15.5f, 88, &x, &y);
    CHECK(x > 195 && rect_covers(x, y));
    app_dial_point_f(30.0f, 100, &x, &y);
    CHECK(y > 210 && rect_covers(x, y));
    /* hour hand sweeps with minutes: 15 + 15.5/12 */
    app_dial_point_f(15.0f + 15.5f / 12.0f, 58, &x, &y);
    CHECK(rect_covers(x, y));

    g_sec = 0;
    g_rect_n = 0;
    app_render(&a);
    app_dial_point(0, 100, &x, &y);
    CHECK(rect_covers(x, y));
    app_dial_point(30, 50, &x, &y);
    CHECK(!rect_covers(x, y));
}

static void test_face_bounds(void)
{
    app_t a;
    int x, y;
    app_init(&a);
    a.analog_face = 1;
    g_hour = 11; g_min = 58; g_sec = 45;
    g_rect_n = 0;
    g_face_text_calls = 0;
    app_render(&a);
    CHECK(g_rect_n > 100);
    for (int i = 0; i < g_rect_n; i++) {
        CHECK(g_rect_x[i] >= 0 && g_rect_y[i] >= 0);
        CHECK(g_rect_x[i] + g_rect_w[i] <= VOIDOS_WIDTH);
        CHECK(g_rect_y[i] + g_rect_h[i] <= VOIDOS_HEIGHT);
    }
    static const int card[4][3] = {
        {12, 120, 28}, {3, 212, 120}, {6, 120, 212}, {9, 28, 120}
    };
    for (int ci = 0; ci < 4; ci++) {
        int n = card[ci][0];
        x = card[ci][1];
        y = card[ci][2];
        int found = 0;
        for (int i = 0; i < g_face_text_calls && i < 64; i++) {
            int w = 0, h = 0;
            hal_face_text_size(g_face_text_str[i], g_face_text_px[i], &w, &h);
            int cx = g_face_text_x[i] + w / 2;
            int cy = g_face_text_y[i] + h / 2;
            char want[4];
            snprintf(want, sizeof want, "%d", n);
            if (strcmp(g_face_text_str[i], want) == 0 &&
                cx >= x - 2 && cx <= x + 2 && cy >= y - 2 && cy <= y + 2)
                found = 1;
        }
        CHECK(found);
    }
}

static void test_numeral_uniform_px(void)
{
    app_t a;
    app_init(&a);
    a.analog_face = 1;
    g_face_text_calls = 0;
    app_render(&a);
    int nums = 0;
    for (int i = 0; i < g_face_text_calls && i < 64; i++) {
        if (g_face_text_px[i] == 9)
            continue;   /* minute labels / small markers */
        if (g_face_text_str[i][0] >= '0' && g_face_text_str[i][0] <= '9' &&
            (g_face_text_str[i][1] == 0 ||
             (g_face_text_str[i][1] >= '0' && g_face_text_str[i][1] <= '9' &&
              g_face_text_str[i][2] == 0))) {
            CHECK(g_face_text_px[i] == 22);
            nums++;
        }
    }
    CHECK(nums == 4);
}

static void test_face_placement(void)
{
    const struct { int hour, minute, bpm_y; } cases[] = {
        {2, 59, 168}, {3, 0, 54}, {9, 0, 54}, {9, 1, 168}
    };
    app_t a;
    app_init(&a);
    a.analog_face = 1;
    g_sec = 0;
    for (unsigned k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        g_hour = cases[k].hour;
        g_min = cases[k].minute;
        g_rect_n = 0;
        g_face_text_calls = 0;
        app_render(&a);
        int heart_pixels = 0, bpm_text = 0;
        for (int i = 0; i < g_rect_n; i++) {
            /* count only red pixels inside the heart band; the red
               second-hand trail lives on the perimeter */
            if (g_rect_c[i] == HAL_COLOR_RED &&
                g_rect_y[i] >= cases[k].bpm_y &&
                g_rect_y[i] < cases[k].bpm_y + 12 &&
                g_rect_x[i] >= 80 && g_rect_x[i] < 150)
                heart_pixels++;
        }
        for (int i = 0; i < g_face_text_calls && i < 64; i++) {
            const char *s = g_face_text_str[i];
            int len = (int)strlen(s);
            if (len >= 5 && s[0] >= '0' && s[0] <= '9' &&
                strcmp(s + len - 4, " BPM") == 0 &&
                g_face_text_y[i] == cases[k].bpm_y)
                bpm_text++;
        }
        CHECK(heart_pixels > 0 && bpm_text == 1);
        for (int p = 0; p < 60; p += 5) {
            int x, y;
            app_dial_point(p, 110, &x, &y);
            for (int i = 0; i < g_rect_n; i++)
                CHECK(g_rect_c[i] != HAL_COLOR_WHITE ||
                      g_rect_x[i] < x - 2 || g_rect_x[i] > x + 2 ||
                      g_rect_y[i] < y - 2 || g_rect_y[i] > y + 2);
        }
    }
    a.screen = SCR_QUICK;
    g_rect_n = 0;
    app_render(&a);
    int slider_fill = 0;
    for (int i = 0; i < g_rect_n; i++)
        if (g_rect_c[i] == HAL_COLOR_ACCENT &&
            g_rect_c[i] == HAL_COLOR_ACCENT &&
            g_rect_x[i] < 202 && g_rect_y[i] >= 44 &&
            g_rect_y[i] < 102)
            slider_fill++;
    CHECK(slider_fill >= 1);
    g_hour = 7;
    g_min = 30;
}

static void test_settings_parent(void)
{
    app_t a;
    app_init(&a);
    swipe(&a, 120, 60, 120, 180);
    CHECK(a.screen == SCR_QUICK);
    tap(&a, 149, 182);
    CHECK(a.screen == SCR_SETTINGS);
    ev(&a, HAL_EV_BUTTON, 0, 0);
    CHECK(a.screen == SCR_QUICK);
    ev(&a, HAL_EV_BUTTON, 0, 0);
    CHECK(a.screen == SCR_FACE);
    open_launcher(&a);
    int x, y;
    tile_center(4, &x, &y);
    tap(&a, x, y);
    CHECK(a.screen == SCR_SETTINGS);
    ev(&a, HAL_EV_BUTTON, 0, 0);
    CHECK(a.screen == SCR_LAUNCHER);
}

static void test_nutrition(void)
{
    app_t a;
    app_init(&a);
    swipe(&a, 190, 120, 70, 120);
    CHECK(a.screen == SCR_NUTRITION);
    tap(&a, 120, 152);
    CHECK(a.water_ml == 250);
    tap(&a, 120, 152);
    CHECK(a.water_ml == 500);
    ev(&a, HAL_EV_BUTTON, 0, 0);
    CHECK(a.screen == SCR_FACE);
}

static void test_dark_button_text(void)
{
    app_t a;
    app_init(&a);
    a.screen = SCR_QUICK;
    g_rect_n = 0;
    app_render(&a);
    /* settings hex body must be rendered (icons live in the HAL now) */
    int body_px = 0;
    for (int i = 0; i < g_rect_n; i++)
        if (g_rect_c[i] == HAL_RGB565(30, 36, 48) &&
            g_rect_x[i] < 164 && g_rect_x[i] + g_rect_w[i] > 142 &&
            g_rect_y[i] >= 164 && g_rect_y[i] < 200)
            body_px++;
    CHECK(body_px > 5);
}

static void test_long_press_peek(void)
{
    app_t a;
    app_init(&a);
    g_now = 50000;
    ev(&a, HAL_EV_TOUCH_DOWN, 120, 120);
    g_now += 800;
    ev(&a, HAL_EV_TOUCH_UP, 120, 120);
    CHECK(a.screen == SCR_FACE);
    ev(&a, HAL_EV_TOUCH_DOWN, 120, 120);
    g_now += 100;
    ev(&a, HAL_EV_TOUCH_UP, 120, 120);
    CHECK(a.screen == SCR_LAUNCHER);
}

int main(void)
{
    test_swipe_nav();
    test_face_app_access();
    test_ambiguous_drag_ignored();
    test_launcher_to_workout_summary();
    test_rest_and_pause();
    test_workout_leave_reenter();
    test_drag_back_to_start_not_tap();
    test_intermediate_drag_not_start();
    test_sleep_time_in_bed();
    test_water_adds();
    test_button_back();
    test_onscreen_back();
    test_face_hands();
    test_face_bounds();
    test_numeral_uniform_px();
    test_face_placement();
    test_settings_parent();
    test_nutrition();
    test_dark_button_text();
    test_render_and_quit();
    test_long_press_peek();
    if (failures) {
        printf("%d FAILURES\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
