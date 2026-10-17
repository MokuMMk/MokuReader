/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：长按电源的滑动关机层；只做纯绘制（玻璃模糊缓冲由 ui_nav 申请），不刷屏、写设置或调用硬件。
 * 用户修订：电源选项浮在当前画面上，用与阅读工具栏相同的液态玻璃组件呈现。
 * English: Slide-to-off layer for a power hold; drawing only (ui_nav allocates the blur buffer), with no presentation, settings writes or hardware calls.
 * User revision: the power options float over the current frame as the same liquid-glass components as the reader toolbar.
 */
#include "ui_power_dialog.h"
#include <stdlib.h>
#include <string.h>
#include "ui_kit.h"
#include "ui_nav.h"

enum { HIT_NONE, HIT_SLIDER, HIT_RESTART, HIT_CANCEL };

static bool in_circle(int x, int y, int cx, int cy, int r) {
    int dx = x - cx, dy = y - cy;
    return abs(dx) <= r && abs(dy) <= r && dx * dx + dy * dy <= r * r;
}

static EpdRect slider_change(int before, int after) {
    if (before == after) return (EpdRect){0};
    int left = before < after ? before : after;
    return (EpdRect){UI_POWER_THUMB_X + left - UI_POWER_THUMB_R - 1,
        UI_POWER_THUMB_Y - UI_POWER_THUMB_R - 1,
        abs(after - before) + 2 * UI_POWER_THUMB_R + 3, 2 * UI_POWER_THUMB_R + 3};
}

static EpdRect button_area(int hit) {
    int x = hit == HIT_RESTART ? UI_POWER_RESTART_X : UI_POWER_CANCEL_X;
    return (EpdRect){x - UI_POWER_BUTTON_R - 1, UI_POWER_BUTTON_Y - UI_POWER_BUTTON_R - 1,
        2 * UI_POWER_BUTTON_R + 3, 2 * UI_POWER_BUTTON_R + 3};
}

void ui_power_dialog_reset(ui_power_dialog_t *dialog, bool touching) {
    if (!dialog) return;
    memset(dialog, 0, sizeof(*dialog));
    dialog->swallow = touching;
}

void ui_power_dialog_cancel_input(ui_power_dialog_t *dialog, EpdRect *dirty) {
    if (dirty) *dirty = (EpdRect){0};
    if (!dialog) return;
    if (dirty) {
        if (dialog->position) *dirty = slider_change(dialog->position, 0);
        else if (dialog->pressed) *dirty = button_area(dialog->hit);
    }
    ui_power_dialog_reset(dialog, true);
}

ui_power_action_t ui_power_dialog_feed(ui_power_dialog_t *dialog, const app_ctx_t *ctx, EpdRect *dirty) {
    if (!dirty) return UI_POWER_ACTION_NONE;
    *dirty = (EpdRect){0};
    if (!dialog) return UI_POWER_ACTION_NONE;
    if (!ctx || ctx->consumed || !ctx->touch ||
        (ctx->touch->touched && (ctx->touch->count != 1 ||
          ctx->touch->x >= UI_LOCK_WIDTH || ctx->touch->y >= UI_LOCK_HEIGHT))) {
        ui_power_dialog_cancel_input(dialog, dirty);
        return UI_POWER_ACTION_NONE;
    }
    const cst836u_touch_t *touch = ctx->touch;
    if (dialog->swallow) {
        if (!touch->touched) dialog->swallow = false;
        return UI_POWER_ACTION_NONE;
    }
    int x = touch->x, y = touch->y;
    if (ctx->pressed && touch->touched && !dialog->tracking) {
        dialog->tracking = true;
        dialog->x0 = x; dialog->y0 = y;
        dialog->started_ms = ctx->now_ms;
        int cx = UI_POWER_THUMB_X + dialog->position;
        if (in_circle(x, y, cx, UI_POWER_THUMB_Y, UI_POWER_THUMB_R + 6)) {
            dialog->hit = HIT_SLIDER;
            dialog->grab_x = x - cx;
        } else if (in_circle(x, y, UI_POWER_RESTART_X, UI_POWER_BUTTON_Y, UI_POWER_BUTTON_R)) {
            dialog->hit = HIT_RESTART;
        } else if (in_circle(x, y, UI_POWER_CANCEL_X, UI_POWER_BUTTON_Y, UI_POWER_BUTTON_R)) {
            dialog->hit = HIT_CANCEL;
        }
        if (dialog->hit >= HIT_RESTART) {
            dialog->pressed = true;
            *dirty = button_area(dialog->hit);
        }
    }
    if (!dialog->tracking) return UI_POWER_ACTION_NONE;
    if (ctx->now_ms < dialog->started_ms || (!touch->touched && !ctx->released)) {
        ui_power_dialog_cancel_input(dialog, dirty);
        return UI_POWER_ACTION_NONE;
    }
    if (dialog->hit == HIT_SLIDER) {
        // 捕获后可水平越界；垂直滑出取消，不能在别处抬手触发关机。
        // Capture permits horizontal overshoot; leaving vertically cancels, preventing off-track release shutdown.
        if (abs(y - UI_POWER_THUMB_Y) > UI_POWER_TRACK_H) {
            ui_power_dialog_cancel_input(dialog, dirty);
            return UI_POWER_ACTION_NONE;
        }
        if (touch->touched) {
            int next = x - dialog->grab_x - UI_POWER_THUMB_X;
            if (next < 0) next = 0;
            if (next > UI_POWER_SLIDE_RANGE) next = UI_POWER_SLIDE_RANGE;
            *dirty = slider_change(dialog->position, next);
            dialog->position = next;
        } else if (ctx->released) {
            if (dialog->position >= UI_POWER_SLIDE_COMMIT) {
                ui_power_dialog_reset(dialog, false);
                return UI_POWER_ACTION_SHUTDOWN;
            }
            ui_power_dialog_cancel_input(dialog, dirty);
            dialog->swallow = false;
        }
    } else if (dialog->hit >= HIT_RESTART) {
        int cx = dialog->hit == HIT_RESTART ? UI_POWER_RESTART_X : UI_POWER_CANCEL_X;
        if (!in_circle(x, y, cx, UI_POWER_BUTTON_Y, UI_POWER_BUTTON_R) ||
            abs(x - dialog->x0) > UI_TOUCH_SLOP_PX || abs(y - dialog->y0) > UI_TOUCH_SLOP_PX) {
            ui_power_dialog_cancel_input(dialog, dirty);
            if (!touch->touched) dialog->swallow = false;
        } else if (ctx->released) {
            ui_power_action_t action = dialog->hit == HIT_RESTART ? UI_POWER_ACTION_RESTART : UI_POWER_ACTION_CANCEL;
            ui_power_dialog_reset(dialog, false);
            return action;
        }
    } else if (ctx->released) {
        ui_power_dialog_reset(dialog, false);
    }
    return UI_POWER_ACTION_NONE;
}

EpdRect ui_power_dialog_rect(void) { return (EpdRect){0, 0, UI_LOCK_WIDTH, UI_LOCK_HEIGHT}; }

// 每个组件各一块液态玻璃：滑轨、重启、取消。玻璃只模糊一次，底图留给拖动/按压时恢复脏区。
// Each component gets its own liquid-glass surface (rail, restart, cancel). Each glass is blurred
// once and its pixels are kept so drags and presses restore their dirty region instead of blurring
// the same surface again.
static uint8_t *s_glass_bg;
static bool s_glass_ready;

static EpdRect power_track_rect(void) {
    return (EpdRect){UI_POWER_TRACK_X, UI_POWER_TRACK_Y, UI_POWER_TRACK_W, UI_POWER_TRACK_H};
}
static EpdRect power_button_rect(int hit) {
    const int cx = hit == HIT_RESTART ? UI_POWER_RESTART_X : UI_POWER_CANCEL_X;
    return (EpdRect){cx - UI_POWER_BUTTON_R, UI_POWER_BUTTON_Y - UI_POWER_BUTTON_R,
                     2 * UI_POWER_BUTTON_R, 2 * UI_POWER_BUTTON_R};
}
static size_t power_glass_bg_bytes(void) {
    const size_t circle = (size_t)(2 * UI_POWER_BUTTON_R) * (2 * UI_POWER_BUTTON_R);
    return (size_t)UI_POWER_TRACK_W * UI_POWER_TRACK_H + 2u * circle;
}
static uint8_t *power_glass_slot(int slot) {
    if (!s_glass_bg) return NULL;
    if (slot == 0) return s_glass_bg;
    const size_t rail = (size_t)UI_POWER_TRACK_W * UI_POWER_TRACK_H;
    const size_t circle = (size_t)(2 * UI_POWER_BUTTON_R) * (2 * UI_POWER_BUTTON_R);
    return s_glass_bg + rail + (size_t)(slot - 1) * circle;
}
static void power_glass_save(uint8_t *fb, int slot, EpdRect r) {
    uint8_t *dst = power_glass_slot(slot);
    if (!dst) return;
    for (int y = 0; y < r.height; ++y)
        for (int x = 0; x < r.width; ++x)
            dst[(size_t)y * r.width + x] = ui_read_pixel(fb, r.x + x, r.y + y);
}
static void power_glass_restore(uint8_t *fb, int slot, EpdRect r, EpdRect area) {
    uint8_t *src = power_glass_slot(slot);
    if (!src) return;
    const int left = r.x > area.x ? r.x : area.x;
    const int top = r.y > area.y ? r.y : area.y;
    const int right = r.x + r.width < area.x + area.width ? r.x + r.width : area.x + area.width;
    const int bottom = r.y + r.height < area.y + area.height ? r.y + r.height : area.y + area.height;
    if (right <= left || bottom <= top) return;
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x)
            epd_draw_pixel(x, y, (uint8_t)(src[(size_t)(y - r.y) * r.width + (x - r.x)] << 4), fb);
}

static void power_draw_button(uint8_t *fb, const ui_power_dialog_t *dialog, int hit, bool restart,
                              const char *label) {
    const EpdRect r = power_button_rect(hit);
    const int cx = r.x + r.width / 2, cy = r.y + r.height / 2;
    if (dialog->pressed && dialog->hit == hit) {
        // 按下时在内圈压一层中灰，玻璃亮边保留；抬手由底图恢复。
        // Press darkens an inset disc while the glass rim stays; release restores the backdrop.
        epd_fill_circle(cx, cy, UI_POWER_BUTTON_R - 14, 0xc8, fb);
    }
    if (restart) ui_draw_icon(fb, cx, cy - 20, 44, UI_ICON_ROTATE_CW, UI_GRAY_BLACK);
    else {
        // 圆帽取消符号，画在图标位置。/ Round-ended cancel mark at the icon position.
        for (int i = -15; i <= 15; ++i) {
            epd_fill_circle(cx + i, cy - 20 + i, 2, UI_GRAY_BLACK, fb);
            epd_fill_circle(cx + i, cy - 20 - i, 2, UI_GRAY_BLACK, fb);
        }
    }
    ui_text_box(fb, (EpdRect){cx - 90, cy + 18, 180, 38}, 24, label, EPD_DRAW_ALIGN_CENTER, false);
}

static bool power_rect_overlap(EpdRect a, EpdRect b) {
    return a.x < b.x + b.width && b.x < a.x + a.width &&
           a.y < b.y + b.height && b.y < a.y + a.height;
}

void ui_power_dialog_draw_controls(uint8_t *fb, const ui_power_dialog_t *dialog, EpdRect dirty) {
    if (!fb || !dialog) return;
    const EpdRect track = power_track_rect();
    const EpdRect restart = power_button_rect(HIT_RESTART), cancel = power_button_rect(HIT_CANCEL);
    // 只重画与脏区相交的组件：图标抗锯齿会读取当前底色，同一像素重复画会把边缘越压越深。
    // Repaint only components that intersect the dirty area: icon antialiasing reads the current
    // pixel, so painting the same icon twice without a restore darkens its edges.
    const bool rail = power_rect_overlap(track, dirty);
    const bool restart_hit = power_rect_overlap(restart, dirty);
    const bool cancel_hit = power_rect_overlap(cancel, dirty);
    if (s_glass_ready) {
        if (rail) power_glass_restore(fb, 0, track, dirty);
        if (restart_hit) power_glass_restore(fb, 1, restart, dirty);
        if (cancel_hit) power_glass_restore(fb, 2, cancel, dirty);
    } else {
        // 实心回退：只重画相交组件的底色，旧把手/按下态自然被盖掉。
        // Solid fallback: repaint the fills of intersecting components so the old knob or pressed
        // state is covered.
        if (rail) ui_fill_round_rect(fb, track, UI_POWER_TRACK_H / 2, 0xcc);
        if (restart_hit) epd_fill_circle(UI_POWER_RESTART_X, UI_POWER_BUTTON_Y, UI_POWER_BUTTON_R, 0xf4, fb);
        if (cancel_hit) epd_fill_circle(UI_POWER_CANCEL_X, UI_POWER_BUTTON_Y, UI_POWER_BUTTON_R, 0xf4, fb);
    }
    if (rail) {
        ui_text_box(fb, (EpdRect){track.x + 80, track.y + 45, track.width - 160, 60}, 40,
            "滑动关机", EPD_DRAW_ALIGN_CENTER, false);
        const int cx = UI_POWER_THUMB_X + dialog->position;
        // 中灰把手在浅色玻璃和深色内容上都看得清，图标保持黑色。
        // A mid-gray knob reads on both light glass and dark content, with a black icon.
        epd_fill_circle(cx, UI_POWER_THUMB_Y, UI_POWER_THUMB_R, 0xc8, fb);
        ui_draw_icon(fb, cx, UI_POWER_THUMB_Y, 52, UI_ICON_POWER, UI_GRAY_BLACK);
    }
    if (restart_hit) power_draw_button(fb, dialog, HIT_RESTART, true, "重启");
    if (cancel_hit) power_draw_button(fb, dialog, HIT_CANCEL, false, "取消");
}

void ui_power_dialog_draw(uint8_t *fb, const ui_power_dialog_t *dialog) {
    if (!fb || !dialog) return;
    const EpdRect track = power_track_rect();
    const EpdRect restart = power_button_rect(HIT_RESTART), cancel = power_button_rect(HIT_CANCEL);
    // 每块玻璃都带白层，配合 0x20 的玻璃下限，黑字在任意页面内容上都读得清。
    // Each surface carries a white layer; with the 0x20 glass floor, black labels stay legible over
    // any page content.
    s_glass_bg = ui_nav_glass_scratch(power_glass_bg_bytes());
    s_glass_ready = s_glass_bg != NULL &&
        ui_nav_glass_panel(fb, track, UI_POWER_TRACK_H / 2, UI_POWER_WHITE, UI_POWER_REFRACT) &&
        ui_nav_glass_panel(fb, restart, UI_POWER_BUTTON_R, UI_POWER_WHITE, UI_POWER_REFRACT) &&
        ui_nav_glass_panel(fb, cancel, UI_POWER_BUTTON_R, UI_POWER_WHITE, UI_POWER_REFRACT);
    if (s_glass_ready) {
        power_glass_save(fb, 0, track);
        power_glass_save(fb, 1, restart);
        power_glass_save(fb, 2, cancel);
    } else {
        // 回退：实心组件盖住可能已经画出的玻璃，文字仍然可读。
        // Fallback: solid components cover any glass already painted and keep labels readable.
        ui_fill_round_rect(fb, track, UI_POWER_TRACK_H / 2, 0xcc);
        epd_fill_circle(UI_POWER_RESTART_X, UI_POWER_BUTTON_Y, UI_POWER_BUTTON_R, 0xf4, fb);
        epd_fill_circle(UI_POWER_CANCEL_X, UI_POWER_BUTTON_Y, UI_POWER_BUTTON_R, 0xf4, fb);
    }
    ui_power_dialog_draw_controls(fb, dialog, (EpdRect){0, 0, UI_LOCK_WIDTH, UI_LOCK_HEIGHT});
}

void ui_power_final_draw(uint8_t *fb, bool restarting) {
    ui_clear_page(fb);
    const int cx = UI_LOCK_WIDTH / 2;
    ui_draw_icon(fb, cx, 486, 72, restarting ? UI_ICON_ROTATE_CW : UI_ICON_POWER, UI_GRAY_BLACK);
    ui_text(fb, cx, 548, 48, restarting ? "正在重启" : "kiikoread 已关机", EPD_DRAW_ALIGN_CENTER, false);
    if (!restarting) ui_text(fb, cx, 624, 26, "长按电源键开机", EPD_DRAW_ALIGN_CENTER, false);
}
