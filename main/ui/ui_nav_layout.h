/* SPDX-License-Identifier: Apache-2.0
 * 中文：轻量底栏边界，绘制和刷新共用，防止局部扫描碰到固定图标。
 * English: Lightweight navigation bounds shared by painting and presentation to protect fixed icons.
 * 用户修订：底栏改为设计稿的三格胶囊；内容/刷新边界沿用原值，胶囊画在导航带内部。
 * User revision: the dock is the design's three-cell pill; content and refresh bounds keep their
 * original values and the pill is painted inside the navigation band.
 */
#pragma once
#define UI_NAV_TOP 1096
// 只扩大触控热区，不移动图标或改变内容/底栏的刷新边界。
// Expand touch admission only; icons and content/navigation refresh boundaries stay fixed.
#define UI_NAV_TOUCH_TOP (UI_NAV_TOP - 16)
#define UI_NAV_MARKER_TOP (UI_NAV_TOP + 1)
#define UI_NAV_MARKER_BOTTOM (UI_NAV_TOP + 6)
// 内容向内收至32列边界，外扩范围止于图标首笔之前。
// Round content inward to 32 columns; any expansion ends before the first icon ink.
// 横条边界保留给 display.c 的 32 列裁剪：胶囊底栏已经不画横条，这两个值只定义那条窄带。
// The marker bounds stay for display.c's 32-column crop: the pill dock draws no marker, so these two
// values now only name that narrow band.
#define UI_NAV_REFRESH_END (UI_NAV_TOP & ~31)
#define UI_NAV_MARKER_SCAN_END ((UI_NAV_MARKER_BOTTOM + 31) & ~31)

// 设计稿底栏：455×112 圆角胶囊，内嵌等宽三格。
// Design dock: a 455x112 rounded pill holding three equal cells.
#define UI_NAV_TABS 3
#define UI_NAV_DOCK_LEFT 116
#define UI_NAV_DOCK_WIDTH 455
#define UI_NAV_DOCK_HEIGHT 112
#define UI_NAV_DOCK_RADIUS 56
// 选中胶囊在格内上下各收6px，半径取半高得到设计稿的跑道形。
// The selected pill insets 6 px top and bottom inside its cell; a half-height radius gives the design's stadium.
#define UI_NAV_CELL_TOP (UI_NAV_TOP + 6)
#define UI_NAV_CELL_HEIGHT 100
#define UI_NAV_CELL_PILL_INSET 3

// 图标盒与按压缩放：按下时在同一个盒子里把字形放大，反馈区域因此始终等于这个盒子。
// ui_click_feedback_register() 的上限是 64，盒子和按压字形都不能超过它。
// Icon box and press zoom: the pressed glyph enlarges inside the same box so the feedback area equals it.
// ui_click_feedback_register() caps at 64, which bounds both the box and the pressed glyph.
#define UI_NAV_ICON_BOX 64
#define UI_NAV_ICON_PX 56
#define UI_NAV_ICON_PRESS_PX 64
// 图标盒中心与标签顶边，取设计稿格内 top 12 + 30 和 top 68。
// Icon-box centre and label top, from the design's cell-relative top 12 + 30 and top 68.
#define UI_NAV_ICON_CENTER_Y (UI_NAV_TOP + 42)
#define UI_NAV_LABEL_TOP (UI_NAV_TOP + 74)

// 底栏是半透明毛玻璃：先模糊胶囊后面的画面，再把墨按透明度压上去，所以底色越深表示越不透明。
// 白底上 20% 与 40% 的黑落在面板的 0xc0 与 0x90 两级。
// The dock is translucent frosted glass: blur what is behind the pill, then lay ink over it by opacity,
// so a darker tone means less transparency. On a white backdrop 20% and 40% ink land on this panel's
// 0xc0 and 0x90 levels.
#define UI_NAV_DOCK_FILL 0xc0
#define UI_NAV_DOCK_ACTIVE 0x90
// 模糊半径与两档透明度（百分比）。半径越大越糊，代价是每帧多读若干行像素。
// Blur radius and the two opacity percentages. A larger radius blurs more and costs more reads per frame.
#define UI_NAV_FROST_RADIUS 5
#define UI_NAV_FROST_ALPHA 20
#define UI_NAV_FROST_ALPHA_ACTIVE 40

/// 第 index 格的左边界（含）与右边界（不含）。/ Left edge (inclusive) and right edge (exclusive) of a cell.
/// 455 不能被 3 整除，所以三格宽度是 151/152/152；绘制、命中、按压反馈都只看这两个函数。
/// 455 does not divide by 3, so the cells are 151/152/152 wide; painting, hit-testing and press
/// feedback all derive their bounds from these two functions alone.
static inline int ui_nav_cell_left(int index) {
    return UI_NAV_DOCK_LEFT + index * UI_NAV_DOCK_WIDTH / UI_NAV_TABS;
}

static inline int ui_nav_cell_right(int index) {
    return UI_NAV_DOCK_LEFT + (index + 1) * UI_NAV_DOCK_WIDTH / UI_NAV_TABS;
}

/// 底栏胶囊内三格：落在胶囊内返回 0..2，落在胶囊外返回 -1。
/// The dock's three cells: 0..2 inside the pill, -1 outside it.
static inline int ui_nav_cell_at(int x) {
    for (int i = 0; i < UI_NAV_TABS; ++i) {
        if (x >= ui_nav_cell_left(i) && x < ui_nav_cell_right(i)) return i;
    }
    return -1;
}
