#!/usr/bin/env python3
"""中文：实测液态玻璃面板的模糊、菲涅尔亮边�?x50 亮度下限与快刷圆角回退�?English: Exercise the liquid-glass panel's blur, Fresnel rim, 0x50 lightness floor and fast-mode rounded fallback.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

W, H = 684, 1216

# �?ui_nav.c 读取玻璃下限，测试跟着这个常量走�? Take the glass floor from ui_nav.c so the test follows it.
NAV = (ROOT / 'main/ui/ui_nav.c').read_text()
FLOOR_MATCH = re.search(r'#define\s+UI_NAV_GLASS_FLOOR\s+(\d+)', NAV)
assert FLOOR_MATCH, 'UI_NAV_GLASS_FLOOR'
GLASS_FLOOR = int(FLOOR_MATCH.group(1))


def function(path, name):
    text = (ROOT / path).read_text()
    scan = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"',
                  lambda m: ' ' * len(m[0]), text)
    match = re.search(r'^(?:static )?[^\n]+\b' + name + r'\([^;{}]*?\)\s*\{', scan, re.M)
    assert match, name
    at, depth = match.end(), 1
    while depth:
        depth += (scan[at] == '{') - (scan[at] == '}')
        at += 1
    return text[match.start():at]


unit = r'''
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 684
#define H 1216
#define UI_LOCK_WIDTH W
#define UI_LOCK_HEIGHT H
#define UI_NAV_FROST_ALPHA 20
#define REFRACT_MAX 28
#define REFRACT_BEZEL 22
#define REFRACT_SAMPLES 128
#define REFRACT_N 1.5168f
#define REFRACT_T0 1.3f
#define GAUSS_RADIUS 3
#define GAUSS_TAPS (2 * GAUSS_RADIUS + 1)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define UI_GRAY_WHITE 0xff
#define UI_NAV_GLASS_FLOOR @GLASS_FLOOR@
#define GLASS_FLOOR UI_NAV_GLASS_FLOOR

typedef struct { int x, y, width, height; } EpdRect;
enum EpdRotation { EPD_ROT_LANDSCAPE, EPD_ROT_PORTRAIT, EPD_ROT_INVERTED_LANDSCAPE, EPD_ROT_INVERTED_PORTRAIT };

static const uint8_t s_gauss[GAUSS_TAPS] = {9, 28, 55, 72, 55, 28, 9};
static uint8_t fb[W * H / 2];
static bool s_fast;

/* ---- 面板状态与依赖替身 / Panel state and dependency stand-ins ---- */
static uint8_t s_disp[REFRACT_SAMPLES + 1];
static uint8_t s_spec[REFRACT_SAMPLES + 1];
static bool s_refract_ready;
static uint8_t *s_panel_blur;
static size_t s_panel_blur_cap;
static uint8_t s_panel_ring[GAUSS_TAPS][UI_LOCK_WIDTH];

static int epd_width(void) { return W; }
static int epd_height(void) { return H; }
static enum EpdRotation epd_get_rotation(void) { return EPD_ROT_LANDSCAPE; }
static uint8_t epd_get_pixel(int x, int y, int w, int h, const uint8_t *p) {
    assert(w == W && h == H && x >= 0 && y >= 0 && x < W && y < H);
    unsigned offset = (unsigned)y * W + x;
    return ((p[offset / 2] >> ((offset & 1) * 4)) & 15) << 4;
}
static void epd_draw_pixel(int x, int y, uint8_t v, uint8_t *p) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    unsigned offset = (unsigned)y * W + x;
    p[offset / 2] = (uint8_t)((p[offset / 2] & ~(15u << ((offset & 1) * 4))) |
                              ((v >> 4) << ((offset & 1) * 4)));
}
static void epd_draw_vline(int x, int y, int h, uint8_t v, uint8_t *p) {
    for (int j = 0; j < h; ++j) epd_draw_pixel(x, y + j, v, p);
}
static void ui_fill_rect_fast(EpdRect r, uint8_t v, uint8_t *p) {
    for (int y = r.y; y < r.y + r.height; ++y)
        for (int x = r.x; x < r.x + r.width; ++x) epd_draw_pixel(x, y, v, p);
}
static void *heap_caps_malloc(size_t bytes, int caps) { (void)caps; return malloc(bytes); }
static void heap_caps_free(void *p) { free(p); }
static bool app_settings_main_fast_refresh(void) { return s_fast; }
static int app_settings_system_contrast(void) { return 100; }
'''

for name in ('epd_fill_circle_helper',):
    unit += function('components/epdiy/src/epdiy.c', name)
for name in ('ui_read_pixel', 'ui_contrast_gray', 'clamp_radius', 'ui_fill_round_rect'):
    unit += function('main/ui/ui_kit.c', name)
unit += function('main/ui/ui_image_dither.c', 'ui_image_dither_gray')
for name in ('refract_prepare', 'refract_sample', 'panel_row_h', 'panel_sdf', 'ui_nav_glass_panel'):
    unit += function('main/ui/ui_nav.c', name)

unit += r'''
/* ---- 工具 / Helpers ---- */
static void fill_page(uint8_t tone) { memset(fb, tone, sizeof(fb)); }
static int px(int x, int y) { return epd_get_pixel(x, y, W, H, fb) >> 4; }
static void range_in(const EpdRect r, int *minv, int *maxv) {
    *minv = 15;
    *maxv = 0;
    for (int y = r.y; y < r.y + r.height; ++y)
        for (int x = r.x; x < r.x + r.width; ++x) {
            const int v = px(x, y);
            if (v < *minv) *minv = v;
            if (v > *maxv) *maxv = v;
        }
}
static int max_step_in(const EpdRect r) {
    int step = 0;
    for (int y = r.y; y < r.y + r.height; ++y)
        for (int x = r.x; x + 1 < r.x + r.width; ++x) {
            const int d = abs(px(x, y) - px(x + 1, y));
            if (d > step) step = d;
        }
    return step;
}

int main(void) {
    const EpdRect panel = {40, 400, 600, 200};
    /* 面板内部取样区，避开圆角外的页面�? Sampling region well inside the rounded shape. */
    const EpdRect inside = {70, 430, 540, 140};
    int lo, hi;

    // 1) 白底：玻璃把白色压到 ~0xcc，菲涅尔亮边只出现在边框带里�?    // White page: glass pulls white down to ~0xcc and the Fresnel rim only brightens the bezel band.
    fill_page(0xff);
    s_fast = false;
    assert(ui_nav_glass_panel(fb, panel, 30, 20, 100));
    range_in(inside, &lo, &hi);
    assert(lo == 12 && hi == 12);
    int rim = 0;
    for (int x = 70; x < 610; ++x) if (px(x, 402) > 12) ++rim;
    assert(rim > 0);

    // 2) 黑底 + 20% 白层：中央正好停在下限，亮边仍在�?    // Black page with a 20% white layer: the centre lands exactly on the floor and the bezel lifts.
    fill_page(0x00);
    assert(ui_nav_glass_panel(fb, panel, 30, 20, 100));
    range_in(inside, &lo, &hi);
    assert(lo == GLASS_FLOOR && hi == GLASS_FLOOR);
    int dark_rim = 0;
    for (int x = 70; x < 610; ++x) if (px(x, 402) > GLASS_FLOOR) ++dark_rim;
    assert(dark_rim > 0);
    int centre_lo = 15;
    for (int y = 480; y < 560; ++y)
        for (int x = 200; x < 480; ++x) {
            const int v = px(x, y);
            if (v < centre_lo) centre_lo = v;
        }
    assert(centre_lo >= 5);

    // 3) 黑底 + 55% 白层（电源组件用的白层）：中央被白层提亮�?0x80 以上�?    // Black page with the power components' 55% white layer: lifted above 0x80.
    fill_page(0x00);
    assert(ui_nav_glass_panel(fb, panel, 30, 55, 100));
    range_in(inside, &lo, &hi);
    assert(lo >= 8);

    // 4) 模糊�? 像素黑白条纹进入玻璃后，相邻像素的最大跳变明显小于原图的 15 级�?    // Blur: 8-px stripes lose most of their contrast inside the glass (raw jump would be 15 levels).
    fill_page(0x00);
    for (int y = 380; y < 640; ++y)
        for (int x = 30; x < 650; ++x)
            epd_draw_pixel(x, y, ((x / 8) & 1) ? 0xf0 : 0x00, fb);
    const int stripe_before = max_step_in(inside);
    assert(stripe_before == 15);
    assert(ui_nav_glass_panel(fb, panel, 30, 20, 100));
    const int stripe_after = max_step_in(inside);
    assert(stripe_after <= 10);

    // 5) 快刷模式：退化圆角白底，圆角外保持页面原样�?    // Fast mode: rounded white fill only, corners outside the radius keep the page.
    fill_page(0x00);
    s_fast = true;
    assert(ui_nav_glass_panel(fb, panel, 30, 20, 100));
    assert(px(panel.x + panel.width / 2, panel.y + panel.height / 2) == 15);
    assert(px(panel.x + 1, panel.y + 1) == 0);
    s_fast = false;

    printf("PASS: glass blur/rim/floor(level %d ~0x%02x)/fast-mode rounding: white centre=12 rim>12, "
           "black centre=%d (floor), 55%% white>=8, stripe step %d->%d, fast corner page kept\n",
           GLASS_FLOOR, GLASS_FLOOR * 0x11, GLASS_FLOOR, stripe_before, stripe_after);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='kiikoread-glass-', dir=str(ROOT / 'build')) as folder:
    c, binary = Path(folder) / 'test.c', Path(folder) / 'test'
    c.write_text(unit.replace('@GLASS_FLOOR@', str(GLASS_FLOOR)))
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-g',
                    '-fsanitize=address,undefined', str(c), '-o', str(binary), '-lm'], check=True)
    subprocess.run([str(binary)], check=True)
