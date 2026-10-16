/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 产品页清单。三个主页面（主页、书库、设置）对应底部导航；文件管理和图片页由页面显式请求，
 * 传书页由文件首页的直达按钮进入，工厂页独立。
 *
 * Product page table. Three main pages (home, library, settings) match the dock; Files and Image are
 * requested explicitly, file-home buttons open transfer directly, and factory pages stay separate.
 */

#include "app_registry.h"

extern const app_desc_t app_book;
extern const app_desc_t app_image;
extern const app_desc_t app_dashboard;
extern const app_desc_t app_files;
extern const app_desc_t app_device_settings;

// 正式产品页面清单；工厂测试与历史示例不进入用户导航。
// Production page registry; factory diagnostics and legacy examples are not user-navigable.
static const app_desc_t* const s_apps[] = {
    &app_dashboard,
    &app_book,
    &app_files,
    &app_device_settings,
    &app_image,
};

#define APP_COUNT ((int)(sizeof(s_apps) / sizeof(s_apps[0])))

// 底栏三格，顺序即设计稿从左到右的顺序。
// The three dock tabs, in the design's left-to-right order.
static const app_desc_t* const s_tabs[] = {
    &app_dashboard,
    &app_book,
    &app_device_settings,
};

#define APP_TAB_COUNT ((int)(sizeof(s_tabs) / sizeof(s_tabs[0])))

int app_count(void) {
    return APP_COUNT;
}

const app_desc_t* app_at(int index) {
    if (index < 0 || index >= APP_COUNT) return NULL;
    return s_apps[index];
}

int app_index_of(const app_desc_t* app) {
    for (int i = 0; i < APP_COUNT; i++) {
        if (s_apps[i] == app) return i;
    }
    return -1;
}

int app_tab_count(void) {
    return APP_TAB_COUNT;
}

const app_desc_t* app_tab_at(int index) {
    if (index < 0 || index >= APP_TAB_COUNT) return NULL;
    return s_tabs[index];
}

int app_tab_index_of(const app_desc_t* app) {
    for (int i = 0; i < APP_TAB_COUNT; i++) {
        if (s_tabs[i] == app) return i;
    }
    return -1;
}

const app_desc_t* app_home_page(void) {
    return &app_dashboard;
}

const app_desc_t* app_files_page(void) {
    return &app_files;
}
