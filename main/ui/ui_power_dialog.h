/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：电源层的滑动确认、圆钮输入及纯绘图，硬件动作由主循环执行。
 * English: Power-layer slide confirmation, circle input and pure drawing; the loop owns hardware actions.
 */
#pragma once

#include "app.h"
#include "epdiy.h"

#define UI_POWER_TRACK_X 36
#define UI_POWER_TRACK_Y 330
#define UI_POWER_TRACK_W 612
#define UI_POWER_TRACK_H 150
#define UI_POWER_THUMB_R 60
#define UI_POWER_THUMB_X (UI_POWER_TRACK_X + UI_POWER_TRACK_H / 2)
#define UI_POWER_THUMB_Y (UI_POWER_TRACK_Y + UI_POWER_TRACK_H / 2)
#define UI_POWER_SLIDE_RANGE (UI_POWER_TRACK_W - UI_POWER_TRACK_H)
#define UI_POWER_SLIDE_COMMIT ((UI_POWER_SLIDE_RANGE * 92 + 99) / 100)
#define UI_POWER_BUTTON_Y 811
#define UI_POWER_BUTTON_R 72
#define UI_POWER_RESTART_X 196
#define UI_POWER_CANCEL_X (684 - UI_POWER_RESTART_X)
// 电源页单独加强折射：100 是默认范围，160 会把折射带和位移一起放大，边缘形变更明显。
// The power page strengthens refraction on its own: 100 is the default range, and 160 widens both
// the refraction band and the displacement so the rim deforms more.
#define UI_POWER_REFRACT 160
// 电源页玻璃的白层百分比：越小玻璃越透、越暗。用户 2026-10-17 要求 25%。
// White-layer percentage for the power page glass: lower means clearer and darker. Per the user's
// 2026-10-17 request it is 25%.
#define UI_POWER_WHITE 25

typedef enum {
    UI_POWER_ACTION_NONE = 0,
    UI_POWER_ACTION_CANCEL,
    UI_POWER_ACTION_SHUTDOWN,
    UI_POWER_ACTION_RESTART,
} ui_power_action_t;

typedef struct {
    int position; ///< 滑块真实像素位移。/ Thumb displacement in native pixels.
    int hit; ///< 0无、1滑块、2重启、3取消。/ None, slider, restart or cancel.
    int grab_x, x0, y0; ///< 保留抓取偏移与按下原点。/ Retain grab offset and contact origin.
    int64_t started_ms; ///< 接触起始时间，时钟倒退取消。/ Contact start; cancel on clock regression.
    bool tracking, pressed, swallow; ///< 捕获、圆钮按压及中断接触抑制。/ Capture, circle press and interrupted-contact suppression.
} ui_power_dialog_t;

/// 打开时吞掉已有接触直到抬手。/ On open, swallow an existing contact until lift.
void ui_power_dialog_reset(ui_power_dialog_t *dialog, bool touching);
/// 输入错误或媒体中断复位，输出需恢复的局部区域。/ Reset on input/media interruption, reporting the local restoration area.
void ui_power_dialog_cancel_input(ui_power_dialog_t *dialog, EpdRect *dirty);
/// 单指跟手；滑到92%之后松手才关机，圆钮按下不执行动作。
/// Track one finger; only release past 92% shuts down; circle presses perform no actions.
ui_power_action_t ui_power_dialog_feed(ui_power_dialog_t *dialog, const app_ctx_t *ctx, EpdRect *dirty);
/// 无缓存纯绘制，局部刷新仅提交dirty区域；dirty 用于从玻璃底图恢复脏区后再画控件。
/// Pure drawing without caches; locally present only the dirty area; dirty restores the glass
/// backdrop before the controls are repainted.
void ui_power_dialog_draw_controls(uint8_t *fb, const ui_power_dialog_t *dialog, EpdRect dirty);
void ui_power_dialog_draw(uint8_t *fb, const ui_power_dialog_t *dialog);
EpdRect ui_power_dialog_rect(void);
void ui_power_final_draw(uint8_t *fb, bool restarting);
