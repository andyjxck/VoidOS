#include <math.h>
#include <stdio.h>
#include <time.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>
#include "voidos/hal.h"

#define FACE_FONT_MAX_PX 64

static SDL_Window *g_window;
static SDL_Renderer *g_renderer;
static TTF_Font *g_fonts[FACE_FONT_MAX_PX + 1];
static SDL_Texture *g_logo;
static SDL_Texture *g_logo_shine;
static SDL_Texture *g_icons[HAL_ICON_COUNT];
static char *g_base_path;
static int g_mouse_down;
static int g_touch_x;
static int g_touch_y;
static int g_pending_valid;
static hal_event_t g_pending;

static void queue_swipe(int sx, int sy, int ex, int ey, hal_event_t *ev)
{
    ev->type = HAL_EV_TOUCH_DOWN;
    ev->x = sx;
    ev->y = sy;
    g_pending.type = HAL_EV_TOUCH_UP;
    g_pending.x = ex;
    g_pending.y = ey;
    g_pending_valid = 1;
}

static void to_logical(int px, int py, int *x, int *y)
{
    *x = px;
    *y = py;
    if (*x < 0) *x = 0;
    if (*x >= VOIDOS_WIDTH) *x = VOIDOS_WIDTH - 1;
    if (*y < 0) *y = 0;
    if (*y >= VOIDOS_HEIGHT) *y = VOIDOS_HEIGHT - 1;
}

int hal_init(void)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 0;
    }
    g_window = SDL_CreateWindow("VoidOS Simulator",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                VOIDOS_WIDTH * 2, VOIDOS_HEIGHT * 2, 0);
    if (!g_window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 0;
    }
    g_renderer = SDL_CreateRenderer(g_window, -1,
                                    SDL_RENDERER_ACCELERATED);
    if (!g_renderer)
        g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
    if (!g_renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_window);
        g_window = NULL;
        SDL_Quit();
        return 0;
    }
    SDL_RenderSetLogicalSize(g_renderer, VOIDOS_WIDTH, VOIDOS_HEIGHT);
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        hal_shutdown();
        return 0;
    }
    g_base_path = SDL_GetBasePath();
    if (!g_base_path) {
        fprintf(stderr, "SDL_GetBasePath failed: %s\n", SDL_GetError());
        hal_shutdown();
        return 0;
    }
    if (IMG_Init(IMG_INIT_PNG) == 0) {
        fprintf(stderr, "IMG_Init failed: %s\n", IMG_GetError());
        hal_shutdown();
        return 0;
    }
    return 1;
}

void hal_shutdown(void)
{
    if (g_logo) {
        SDL_DestroyTexture(g_logo);
        g_logo = NULL;
    }
    if (g_logo_shine) {
        SDL_DestroyTexture(g_logo_shine);
        g_logo_shine = NULL;
    }
    for (int i = 0; i < HAL_ICON_COUNT; i++) {
        if (g_icons[i]) {
            SDL_DestroyTexture(g_icons[i]);
            g_icons[i] = NULL;
        }
    }
    IMG_Quit();
    for (int p = 0; p <= FACE_FONT_MAX_PX; p++)
        if (g_fonts[p]) {
            TTF_CloseFont(g_fonts[p]);
            g_fonts[p] = NULL;
        }
    if (g_base_path) {
        SDL_free(g_base_path);
        g_base_path = NULL;
    }
    TTF_Quit();
    if (g_renderer) {
        SDL_DestroyRenderer(g_renderer);
        g_renderer = NULL;
    }
    if (g_window) {
        SDL_DestroyWindow(g_window);
        g_window = NULL;
    }
    SDL_Quit();
}

int hal_poll_event(hal_event_t *ev)
{
    SDL_Event e;
    if (g_pending_valid) {
        *ev = g_pending;
        g_pending_valid = 0;
        return 1;
    }
    ev->type = HAL_EV_NONE;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            ev->type = HAL_EV_QUIT;
            return 1;
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                g_mouse_down = 1;
                to_logical(e.button.x, e.button.y, &g_touch_x, &g_touch_y);
                ev->type = HAL_EV_TOUCH_DOWN;
                ev->x = g_touch_x;
                ev->y = g_touch_y;
                return 1;
            }
            break;
        case SDL_MOUSEMOTION:
            if (g_mouse_down) {
                to_logical(e.motion.x, e.motion.y, &g_touch_x, &g_touch_y);
                ev->type = HAL_EV_TOUCH_MOVE;
                ev->x = g_touch_x;
                ev->y = g_touch_y;
                return 1;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT && g_mouse_down) {
                g_mouse_down = 0;
                to_logical(e.button.x, e.button.y, &g_touch_x, &g_touch_y);
                ev->type = HAL_EV_TOUCH_UP;
                ev->x = g_touch_x;
                ev->y = g_touch_y;
                return 1;
            }
            break;
        case SDL_KEYDOWN:
            if (e.key.repeat)
                break;
            switch (e.key.keysym.sym) {
            case SDLK_ESCAPE:
                ev->type = HAL_EV_QUIT;
                return 1;
            case SDLK_b:
            case SDLK_SPACE:
                ev->type = HAL_EV_BUTTON;
                return 1;
            case SDLK_UP:
                queue_swipe(120, 190, 120, 80, ev);
                return 1;
            case SDLK_DOWN:
                queue_swipe(120, 60, 120, 180, ev);
                return 1;
            case SDLK_LEFT:
                queue_swipe(190, 120, 70, 120, ev);
                return 1;
            case SDLK_RIGHT:
                queue_swipe(60, 120, 190, 120, ev);
                return 1;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }
    return 0;
}

void hal_draw_rect(int x, int y, int w, int h, hal_color_t color)
{
    if (w <= 0 || h <= 0)
        return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > VOIDOS_WIDTH) w = VOIDOS_WIDTH - x;
    if (y + h > VOIDOS_HEIGHT) h = VOIDOS_HEIGHT - y;
    if (w <= 0 || h <= 0)
        return;
    SDL_SetRenderDrawColor(g_renderer,
                           (color >> 8) & 0xF8,
                           (color >> 3) & 0xFC,
                           (color << 3) & 0xF8, 255);
    SDL_Rect r = {x, y, w, h};
    SDL_RenderFillRect(g_renderer, &r);
}

void hal_draw_rect_a(int x, int y, int w, int h, hal_color_t color,
                     unsigned alpha)
{
    if (w <= 0 || h <= 0)
        return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > VOIDOS_WIDTH) w = VOIDOS_WIDTH - x;
    if (y + h > VOIDOS_HEIGHT) h = VOIDOS_HEIGHT - y;
    if (w <= 0 || h <= 0)
        return;
    if (alpha > 255)
        alpha = 255;
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_renderer,
                           (color >> 8) & 0xF8,
                           (color >> 3) & 0xFC,
                           (color << 3) & 0xF8, (Uint8)alpha);
    SDL_Rect r = {x, y, w, h};
    SDL_RenderFillRect(g_renderer, &r);
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
}

void hal_present(void)
{
    SDL_RenderPresent(g_renderer);
}

void hal_delay_ms(unsigned ms)
{
    SDL_Delay(ms);
}

uint32_t hal_window_id(void)
{
    return SDL_GetWindowID(g_window);
}

uint32_t hal_millis(void)
{
    return SDL_GetTicks();
}

int hal_clock_hour(void)
{
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    return tmv.tm_hour;
}

int hal_clock_minute(void)
{
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    return tmv.tm_min;
}

int hal_clock_second(void)
{
    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    return tmv.tm_sec;
}

int hal_clock_millis(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int)(ts.tv_nsec / 1000000L);
}

static TTF_Font *face_font(int px)
{
    if (px <= 0 || px > FACE_FONT_MAX_PX)
        return NULL;
    TTF_Font *f = g_fonts[px];
    if (!f) {
        char path[512];
        snprintf(path, sizeof path, "%s../assets/fonts/Kenney Future.ttf",
                 g_base_path);
        f = TTF_OpenFont(path, px);
        if (!f) {
            fprintf(stderr, "TTF_OpenFont failed for %s: %s\n",
                    path, TTF_GetError());
            return NULL;
        }
        g_fonts[px] = f;
    }
    return f;
}

void hal_face_text_size(const char *text, int px, int *w, int *h)
{
    TTF_Font *f = face_font(px);
    *w = 0;
    *h = 0;
    if (!f || TTF_SizeText(f, text, w, h) != 0) {
        *w = 0;
        *h = 0;
    }
}

void hal_face_draw_text(int x, int y, const char *text,
                        hal_color_t color, int px)
{
    TTF_Font *f = face_font(px);
    if (!f)
        return;
    SDL_Color c = {(uint8_t)((color >> 8) & 0xF8),
                   (uint8_t)((color >> 3) & 0xFC),
                   (uint8_t)((color << 3) & 0xF8), 255};
    SDL_Surface *s = TTF_RenderText_Blended(f, text, c);
    if (!s) {
        fprintf(stderr, "TTF_RenderText_Blended failed: %s\n", TTF_GetError());
        return;
    }
    SDL_Texture *t = SDL_CreateTextureFromSurface(g_renderer, s);
    SDL_FreeSurface(s);
    if (!t) {
        fprintf(stderr, "SDL_CreateTextureFromSurface failed: %s\n",
                SDL_GetError());
        return;
    }
    int w = 0, h = 0;
    SDL_QueryTexture(t, NULL, NULL, &w, &h);
    SDL_Rect dst = {x, y, w, h};
    SDL_RenderCopy(g_renderer, t, NULL, &dst);
    SDL_DestroyTexture(t);
}

static int logo_load(void)
{
    if (g_logo)
        return 1;
    char path[512];
    snprintf(path, sizeof path,
             "%s../assets/fonts/"
             "Gemini_Generated_Image_3ig0p63ig0p63ig0-removebg-preview.png",
             g_base_path);
    SDL_Surface *s = IMG_Load(path);
    if (!s) {
        fprintf(stderr, "IMG_Load failed for %s: %s\n", path, IMG_GetError());
        return 0;
    }
    g_logo = SDL_CreateTextureFromSurface(g_renderer, s);
    if (!g_logo) {
        fprintf(stderr, "logo texture: %s\n", SDL_GetError());
        SDL_FreeSurface(s);
        return 0;
    }
    SDL_SetTextureBlendMode(g_logo, SDL_BLENDMODE_BLEND);

    /* shine texture: the logo's alpha channel as a pure-white silhouette */
    SDL_Surface *sh = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(s);
    if (!sh)
        return 1;    /* logo still works, just no shimmer */
    Uint32 *px = sh->pixels;
    int n = sh->w * sh->h;
    for (int i = 0; i < n; i++)
        px[i] = (px[i] & 0xFF000000u) | 0x00FFFFFFu;
    g_logo_shine = SDL_CreateTextureFromSurface(g_renderer, sh);
    SDL_FreeSurface(sh);
    if (g_logo_shine)
        SDL_SetTextureBlendMode(g_logo_shine, SDL_BLENDMODE_BLEND);
    return 1;
}

/* shimmer_col: screen x of a diagonal light band's centre, or <0 for none.
   The band is clipped per column, and since the shine texture is a white
   silhouette of the logo, light only lands on the logo's actual shape. */
void hal_draw_logo(int x, int y, int w, int h, unsigned alpha,
                   int shimmer_col)
{
    if (!logo_load())
        return;
    if (alpha > 255)
        alpha = 255;
    SDL_Rect dst = {x, y, w, h};
    SDL_SetTextureAlphaMod(g_logo, (Uint8)alpha);
    SDL_RenderCopy(g_renderer, g_logo, NULL, &dst);

    if (shimmer_col < 0 || !g_logo_shine || alpha == 0)
        return;
    for (int cx = x; cx < x + w; cx++) {
        float d = fabsf((float)(cx - shimmer_col));
        if (d > 26.0f)
            continue;
        unsigned sa = (unsigned)((1.0f - d / 26.0f) * 120.0f *
                                 (float)alpha / 255.0f);
        int yc = y + h / 2 + (int)((float)(cx - shimmer_col) * 0.6f);
        SDL_Rect clip = {cx, yc - 14, 1, 28};
        SDL_RenderSetClipRect(g_renderer, &clip);
        SDL_SetTextureAlphaMod(g_logo_shine, (Uint8)sa);
        SDL_RenderCopy(g_renderer, g_logo_shine, NULL, &dst);
    }
    SDL_RenderSetClipRect(g_renderer, NULL);
}

/* icon textures, lazily loaded from the assets folder */
static const char *const g_icon_files[HAL_ICON_COUNT] = {
    "do_not_disturb_on_24dp_E3E3E3_FILL0_wght400_GRAD0_opsz24.png",
    "flight_24dp_E3E3E3_FILL0_wght400_GRAD0_opsz24.png",
    "bluetooth_24dp_E3E3E3_FILL0_wght400_GRAD0_opsz24.png",
    "flashlight_on_24dp_E3E3E3_FILL0_wght400_GRAD0_opsz24.png",
    "mobile_vibrate_24dp_E3E3E3_FILL0_wght400_GRAD0_opsz24.png",
    "water_drop_24dp_E3E3E3_FILL0_wght400_GRAD0_opsz24.png",
    "settings_24dp_E3E3E3_FILL0_wght400_GRAD0_opsz24.png",
    "light_mode_24dp_E3E3E3_FILL0_wght400_GRAD0_opsz24.png",
    "battery_full_24dp_E3E3E3_FILL0_wght400_GRAD0_opsz24.png",
    "do_not_disturb_off_24dp_E3E3E3_FILL0_wght400_GRAD0_opsz24.png",
    "airplanemode_inactive_24dp_E3E3E3_FILL0_wght400_GRAD0_opsz24.png",
    "bluetooth_disabled_24dp_E3E3E3_FILL0_wght400_GRAD0_opsz24.png",
    "flashlight_off_24dp_E3E3E3_FILL0_wght400_GRAD0_opsz24.png",
    "cloud.png",
};
static int g_icons_failed = 0;

void hal_draw_icon(hal_icon_t icon, int x, int y, int size, unsigned alpha)
{
    if (icon < 0 || icon >= HAL_ICON_COUNT)
        return;
    if (!g_icons[icon]) {
        if (g_icons_failed)
            return;
        char path[512];
        snprintf(path, sizeof path, "%s../assets/fonts/%s",
                 g_base_path, g_icon_files[icon]);
        SDL_Surface *s = IMG_Load(path);
        if (!s) {
            fprintf(stderr, "icon load failed: %s: %s\n",
                    path, IMG_GetError());
            g_icons_failed = 1;
            return;
        }
        g_icons[icon] = SDL_CreateTextureFromSurface(g_renderer, s);
        SDL_FreeSurface(s);
        if (!g_icons[icon]) {
            g_icons_failed = 1;
            return;
        }
        SDL_SetTextureBlendMode(g_icons[icon], SDL_BLENDMODE_BLEND);
    }
    if (alpha > 255)
        alpha = 255;
    SDL_Rect dst = {x, y, size, size};
    SDL_SetTextureAlphaMod(g_icons[icon], (Uint8)alpha);
    SDL_RenderCopy(g_renderer, g_icons[icon], NULL, &dst);
}

void hal_mark_dirty(int x, int y, int w, int h)
{
    (void)x; (void)y; (void)w; (void)h;
}

void hal_mark_all_dirty(void) {}
void hal_mark_screen(void) {}
int hal_render_static(void) { return 1; }

int hal_save_screenshot(const char *path)
{
    int ow = 0, oh = 0;
    if (SDL_GetRendererOutputSize(g_renderer, &ow, &oh) != 0 || ow <= 0 || oh <= 0) {
        fprintf(stderr, "SDL_GetRendererOutputSize failed: %s\n", SDL_GetError());
        return 0;
    }
    SDL_Surface *full = SDL_CreateRGBSurfaceWithFormat(
        0, ow, oh, 24, SDL_PIXELFORMAT_RGB24);
    if (!full) {
        fprintf(stderr, "screenshot surface failed: %s\n", SDL_GetError());
        return 0;
    }
    if (SDL_RenderReadPixels(g_renderer, NULL, SDL_PIXELFORMAT_RGB24,
                             full->pixels, full->pitch) != 0) {
        fprintf(stderr, "SDL_RenderReadPixels failed: %s\n", SDL_GetError());
        SDL_FreeSurface(full);
        return 0;
    }
    SDL_Surface *shot = SDL_CreateRGBSurfaceWithFormat(
        0, VOIDOS_WIDTH, VOIDOS_HEIGHT, 24, SDL_PIXELFORMAT_RGB24);
    if (!shot) {
        fprintf(stderr, "screenshot surface failed: %s\n", SDL_GetError());
        SDL_FreeSurface(full);
        return 0;
    }
    if (SDL_BlitScaled(full, NULL, shot, NULL) != 0) {
        fprintf(stderr, "SDL_BlitScaled failed: %s\n", SDL_GetError());
        SDL_FreeSurface(shot);
        SDL_FreeSurface(full);
        return 0;
    }
    SDL_FreeSurface(full);
    if (SDL_SaveBMP(shot, path) != 0) {
        fprintf(stderr, "SDL_SaveBMP failed: %s\n", SDL_GetError());
        SDL_FreeSurface(shot);
        return 0;
    }
    SDL_FreeSurface(shot);
    return 1;
}
