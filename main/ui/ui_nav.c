/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：绘制与命中设计稿的三格胶囊底栏；细线图标分别表示主页、书库和设置，图形来自 Lucide。
 * English: Draw and hit-test the design's three-cell pill dock with thin-line home, library and settings icons from Lucide.
 * 用户修订：长按或直接横向拖动底栏选中胶囊都会放大到比底栏略高并跟手移动，胶囊只靠白底区分、不描边，抬手切到胶囊下的页面。
 * User revision: holding or directly dragging the active dock capsule grows it slightly taller than the bar and follows the finger; the capsule separates by a white fill without an outline, and release switches to the page under it.
 * 用户修订：底栏改成白底上浮起的一枚浅灰胶囊，选中格叠一层更深的胶囊；不再画通栏底色、分隔线和横条。
 * User revision: the dock is now a light-gray pill floating on white with a darker pill on the selected cell; the full-width bar, rule and marker are gone.
 * 用户增加点击反馈：共享返回键和底栏图标登记局部命中，主循环只在按下和复位推该控件。
 * User adds click feedback: register local hits for shared backs and navigation icons; the loop presents only that control on press and restoration.
 * 用户修订：所有模式把内容扫描截止到分隔线之间的8行固定为白色留白；滚动不能把列表画进这条扫描保护带。
 * User revision: every mode keeps the eight rows between content scans and the divider blank white; scrolling must not paint list content into this guard.
 * 用户修订：状态栏签名按系统字体和内建界面字体的共同安全宽度截断，阅读页复用小型字宽表，不重载系统字体。
 * User revision: fit status signatures to widths safe for the system and built-in UI faces; reading reuses a small metrics table without reloading the system font.
 */
#include "ui_nav.h"
#include <math.h>
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "app_registry.h"
#include "read_pico_pmu.h"
#include "read_pico_pmu_protocol.h"
#include "read_pico_transfer.h"
#include "ui_kit.h"
#include "ui_image_dither.h"
#include "settings.h"
#include "ui_click_feedback.h"
#include "ttf_font.h"
#include "ui_font.h"

// 底栏图标盒、返回键箭头、充电闪电的边长。
// Box sizes for the bar icons, the back chevron and the charging bolt.
#define UI_NAV_BACK_ICON_PX 38
#define UI_NAV_BOLT_ICON_PX 26
#define UI_NAV_NETWORK_ICON_PX 28
#define UI_STATUS_TIME_PX 28
#define UI_STATUS_PERCENT_PX 28
#define UI_STATUS_SIGNATURE_PX 26

static bool s_status_reading;
static struct {
    bool ready;
    char signature[96], face[TTF_FONT_PATH_MAX], chosen[TTF_FONT_PATH_MAX];
    uint16_t widths[96], digits[10], colon, percent, dash;
} s_status_metrics;
_Static_assert(UI_STATUS_TIME_PX == UI_STATUS_PERCENT_PX, "status numbers share one native metrics cache");
_Static_assert(sizeof(s_status_metrics) <= 1024, "status metrics never retain a second font or bitmap");

static int status_safe_width(int px, const char *text, const char *sample) {
    int active = ui_text_fixed_context_width_px(px, text, sample);
    int builtin = ui_font_has_text(text) ? ui_font_text_width_px(px, text) : 0;
    return active > builtin ? active : builtin;
}

static void status_prepare_metrics(void) {
    if (s_status_reading) return;
    const char *signature = app_settings_status_signature();
    const char *face = ttf_font_path(), *chosen = app_settings_system_font_path();
    if (s_status_metrics.ready && !strcmp(signature, s_status_metrics.signature) &&
        !strcmp(face, s_status_metrics.face) && !strcmp(chosen, s_status_metrics.chosen)) return;
    memset(&s_status_metrics, 0, sizeof(s_status_metrics));
    snprintf(s_status_metrics.signature, sizeof(s_status_metrics.signature), "%s", signature);
    snprintf(s_status_metrics.face, sizeof(s_status_metrics.face), "%s", face);
    snprintf(s_status_metrics.chosen, sizeof(s_status_metrics.chosen), "%s", chosen);
    char prefix[96] = {0};
    const size_t length = strlen(s_status_metrics.signature);
    for (size_t n = 1; n <= length; ++n) {
        prefix[n - 1] = signature[n - 1];
        if (n < length && ((unsigned char)signature[n] & 0xc0) == 0x80) continue;
        s_status_metrics.widths[n] = status_safe_width(UI_STATUS_SIGNATURE_PX, prefix, signature);
    }
    for (int digit = 0; digit < 10; ++digit) {
        char text[] = {(char)('0' + digit), 0};
        s_status_metrics.digits[digit] = status_safe_width(UI_STATUS_TIME_PX, text, "0123456789:%-");
    }
    s_status_metrics.colon = status_safe_width(UI_STATUS_TIME_PX, ":", "0123456789:%-");
    s_status_metrics.percent = status_safe_width(UI_STATUS_PERCENT_PX, "%", "0123456789:%-");
    s_status_metrics.dash = status_safe_width(UI_STATUS_PERCENT_PX, "-", "0123456789:%-");
    s_status_metrics.ready = true;
}

void ui_nav_status_font_context(bool reading, bool face_changed) {
    s_status_reading = reading;
    if (!reading && face_changed) s_status_metrics.ready = false;
    status_prepare_metrics();
}

static int status_number_width(const char *text) {
    if (!s_status_metrics.ready) return ui_text_fixed_width_px(UI_STATUS_TIME_PX, text);
    int width = 0;
    for (; *text; ++text) width += *text >= '0' && *text <= '9' ? s_status_metrics.digits[*text - '0']
        : *text == ':' ? s_status_metrics.colon : *text == '%' ? s_status_metrics.percent : s_status_metrics.dash;
    return width;
}

static void status_fit_signature(char *text, int width) {
    bool cached = s_status_metrics.ready && !strcmp(text, s_status_metrics.signature);
    size_t n = strlen(text);
    while (n && (cached ? s_status_metrics.widths[n]
                       : ui_text_fixed_context_width_px(UI_STATUS_SIGNATURE_PX, text, app_settings_status_signature())) > width) {
        --n;
        while (n && ((unsigned char)text[n] & 0xc0) == 0x80) --n;
        text[n] = 0;
    }
}

static const char *const labels[UI_NAV_TABS] = {"主页", "书库", "设置"};

void ui_nav_back(uint8_t *fb, int x, int y) {
    ui_click_feedback_register(fb, (EpdRect){x - 10, y + 3, 64, 64}, UI_CLICK_BACK_SMALL, UI_ICON_CHEVRON_LEFT);
    // 参考系统题头缩小返回键，中心仍保持在 y=113。/ Match the smaller reference back control while retaining the title centerline.
    ui_fill_round_rect(fb, (EpdRect){x, y + 13, 44, 44}, 22, UI_GRAY_WHITE);
    ui_draw_round_rect(fb, (EpdRect){x, y + 13, 44, 44}, 22, UI_NAV_BACK_BORDER_GRAY);
    ui_draw_round_rect(fb, (EpdRect){x + 1, y + 14, 42, 42}, 21, UI_NAV_BACK_BORDER_GRAY);
    // Lucide chevron-left 的笔画占画布 14/24 高，取 38 像素复现原来 22 像素的视觉高度。
    // Lucide chevron-left spans 14/24 of its canvas, so 38 px reproduces the old 22 px visual height.
    ui_draw_icon(fb, x + 22, y + 35, UI_NAV_BACK_ICON_PX, UI_ICON_CHEVRON_LEFT, UI_GRAY_BLACK);
}

void ui_nav_wifi_icon(uint8_t *fb, int cx, int cy, int size, uint8_t gray) {
    if (!fb || size < 16 || size > 40) return;
    ui_draw_icon(fb, cx, cy, size, UI_ICON_WIFI, gray);
}

// 胶囊形状判定，圆角取四角圆弧；内部一律算在胶囊里。
// Pill shape test: the four corner arcs, everything else counts as inside.
static bool dock_inside(int x, int y) {
    const int r = UI_NAV_DOCK_RADIUS;
    const int dx = x < r ? r - x : x >= UI_NAV_DOCK_WIDTH - r ? x - (UI_NAV_DOCK_WIDTH - r - 1) : 0;
    const int dy = y < r ? r - y : y >= UI_NAV_DOCK_HEIGHT - r ? y - (UI_NAV_DOCK_HEIGHT - r - 1) : 0;
    return dx * dx + dy * dy <= r * r;
}

// 选中格的加深胶囊：格内再上下各收 6px、左右各收 3px，半径取半高得到跑道形。
// The darker pill on the selected cell: 6 px inset top and bottom, 3 px left and right, with a
// half-height radius for the stadium shape.
static bool active_pill_inside(int x, int y, int active) {
    if (active < 0 || active >= UI_NAV_TABS) return false;
    const int left = ui_nav_cell_left(active) - UI_NAV_DOCK_LEFT + UI_NAV_CELL_PILL_INSET;
    const int right = ui_nav_cell_right(active) - UI_NAV_DOCK_LEFT - UI_NAV_CELL_PILL_INSET;
    const int top = UI_NAV_CELL_TOP - UI_NAV_TOP, bottom = top + UI_NAV_CELL_HEIGHT;
    if (x < left || x >= right || y < top || y >= bottom) return false;
    const int r = UI_NAV_CELL_HEIGHT / 2;
    const int dx = x < left + r ? left + r - x : x >= right - r ? x - (right - r - 1) : 0;
    const int dy = y < top + r ? top + r - y : y >= bottom - r ? y - (bottom - r - 1) : 0;
    return dx * dx + dy * dy <= r * r;
}

// ---------------------------------------------------------------------------
// 液态玻璃折射 / Liquid-glass refraction
//
// 苹果的做法：光线穿过玻璃边框时按斯涅尔定律偏折，位移方向垂直于边框，位移量由边框的高度剖面
// 决定——法线就是高度函数的导数。位移量只跟「到边框的距离」有关，所以沿边框算一次就能复用。
// 这里照搬同一套：凸圆角（squircle）剖面 h = (1-(1-x)^4)^(1/4)，就是苹果偏爱的那个，
// 因为它从平面过渡到曲面的梯度更平滑，拉成胶囊形也不会出现生硬的内部棱边。
//
// Apple's approach: rays bend at the bezel following Snell's law, the displacement points along the
// border normal and its magnitude comes from the bezel height profile, whose derivative is the normal.
// Magnitude depends only on the distance from the border, so one radius is computed once and reused
// around the shape. Same here, with the convex squircle profile h = (1-(1-x)^4)^(1/4) Apple favours:
// its flat-to-curve gradient stays smooth even when the shape is stretched into a stadium.
// ---------------------------------------------------------------------------
// 玻璃浅色下限：玻璃再暗也不低于这一级灰（2 ≈ 0x20），深色内容上黑字和图标仍然可读。
// 用户 2026-10-17 要求把下限定在 0x20。
// Glass lightness floor: never darker than this gray level (2 ≈ 0x20), so black labels and icons
// stay readable on dark content. Per the user's 2026-10-17 request the floor is 0x20.
#define UI_NAV_GLASS_FLOOR 2
#define REFRACT_MAX 28      // 位移上限（夹住用，也是查表的量化上限）/ shift clamp and table quantisation ceiling
#define REFRACT_BEZEL 22    // 边框宽度，决定折射的影响范围 / bezel width, which sets how far the refraction reaches
#define REFRACT_SAMPLES 128
// BK7 冕牌玻璃 d 线折射率，与上游开源模型同值。T0 是边框下平面厚度，以边框宽度为单位：
// BK7 crown-glass d-line index, the value the upstream open-source model uses. T0 is the flat base
// thickness under the bezel, in units of the bezel width.
#define REFRACT_N 1.5168f
#define REFRACT_T0 1.3f   // 上游是 0.35；调大等于把玻璃加厚，位移同步变大（现在是 3.7 倍） / upstream uses 0.35; a larger value is thicker glass and a larger shift
// 高斯核 σ=1.5、半径 3，归一化到 256。设计稿的 backdrop-filter 是 blur(18px)，换算到这块屏上
// 中央要糊到看不清内容才算毛玻璃，所以核必须够宽——之前半径 3、σ≈1.5 只有设计稿的一半力度。
// Gaussian kernel, sigma 1.5 and radius 3, normalised to 256. The mock-up asks for blur(18px); scaled to
// this panel the centre has to be unreadable to read as frosted, so the kernel must be wide. An earlier
// radius-3 kernel at sigma 1.5 was only half the strength the design calls for.
// 两个一维卷积就等于二维高斯，所以窗口里每行先做一次横向，取值时再做纵向。
// Two 1-D convolutions give the full 2-D Gaussian, so each window row is blurred horizontally once and
// sampling does the vertical pass.
#define GAUSS_RADIUS 3
#define GAUSS_TAPS (2 * GAUSS_RADIUS + 1)
static const uint8_t s_gauss[GAUSS_TAPS] = {9, 28, 55, 72, 55, 28, 9};
// 窗口要同时容下最大位移和高斯半径：取样点最远偏 REFRACT_MAX，模糊还要在它周围再取 GAUSS_RADIUS。
// The window holds both the largest shift and the blur radius: the sample point moves up to REFRACT_MAX
// and the blur reaches GAUSS_RADIUS further.
#define REFRACT_WINDOW_R (REFRACT_MAX + GAUSS_RADIUS)
#define REFRACT_SPAN (2 * REFRACT_WINDOW_R + 1)

static uint8_t s_disp[REFRACT_SAMPLES + 1];
static uint8_t s_spec[REFRACT_SAMPLES + 1];
static bool s_refract_ready;
// 整块胶囊的高斯模糊结果。第一步先把它算完，第二步的折射只从这里取值，
// 所以中央在结构上不可能被位移——折射代码根本不在那条路径上。
// The Gaussian blur of the whole pill. Step one fills it, and step two's refraction samples only from
// here, so the centre cannot be displaced by construction: the refraction code is not on that path.
static uint8_t *s_blur;
// 纵向高斯要的 2*GAUSS_RADIUS+1 行横向结果，环形缓冲。
// The 2*GAUSS_RADIUS+1 horizontally-blurred rows the vertical pass needs, in a ring buffer.
static uint8_t s_blur_rowh[GAUSS_TAPS][UI_NAV_DOCK_WIDTH];

/// 读源图一行并做横向高斯；横向的源是页面原图，所以可以直接随机读取。
/// Read one source row and blur it horizontally; the source is the untouched page, so random access is fine.
static void blur_row_load(const uint8_t *fb, int row, uint8_t *dst) {
    for (int x = 0; x < UI_NAV_DOCK_WIDTH; ++x) {
        int sum = 0;
        for (int k = 0; k < GAUSS_TAPS; ++k) {
            int sx = x + k - GAUSS_RADIUS;
            if (sx < 0) sx = 0; else if (sx >= UI_NAV_DOCK_WIDTH) sx = UI_NAV_DOCK_WIDTH - 1;
            sum += s_gauss[k] * ui_read_pixel(fb, UI_NAV_DOCK_LEFT + sx, UI_NAV_TOP + row);
        }
        dst[x] = (uint8_t)(sum >> 8);
    }
}


/// 从 (left, top+row) 读一行并做横向高斯，写进 dst[0..w)。
/// Read one row starting at (left, top+row) and blur it horizontally into dst[0..w).
static void blur_row_load_at(const uint8_t *fb, int left, int top, int w, int row, uint8_t *dst) {
    for (int x = 0; x < w; ++x) {
        int sum = 0;
        for (int k = 0; k < GAUSS_TAPS; ++k) {
            int sx = x + k - GAUSS_RADIUS;
            if (sx < 0) sx = 0; else if (sx >= w) sx = w - 1;
            sum += s_gauss[k] * ui_read_pixel(fb, left + sx, top + row);
        }
        dst[x] = (uint8_t)(sum >> 8);
    }
}
/// 按上游开源模型逐点做光线追迹，得到位移与镜面高光，沿整个边框复用一次结果。
/// 位移量的单位是边框宽度的倍数，最后乘 REFRACT_BEZEL 换成像素。
/// Ray-trace the bezel once with the upstream open-source model to get displacement and specular
/// highlight, reused around the whole shape. Displacement is in bezel widths and is scaled to pixels
/// by REFRACT_BEZEL.
static void refract_prepare(void) {
    if (s_refract_ready) return;
    s_refract_ready = true;
    // 剖面与斜率：f(x) = (1-(1-x)^4)^(1/4)，x=0 在外沿、x=1 在平面起点。
    // Profile and slope: f(x) = (1-(1-x)^4)^(1/4), x=0 at the outer edge and x=1 where the flat starts.
    for (int i = 0; i <= REFRACT_SAMPLES; ++i) {
        const float x = (float)i / REFRACT_SAMPLES;
        const float u = 1.0f - x;
        const float h4 = 1.0f - u * u * u * u;
        const float f = h4 > 0.0f ? powf(h4, 0.25f) : 0.0f;
        // 解析导数 u³/f³ 在外沿发散，夹住即可：那里几乎不透过，位移随后被菲涅尔压掉。
        // The analytic derivative diverges at the rim; clamping is enough because almost nothing is
        // transmitted there and Fresnel fades the shift out anyway.
        const float slope = f > 0.02f ? (u * u * u) / (f * f * f) : 14.0f;
        const float theta_s = atanf(slope);
        const float theta_r = asinf(sinf(theta_s) / REFRACT_N);
        // 厚度向内变厚，位移是「厚度 × 偏折角」的乘积，所以峰值在外沿附近而不是最外一条线。
        // The glass thickens inward and the shift is thickness times deviation, which puts the peak
        // near the rim rather than on the outermost line.
        float d = (REFRACT_T0 + f) * tanf(theta_s - theta_r);
        // 菲涅尔：掠射的外沿几乎全反射，位移按透过率淡出，反射率本身留着做高光。
        // Fresnel: the grazing rim barely transmits, so fade the shift by transmittance, and keep the
        // reflectance itself to shape the bright rim.
        const float ci = cosf(theta_s);
        const float st = sinf(theta_s) / REFRACT_N;
        float refl = 1.0f;
        if (st < 1.0f) {
            const float ct = sqrtf(1.0f - st * st);
            const float rs = (ci - REFRACT_N * ct) / (ci + REFRACT_N * ct);
            const float rp = (REFRACT_N * ci - ct) / (REFRACT_N * ci + ct);
            refl = 0.5f * (rs * rs + rp * rp);
        }
        d *= 1.0f - refl;
        float px = d * (float)REFRACT_BEZEL;
        if (px < 0.0f) px = 0.0f;
        if (px > (float)REFRACT_MAX) px = (float)REFRACT_MAX;
        s_disp[i] = (uint8_t)(px + 0.5f);
        s_spec[i] = (uint8_t)(refl * 255.0f + 0.5f);
    }
    // 反射率在最外一像素上冲得极高，直接当高光就是一圈硬描边。做一次平滑摊到几像素上，
    // 才像弧形玻璃边缘那圈柔和的亮边。
    // Reflectance spikes on the outermost pixel; used raw it reads as a hard outline. One smoothing pass
    // spreads it over a few pixels, matching the soft rim of curved glass.
    static uint8_t smoothed[REFRACT_SAMPLES + 1];
    for (int i = 0; i <= REFRACT_SAMPLES; ++i) {
        int acc = 0, n = 0;
        for (int k = -16; k <= 16; ++k) {
            const int j = i + k;
            if (j < 0 || j > REFRACT_SAMPLES) continue;
            acc += s_spec[j];
            ++n;
        }
        // 再压到七成：物理反射率是按镜面算的，屏幕上不需要那么亮。
        // Scale to 70%: the physical value assumes a mirror finish, which a screen does not need.
        smoothed[i] = (uint8_t)(acc / n * 7 / 10);
    }
    memcpy(s_spec, smoothed, sizeof(s_spec));
}

/// 胶囊的带符号距离：内部为负，值就是到边框的距离；梯度指向外。
/// Signed distance to the pill: negative inside and equal to the distance from the border; the gradient points out.
static float dock_sdf(float x, float y) {
    const float cx = UI_NAV_DOCK_WIDTH * 0.5f, cy = UI_NAV_DOCK_HEIGHT * 0.5f;
    const float r = UI_NAV_DOCK_RADIUS;
    const float qx = fabsf(x - cx) - (cx - r);
    const float qy = fabsf(y - cy) - (cy - r);
    const float mx = qx > 0.0f ? qx : 0.0f, my = qy > 0.0f ? qy : 0.0f;
    const float inner = qx > qy ? qx : qy;
    return sqrtf(mx * mx + my * my) + (inner < 0.0f ? inner : 0.0f) - r;
}

/// 线性插值取表，避免 16 级灰上出现色带（上游同样用线性插值）。
/// Linear interpolation into the table, matching upstream, so the 16 levels do not band.
static float refract_sample(const uint8_t *table, float x) {
    if (x <= 0.0f) return (float)table[0];
    if (x >= 1.0f) return (float)table[REFRACT_SAMPLES];
    const float at = x * REFRACT_SAMPLES;
    const int i = (int)at;
    const int j = i < REFRACT_SAMPLES ? i + 1 : REFRACT_SAMPLES;
    const float frac = at - (float)i;
    return (float)table[i] * (1.0f - frac) + (float)table[j] * frac;
}

// 半透明液态玻璃胶囊，分两步，中央与边缘互不干扰：
//   第一步 整块胶囊做纯高斯模糊，结果留在 s_blur；源是页面原图，fb 此时只读。
//   第二步 只有边框带（到边框距离 < REFRACT_BEZEL）按位移从 s_blur 里取样，中央直接取模糊值。
// 位移为 0 处取到的就是同一点的模糊值，所以边框内沿不会留下接缝。
// The translucent liquid-glass pill, in two steps that cannot interfere:
//   1. Blur the whole pill with a pure Gaussian into s_blur; the source is the untouched page.
//   2. Only the bezel band (distance from the border < REFRACT_BEZEL) samples s_blur at a displaced
//      point; the centre takes the blurred value directly. Where the shift is zero the two agree, so the
//      inner bezel edge leaves no seam.
static void frost_dock(uint8_t *fb, int active) {
    const int w = UI_NAV_DOCK_WIDTH, h = UI_NAV_DOCK_HEIGHT;
    // 快刷是黑白主页：不铺半透明底，只在选中格压一块实心黑，和原来固定底栏的黑横条同一种语言，
    // 也绝不会把胶囊后面的内容盖成一片白。
    // Fast mode is a black/white main page: lay no translucent fill and ink only the selected cell
    // solid, the same language as the old fixed bar's black marker, and never white out what is behind.
    if (app_settings_main_fast_refresh()) {
        // 快刷是黑白页面，半透明没有意义：胶囊先铺成不透明白底，内容不会透过它露出来，
        // 再描出胶囊和选中格的轮廓。判断办法是「自己在里面、四邻有一个在外面」。
        // Fast mode is black and white, where translucency means nothing: the capsule is filled an
        // opaque white so nothing shows through, then the capsule and the active cell are stroked.
        // A pixel is on the border when it is inside and one of its four neighbours is not.
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (dock_inside(x, y))
                    epd_draw_pixel(UI_NAV_DOCK_LEFT + x, UI_NAV_TOP + y, UI_GRAY_WHITE, fb);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                if (!dock_inside(x, y)) continue;
                if (dock_inside(x - 1, y) && dock_inside(x + 1, y) &&
                    dock_inside(x, y - 1) && dock_inside(x, y + 1)) continue;
                epd_draw_pixel(UI_NAV_DOCK_LEFT + x, UI_NAV_TOP + y, 0, fb);
            }
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                if (!active_pill_inside(x, y, active)) continue;
                if (active_pill_inside(x - 1, y, active) && active_pill_inside(x + 1, y, active) &&
                    active_pill_inside(x, y - 1, active) && active_pill_inside(x, y + 1, active)) continue;
                epd_draw_pixel(UI_NAV_DOCK_LEFT + x, UI_NAV_TOP + y, 0, fb);
            }
        return;
    }
    if (!s_blur) s_blur = heap_caps_malloc((size_t)w * h, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    // 分配不到就整块不画：宁可让底栏后面保持页面原样，也不要画出半块玻璃。
    // Without the buffer, draw nothing: better to leave the page showing through than half a glass.
    if (!s_blur) return;
    refract_prepare();

    // 第一步：纯高斯模糊整块胶囊。横向在进窗口时做，纵向在写 s_blur 时做。
    // Step 1: blur the whole pill. The horizontal pass runs when a row enters the window, the vertical
    // pass when the row is written into s_blur.
    for (int k = 0; k < GAUSS_TAPS; ++k) {
        int row = k - GAUSS_RADIUS;
        if (row < 0) row = 0; else if (row >= h) row = h - 1;
        blur_row_load(fb, row, s_blur_rowh[k]);
    }
    int base = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!dock_inside(x, y)) continue;
            int sum = 0;
            for (int k = 0; k < GAUSS_TAPS; ++k) sum += s_gauss[k] * s_blur_rowh[(base + k) % GAUSS_TAPS][x];
            s_blur[(size_t)y * w + x] = (uint8_t)(sum >> 8);
        }
        int next = y + GAUSS_RADIUS + 1;
        if (next >= h) next = h - 1;
        blur_row_load(fb, next, s_blur_rowh[base]);
        base = (base + 1) % GAUSS_TAPS;
    }

    // 第二步：中央取模糊值，只有边框带折射。
    // Step 2: the centre takes the blurred value; only the bezel band refracts.
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!dock_inside(x, y)) continue;
            int value = s_blur[(size_t)y * w + x];
            int spec = 0;
            const float t = -dock_sdf((float)x + 0.5f, (float)y + 0.5f) / (float)REFRACT_BEZEL;
            if (t > 0.0f && t < 1.0f) {
                // 位移与高光都线性插值取表：最近邻会在 16 级灰上留下色带。
                // Displacement and highlight are linearly interpolated: nearest-neighbour bands on 16 levels.
                const float disp = refract_sample(s_disp, t);
                spec = (int)(refract_sample(s_spec, t) + 0.5f);
                if (disp > 0.5f) {
                    // 梯度步长取 0.75，与上游 sdfGradient 一致。
                    // Gradient epsilon 0.75, matching upstream's sdfGradient.
                    const float e = 0.75f;
                    float gx = dock_sdf((float)x + e + 0.5f, (float)y + 0.5f) -
                               dock_sdf((float)x - e + 0.5f, (float)y + 0.5f);
                    float gy = dock_sdf((float)x + 0.5f, (float)y + e + 0.5f) -
                               dock_sdf((float)x + 0.5f, (float)y - e + 0.5f);
                    const float g = sqrtf(gx * gx + gy * gy);
                    if (g > 0.0001f) {
                        // 梯度指向外，凸玻璃把背景取样点推向内部。
                        // The SDF gradient points outward; convex glass shifts the sample point inward.
                        gx /= g; gy /= g;
                        int sx = x - (int)lroundf(gx * disp);
                        int sy = y - (int)lroundf(gy * disp);
                        if (sx < 0) sx = 0; else if (sx >= w) sx = w - 1;
                        if (sy < 0) sy = 0; else if (sy >= h) sy = h - 1;
                        value = s_blur[(size_t)sy * w + sx];
                    }
                }
            }
            const int alpha = active_pill_inside(x, y, active) ? UI_NAV_FROST_ALPHA_ACTIVE : UI_NAV_FROST_ALPHA;
            // 遮罩给的是高四位里的 0..15，epd_draw_pixel 要的是带高四位的 8 位灰。
            // The mask yields 0..15 from the high nibble; epd_draw_pixel wants the 8-bit form.
            int out = (value * (100 - alpha) + 50) / 100;
            // 与玻璃面板共用同一个浅色下限（0x20），深色内容上图标和标签也读得清。
            // Shares the panels' light floor (0x20) so icons and labels read on dark content.
            if (out < UI_NAV_GLASS_FLOOR) out = UI_NAV_GLASS_FLOOR;
            // 镜面高光：边框外圈往白提，越靠边越亮。
            // Specular rim: lift the outer bezel toward white, brightest at the edge.
            const int lifted = out + spec / 17;
            if (lifted > out) out = lifted > 15 ? 15 : lifted;
            // 平滑的折射梯度要抖动到 16 级灰上，否则会出现色带。
            // The smooth refraction gradient is dithered onto the 16 levels or it bands.
            epd_draw_pixel(UI_NAV_DOCK_LEFT + x, UI_NAV_TOP + y,
                           ui_image_dither_gray((uint8_t)(out * 17), UI_NAV_DOCK_LEFT + x, UI_NAV_TOP + y), fb);
        }
    }
}


/* ---- 液态玻璃面板 / Liquid-glass panel ---- */

static uint8_t *s_panel_blur;                            // w*h 模糊结果 / blurred panel
static size_t s_panel_blur_cap;                          // 已申请容量 / allocated capacity
static uint8_t s_panel_ring[GAUSS_TAPS][UI_LOCK_WIDTH];  // 纵向滚动的横向结果 / rolling horizontal rows

/// 从 (left, top+row) 读一行并做横向高斯，写进 dst[0..w)。
/// Read one row starting at (left, top+row) and blur it horizontally into dst[0..w).
static void panel_row_h(const uint8_t *fb, int left, int top, int w, int row, uint8_t *dst) {
    for (int x = 0; x < w; ++x) {
        int sum = 0;
        for (int k = 0; k < GAUSS_TAPS; ++k) {
            int sx = x + k - GAUSS_RADIUS;
            if (sx < 0) sx = 0; else if (sx >= w) sx = w - 1;
            sum += s_gauss[k] * ui_read_pixel(fb, left + sx, top + row);
        }
        dst[x] = (uint8_t)(sum >> 8);
    }
}

/// 圆角矩形的带符号距离：内部为负，值就是到边框的距离。
/// Signed distance to a rounded rect: negative inside, equal to the distance from the border.
static float panel_sdf(float x, float y, float w, float h, float r) {
    const float qx = fabsf(x - w * 0.5f) - (w * 0.5f - r);
    const float qy = fabsf(y - h * 0.5f) - (h * 0.5f - r);
    const float mx = qx > 0.0f ? qx : 0.0f, my = qy > 0.0f ? qy : 0.0f;
    const float inner = qx > qy ? qx : qy;
    return sqrtf(mx * mx + my * my) + (inner < 0.0f ? inner : 0.0f) - r;
}

bool ui_nav_glass_panel(uint8_t *fb, EpdRect rect, int radius, int white, int refract) {
    if (!fb || rect.width <= 0 || rect.height <= 0) return false;
    if (rect.x < 0 || rect.y < 0) return false;
    if (rect.x + rect.width > UI_LOCK_WIDTH || rect.y + rect.height > UI_LOCK_HEIGHT) return false;
    // 快刷是黑白页面，玻璃没有意义：只铺不透明白底（保持圆角），描边交给页面自己画。
    // Fast mode is black and white where glass means nothing: fill an opaque white ground with the
    // same rounded shape and let the page draw its own outline.
    if (app_settings_main_fast_refresh()) {
        ui_fill_round_rect(fb, rect, radius, UI_GRAY_WHITE);
        return true;
    }
    const int w = rect.width, h = rect.height;
    // 只按面板实际尺寸申请：整屏大小（812KB）在这个固件里多半申请不到，
    // 申请失败会静默返回，结果就是「什么都没画」，看起来像胶囊根本没出现。
    // Allocate only what the panel needs: a full-screen buffer (812 KB) usually fails on this
    // firmware, and a failed allocation returns silently, which looks like no capsule at all.
    const size_t need = (size_t)w * h;
    if (s_panel_blur_cap < need) {
        if (s_panel_blur) heap_caps_free(s_panel_blur);
        s_panel_blur = heap_caps_malloc(need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        s_panel_blur_cap = s_panel_blur ? need : 0;
    }
    if (!s_panel_blur) return false;
    refract_prepare();
    // 第一步：整块面板的纯高斯模糊。源是页面原图（fb 此时只读），横向进环、纵向写输出。
    // Step one: a pure Gaussian blur of the whole panel. The source is the untouched page, so the
    // horizontal pass fills the ring and the vertical pass writes the output.
    int base = 0;
    for (int k = 0; k < GAUSS_TAPS; ++k) {
        int row = k - GAUSS_RADIUS;
        if (row < 0) row = 0; else if (row >= h) row = h - 1;
        panel_row_h(fb, rect.x, rect.y, w, row, s_panel_ring[k]);
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int sum = 0;
            for (int k = 0; k < GAUSS_TAPS; ++k) sum += s_gauss[k] * s_panel_ring[(base + k) % GAUSS_TAPS][x];
            s_panel_blur[(size_t)y * w + x] = (uint8_t)(sum >> 8);
        }
        int next = y + GAUSS_RADIUS + 1;
        if (next >= h) next = h - 1;
        panel_row_h(fb, rect.x, rect.y, w, next, s_panel_ring[base]);
        base = (base + 1) % GAUSS_TAPS;
    }
    // 第二步：中央取模糊值，只有边框带折射，和底栏同一套模型。
    // 折射强度：refract 调大时同时加宽折射带并放大位移，玻璃边缘的形变范围更大。
    // Step two: the centre keeps the blurred value and only the bezel band refracts, same model as
    // the dock. A larger refract widens the band and grows the displacement.
    const float fw = (float)w, fh = (float)h, fr = (float)radius;
    const int refract_pct = refract < 10 ? 10 : refract;
    const float refract_scale = (float)refract_pct / 100.0f;
    const float bezel = (float)REFRACT_BEZEL * refract_scale;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // 圆角外直接跳过：不判这一下就画成整块矩形，四角是方的而不是胶囊。
            // Skip everything outside the rounded shape: without this the panel is painted as a
            // full rectangle and its corners stay square instead of a capsule.
            const float dist = panel_sdf((float)x + 0.5f, (float)y + 0.5f, fw, fh, fr);
            if (dist > 0.0f) continue;
            const float t = -dist / bezel;
            int value = s_panel_blur[(size_t)y * w + x];
            int spec = 0;
            if (t > 0.0f && t < 1.0f) {
                const float disp = refract_sample(s_disp, t) * refract_scale;
                spec = (int)(refract_sample(s_spec, t) + 0.5f);
                if (disp > 0.5f) {
                    const float e = 0.75f;
                    float gx = panel_sdf((float)x + e + 0.5f, (float)y + 0.5f, fw, fh, fr) -
                               panel_sdf((float)x - e + 0.5f, (float)y + 0.5f, fw, fh, fr);
                    float gy = panel_sdf((float)x + 0.5f, (float)y + e + 0.5f, fw, fh, fr) -
                               panel_sdf((float)x + 0.5f, (float)y - e + 0.5f, fw, fh, fr);
                    const float g = sqrtf(gx * gx + gy * gy);
                    if (g > 0.0001f) {
                        gx /= g; gy /= g;
                        int sx = x - (int)lroundf(gx * disp);
                        int sy = y - (int)lroundf(gy * disp);
                        if (sx < 0) sx = 0; else if (sx >= w) sx = w - 1;
                        if (sy < 0) sy = 0; else if (sy >= h) sy = h - 1;
                        value = s_panel_blur[(size_t)sy * w + sx];
                    }
                }
            }
            int out = (value * (100 - UI_NAV_FROST_ALPHA) + 50) / 100;
            // 白色层：把结果往白里提，玻璃上的字和图标因此更清楚。
            // White layer: lift the result toward white so text and icons on the glass read better.
            if (white > 0) {
                out += (15 - out) * white / 100;
                // 按百分比提亮对深色底是无效的，所以再加一个统一的下限，玻璃永远是浅色。
                // Percentage blending does nothing on a dark backdrop, so the shared floor below
                // keeps the glass light on any content.
                if (out < UI_NAV_GLASS_FLOOR) out = UI_NAV_GLASS_FLOOR;
            }
            const int lifted = out + spec / 17;
            if (lifted > out) out = lifted > 15 ? 15 : lifted;
            epd_draw_pixel(rect.x + x, rect.y + y,
                           ui_image_dither_gray((uint8_t)(out * 17), rect.x + x, rect.y + y), fb);
        }
    }
    return true;
}

static uint8_t *s_glass_scratch;   // 玻璃控件暂存区 / pixel scratch for glass controls
static size_t s_glass_scratch_cap; // 已申请容量 / allocated capacity

uint8_t *ui_nav_glass_scratch(size_t bytes) {
    if (bytes == 0) return NULL;
    if (s_glass_scratch_cap < bytes) {
        if (s_glass_scratch) heap_caps_free(s_glass_scratch);
        s_glass_scratch = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        s_glass_scratch_cap = s_glass_scratch ? bytes : 0;
    }
    return s_glass_scratch;
}

static void icon(uint8_t *fb, int tab, int cx, int cy) {
    // 三格图标固定用 Lucide 细线原图，线宽与状态栏一致；cy 是图标盒中心。
    // The three dock icons stay on upstream Lucide line art at the status-bar stroke weight; cy is the box centre.
    static const ui_icon_t tabs[UI_NAV_TABS] = {
        UI_ICON_HOUSE, UI_ICON_LIBRARY_BIG, UI_ICON_SLIDERS_HORIZONTAL,
    };
    if (tab < 0 || tab >= UI_NAV_TABS) return;
    ui_click_feedback_register(fb, (EpdRect){cx - UI_NAV_ICON_BOX / 2, cy - UI_NAV_ICON_BOX / 2,
                                             UI_NAV_ICON_BOX, UI_NAV_ICON_BOX}, UI_CLICK_NAV, tabs[tab]);
    ui_draw_icon(fb, cx, cy, UI_NAV_ICON_PX, tabs[tab], UI_GRAY_BLACK);
}

void ui_nav_draw(uint8_t *fb, int active) {
    // 设计稿底栏是浮在画面上的半透明毛玻璃胶囊：除了胶囊本身什么都不铺。
    // 之前整带刷白会在胶囊后面留一条白条，也会把玻璃要模糊的内容擦掉。
    // The dock is a translucent frosted pill floating over the page: nothing is laid down but the pill.
    // Wiping the band used to leave a white bar behind the glass and erased the content it should blur.
    frost_dock(fb, active);
    for (int i = 0; i < UI_NAV_TABS; ++i) {
        const int center = (ui_nav_cell_left(i) + ui_nav_cell_right(i)) / 2;
        icon(fb, i, center, UI_NAV_ICON_CENTER_Y);
        ui_text(fb, center, UI_NAV_LABEL_TOP, 17, labels[i], EPD_DRAW_ALIGN_CENTER, false);
    }
}

/* ---- 底栏选中胶囊拖拽 / Dock active-capsule drag ---- */
// 长按选中格后，选中胶囊放大到比底栏上下各高出几像素，并随手指左右移动。
// 底栏含上下留白先存成底图，移动时只恢复胶囊经过的脏区，抬手由主循环切页或整条还原。
// Holding the active cell grows its capsule a few pixels above and below the dock and moves it with
// the finger. The dock band with its margins is snapshotted first; each move restores only the dirty
// capsule path, and release either switches pages from the loop or restores the whole band.
#define UI_NAV_CAPSULE_PAD 12    // 底图在底栏四周多留的像素 / margin around the dock in the snapshot
#define UI_NAV_CAPSULE_GROW 12   // 胶囊上下各长出的像素 / extra height above and below the cell
static uint8_t *s_capsule_bg;
static EpdRect s_capsule_last;

static int capsule_center_min(void) {
    return (ui_nav_cell_left(0) + ui_nav_cell_right(0)) / 2;
}
static int capsule_center_max(void) {
    return (ui_nav_cell_left(UI_NAV_TABS - 1) + ui_nav_cell_right(UI_NAV_TABS - 1)) / 2;
}
static EpdRect capsule_band(void) {
    const int top = UI_NAV_TOP - UI_NAV_CAPSULE_PAD;
    int bottom = UI_NAV_TOP + UI_NAV_DOCK_HEIGHT + UI_NAV_CAPSULE_PAD;
    if (bottom > UI_LOCK_HEIGHT) bottom = UI_LOCK_HEIGHT;
    return (EpdRect){UI_NAV_DOCK_LEFT - UI_NAV_CAPSULE_PAD, top,
                     UI_NAV_DOCK_WIDTH + 2 * UI_NAV_CAPSULE_PAD, bottom - top};
}
static EpdRect capsule_rect_clamped(int cx) {
    const int width = UI_NAV_DOCK_WIDTH / UI_NAV_TABS + 12;
    const int height = UI_NAV_CELL_HEIGHT + 2 * UI_NAV_CAPSULE_GROW;
    const int center = cx < capsule_center_min() ? capsule_center_min()
                     : cx > capsule_center_max() ? capsule_center_max() : cx;
    return (EpdRect){center - width / 2, UI_NAV_CELL_TOP - UI_NAV_CAPSULE_GROW, width, height};
}
static void capsule_restore(uint8_t *fb, EpdRect area) {
    const EpdRect band = capsule_band();
    const int left = area.x > band.x ? area.x : band.x;
    const int top = area.y > band.y ? area.y : band.y;
    const int right = area.x + area.width < band.x + band.width ? area.x + area.width : band.x + band.width;
    const int bottom = area.y + area.height < band.y + band.height ? area.y + area.height : band.y + band.height;
    if (!s_capsule_bg || right <= left || bottom <= top) return;
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
            epd_draw_pixel(x, y,
                (uint8_t)(s_capsule_bg[(size_t)(y - band.y) * band.width + (x - band.x)] << 4), fb);
}
static void capsule_draw(uint8_t *fb, EpdRect r) {
    // 抬起的选中胶囊只靠底色区分，不描边：白底浮在毛玻璃上，快刷用中灰底。
    // The lifted active capsule separates by fill alone, no outline: white over the frosted dock,
    // mid-gray in fast mode.
    ui_fill_round_rect(fb, r, r.height / 2,
                       app_settings_main_fast_refresh() ? 0xb8 : UI_GRAY_WHITE);
    const int tab = ui_nav_cell_at(r.x + r.width / 2);
    if (tab < 0 || tab >= UI_NAV_TABS) return;
    const int center = (ui_nav_cell_left(tab) + ui_nav_cell_right(tab)) / 2;
    icon(fb, tab, center, UI_NAV_ICON_CENTER_Y);
    ui_text(fb, center, UI_NAV_LABEL_TOP, 17, labels[tab], EPD_DRAW_ALIGN_CENTER, false);
}

bool ui_nav_capsule_drag_begin(uint8_t *fb) {
    const EpdRect band = capsule_band();
    if (!fb || band.x < 0 || band.y < 0 ||
        band.x + band.width > UI_LOCK_WIDTH || band.y + band.height > UI_LOCK_HEIGHT) return false;
    const size_t need = (size_t)band.width * band.height;
    if (!s_capsule_bg) s_capsule_bg = heap_caps_malloc(need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_capsule_bg) return false;
    for (int y = 0; y < band.height; ++y)
        for (int x = 0; x < band.width; ++x)
            s_capsule_bg[(size_t)y * band.width + x] = ui_read_pixel(fb, band.x + x, band.y + y);
    s_capsule_last = (EpdRect){0, 0, 0, 0};
    return true;
}

bool ui_nav_capsule_drag_move(uint8_t *fb, int cx, EpdRect *dirty) {
    if (!fb || !s_capsule_bg) return false;
    const EpdRect next = capsule_rect_clamped(cx);
    const EpdRect area = s_capsule_last.width > 0 ? ui_rect_union(s_capsule_last, next) : next;
    capsule_restore(fb, area);
    capsule_draw(fb, next);
    s_capsule_last = next;
    if (dirty) *dirty = area;
    return true;
}

void ui_nav_capsule_drag_end(uint8_t *fb) {
    if (!fb || !s_capsule_bg) return;
    capsule_restore(fb, capsule_band());
    s_capsule_last = (EpdRect){0, 0, 0, 0};
}

EpdRect ui_nav_capsule_drag_rect(void) { return capsule_band(); }

int ui_nav_capsule_tab(int cx) {
    const int center = cx < capsule_center_min() ? capsule_center_min()
                     : cx > capsule_center_max() ? capsule_center_max() : cx;
    const int tab = ui_nav_cell_at(center);
    return tab >= 0 && tab < UI_NAV_TABS ? tab : -1;
}

static time_t status_time(void) {
    time_t now = time(NULL);
    const pmu_snapshot_t *pmu = read_pico_pmu_get();
    if (now < 1704067200 && pmu && pmu->time_synced && pmu->unix_sec >= 1704067200)
        now = pmu->unix_sec;
    return now;
}
int64_t ui_nav_clock_minute(void) {
    time_t now = status_time();
    return now >= 1704067200 ? (int64_t)now / 60 : -1;
}
static void clock_text(char *clock, size_t capacity) {
    time_t now = status_time();
    snprintf(clock,capacity,"--:--");
    if (now > 1704067200) {
        time_t china = now + 8 * 3600;
        struct tm date;
        if (gmtime_r(&china, &date)) snprintf(clock, capacity, "%02d:%02d", date.tm_hour, date.tm_min);
    }
}
void ui_nav_clock_draw(uint8_t *fb) {
    char clock[12]; clock_text(clock,sizeof(clock));
    ui_text_fixed_vc(fb,36,40,UI_STATUS_TIME_PX,clock,EPD_DRAW_ALIGN_LEFT,false);
}
void ui_nav_status(uint8_t *fb) {
    status_prepare_metrics();
    const pmu_snapshot_t *pmu = read_pico_pmu_get();
    char clock[12]; clock_text(clock,sizeof(clock));
    const int status_center_y = 40;
    // 固定状态栏字号，让系统字体缩放时文字仍与图标共用中心线。
    // Keep status type fixed so text and glyphs share one centerline at every system font scale.
    ui_text_fixed_vc(fb, 36, status_center_y, UI_STATUS_TIME_PX, clock, EPD_DRAW_ALIGN_LEFT, false);
    read_pico_transfer_status_t network = {0};
    read_pico_transfer_get_status(&network);
    const bool wifi_visible = network.network_ready && network.mode == READ_PICO_TRANSFER_MODE_STA;
    const bool bluetooth_visible = app_settings_ble_turner();
    char percent[8] = "--%";
    int gauge_percent = pmu_battery_percent(pmu);
    if (gauge_percent >= 0) snprintf(percent, sizeof(percent), "%u%%", (unsigned)gauge_percent);
    // 按实际百分比宽度排列网络图标，留 10 像素间隔；墨色与电池相同。
    // Place the network marks 10 px before the measured percentage, using the battery ink.
    const int percent_left = 595 - status_number_width(percent);
    const int network_right = percent_left - 10;
    const int wifi_center = network_right - UI_NAV_NETWORK_ICON_PX / 2;
    const int bluetooth_center = wifi_center - (wifi_visible ? UI_NAV_NETWORK_ICON_PX + 8 : 0);
    int signature_width = 280;
    const int clock_right = 36 + status_number_width(clock);
    int available = 2 * (UI_LOCK_WIDTH / 2 - clock_right - 12);
    if (available < signature_width) signature_width = available > 0 ? available : 0;
    int group_left = percent_left;
    if (wifi_visible || bluetooth_visible) {
        group_left = (bluetooth_visible ? bluetooth_center : wifi_center) - UI_NAV_NETWORK_ICON_PX / 2;
    }
    available = 2 * (group_left - 12 - UI_LOCK_WIDTH / 2);
    if (available < signature_width) signature_width = available > 0 ? available : 0;
    char signature[96];
    snprintf(signature, sizeof(signature), "%s", app_settings_status_signature());
    status_fit_signature(signature, signature_width);
    if (signature[0]) ui_text_fixed_vc(fb, UI_LOCK_WIDTH / 2, status_center_y, UI_STATUS_SIGNATURE_PX,
                                      signature, EPD_DRAW_ALIGN_CENTER, false);
    if (wifi_visible) ui_nav_wifi_icon(fb, wifi_center, status_center_y, UI_NAV_NETWORK_ICON_PX, UI_GRAY_BLACK);
    if (bluetooth_visible)
        ui_draw_icon(fb, bluetooth_center, status_center_y, UI_NAV_NETWORK_ICON_PX,
                     UI_ICON_BLUETOOTH, UI_GRAY_BLACK);
    EpdRect battery = {606, status_center_y - 11, 40, 22};
    ui_draw_round_rect(fb, battery, 6, UI_GRAY_BLACK);
    ui_fill_round_rect(fb, (EpdRect){648, status_center_y - 4, 5, 8}, 2, UI_GRAY_BLACK);
    bool charging = pmu_battery_charging(pmu);
    if (gauge_percent >= 0) {
        int width = (31 * gauge_percent + 50) / 100;
        if (width > 0) ui_fill_round_rect(fb, (EpdRect){610, status_center_y - 7, width, 14}, 4, UI_GRAY_BLACK);
    }
    if (charging) {
        // 闪电只落在深色电量块上：透明像素保留底色，白色墨在浅色区自然消失，
        // 不必再按电量高低切换墨色。
        // The bolt shows only over the dark fill: transparent pixels keep the background,
        // so white ink vanishes on the light part and no SOC-dependent colour is needed.
        ui_draw_icon(fb, 627, status_center_y, UI_NAV_BOLT_ICON_PX, UI_ICON_ZAP, UI_GRAY_WHITE);
    }
    ui_text_fixed_vc(fb, 595, status_center_y, UI_STATUS_PERCENT_PX, percent, EPD_DRAW_ALIGN_RIGHT, false);
}

int ui_nav_hit(uint16_t x, uint16_t y) {
    // 只有胶囊本身可点：设计稿底栏两侧留白一直通到屏幕边。
    // Only the pill is touchable: the design leaves white on both sides all the way to the screen edge.
    if (y < UI_NAV_TOUCH_TOP || y >= UI_LOCK_HEIGHT) return -1;
    return ui_nav_cell_at((int)x);
}

void ui_nav_request(app_ctx_t *ctx, int index) {
    const app_desc_t *page = app_tab_at(index);
    if (page) ctx->request_app = page;
}
