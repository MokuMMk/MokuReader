/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：三个产品主页面（主页、书库、设置）共用的胶囊底栏；阅读页自行隐藏。
 * English: Shared pill dock for the three product pages (home, library, settings); the reader hides it.
 */
#pragma once

#include "app.h"
#include "ui_nav_layout.h"

#define UI_NAV_BACK_BORDER_GRAY 0x58

/// 绘制三格底栏；active 是 0..2，-1 表示当前页面不在底栏里，三格都不高亮。
/// Paint the three-cell dock; active is 0..2, or -1 when the current page is not a dock tab.
void ui_nav_draw(uint8_t *fb, int active);
void ui_nav_status(uint8_t *fb);
/// 系统字体就绪时缓存状态栏字宽，阅读页只复用，不重载整份系统字体。
/// Cache status metrics when the system face is ready; reading reuses them without reloading that font.
void ui_nav_status_font_context(bool reading, bool face_changed);
/// 当前 UTC 分钟，用于显示更新判断；不读取卡或请求网络校时。
/// Current UTC minute for display updates, without card IO or network time requests.
int64_t ui_nav_clock_minute(void);
/// 只绘制状态栏时间，供主页面按分钟局部更新。/ Draw only the status clock for local minute updates.
void ui_nav_clock_draw(uint8_t *fb);
/// 绘制设置页与状态栏共用的 WiFi 标志。/ Draw the WiFi mark shared by Settings and the status bar.
void ui_nav_wifi_icon(uint8_t *fb, int cx, int cy, int size, uint8_t gray);
/// 二级页面共用的圆形返回按钮。/ Shared circular back control for secondary pages.
void ui_nav_back(uint8_t *fb, int x, int y);
/// 三格胶囊内的整列热区；落在胶囊外或导航带外返回 -1。触控热区向上扩16px，不改变画面和刷新区域。
/// Whole-cell hits inside the three-cell pill; -1 outside the pill or the navigation band. Touch admission
/// extends 16 px upward without changing paint or refresh bounds.
int ui_nav_hit(uint16_t x, uint16_t y);
/// index 是底栏格号（0..2）；越界时什么也不做。/ index is a dock tab number (0..2); out-of-range does nothing.
void ui_nav_request(app_ctx_t *ctx, int index);

