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
/// 液态玻璃面板：模糊 + 边框折射 + 菲涅尔亮边，画进一块圆角矩形。读的是 fb 里已有的页面内容，
/// 所以必须先画页面再调它。快刷模式退化成不透明白底。返回 false 表示模糊缓冲申请失败、什么都没画，
/// 调用方应自行退回实心面板。
/// Liquid-glass panel: blur, bezel refraction and a Fresnel rim inside a rounded rect. It reads the
/// page already present in fb, so paint the page first. Fast mode degrades to an opaque white ground.
/// Returns false when the blur buffer cannot be allocated and nothing was painted, so callers can
/// fall back to a solid panel.
/// white 是叠在上面那层白色的百分比：苹果用一层白提高玻璃上内容的对比度，0 表示不加。
/// white is the percentage of a white layer laid on top; Apple uses one to raise the contrast of
/// what sits on the glass. 0 disables it.
/// refract 是折射强度百分比：100 为默认边框宽度与位移，调大同时加宽折射影响范围和位移。
/// refract scales the refraction: 100 is the default bezel width and shift; larger values widen the
/// refraction band and increase the displacement.
bool ui_nav_glass_panel(uint8_t *fb, EpdRect rect, int radius, int white, int refract);
/// 玻璃控件的像素暂存区（PSRAM，按需增长，不释放）。玻璃只模糊一次，拖动/按压时从这里恢复脏区。
/// Pixel scratch for glass controls (PSRAM, grows on demand, never freed): the glass is blurred once
/// and drags or presses restore their dirty region from here. NULL 表示无法分配。
/// NULL when the allocation is unavailable.
uint8_t *ui_nav_glass_scratch(size_t bytes);
/// 底栏选中胶囊拖拽：长按选中格后胶囊放大到比底栏略高，跟手左右移动，抬手由主循环切页。
/// 开始前先把底栏含上下留白存成底图；失败返回 false（不进入拖拽）。
/// Dock active-capsule drag: holding the active cell grows the capsule slightly taller than the dock
/// and moves it with the finger; the loop switches pages on release. Begin snapshots the dock band
/// with its top/bottom margin first and returns false when the snapshot cannot be allocated.
bool ui_nav_capsule_drag_begin(uint8_t *fb);
/// 胶囊跟手移动到中心 cx；返回本次需要局部刷新的矩形（胶囊已画好）。
/// Move the capsule to centre cx and report the area to refresh locally (the capsule is painted).
bool ui_nav_capsule_drag_move(uint8_t *fb, int cx, EpdRect *dirty);
/// 结束拖拽并把底栏底图还原到 fb（不推送）。/ Restore the dock band into fb (no presentation).
void ui_nav_capsule_drag_end(uint8_t *fb);
/// 拖拽底图的覆盖范围，用于结束后恢复整条底栏。/ Coverage of the drag snapshot, used to restore the band.
EpdRect ui_nav_capsule_drag_rect(void);
/// 胶囊中心落在哪一格（0..2，越界夹到两端）；页面不是底栏页时由调用方处理。
/// Cell under a capsule centre (0..2, clamped to the ends); callers handle pages that are not dock tabs.
int ui_nav_capsule_tab(int cx);
/// index 是底栏格号（0..2）；越界时什么也不做。/ index is a dock tab number (0..2); out-of-range does nothing.
void ui_nav_request(app_ctx_t *ctx, int index);

