#ifndef VOIDOS_FW_ASSETS_H
#define VOIDOS_FW_ASSETS_H

#include <stdint.h>

/* Kenney Future baked at fixed pixel sizes: 4-bit alpha glyph bitmaps */
typedef struct {
    int16_t adv;      /* horizontal advance */
    int16_t x0, y0;   /* bitmap offset from pen position (top-left origin) */
    int16_t w, h;     /* bitmap size */
    uint32_t off;     /* byte offset into the font's bitmap pool */
} glyph_t;

typedef struct {
    int px;           /* nominal pixel size the app asks for */
    int height;       /* line height (ascent + descent) */
    const glyph_t *glyphs;   /* ASCII 32..126 */
    const uint8_t *bits;
} font_t;

#define FONT_FIRST 32
#define FONT_LAST 126

extern const font_t fonts[];
extern const int font_count;

extern const uint8_t icon_alpha[][24 * 24];

#define LOGO_PX 112
extern const uint16_t logo_rgb[LOGO_PX * LOGO_PX];
extern const uint8_t logo_alpha[LOGO_PX * LOGO_PX];
/* contiguous [rgb565][alpha] for LV_COLOR_FORMAT_RGB565A8 */
extern const uint8_t logo_rgba[LOGO_PX * LOGO_PX * 3];

#endif
