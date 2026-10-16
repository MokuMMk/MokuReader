/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 正式产品页面的清单。菜单、主循环和开机页都从这里取。
 * 底栏只有三格（主页、书库、设置）；文件管理与图片页不在底栏里，由页面显式请求。
 *
 * Registry of production pages used by the menu, app loop, and boot route.
 * The dock holds three tabs (home, library, settings); Files and Image stay outside it
 * and are requested explicitly by other pages.
 */

#pragma once

#include "app.h"

#ifdef __cplusplus
extern "C" {
#endif

int app_count(void);
const app_desc_t* app_at(int index);
/// 找不到时返回 -1。/ Returns -1 when not found.
int app_index_of(const app_desc_t* app);

/// 底栏格数。设计稿是主页 / 书库 / 设置三格。/ Number of dock tabs: home, library and settings.
int app_tab_count(void);
/// 第 index 格对应的页面；越界返回 NULL。/ Page behind a dock tab, or NULL when out of range.
const app_desc_t* app_tab_at(int index);
/// 页面属于哪一格；不是底栏页时返回 -1。/ Which dock tab owns the page, or -1 when it is not a tab.
int app_tab_index_of(const app_desc_t* app);

/// 开机默认产品页。/ Default production page at boot.
const app_desc_t* app_home_page(void);
/// 文件管理不是底栏页，但书库的导入菜单和管理页要能切过去。
/// Files is not a dock tab, yet the shelf's import menu and the detail page switch to it.
const app_desc_t* app_files_page(void);

#ifdef __cplusplus
}
#endif
