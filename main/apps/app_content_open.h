/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：文件管理与首页请求内容页打开指定文件。
 * English: File manager and home request a specific file in the content pages.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

bool app_book_request_open(const char *path);
bool app_book_request_open_from_home(const char *path);
/// 一次性深睡恢复；保持保存的正文位置与全屏状态。/ One-shot deep-sleep resume with saved position and fullscreen state.
bool app_book_request_resume(const char *path, bool fullscreen);
/// 已读完标记：普通书架分类与首页继续阅读都不显示，只在分类弹窗顶部固定项里出现；
/// 再次打开这本书会自动清除。
/// Finished flag: hidden from ordinary shelf groups and the Home strip, shown only under the
/// category popup's fixed row; opening the book again clears it.
bool app_book_finished(const char *path);
bool app_book_set_finished(const char *path, bool finished);
/// 从首页继续阅读隐藏：只影响首页卡带，进度与书架条目保留；再次打开这本书会自动恢复。
/// Hide from the Home continue-reading strip: keeps progress and the shelf entry; opening the book
/// again clears it.
bool app_book_resume_hidden(const char *path);
bool app_book_set_resume_hidden(const char *path, bool hidden);
/// 逐书标记的 RAM 版本号；变化时书架与首页重新取书，不影响内容版本和缓存任务。
/// RAM revision of the per-book marks; a change makes the shelf and Home re-enumerate without
/// touching the content revision or cache jobs.
unsigned app_book_shelf_flag_revision(void);
/// 仅导出已打开阅读上下文。/ Export only an already open reader context.
bool app_book_resume_context(char *path, size_t capacity, bool *fullscreen);
bool app_image_request_open(const char *path);
void app_files_request_folder(int folder);
/// 在文件管理里显示这本书所在的文件夹并翻到它；不能定位时退回文件管理首页。
/// Reveal the book's folder in Files and page to it; fall back to the Files root when it cannot.
void app_files_request_reveal(const char *path);
void app_book_request_manage(void);
bool app_book_reader_body_visible(void);
/// 模式/对比度变化时释放旧格式封面；绘制前由主循环一次调用。/ Drop obsolete cover formats once on mode/contrast changes, before drawing.
void app_book_cover_mode_changed(void);
void app_home_cover_mode_changed(void);
/// 开书前释放可重建的表盘底图，让解析器使用这部分 PSRAM。/ Release rebuildable clock bases before opening a book so parsing can use that PSRAM.
void app_home_release_clock_cache(void);
