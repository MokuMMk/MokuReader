/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：绘制与命中设计稿的三格胶囊底栏；细线图标分别表示主页、书库和设置，图形来自 Lucide。
 * English: Draw and hit-test the design's three-cell pill dock with thin-line home, library and settings icons from Lucide.
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
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "app_registry.h"
#include "read_pico_pmu.h"
#include "read_pico_pmu_protocol.h"
#include "read_pico_transfer.h"
#include "ui_kit.h"
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

// 半透明毛玻璃胶囊：把胶囊后面的画面做一次滚动窗口盒式模糊，再按透明度压墨。
// 窗口只留模糊需要的 2R+1 行，所以不申请内存，也不随屏幕宽度增长。
// The translucent frosted pill: box-blur what sits behind it through a rolling window, then lay ink
// over it by opacity. The window keeps only the 2R+1 rows the blur needs, so nothing is allocated
// and the cost does not grow with the screen width.
static void frost_dock(uint8_t *fb, int active) {
    const int w = UI_NAV_DOCK_WIDTH, h = UI_NAV_DOCK_HEIGHT;
    // 快刷是黑白主页：不铺半透明底，只在选中格压一块实心黑，和原来固定底栏的黑横条同一种语言，
    // 也绝不会把胶囊后面的内容盖成一片白。
    // Fast mode is a black/white main page: lay no translucent fill and ink only the selected cell
    // solid, the same language as the old fixed bar's black marker, and never white out what is behind.
    if (app_settings_main_fast_refresh()) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (active_pill_inside(x, y, active))
                    epd_draw_pixel(UI_NAV_DOCK_LEFT + x, UI_NAV_TOP + y, 0, fb);
        return;
    }
    const int span = 2 * UI_NAV_FROST_RADIUS + 1;
    static uint8_t window[2 * UI_NAV_FROST_RADIUS + 1][UI_NAV_DOCK_WIDTH];
    for (int k = 0; k < span; ++k) {
        int row = k - UI_NAV_FROST_RADIUS;
        if (row < 0) row = 0; else if (row >= h) row = h - 1;
        for (int x = 0; x < w; ++x)
            window[k][x] = ui_read_pixel(fb, UI_NAV_DOCK_LEFT + x, UI_NAV_TOP + row);
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!dock_inside(x, y)) continue;
            int sum = 0;
            for (int k = 0; k < span; ++k) sum += window[k][x];
            const int blur = sum / span;
            const int alpha = active_pill_inside(x, y, active) ? UI_NAV_FROST_ALPHA_ACTIVE : UI_NAV_FROST_ALPHA;
            // ui_read_pixel 给的是高四位里的 0..15，epd_draw_pixel 要的是带高四位的 8 位灰。
            // ui_read_pixel yields 0..15 from the high nibble; epd_draw_pixel wants the 8-bit form.
            epd_draw_pixel(UI_NAV_DOCK_LEFT + x, UI_NAV_TOP + y,
                           (uint8_t)(((blur * (100 - alpha) + 50) / 100) << 4), fb);
        }
        int next = y + UI_NAV_FROST_RADIUS + 1;
        if (next >= h) next = h - 1;
        uint8_t *slot = window[(y + UI_NAV_FROST_RADIUS) % span];
        for (int x = 0; x < w; ++x)
            slot[x] = ui_read_pixel(fb, UI_NAV_DOCK_LEFT + x, UI_NAV_TOP + next);
    }
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
