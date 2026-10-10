#include <stdio.h>
#include <SDL.h>
#include "voidos/app.h"

uint32_t hal_window_id(void);
static uint32_t g_winid;

static int fails;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        fails++; \
    } \
} while (0)

static void push_button(Uint32 type, int wx, int wy)
{
    SDL_Event e;
    e.type = type;
    e.button.type = type;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = wx;
    e.button.y = wy;
    e.button.windowID = g_winid;
    SDL_PushEvent(&e);
}

static int pump(app_t *a, hal_event_t *out, int max)
{
    int n = 0;
    hal_event_t ev;
    while (n < max && hal_poll_event(&ev)) {
        app_handle_event(a, &ev);
        out[n++] = ev;
    }
    return n;
}

int main(void)
{
    if (!hal_init()) {
        printf("FAIL hal_init\n");
        return 1;
    }
    app_t app;
    app_init(&app);
    g_winid = hal_window_id();
    hal_event_t evs[8];

    push_button(SDL_MOUSEBUTTONDOWN, 320, 344);
    push_button(SDL_MOUSEBUTTONUP, 320, 344);
    int n = pump(&app, evs, 8);
    CHECK(n == 2);
    CHECK(evs[0].type == HAL_EV_TOUCH_DOWN);
    CHECK(evs[0].x == 160 && evs[0].y == 172);
    CHECK(evs[1].type == HAL_EV_TOUCH_UP);
    CHECK(evs[1].x == 160 && evs[1].y == 172);
    CHECK(app.screen == SCR_LAUNCHER);

    push_button(SDL_MOUSEBUTTONDOWN, 192, 236);
    push_button(SDL_MOUSEBUTTONUP, 192, 236);
    n = pump(&app, evs, 8);
    CHECK(n == 2);
    CHECK(evs[0].type == HAL_EV_TOUCH_DOWN);
    CHECK(evs[0].x == 96 && evs[0].y == 118);
    CHECK(evs[1].x == 96 && evs[1].y == 118);
    CHECK(app.screen == SCR_WATER);

    push_button(SDL_MOUSEBUTTONDOWN, 4, 4);
    n = pump(&app, evs, 8);
    CHECK(n == 1);
    CHECK(evs[0].x == 2 && evs[0].y == 2);
    push_button(SDL_MOUSEBUTTONUP, 478, 478);
    n = pump(&app, evs, 8);
    CHECK(n == 1);
    CHECK(evs[0].type == HAL_EV_TOUCH_UP);
    CHECK(evs[0].x == 239 && evs[0].y == 239);

    hal_shutdown();
    if (fails)
        return 1;
    printf("input mapping test passed\n");
    return 0;
}
