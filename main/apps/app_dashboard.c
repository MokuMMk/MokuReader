#ifdef KIIKO_LOCAL_LATENCY
#include "esp_timer.h"
#include "esp_log.h"
#endif
/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 用户修订：快刷只保留 1 位封面，转码后立即释放灰阶；切换模式或对比度清理旧格式。
 * User revision: fast Home retains only a 1-bit cover, releases gray after conversion, and drops stale formats on mode/contrast changes.
 * 用户修订：首页原比例点阵独立落卡，复用灰阶缩略图；普通/水波纹复用灰阶缓存，始终预留内存。
 * User revision: persist proportional Home monochrome separately and reuse gray thumbnail files; ordinary/water reuse gray caches with a memory reserve.
 * 中文：首页使用真实续读、完整缓存书摘、可选书夹/时钟与月阅读记录；绘图不读卡。
 * English: Home uses real resume data, cached complete sentences, selectable folder/clock and monthly records; painting never reads media.
 * 用户修订：缓存原位换位，只用一个条目的栈空间；保留复用速度，避免启动时复制七条占用半个主任务栈。
 * User revision: reorder the cache in place with one stack entry, retaining hot reuse without copying seven entries into half the main stack at boot.
 * 用户最新修订：续读书名复用已加载系统字体，按原40px直接生成字形；不加载阅读字体或增加内建字库。
 * Latest user revision: resume titles reuse the loaded system face at the original 40px, without loading the reader face or adding built-in fonts.
 * 用户修订：首页静止时按分钟局部更新时间，不扫描图书或重画整页；保留快刷白底。
 * User revision: locally advance the idle clock by minute without scanning books or repainting the page; retain the fast white background.
 * 用户修订：普通/水波纹续读卡有浅灰底，三模式结构边线加粗；快刷组件预先二值化且不增加补刷。
 * User revision: ordinary/water resume cards have a light-gray fill and thicker rules in all modes; fast widgets are pre-binarized without an extra refresh.
 * 用户修订：Kehai秒针保持未变化的表盘像素，避免灰底形成矩形黑闪；快刷只驱动黑白圆点差分。
 * User revision: Kehai seconds hold unchanged face pixels to avoid rectangular gray-background flashes; fast mode drives binary dot differences only.
 * 用户修订：首页题头与书架、文件和设置共用左侧起点、顶边及52px名义字号，仍使用当前系统字体。
 * User revision: Home's title matches the shelf/files/settings left edge, top and nominal 52px size in the active system face.
 * 用户修订：续读卡「…」菜单接入标记已读完、在文件中显示、从继续阅读移除；三种动作都不删文件也不清进度。
 * User revision: the resume card's "..." menu marks finished, reveals the book in Files and removes it from
 * continue reading; none of the three deletes files or clears progress.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <esp_timer.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>

#include "app.h"
#include "app_content_open.h"
#include "boot_state.h"
#include "app_registry.h"
#include "book_cover.h"
#include "book_cover_jobs.h"
#include "ui_image_dither.h"
#include "book_epub.h"
#include "book_progress.h"
#include "book_store.h"
#include "book_title.h"
#include "book_ticket.h"
#include "book_excerpt.h"
#include "ui_home_widget.h"
#include "ui_home_clock.h"
#include "ui_wallpaper.h"
#include "ui_avatar.h"
#include "read_pico_pmu.h"
#include "read_pico_crashlog.h"
#include <math.h>
#include "display.h"
#include "display_main_waveform.h"
#include "e0470_epaper_waveform.h"
#include "esp_heap_caps.h"
#include "read_pico_sd.h"
#include "settings.h"
#include "ttf_font.h"
#include "ui_gesture.h"
#include "ui_kit.h"
#include "ui_nav.h"

typedef struct {
    char path[BOOK_STORE_PATH_MAX];
    char title[128];
    char author[128];
    uint8_t *cover;
    bool cover_fast;
    uint16_t cover_width,cover_height;
    time_t modified;
    book_progress_t progress;
    bool has_progress;
    // 书架标记：已读完、或用户把这本书移出了继续阅读；两种都不进首页卡带。
    // Shelf marks: marked finished, or removed from continue reading; neither enters the Home strip.
    bool finished, resume_hidden;
} home_book_t;

static home_book_t s_current, s_recent[6];
typedef struct {
    char path[BOOK_STORE_PATH_MAX];
    char title[128];
    char author[128];
    off_t size;
    time_t modified;
    uint8_t *pixels;
    bool fast;
    uint16_t width,height;
} home_cover_cache_t;
static home_cover_cache_t s_cover_cache[7];
#define HOME_COVER_RESERVE (512u * 1024u)
static int s_cover_next;
static bool s_scan_pending;
static bool s_books_cache_valid;
static bool s_cached_sd_present;
static bool s_cached_sd_mounted;
static unsigned s_cached_store_revision;
// 逐书标记的 RAM 版本；标记变化同样只重扫首页，不动内容版本和缓存任务。
// RAM revision of the per-book marks; a mark change only rescans Home, never the content revision
// or the cache jobs.
static unsigned s_cached_flag_revision;
static char s_cached_books_dir[128];
static char s_cached_last_path[BOOK_STORE_PATH_MAX];
static EpdRect s_area;
static bool s_clock_pending;
static int64_t s_clock_minute=-2;
static uint32_t s_reading_days[31];
static struct tm s_date;
static enum { HOME_ROOT,HOME_COMPONENT,HOME_GOAL,HOME_TIME } s_view;
static app_home_component_t s_component_browse;
static bool s_component_pending;
static const app_home_component_t s_components[]={APP_HOME_MOON,APP_HOME_CLOCK,APP_HOME_KEHAI,APP_HOME_CAT,APP_HOME_DALI,APP_HOME_ASTERISK,APP_HOME_HOUSE,APP_HOME_FOLDER};
#define HOME_COMPONENT_OPTIONS (sizeof(s_components)/sizeof(s_components[0]))
static bool s_time_pending;
static struct tm s_time_date;
static int64_t s_time_stamp=-1;
static bool s_widget_pending;
static struct tm s_widget_date,s_widget_shown;
static int64_t s_widget_stamp=-1;
static unsigned s_widget_phase,s_widget_phase_shown,s_time_phase,s_time_phase_shown;
static int64_t s_motion_ms=-100;
typedef struct {unsigned start,last,steps;int64_t stamp;int view;bool valid;} clock_trail_t;
static clock_trail_t s_widget_trail,s_time_trail;
static uint8_t s_goal_draft;
static bool s_save_error,s_folder_attempted;
static uint8_t *s_folder;
static bool s_folder_fast;
static uint32_t s_folder_key;
// 设计稿首页：大标题、继续阅读、右上头像和一条可左右滑动的续读卡带；卡带下面是原来的时钟与日历。
// Design home: a large title, the continue-reading lead, a top-right avatar and a swipeable resume-card
// strip; the original clock and calendar stay below the strip.
#define HOME_TITLE_X 63
#define HOME_TITLE_CENTER_Y 111
#define HOME_TITLE_PX 72
#define HOME_LEAD_X 63
#define HOME_LEAD_CENTER_Y 184
#define HOME_LEAD_PX 48
#define HOME_AVATAR_X 546   // 与设置页共用 82 槽位，圆心不变 / shares the settings page's 82 slot, same centre
#define HOME_AVATAR_Y 71
#define HOME_AVATAR_SIDE 82  // ui_avatar 只支持 60/82 两档，75 拿不到槽位 / ui_avatar only has 60 and 82; 75 has no slot
#define HOME_CARD_X 69
#define HOME_CARD_Y 231
#define HOME_CARD_W 344
#define HOME_CARD_H 451
#define HOME_CARD_GAP 60
#define HOME_CARD_PITCH (HOME_CARD_W + HOME_CARD_GAP)
// 滚到尽头时最后一张卡的右边缘离屏幕右边留这么多，不要贴死。
// At the end the last card keeps this much clear of the screen's right edge instead of butting
// against it.
#define HOME_CARD_RIGHT_GAP 24
#define HOME_CARD_RADIUS 25
// 卡带一次显示一本，左右滑动换下一本；第三本滑到头就不再前进。
// The strip shows one book at a time and swipes to the next; the third book is the last stop.
#define HOME_CARDS 3
// 封面从卡顶偏移 271 起被白色渐隐覆盖，到 418 已是纯白，书名和作者因此落在白底上。
// The cover is covered by a white bloom from 271 px below the card top and is fully white by 418,
// which is the white background the title and author sit on.
#define HOME_CARD_BLOOM_TOP 271
#define HOME_CARD_BLOOM_SPAN 147
#define HOME_CARD_TITLE_CENTER_Y 616
#define HOME_CARD_AUTHOR_CENTER_Y 654
#define HOME_CARD_DOTS_CENTER_X 296
#define HOME_CARD_ACTION_CENTER_Y 654
// 卡片菜单（“…”）：设计稿 304×191，三行各 53，上下留白 16。
// The card menu ("..."): the design's 304x191, three 53 px rows with 16 px above and below.
#define HOME_MENU_W 304
#define HOME_MENU_ROW_H 53
#define HOME_MENU_PAD_V 16
#define HOME_MENU_PAD_H 25
#define HOME_MENU_H (2 * HOME_MENU_PAD_V + 3 * HOME_MENU_ROW_H)
#define HOME_MENU_RADIUS 37
// 卡带下面是原来的组件与日历：整块下移 56px 让开卡片，内部几何一点没改。
// The original component and calendar sit below the strip, shifted down 56 px to clear the cards;
// nothing inside them changes.
#define HOME_BAND_DY 56
#define HOME_MENU_TOP 627
static uint8_t s_avatar[HOME_AVATAR_SIDE*HOME_AVATAR_SIDE];
static bool s_avatar_ready,s_avatar_pending;
static char s_avatar_path[288];
static EpdRect s_pressed;
static bool s_press_pending;
// 续读卡只显示最近三本有进度的书；没进度时不占位。
// The strip shows the three most recent books with progress and leaves no placeholder when there are none.
static int s_card_page; // 焦点卡号，由偏移量导出 / focused card, derived from the offset
static bool s_card_menu;
// 卡带自由滑动：偏移量是连续像素，松手后按测得的速度做惯性，不再一格一格吸附。
// s_card_page 只用来标记「当前焦点是哪一张」，给圆点和卡片菜单用。
// The strip scrolls freely: the offset is continuous pixels and releasing applies the measured
// velocity as inertia instead of snapping card by card. s_card_page only marks which card is
// focused, for the dots and the card menu.
static int s_card_off;         // 当前横向偏移 / current horizontal offset
static int s_card_off_max;     // 允许的最大偏移 / largest allowed offset
static bool s_card_drag;       // 手指正在拖卡带 / the finger is dragging the strip
static int s_card_from;        // 按下时的偏移 / offset at press
static int s_card_flick;       // 剩余惯性速度，像素每秒 / remaining inertia in px per second
static int s_card_flick_ms;    // 惯性上一帧的时刻 / last inertia step time
static int s_card_last_x;      // 速度采样：上次 x / last sampled x
static int s_card_to_x;        // 拖动中手指最新 x / latest finger x while dragging
static int s_card_dir;         // 1 向左（下一张）/-1 向右 / 1 leftwards (next card), -1 rightwards
static int64_t s_card_last_ms; // 速度采样：上次时刻 / last sampled time
static EpdRect card_rect(int page, int index) {
    (void)page;
    return (EpdRect){HOME_CARD_X + index * HOME_CARD_PITCH - s_card_off,
                     HOME_CARD_Y, HOME_CARD_W, HOME_CARD_H};
}
/// 最大偏移：滚到尽头时最后一张卡的右边缘贴住屏幕右边，后面不留空白。
/// Largest offset: at the end the last card's right edge sits on the screen edge with no blank
/// after it.
static int card_max_off(unsigned count) {
    if (count < 2) return 0;
    const int max = HOME_CARD_X + (int)(count - 1) * HOME_CARD_PITCH + HOME_CARD_W
                    - UI_LOCK_WIDTH + HOME_CARD_RIGHT_GAP;
    return max > 0 ? max : 0;
}
/// 偏移量到焦点卡号：哪张卡盖住设计位置就算哪张。
/// Offset to the focused card: whichever card covers the design position.
static int card_focus(unsigned count) {
    if (!count) return 0;
    int index = (s_card_off + HOME_CARD_PITCH / 2) / HOME_CARD_PITCH;
    if (index < 0) index = 0;
    if (index >= (int)count) index = (int)count - 1;
    return index;
}
static EpdRect card_strip_area(void);
static unsigned home_cards(home_book_t *books[HOME_CARDS]);
static void render_card(uint8_t *fb, const home_book_t *item, int page, int index, bool focused);
/// 松手后落位：一次滑动固定翻一张卡，两端夹紧。这段落位与 on_tick 里的其他时钟逻辑
/// 分开，便于调试和宿主测试单独验证边界。
/// Release landing: one swipe advances exactly one card and clamps at both ends. Keeping it apart
/// from the clock work in on_tick makes the boundary checkable on the host.
static int strip_release_offset(void) {
    int next=s_card_from+(s_card_dir>0?HOME_CARD_PITCH:-HOME_CARD_PITCH);
    if(next<0)next=0;
    if(next>s_card_off_max)next=s_card_off_max;
    return next;
}
/// 只重画卡带那一条。APP_REDRAW_AREA 的契约是回调先画好 fb、主循环只推 area_hint()，
/// 所以拖动和惯性每帧都必须先调它，否则偏移量在变、推上去的却还是旧画面。
/// Repaint only the strip band. APP_REDRAW_AREA means the callback paints the framebuffer and the
/// loop only presents the hint, so every drag and inertia frame must call this first, or the offset
/// changes while the old pixels are what actually gets pushed.
static void strip_paint(app_ctx_t *ctx) {
    home_book_t *cards[HOME_CARDS];unsigned count=home_cards(cards);
    s_card_off_max=card_max_off(count);
    if(s_card_off>s_card_off_max)s_card_off=s_card_off_max;
    if(s_card_off<0)s_card_off=0;
    s_card_page=card_focus(count);
    ui_fill_rect_fast(card_strip_area(),UI_GRAY_WHITE,ctx->fb);
    for(unsigned i=0;i<count;++i) {
        const EpdRect card=card_rect(s_card_page,(int)i);
        if(card.x>=UI_LOCK_WIDTH||card.x+card.width<=0)continue;
        render_card(ctx->fb,cards[i],s_card_page,(int)i,i==(unsigned)s_card_page);
    }
}
static EpdRect card_hit_rect(int page, int index) {
    EpdRect r = card_rect(page, index);
    return (EpdRect){r.x - 8, r.y - 8, r.width + 16, r.height + 16};
}
/// 局部更新整条卡带，卡片左右移动时必须整条重画。
/// Refresh the whole strip: cards move horizontally, so the full band repaints.
static EpdRect card_strip_area(void) {
    return (EpdRect){0, HOME_CARD_Y - 8, UI_LOCK_WIDTH, HOME_CARD_H + 16};
}
static EpdRect dots_hit_rect(int page, int index) {
    const EpdRect card = card_rect(page, index);
    return (EpdRect){card.x + HOME_CARD_DOTS_CENTER_X - 32, HOME_CARD_ACTION_CENTER_Y - 32, 64, 64};
}
static EpdRect card_menu_rect(int page, int index) {
    const EpdRect card = card_rect(page, index);
    int left = card.x + HOME_CARD_DOTS_CENTER_X - 13;
    if (left + HOME_MENU_W > UI_LOCK_WIDTH) left = UI_LOCK_WIDTH - HOME_MENU_W;
    if (left < 0) left = 0;
    return (EpdRect){left, HOME_MENU_TOP, HOME_MENU_W, HOME_MENU_H};
}
static EpdRect control_feedback_area(EpdRect control) { return control; }
static bool s_reading_days_valid;
static bool s_home_force_full_once;
static uint32_t s_cached_progress_revision=UINT32_MAX;
static uint8_t *s_clock_base_cache;
static bool s_clock_base_attempted;
void app_home_release_clock_cache(void) {
    ui_home_clock_bind_base_cache(NULL,0);
    free(s_clock_base_cache);s_clock_base_cache=NULL;s_clock_base_attempted=false;
}
static void clock_base_prepare(void){
    if(s_clock_base_cache||s_clock_base_attempted||app_settings_home_component()!=APP_HOME_KEHAI)return;
    s_clock_base_attempted=true;
    if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)<UI_HOME_CLOCK_BASE_CACHE_BYTES+2u*1024u*1024u)return;
    s_clock_base_cache=heap_caps_malloc(UI_HOME_CLOCK_BASE_CACHE_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    ui_home_clock_bind_base_cache(s_clock_base_cache,s_clock_base_cache?UI_HOME_CLOCK_BASE_CACHE_BYTES:0);
}

static home_book_t *book_at(int index);

static bool is_book(const char *name) {
    if (!name[0] || name[0] == '.' || !strncmp(name, "._", 2)) return false;
    const char *dot = strrchr(name, '.');
    return dot && (!strcasecmp(dot, ".epub") || !strcasecmp(dot, ".txt"));
}
static void copy_title(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n && ((unsigned char)src[n] & 0xc0) == 0x80) --n;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

static void title_for(home_book_t *item) {
    if (!book_title_from_path(item->path, item->title, sizeof(item->title)))
        copy_title(item->title, sizeof(item->title), "未命名");
}

static void consider_book(const char *path, const struct stat *st) {
    if (s_current.path[0] && !strcmp(path, s_current.path)) return;
    book_progress_t progress = {0};
    bool has_progress = st->st_size >= 0 && (uint64_t)st->st_size <= UINT32_MAX &&
                        book_progress_load(path, (uint32_t)st->st_size, &progress);
    for (int i=0;i<6;++i) if (!strcmp(path,s_recent[i].path)) return;
    uint64_t score = has_progress ? (UINT64_C(1) << 32) + progress.last_open_s : (uint64_t)st->st_mtime;
    int slot = -1;
    for (int i = 0; i < 6; ++i) {
        uint64_t other = s_recent[i].has_progress ?
            (UINT64_C(1) << 32) + s_recent[i].progress.last_open_s : (uint64_t)s_recent[i].modified;
        if (!s_recent[i].path[0] || score > other) { slot = i; break; }
    }
    if (slot < 0) return;
    for (int i = 5; i > slot; --i) s_recent[i] = s_recent[i - 1];
    memset(&s_recent[slot], 0, sizeof(s_recent[slot]));
    snprintf(s_recent[slot].path, sizeof(s_recent[slot].path), "%s", path);
    s_recent[slot].modified = st->st_mtime;
    s_recent[slot].has_progress = has_progress;
    if (has_progress) s_recent[slot].progress = progress;
}

static void scan_root(const char *root) {
    DIR *dir = opendir(root);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!is_book(entry->d_name)) continue;
        char path[BOOK_STORE_PATH_MAX];
        if (snprintf(path, sizeof(path), "%s/%s", root, entry->d_name) >= sizeof(path)) continue;
        struct stat st;
        if (!stat(path, &st) && S_ISREG(st.st_mode)) consider_book(path, &st);
    }
    closedir(dir);
}

static bool recent_visit(const char *path,const book_progress_t *progress,void *ctx) {
    (void)ctx;
    struct stat st;
    if (is_book(path) && !stat(path,&st) && S_ISREG(st.st_mode) && st.st_size>=0 && (uint64_t)st.st_size==progress->file_size)
        consider_book(path,&st);
    return true;
}
static void scan_books(void) {
    char last[BOOK_STORE_PATH_MAX];
    if (book_progress_last_path(last, sizeof(last))) {
        struct stat st;
        if (!stat(last, &st) && S_ISREG(st.st_mode) && st.st_size >= 0 && (uint64_t)st.st_size <= UINT32_MAX) {
            snprintf(s_current.path, sizeof(s_current.path), "%s", last);
            s_current.has_progress = book_progress_load(last, (uint32_t)st.st_size, &s_current.progress);
            title_for(&s_current);
        }
    }
    (void)book_progress_list(recent_visit,NULL);
    read_pico_sd_info_t sd = {0};
    read_pico_sd_get_info(&sd);
    if (sd.mounted) {
        if (!strcmp(app_settings_books_dir(), "/sdcard/books")) scan_root("/sdcard/book");
        scan_root(app_settings_books_dir());
    }
    if (book_store_flash_ready()) scan_root("/flash/books");
    for (int i=0;i<7;++i) {
        home_book_t *b=book_at(i);
        if (!b->path[0]) continue;
        // 标记只在扫描时读一次：首页每帧都要取卡，不能逐帧问 NVS。
        // Read the marks once per scan: Home picks cards every frame and must not query NVS there.
        b->finished=app_book_finished(b->path);
        b->resume_hidden=app_book_resume_hidden(b->path);
        title_for(b);
        char metadata_title[128];
        if (book_epub_metadata_cached(b->path,metadata_title,sizeof(metadata_title),b->author,sizeof(b->author))==ESP_OK && metadata_title[0])
            copy_title(b->title,sizeof(b->title),metadata_title);
    }
    home_book_t *featured = s_current.path[0] ? &s_current : &s_recent[0];
    const char *ext = strrchr(featured->path, '.');
    if (ext && !strcasecmp(ext, ".epub") && pico_boot_asset_allowed(featured->path)) {
        char metadata_title[256];
        if (book_epub_metadata(featured->path, metadata_title, sizeof(metadata_title),
                               featured->author, sizeof(featured->author)) != ESP_OK)
            featured->author[0] = 0;
        else if (metadata_title[0]) copy_title(featured->title,sizeof(featured->title),metadata_title);
    }
}

static void refresh_progress(void) {
    for (int i = 0; i < 7; ++i) {
        home_book_t *item = book_at(i);
        struct stat st;
        if (!item->path[0] || stat(item->path, &st) || !S_ISREG(st.st_mode) ||
            st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX) continue;
        item->has_progress = book_progress_load(item->path, (uint32_t)st.st_size, &item->progress);
    }
}

static void remember_scan_state(const read_pico_sd_info_t *sd) {
    s_books_cache_valid = true;
    s_cached_store_revision = book_store_revision();
    s_cached_flag_revision = app_book_shelf_flag_revision();
    s_cached_sd_present = sd->present;
    s_cached_sd_mounted = sd->mounted;
    snprintf(s_cached_books_dir, sizeof(s_cached_books_dir), "%s", app_settings_books_dir());
    s_cached_last_path[0] = 0;
    (void)book_progress_last_path(s_cached_last_path, sizeof(s_cached_last_path));
}

static home_book_t *book_at(int index) { return index ? &s_recent[index - 1] : &s_current; }
static void clear_cover_cache(void) {
    free(s_folder);s_folder=NULL;s_folder_key=0;s_folder_attempted=false;
    for (int i = 0; i < 7; ++i) {
        free(s_cover_cache[i].pixels);
        memset(&s_cover_cache[i], 0, sizeof(s_cover_cache[i]));
    }
    s_current.cover = NULL;
    for (int i = 0; i < 6; ++i) s_recent[i].cover = NULL;
}

void app_home_cover_mode_changed(void) {
    clear_cover_cache();
    s_cover_next = 0;
}
static void restore_cover_cache(void) {
    bool used[7] = {0};
    int match[7];
    for (int i = 0; i < 7; ++i) {
        home_book_t *book = book_at(i);
        struct stat st;
        book->cover = NULL;
        match[i] = -1;
        if (!book->path[0] || stat(book->path, &st) || !S_ISREG(st.st_mode)) continue;
        for (int j = 0; j < 7; ++j) {
            home_cover_cache_t *cached = &s_cover_cache[j];
            if (used[j] || !cached->pixels || strcmp(book->path, cached->path) ||
                strcmp(book->title, cached->title) || strcmp(book->author, cached->author) ||
                st.st_size != cached->size || st.st_mtime != cached->modified) continue;
            match[i] = j;
            used[j] = true;
            break;
        }
    }
    for (int j = 0; j < 7; ++j) if (!used[j]) {
        free(s_cover_cache[j].pixels);
        memset(&s_cover_cache[j], 0, sizeof(s_cover_cache[j]));
    }
    for (int i = 0; i < 7; ++i) {
        int j = match[i];
        if (j < 0) {
            for (j = i; j < 7 && s_cover_cache[j].pixels; ++j) {}
        }
        if (j < 7 && j != i) {
            home_cover_cache_t swap = s_cover_cache[i];
            s_cover_cache[i] = s_cover_cache[j];
            s_cover_cache[j] = swap;
            for (int k = i + 1; k < 7; ++k) {
                if (match[k] == i) match[k] = j;
                else if (match[k] == j) match[k] = i;
            }
        }
        book_at(i)->cover = s_cover_cache[i].pixels;
        book_at(i)->cover_fast = s_cover_cache[i].fast;
        book_at(i)->cover_width=s_cover_cache[i].width;
        book_at(i)->cover_height=s_cover_cache[i].height;
    }
}

static unsigned s_cover_generation;
static bool s_cover_waiting;
static bool load_cover(home_book_t *item,int slot) {
    s_cover_waiting=false;
    if(!pico_boot_asset_allowed(item->path))return false;
    book_cover_job_t request={.list=true,.natural=true,.fast=app_settings_main_fast_refresh(),
        .contrast=app_settings_system_contrast(),.phase=17};
    snprintf(request.path,sizeof(request.path),"%s",item->path);
    snprintf(request.title,sizeof(request.title),"%s",item->title);
    snprintf(request.author,sizeof(request.author),"%s",item->author);
    if(!s_cover_generation)s_cover_generation=book_cover_jobs_begin();
    if(!book_cover_jobs_submit(s_cover_generation,slot,&request)){
        s_cover_generation=book_cover_jobs_begin();
        if(!book_cover_jobs_submit(s_cover_generation,slot,&request))return false;
    }
    book_cover_result_t result={0};int state=book_cover_jobs_take(s_cover_generation,slot,&result);
    if(!state){s_cover_waiting=true;return false;}if(state<0)return false;
    home_cover_cache_t *cached=&s_cover_cache[slot];free(cached->pixels);
    snprintf(cached->path,sizeof(cached->path),"%s",item->path);
    snprintf(cached->title,sizeof(cached->title),"%s",item->title);
    snprintf(cached->author,sizeof(cached->author),"%s",item->author);
    cached->size=result.size;cached->modified=result.modified;cached->pixels=result.pixels;
    cached->fast=result.fast;cached->width=result.width;cached->height=result.height;
    item->cover=result.pixels;item->cover_fast=result.fast;item->cover_width=result.width;item->cover_height=result.height;
    return true;
}

static void label(uint8_t *fb,int x,int y,int px,const char *text,enum EpdFontFlags align,uint8_t gray) {
    if (gray >= 0xf0) ui_text_fixed_vc(fb,x,y,px,text,align,true);
    else ui_text_fixed_ink_vc(fb,x,y,px,text,align,gray);
}
static bool date_now(struct tm *out) {
    time_t now=time(NULL);
    if(now<1704067200) { const pmu_snapshot_t *pmu=read_pico_pmu_get(); if(pmu&&pmu->time_synced)now=pmu->unix_sec; }
    if(now<1704067200) {memset(out,0,sizeof(*out));return false;}
    now+=8*3600;gmtime_r(&now,out);return true;
}
static int64_t date_stamp(const struct tm *d) {return ((int64_t)d->tm_year*366+d->tm_yday)*86400+d->tm_hour*3600+d->tm_min*60+d->tm_sec;}
static unsigned clock_phase_now(struct tm *date) {
    struct timeval now;
    if(!gettimeofday(&now,NULL)&&now.tv_sec>=1704067200){time_t local=now.tv_sec+8*3600;gmtime_r(&local,date);return date->tm_sec*10+(unsigned)now.tv_usec/100000;}
    return (unsigned)date->tm_sec*10;
}
static bool clock_motion_due(app_ctx_t *ctx){return ctx->now_ms<=0||ctx->now_ms<s_motion_ms||ctx->now_ms-s_motion_ms>=100;}
static unsigned component_index(app_home_component_t component) {
    for(unsigned i=0;i<HOME_COMPONENT_OPTIONS;++i)if(s_components[i]==component)return i;
    return 0;
}
static ui_home_clock_style_t clock_style(app_home_component_t component) {
    // 浏览顺序独立于稳定设置编号，避免旧选择错配。/ Keep browsing order separate from persisted IDs.
    switch(component){
        case APP_HOME_MOON:return UI_HOME_CLOCK_MOON;
        case APP_HOME_KEHAI:return UI_HOME_CLOCK_KEHAI;
        case APP_HOME_CAT:return UI_HOME_CLOCK_CAT;
        case APP_HOME_DALI:return UI_HOME_CLOCK_DALI;
        case APP_HOME_ASTERISK:return UI_HOME_CLOCK_ASTERISK;
        case APP_HOME_HOUSE:return UI_HOME_CLOCK_HOUSE;
        default:return UI_HOME_CLOCK_PS1995;
    }
}
static void clock_draw_region(uint8_t *fb,EpdRect bounds,const struct tm *date,bool binary,ui_home_clock_style_t style,EpdRect clip,unsigned phase){
    clock_trail_t *trail=s_view==HOME_TIME?&s_time_trail:&s_widget_trail;
    int64_t stamp=date_stamp(date);
    if(style!=UI_HOME_CLOCK_KEHAI){trail->valid=false;ui_home_clock_phase_draw_region(fb,bounds,date,binary,style,clip,phase);return;}
    if(!trail->valid||trail->view!=s_view||stamp<trail->stamp){*trail=(clock_trail_t){.start=phase,.last=phase,.stamp=stamp,.view=s_view,.valid=true};}
    else {unsigned added=(phase+600-trail->last)%600;trail->steps=stamp-trail->stamp>=60?600:trail->steps+added;if(trail->steps>600)trail->steps=600;trail->last=phase;}
    // 保留同一页面上的圆环，分钟更新只重画表盘目标；离页重新开始。
    // Retain the ring on this page through minute hand updates; start anew on a view change.
    ui_home_clock_trail_draw_region(fb,bounds,date,binary,style,clip,phase,trail->start,trail->steps);
}
static const char *component_name(app_home_component_t component) {return component==APP_HOME_FOLDER?"最近阅读书夹":ui_home_clock_name(clock_style(component));}
static EpdRect time_clock_bounds(void) {return(EpdRect){122,275,460,503};}
static EpdRect component_clock_bounds(void){return s_view==HOME_COMPONENT?(EpdRect){141,294,420,460}:(EpdRect){24,640+HOME_BAND_DY,320,350};}
static EpdRect component_clock_graphics(void){return s_view==HOME_COMPONENT?component_clock_bounds():(EpdRect){24,640+HOME_BAND_DY,320,326};}
static bool visible_motion_clock(void){return(s_view==HOME_ROOT&&ui_home_clock_has_motion(clock_style(app_settings_home_component())))||(s_view==HOME_COMPONENT&&ui_home_clock_has_motion(clock_style(s_component_browse)));}
static void refresh_days(void) {
    s_reading_days_valid=date_now(&s_date)&&book_ticket_month_days(s_date.tm_year+1900,s_date.tm_mon+1,s_reading_days);
    if(!s_reading_days_valid)memset(s_reading_days,0,sizeof(s_reading_days));
}
static unsigned today_seconds(void) { return s_reading_days_valid?s_reading_days[s_date.tm_mday-1]:0; }
static unsigned month_length(const struct tm *d) {
    static const uint8_t days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    unsigned year=d->tm_year+1900;
    return days[d->tm_mon]+(d->tm_mon==1&&year%4==0&&(year%100!=0||year%400==0));
}
static void ring(uint8_t *fb,int cx,int cy,int radius,int thick,uint32_t seconds,unsigned goal) {
    const float fraction=goal?fminf(1,seconds/(float)goal):0;
    for(int y=-radius;y<=radius;++y)for(int x=-radius;x<=radius;++x) {
        float r=hypotf(x,y);if(r>radius||r<radius-thick)continue;
        float a=atan2f(x,-y);if(a<0)a+=6.2831853f;
        epd_draw_pixel(cx+x,cy+y,a<=fraction*6.2831853f?0x20:0xe0,fb);
    }
    if(fraction>0&&fraction<1) {
        float angle=fraction*6.2831853f;int r=radius-thick/2;
        epd_fill_circle(cx,cy-r,thick/2,0x20,fb);
        epd_fill_circle(cx+lroundf(sinf(angle)*r),cy-lroundf(cosf(angle)*r),thick/2,0x20,fb);
    }
}
static void calendar(uint8_t *fb,int x,int y,int step_x,int step_y,int px,unsigned goal) {
    static const char *const weekdays[]={"日","一","二","三","四","五","六"};
    if(!s_reading_days_valid) {label(fb,x+3*step_x,y+100,20,"请先设置日期与时间",EPD_DRAW_ALIGN_CENTER,0x60);return;}
    unsigned start=(s_date.tm_wday+7-(s_date.tm_mday-1)%7)%7;
    for(int i=0;i<7;++i)label(fb,x+i*step_x,y-step_y,20,weekdays[i],EPD_DRAW_ALIGN_CENTER,0x70);
    for(unsigned day=1;day<=month_length(&s_date);++day) {
        unsigned cell=start+day-1;int cx=x+(cell%7)*step_x,cy=y+(cell/7)*step_y;
        bool today=day==(unsigned)s_date.tm_mday;char value[4];snprintf(value,sizeof(value),"%u",day);
        if(today)epd_fill_circle(cx,cy,px/2+3,0x20,fb);
        label(fb,cx,cy,px,value,EPD_DRAW_ALIGN_CENTER,today?0xff:day>(unsigned)s_date.tm_mday?0x90:0x20);
        if(s_reading_days[day-1]>=goal && goal)ui_fill_rect_fast((EpdRect){cx-10,cy+px/2+9,20,2},0x30,fb);
    }
}
static home_book_t *featured(void) {return s_current.path[0]?&s_current:&s_recent[0];}
// 设计稿续读卡：封面按 cover 居中裁切铺满整张卡，底部叠白色渐隐，书名和作者压在渐隐上。
// Design resume card: the cover is centre-cropped to fill the card and a white bloom covers the bottom,
// with the title and author sitting on that bloom.
static void draw_card_cover(uint8_t *fb,const home_book_t *item,const EpdRect card) {
    if(!item->cover||!item->cover_width||!item->cover_height) {ui_fill_round_rect(fb,card,HOME_CARD_RADIUS,0xe0);return;}
    const unsigned w=item->cover_width,h=item->cover_height;
    // 取源图里与卡片同比例的最大内接矩形，居中：源图更宽就裁宽，更高就裁高。
    // 裁出来的边长一定不超过源图，否则取样会越过封面缓存。
    // Take the largest source rect matching the card ratio, centred: crop the width when the source is
    // wider, the height when it is taller. Each kept edge stays within the source, or sampling would
    // run past the cover buffer.
    unsigned cw=w,ch=h;
    if((uint64_t)w*card.height>(uint64_t)h*card.width)cw=(unsigned)((uint64_t)h*card.width/card.height);
    else ch=(unsigned)((uint64_t)w*card.height/card.width);
    if(!cw)cw=1;
    if(!ch)ch=1;
    if(cw>w)cw=w;
    if(ch>h)ch=h;
    const unsigned ox=(w-cw)/2,oy=(h-ch)/2;
    // 每行只算一次行偏移和渐隐权重，列映射预存一次，避免逐像素除法。
    // Per row compute the source row and bloom weight once, and precompute the column map, so no
    // division runs per pixel.
    static int column[HOME_CARD_W];
    for(int x=0;x<HOME_CARD_W;++x)column[x]=(int)(ox+(unsigned)x*cw/HOME_CARD_W);
    for(int y=0;y<HOME_CARD_H;++y) {
        const int dy=y<HOME_CARD_RADIUS?HOME_CARD_RADIUS-y:y>=HOME_CARD_H-HOME_CARD_RADIUS?y-(HOME_CARD_H-HOME_CARD_RADIUS-1):0;
        int alpha=(y-HOME_CARD_BLOOM_TOP)*255/HOME_CARD_BLOOM_SPAN;
        if(alpha<0)alpha=0;else if(alpha>255)alpha=255;
        const unsigned sy=oy+(unsigned)y*ch/HOME_CARD_H;
        for(int x=0;x<HOME_CARD_W;++x) {
            const int dx=x<HOME_CARD_RADIUS?HOME_CARD_RADIUS-x:x>=HOME_CARD_W-HOME_CARD_RADIUS?x-(HOME_CARD_W-HOME_CARD_RADIUS-1):0;
            if(dx*dx+dy*dy>HOME_CARD_RADIUS*HOME_CARD_RADIUS)continue;
            const unsigned sx=(unsigned)column[x];
            uint8_t g=item->cover_fast?(item->cover[sy*((w+7)/8)+sx/8]&(1u<<(sx&7))?255:0)
                                      :ui_image_dither_gray(ui_contrast_gray(item->cover[sy*w+sx]),card.x+x,card.y+y);
            if(alpha)g=(uint8_t)((g*(255-alpha)+255*alpha)/255);
            epd_draw_pixel(card.x+x,card.y+y,g,fb);
        }
    }
}
// 单行省略：按可用宽度从尾部逐个字符裁掉，再补省略号。
// Single-line ellipsis: drop whole characters from the tail until the text fits, then append the mark.
static void card_text(char *out,size_t cap,int px,const char *src,int width) {
    snprintf(out,cap,"%s",src);
    if(ui_text_fixed_width_px(px,out)<=width)return;
    const int mark=ui_text_fixed_width_px(px,"…");
    while(out[0]) {
        size_t n=strlen(out)-1;
        while(n&&((unsigned char)out[n]&0xc0)==0x80)--n;
        out[n]=0;
        if(ui_text_fixed_width_px(px,out)+mark<=width)break;
    }
    strlcat(out,"…",cap);
}
static void render_card(uint8_t *fb,const home_book_t *item,int page,int index,bool focused) {
    const EpdRect card=card_rect(page,index);
    if(card.x>=UI_LOCK_WIDTH||card.x+card.width<=0)return;
    draw_card_cover(fb,item,card);
    char text[128];
    if(item->title[0]) {
        card_text(text,sizeof(text),24,item->title,HOME_CARD_W-34);
        ui_text_fixed_context_aa_vc(fb,card.x+17,HOME_CARD_TITLE_CENTER_Y,24,text,item->title,EPD_DRAW_ALIGN_LEFT,0,0xff);
    }
    if(item->author[0]) {
        card_text(text,sizeof(text),18,item->author,190);
        ui_text_fixed_context_aa_vc(fb,card.x+17,HOME_CARD_AUTHOR_CENTER_Y,18,text,item->author,EPD_DRAW_ALIGN_LEFT,0,0xff);
    }
    // “…”只出现在当前这一本上：设计稿的卡片菜单就是从它展开的。
    // The "..." appears only on the focused card, which is the one the design's menu opens from.
    if(!focused)return;
    ui_draw_icon(fb,card.x+HOME_CARD_DOTS_CENTER_X,HOME_CARD_ACTION_CENTER_Y,31,UI_ICON_ELLIPSIS,UI_GRAY_BLACK);
}
static unsigned folder_books(home_book_t *books[3]) {
    unsigned n=0;
    if(s_current.path[0]&&s_current.has_progress&&!s_current.finished&&!s_current.resume_hidden)books[n++]=&s_current;
    for(int i=0;i<6&&n<3;++i)if(s_recent[i].path[0]&&s_recent[i].has_progress&&!s_recent[i].finished&&!s_recent[i].resume_hidden)books[n++]=&s_recent[i];
    return n;
}
static uint32_t folder_key(void) {
    home_book_t *books[3];unsigned n=folder_books(books);uint32_t hash=2166136261u;
    for(unsigned i=0;i<n;++i) {
        for(const char *p=books[i]->path;*p;++p)hash=(hash^(uint8_t)*p)*16777619u;
        struct stat st;
        if(!stat(books[i]->path,&st)) { hash=(hash^(uint32_t)st.st_size)*16777619u;hash=(hash^(uint32_t)st.st_mtime)*16777619u; }
    }
    hash=(hash^n)*16777619u;hash=(hash^app_settings_main_fast_refresh())*16777619u;
    return (hash^app_settings_system_contrast())*16777619u;
}
/// 首页卡带的选书：先放有阅读进度的书，不够三本就用最近扫描到的书补齐。
/// 只按进度取会让刚开机、还没读过任何书时整条卡带空着，封面自然也就一张都看不到。
/// The home strip pick: books with reading progress first, then the most recently seen books to fill
/// up to three. Progress alone would leave the whole strip empty on a fresh device, so no cover at all.
static unsigned home_cards(home_book_t *books[HOME_CARDS]) {
    unsigned n=folder_books(books);
    home_book_t *all[HOME_CARDS+1];
    all[0]=&s_current;
    for(int i=0;i<HOME_CARDS;++i)all[i+1]=&s_recent[i];
    for(int i=0;i<HOME_CARDS+1&&n<HOME_CARDS;++i) {
        home_book_t *b=all[i];
        if(!b->path[0]||b->finished||b->resume_hidden)continue;
        bool seen=false;
        for(unsigned k=0;k<n;++k)if(books[k]==b){seen=true;break;}
        if(!seen)books[n++]=b;
    }
    return n;
}
// 卡片菜单针对的是当前焦点那张卡；卡带为空时没有可操作的书。
// The card menu acts on the focused card; an empty strip has nothing to act on.
static home_book_t *focused_card(void) {
    home_book_t *cards[HOME_CARDS];const unsigned count=home_cards(cards);
    if(s_card_page<0||(unsigned)s_card_page>=count)return NULL;
    return cards[s_card_page];
}
static bool prepare_folder(void) {
    home_book_t *books[3];unsigned n=folder_books(books);ui_home_cover_t covers[3]={0};
    for(unsigned i=0;i<n;++i) {
        covers[i]=(ui_home_cover_t){books[i]->cover,books[i]->cover_width,books[i]->cover_height,
            books[i]->cover_fast?(books[i]->cover_width+7)/8:books[i]->cover_width,books[i]->cover_fast};
    }
    const bool fast=app_settings_main_fast_refresh();
    size_t size=fast?UI_HOME_FOLDER_BW_BYTES:UI_HOME_FOLDER_GRAY_BYTES;
    s_folder_attempted=true;
    if(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)<size+224000+768*1024)return false;
    uint8_t *out=heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    uint8_t *scratch=out?heap_caps_malloc(224000,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT):NULL;
    bool ok=out&&scratch&&ui_home_folder_compose(out,scratch,covers,n,fast);
    free(scratch);
    if(!ok){free(out);return false;}
    free(s_folder);s_folder=out;s_folder_fast=fast;s_folder_key=folder_key();return true;
}
static void avatar_prepare_state(void) {
    const char *path=app_settings_avatar_path();
    if(pico_boot_recovery()||!path[0]){ui_avatar_request("",HOME_AVATAR_SIDE);s_avatar_ready=s_avatar_pending=false;s_avatar_path[0]=0;return;}
    if(strcmp(path,s_avatar_path))s_avatar_ready=false;
    snprintf(s_avatar_path,sizeof(s_avatar_path),"%s",path);
    ui_avatar_request(path,HOME_AVATAR_SIDE);
    ui_avatar_state_t state=ui_avatar_poll(path,HOME_AVATAR_SIDE,s_avatar);
    s_avatar_ready=state==UI_AVATAR_READY;s_avatar_pending=state==UI_AVATAR_PENDING;
}
static void header(uint8_t *fb,const char *title) {
    ui_nav_status(fb);ui_nav_back(fb,36,78);
    char t[96];snprintf(t,sizeof(t),"%s",title);int px=ui_text_title_fit(t,42,530,title);
    ui_text_title_vc(fb,120,113,px,t,title,EPD_DRAW_ALIGN_LEFT);
}
// 卡带下面是原来的组件与日历：整块下移 56px 让开卡片（HOME_BAND_DY），内部几何一点没改。
// The original component and calendar sit below the strip, shifted down by HOME_BAND_DY to clear
// the cards; nothing inside them changes.
static void render_root(uint8_t *fb) {
#ifdef KIIKO_LOCAL_LATENCY
    int64_t probe_home=esp_timer_get_time();
#define HOME_STAGE(name) do { int64_t probe_now=esp_timer_get_time();ESP_LOGI("latency","HOME stage=%s us=%lld",name,(long long)(probe_now-probe_home));probe_home=probe_now; } while(0)
#else
#define HOME_STAGE(name) do {} while(0)
#endif
    // 状态栏沿用原设计：设计稿没有画它，但时间和电量是原有功能。
    // The status bar keeps its original design: the mock-up omits it, yet time and battery remain features.
    ui_nav_status(fb);
    char title[8];snprintf(title,sizeof(title),"主页");
    const int title_px=ui_text_title_fit(title,HOME_TITLE_PX,HOME_AVATAR_X-HOME_TITLE_X-12,title);
    ui_text_title_vc(fb,HOME_TITLE_X,HOME_TITLE_CENTER_Y,title_px,title,title,EPD_DRAW_ALIGN_LEFT);
    ui_text_title_vc(fb,HOME_LEAD_X,HOME_LEAD_CENTER_Y,HOME_LEAD_PX,"继续阅读","继续阅读",EPD_DRAW_ALIGN_LEFT);
    if(s_avatar_ready)ui_wallpaper_draw_avatar(fb,s_avatar,(EpdRect){HOME_AVATAR_X,HOME_AVATAR_Y,HOME_AVATAR_SIDE,HOME_AVATAR_SIDE},HOME_AVATAR_SIDE/2);
    else {
        // 没配头像时照设计稿的 .mk-avatar 画：浅色圆底 + 人形，读起来是“还没有头像”，
        // 而不是一个深色字母章。配色跟设计稿的 --mk-card 底一致。
        // Without a configured avatar follow the design's .mk-avatar: a light circular base with a person
        // glyph, which reads as "no avatar yet" instead of a dark letter stamp. The tone matches the
        // mock-up's --mk-card base.
        const int side=HOME_AVATAR_SIDE;
        ui_fill_round_rect(fb,(EpdRect){HOME_AVATAR_X,HOME_AVATAR_Y,side,side},side/2,0xd0);
        const int head=side*26/100,body=side*46/100;
        const int cx=HOME_AVATAR_X+side/2;
        const int head_top=HOME_AVATAR_Y+side*24/100;
        ui_fill_round_rect(fb,(EpdRect){cx-head/2,head_top,head,head},head/2,0x80);
        ui_fill_round_rect(fb,(EpdRect){cx-body/2,head_top+head+side*6/100,body,body},body/2,0x80);
    }
    HOME_STAGE("header");
    home_book_t *cards[HOME_CARDS];unsigned count=home_cards(cards);
    s_card_off_max=card_max_off(count);
    if(s_card_off>s_card_off_max)s_card_off=s_card_off_max;
    if(s_card_off<0)s_card_off=0;
    s_card_page=card_focus(count);
    if(!count)label(fb,342,HOME_CARD_Y+HOME_CARD_H/2,23,"打开一本书后，会在这里继续阅读",EPD_DRAW_ALIGN_CENTER,0x60);
    for(unsigned i=0;i<count;++i) {
        // 移到屏外的卡不画：拖动时每帧都重绘整条卡带，白画只会拖慢跟手。
        // Skip cards that left the screen: the strip repaints every drag frame, so wasted paint
        // costs feel.
        const EpdRect card=card_rect(s_card_page,(int)i);
        if(card.x>=UI_LOCK_WIDTH||card.x+card.width<=0)continue;
        render_card(fb,cards[i],s_card_page,(int)i,i==(unsigned)s_card_page);
    }
    HOME_STAGE("cards");
    const int band=HOME_BAND_DY;
    if(app_settings_home_component()==APP_HOME_FOLDER) {
        ui_home_folder_draw(fb,s_folder,s_folder_fast,(EpdRect){24,604+band,320,350});
        label(fb,184,985+band,22,"最近阅读",EPD_DRAW_ALIGN_CENTER,0x50);
    } else {
        clock_draw_region(fb,(EpdRect){24,640+band,320,350},&s_date,app_settings_main_fast_refresh(),clock_style(app_settings_home_component()),(EpdRect){24,640+band,320,326},s_widget_phase);
        label(fb,184,985+band,22,component_name(app_settings_home_component()),EPD_DRAW_ALIGN_CENTER,0x50);
    }
    HOME_STAGE("component");
    if(s_reading_days_valid) {
        static const char *const wd[]={"星期日","星期一","星期二","星期三","星期四","星期五","星期六"};
        char value[80];
        snprintf(value,sizeof(value),"%d月%d日",s_date.tm_mon+1,s_date.tm_mday);
        label(fb,354,665+band,30,value,EPD_DRAW_ALIGN_LEFT,0);
        label(fb,642,665+band,24,wd[s_date.tm_wday],EPD_DRAW_ALIGN_RIGHT,0x50);
        int start=(s_date.tm_wday+7-(s_date.tm_mday-1)%7)%7;
        int rows=(start+month_length(&s_date)+6)/7;
        calendar(fb,375,754+band,42,rows==6?38:48,26,app_settings_home_goal_minutes()*60);
    } else label(fb,496,754+band,22,"请先设置日期与时间",EPD_DRAW_ALIGN_CENTER,0x60);
    label(fb,642,985+band,22,"· 已闭环",EPD_DRAW_ALIGN_RIGHT,0x50);
    HOME_STAGE("calendar");
    ui_nav_draw(fb,0);
    HOME_STAGE("nav");
#undef HOME_STAGE
}
// 卡片菜单：标记已读完 / 在文件中显示 / 从继续阅读移除；行高与图标照设计稿。
// Card menu: mark finished, reveal in Files and remove from continue reading, with the design's
// row height and icons.
static void render_card_menu(uint8_t *fb) {
    if(!s_card_menu)return;
    const EpdRect menu=card_menu_rect(s_card_page,s_card_page);
    // 卡片菜单靠浅灰底和投影浮在封面上，不再描边。
    // The card menu floats over the cover with a light gray surface and a shadow, no outline.
    ui_draw_popup_shadow(fb,menu,HOME_MENU_RADIUS);
    ui_fill_round_rect(fb,menu,HOME_MENU_RADIUS,0xf0);
    static const ui_icon_t icons[3]={UI_ICON_CIRCLE_CHECK,UI_ICON_FILE,UI_ICON_TRASH};
    static const char *const rows[3]={"标记为已读完","在文件中显示","从继续阅读移除"};
    for(int i=0;i<3;++i) {
        const int top=menu.y+HOME_MENU_PAD_V+i*HOME_MENU_ROW_H;
        const int center=top+HOME_MENU_ROW_H/2;
        const int icon_x=menu.x+HOME_MENU_PAD_H+17;
        ui_draw_icon(fb,icon_x,center,34,icons[i],UI_GRAY_BLACK);
        ui_text_fixed_vc(fb,menu.x+HOME_MENU_PAD_H+34+18,center,28,rows[i],EPD_DRAW_ALIGN_LEFT,false);
    }
}
static void render_goal(uint8_t *fb) {
    header(fb,"月阅读记录");char value[64];
    snprintf(value,sizeof(value),s_reading_days_valid?"%d年%d月":"日期未设置",s_date.tm_year+1900,s_date.tm_mon+1);
    label(fb,36,226,34,value,EPD_DRAW_ALIGN_LEFT,0);
    ring(fb,622,226,22,6,today_seconds(),s_goal_draft*60);
    calendar(fb,78,348,88,56,32,s_goal_draft*60);
    label(fb,36,678,24,"今日阅读",EPD_DRAW_ALIGN_LEFT,0x50);
    snprintf(value,sizeof(value),"%u 分钟",today_seconds()/60);label(fb,648,678,30,value,EPD_DRAW_ALIGN_RIGHT,0);
    label(fb,36,744,30,"每日闭环目标",EPD_DRAW_ALIGN_LEFT,0);
    label(fb,36,784,22,"达到目标的日期会显示下划线",EPD_DRAW_ALIGN_LEFT,0x60);
    // 灰底胶囊和白圆钮直接分层，不再勾边。
    // The gray capsule and white knobs layer by fill alone, without outlines.
    ui_fill_round_rect(fb,(EpdRect){36,820,612,114},57,0xe0);
    epd_fill_circle(99,877,43,0xff,fb);epd_fill_circle(585,877,43,0xff,fb);
    ui_fill_rect_fast((EpdRect){87,876,24,3},s_goal_draft==5?0xa0:0x30,fb);
    ui_fill_rect_fast((EpdRect){573,876,24,3},s_goal_draft==180?0xa0:0x30,fb);
    ui_fill_rect_fast((EpdRect){584,866,3,24},s_goal_draft==180?0xa0:0x30,fb);
    snprintf(value,sizeof(value),"%u",s_goal_draft);label(fb,342,861,46,value,EPD_DRAW_ALIGN_CENTER,0);
    label(fb,342,902,20,"分钟",EPD_DRAW_ALIGN_CENTER,0x50);
    const unsigned presets[]={15,30,60};
    for(unsigned i=0;i<3;++i) {
        EpdRect box={36+i*214,970,184,56};ui_fill_round_rect(fb,box,28,s_goal_draft==presets[i]?0xc8:0xe8);
        snprintf(value,sizeof(value),"%u 分钟",presets[i]);label(fb,box.x+92,998,25,value,EPD_DRAW_ALIGN_CENTER,0x20);
    }
    if(s_save_error)label(fb,342,1060,23,"保存未完成，请再试一次",EPD_DRAW_ALIGN_CENTER,0x30);
    ui_fill_round_rect(fb,(EpdRect){36,1090,612,72},28,0x20);label(fb,342,1126,28,"保存",EPD_DRAW_ALIGN_CENTER,0xff);
}
static void render_component_controls(uint8_t *fb) {
    ui_draw_icon(fb,68,504,34,UI_ICON_CHEVRON_LEFT,0x30);ui_draw_icon(fb,616,504,34,UI_ICON_CHEVRON_RIGHT,0x30);
    bool current=s_component_browse==app_settings_home_component();
    bool pressed=s_press_pending&&s_pressed.y==1060;
    ui_fill_round_rect(fb,(EpdRect){36,1060,612,76},38,pressed?0xe0:0x20);
    label(fb,342,1098,30,current?"返回首页":"使用此样式",EPD_DRAW_ALIGN_CENTER,pressed?0x20:0xff);
}
static void render_components_body(uint8_t *fb) {
    if(s_component_browse==APP_HOME_FOLDER)ui_home_folder_draw(fb,s_folder,s_folder_fast,(EpdRect){132,282,420,459});
    else clock_draw_region(fb,(EpdRect){141,294,420,460},&s_date,app_settings_main_fast_refresh(),clock_style(s_component_browse),(EpdRect){141,294,420,460},s_widget_phase);
    label(fb,342,787,36,component_name(s_component_browse),EPD_DRAW_ALIGN_CENTER,0);
    unsigned selected=component_index(s_component_browse);
    static const char *const details[]={"圆形表盘，十二个月相刻度","圆滑短底座，清晰数字表盘","短时针、长分针，圆点为秒针","猫眼左右转动，没有尾巴","柔软轮廓，弯曲的罗马数字","深灰放射图案，白色指针配细描边","尖屋顶、圆形开口，四个数字刻度","三本最近阅读，透过磨砂书夹显示"};
    label(fb,342,838,23,details[selected],EPD_DRAW_ALIGN_CENTER,0x60);
    for(unsigned i=0;i<HOME_COMPONENT_OPTIONS;++i)epd_fill_circle(342-(HOME_COMPONENT_OPTIONS-1)*12+i*24,909,i==selected?6:4,i==selected?0x20:app_settings_main_fast_refresh()?0x60:0xc0,fb);
    char value[24];snprintf(value,sizeof(value),"%u / %u",selected+1,(unsigned)HOME_COMPONENT_OPTIONS);label(fb,342,953,23,value,EPD_DRAW_ALIGN_CENTER,0x60);
    bool current=s_component_browse==app_settings_home_component();
    if(s_save_error)label(fb,342,1008,23,"保存未完成，请再试一次",EPD_DRAW_ALIGN_CENTER,0x30);
    else if(current)label(fb,342,1008,23,"当前使用",EPD_DRAW_ALIGN_CENTER,0x50);
    render_component_controls(fb);
}
static void render_components(uint8_t *fb) {
    header(fb,"首页组件");label(fb,36,195,24,"选择你喜欢的样式",EPD_DRAW_ALIGN_LEFT,0x60);
    render_components_body(fb);
}
static void render_time_value(uint8_t *fb,const struct tm *date) {
    char value[18];if(s_reading_days_valid)snprintf(value,sizeof(value),"%02d:%02d:%02d",date->tm_hour,date->tm_min,date->tm_sec);else snprintf(value,sizeof(value),"--:--:--");
    int px=72;while(px>36&&ui_text_fixed_width_px(px,value)>540)--px;
    ui_fill_rect_fast((EpdRect){72,840,540,98},0xff,fb);label(fb,342,884,px,value,EPD_DRAW_ALIGN_CENTER,0);
}
static void render_time(uint8_t *fb) {
    header(fb,"时间");char value[64];
    if(s_reading_days_valid)snprintf(value,sizeof(value),"%d年%d月%d日",s_date.tm_year+1900,s_date.tm_mon+1,s_date.tm_mday);else snprintf(value,sizeof(value),"请先设置日期与时间");
    label(fb,342,219,30,value,EPD_DRAW_ALIGN_CENTER,0x50);
    clock_draw_region(fb,time_clock_bounds(),&s_date,app_settings_main_fast_refresh(),clock_style(app_settings_home_component()),time_clock_bounds(),s_time_phase);
    label(fb,342,804,32,component_name(app_settings_home_component()),EPD_DRAW_ALIGN_CENTER,0x30);
    render_time_value(fb,&s_date);label(fb,342,975,23,"北京时间 · 24小时制",EPD_DRAW_ALIGN_CENTER,0x60);
    bool pressed=s_press_pending&&s_pressed.y==1060;
    ui_fill_round_rect(fb,(EpdRect){36,1060,612,76},38,pressed?0xe0:0x20);label(fb,342,1098,30,"切换时钟样式",EPD_DRAW_ALIGN_CENTER,pressed?0x20:0xff);
}
static void render(app_ctx_t *ctx,uint8_t *fb) {
    (void)ctx;ui_clear_page(fb);ui_fill_rect_fast((EpdRect){0,0,UI_LOCK_WIDTH,UI_LOCK_HEIGHT},0xff,fb);
    if(s_view==HOME_GOAL){s_widget_trail.valid=s_time_trail.valid=false;}
    s_widget_phase=s_time_phase=clock_phase_now(&s_date);
    if(s_view==HOME_ROOT){render_root(fb);render_card_menu(fb);}
    else if(s_view==HOME_GOAL)render_goal(fb);else if(s_view==HOME_TIME)render_time(fb);else render_components(fb);
    if(s_press_pending)ui_draw_selected_round_rect(fb,s_pressed,
        s_pressed.width==HOME_AVATAR_SIDE?HOME_AVATAR_SIDE/2:
        s_pressed.width==64?32:s_pressed.width==86?43:s_pressed.height==76?38:20);
}
static enum EpdDrawError present_clock_area(app_ctx_t *ctx,EpdRect area) {
    // 脏行和脏列交集包含未变化灰底，必须使用等灰保持表，不能重刷该矩形。
    // Dirty-row/column intersections include unchanged grays; hold those pairs instead of driving the rectangle.
    const bool binary=app_settings_main_fast_refresh();
    return update_display_area_diff_with(ctx->hl,
        binary?display_main_bw_waveform():display_main_gray_waveform(),binary?MODE_DU:MODE_GL16,area);
}
static enum EpdDrawError present_clock_motion(app_ctx_t *ctx,EpdRect area) {
    // 秒针只扫变化圆点的短相位；灰底等值保持，不刷框、不重排整页。
    // Short phases drive only changed dot pixels, holding the gray face without a frame or whole-page paint.
    return update_display_area_diff_with(ctx->hl,&E0470_FOLLOW_WAVEFORM,MODE_DU,area);
}
static bool present(app_ctx_t *ctx,app_redraw_t redraw) {
    if(redraw==APP_REDRAW_NONE)return true;
    int64_t minute=ui_nav_clock_minute();
    if(redraw==APP_REDRAW_AREA&&s_view==HOME_COMPONENT&&(s_component_pending||s_area.width==64||s_area.height==76)) {
        if(s_component_pending)s_area=(EpdRect){36,260,612,876};
        ui_fill_rect_fast(s_area,0xff,ctx->fb);
        if(s_component_pending){s_widget_phase=clock_phase_now(&s_date);render_components_body(ctx->fb);}
        else render_component_controls(ctx->fb);
        if(s_press_pending)ui_draw_selected_round_rect(ctx->fb,s_pressed,s_pressed.width==64?32:38);
        enum EpdDrawError result=present_clock_area(ctx,s_area);guard_draw_result(ctx->hl,result);
        if(result==EPD_DRAW_SUCCESS){s_component_pending=false;if(visible_motion_clock()){s_widget_shown=s_date;s_widget_stamp=date_stamp(&s_date);s_widget_phase_shown=s_widget_phase;s_motion_ms=ctx->now_ms;}}
        return true;
    }
    if(redraw==APP_REDRAW_AREA&&s_widget_pending&&visible_motion_clock()){
        s_widget_pending=false;bool repair=s_widget_stamp<0;bool whole=repair||s_widget_date.tm_min!=s_widget_shown.tm_min||s_widget_date.tm_hour!=s_widget_shown.tm_hour;
        ui_home_clock_style_t style=clock_style(s_view==HOME_COMPONENT?s_component_browse:app_settings_home_component());
        bool skipped=style==UI_HOME_CLOCK_KEHAI&&(s_widget_phase+600-s_widget_phase_shown)%600>20;
        EpdRect box=component_clock_bounds(),dirty=whole||skipped?component_clock_graphics():ui_home_clock_motion_area(box,s_widget_phase_shown,s_widget_phase,style);
        clock_draw_region(ctx->fb,box,&s_widget_date,app_settings_main_fast_refresh(),style,dirty,s_widget_phase);
        if(minute!=s_clock_minute){ui_fill_rect_fast((EpdRect){24,18,156,44},0xff,ctx->fb);ui_nav_clock_draw(ctx->fb);dirty=ui_rect_union(dirty,(EpdRect){24,18,156,44});}
        enum EpdDrawError result=repair?present_clock_area(ctx,dirty):present_clock_motion(ctx,dirty);
        guard_draw_result(ctx->hl,result);
        if(result==EPD_DRAW_SUCCESS){s_widget_shown=s_widget_date;s_widget_stamp=date_stamp(&s_widget_date);s_widget_phase_shown=s_widget_phase;s_motion_ms=ctx->now_ms;s_clock_minute=minute;s_date=s_widget_date;}else s_widget_stamp=-1;return true;
    }
    if(redraw==APP_REDRAW_AREA&&s_time_pending&&s_view==HOME_TIME) {
        s_time_pending=false;bool repair=s_time_stamp<0;bool whole=repair||s_time_date.tm_min!=s_date.tm_min||s_time_date.tm_hour!=s_date.tm_hour;
        ui_home_clock_style_t style=clock_style(app_settings_home_component());
        EpdRect changes={0};
        if(s_time_stamp!=date_stamp(&s_time_date)){render_time_value(ctx->fb,&s_time_date);changes=(EpdRect){72,840,540,98};}
        if(whole||ui_home_clock_has_motion(style)){
            EpdRect box=time_clock_bounds();bool skipped=style==UI_HOME_CLOCK_KEHAI&&(s_time_phase+600-s_time_phase_shown)%600>20;
            EpdRect dirty=whole||skipped?box:ui_home_clock_motion_area(box,s_time_phase_shown,s_time_phase,style);
            clock_draw_region(ctx->fb,box,&s_time_date,app_settings_main_fast_refresh(),style,dirty,s_time_phase);
            changes=ui_rect_union(changes,dirty);
        }
        if(minute!=s_clock_minute){ui_fill_rect_fast((EpdRect){24,18,156,44},0xff,ctx->fb);ui_nav_clock_draw(ctx->fb);changes=ui_rect_union(changes,(EpdRect){24,18,156,44});}
        // 秒级数字与圆点合在同一次短局刷，避免数字刷新打断圆周运动。
        // Present second digits and the dot in one short partial update, avoiding a digit-induced pause in circular motion.
        enum EpdDrawError result=ui_home_clock_has_motion(style)&&!repair?present_clock_motion(ctx,changes):present_clock_area(ctx,changes);
        guard_draw_result(ctx->hl,result);
        if(result==EPD_DRAW_SUCCESS){s_date=s_time_date;s_time_stamp=date_stamp(&s_date);s_time_phase_shown=s_time_phase;s_motion_ms=ctx->now_ms;s_clock_minute=minute;}else s_time_stamp=-1;
        return true;
    }
    if(redraw==APP_REDRAW_AREA&&s_clock_pending) {
        s_clock_pending=false;
        ui_fill_rect_fast((EpdRect){24,18,156,44},0xff,ctx->fb);ui_nav_clock_draw(ctx->fb);
        enum EpdDrawError result=update_display_area_diff_with(ctx->hl,&E0470_WAVEFORM,MODE_GL16,(EpdRect){24,18,156,44});
        if(result==EPD_DRAW_SUCCESS&&s_view==HOME_ROOT&&app_settings_home_component()!=APP_HOME_FOLDER) {
            date_now(&s_date);ui_home_clock_style_t style=clock_style(app_settings_home_component());
            clock_draw_region(ctx->fb,(EpdRect){24,640+HOME_BAND_DY,320,350},&s_date,app_settings_main_fast_refresh(),style,(EpdRect){24,640+HOME_BAND_DY,320,326},s_widget_phase);
            result=present_clock_area(ctx,style==UI_HOME_CLOCK_PS1995?(EpdRect){78,684,198,198}:component_clock_graphics());
        }
        guard_draw_result(ctx->hl,result);if(result==EPD_DRAW_SUCCESS)s_clock_minute=minute;return true;
    }
    s_clock_pending=s_widget_pending=s_component_pending=false;render(ctx,ctx->fb);enum EpdDrawError result;
    if(redraw==APP_REDRAW_AREA)result=update_display_area_diff_with(ctx->hl,
        s_press_pending?&E0470_FOLLOW_WAVEFORM:&E0470_WAVEFORM,s_press_pending?MODE_DU:MODE_GL16,s_area);
    else if(redraw==APP_REDRAW_FULL||s_home_force_full_once) {s_home_force_full_once=false;result=update_display_full(ctx->hl);}
    else result=update_display_fast_page(ctx->hl);
    guard_draw_result(ctx->hl,result);
    if(redraw!=APP_REDRAW_AREA&&result==EPD_DRAW_SUCCESS){s_clock_minute=minute;
        if(s_view==HOME_TIME){s_time_stamp=date_stamp(&s_date);s_time_phase_shown=s_time_phase;}
        if(visible_motion_clock()){s_widget_shown=s_date;s_widget_stamp=date_stamp(&s_date);s_widget_phase_shown=s_widget_phase;}
        s_motion_ms=ctx->now_ms;
    }
    return true;
}
static void on_enter(app_ctx_t *ctx) {
    s_cover_generation=book_cover_jobs_begin();
    const uint32_t progress_revision=book_progress_revision();
    s_clock_base_attempted=false;clock_base_prepare();
    s_widget_trail.valid=s_time_trail.valid=false;
    (void)ctx;s_view=HOME_ROOT;s_clock_pending=s_time_pending=s_widget_pending=s_component_pending=false;s_press_pending=false;s_save_error=false;s_time_stamp=s_widget_stamp=-1;s_motion_ms=-100;
    // 每次回到首页都从第一本开始，并收起卡片菜单。
    // Every return to Home starts on the first book with the card menu closed.
    s_card_page=0;s_card_menu=false;s_card_off=0;s_card_flick=0;s_card_drag=false;
    s_home_force_full_once=app_settings_home_full_refresh();refresh_days();avatar_prepare_state();
    read_pico_sd_info_t sd={0};esp_err_t error=read_pico_sd_get_info(&sd);
    bool same=s_books_cache_valid&&error!=ESP_ERR_NOT_FINISHED&&s_cached_store_revision==book_store_revision()&&
        s_cached_flag_revision==app_book_shelf_flag_revision()&&
        s_cached_sd_present==sd.present&&s_cached_sd_mounted==sd.mounted&&!strcmp(s_cached_books_dir,app_settings_books_dir());
    char last[BOOK_STORE_PATH_MAX]={0};book_progress_last_path(last,sizeof(last));
    bool changed=strcmp(last,s_cached_last_path);
    if(same&&!changed){if(s_cached_progress_revision!=progress_revision)refresh_progress();s_scan_pending=false;}
    else {
        memset(&s_current,0,sizeof(s_current));memset(s_recent,0,sizeof(s_recent));s_scan_pending=!same;
        if(s_scan_pending) read_pico_sd_start_probe();
        scan_books();
        if(same||error!=ESP_ERR_NOT_FINISHED)remember_scan_state(&sd);
    }
    if(!same||changed)restore_cover_cache();
    rp_boot_mark("scan");
    s_cached_progress_revision=progress_revision;
    s_cover_next=0;
    if(app_settings_home_component()==APP_HOME_FOLDER&&(!same||changed||!s_folder_key)){uint32_t key=folder_key();
        if(key!=s_folder_key){free(s_folder);s_folder=NULL;s_folder_attempted=false;}}
    rp_boot_mark("excerpt");
}
static void on_media_lost(app_ctx_t *ctx) {
    app_home_release_clock_cache();
    (void)ctx;clear_cover_cache();memset(&s_current,0,sizeof(s_current));memset(s_recent,0,sizeof(s_recent));
    s_books_cache_valid=false;s_avatar_ready=s_avatar_pending=false;
}
static void on_media_ready(app_ctx_t *ctx) {(void)ctx;s_books_cache_valid=false;s_scan_pending=true;avatar_prepare_state();}
static app_redraw_t on_tick(app_ctx_t *ctx) {
    clock_base_prepare();
    if(ctx->consumed||(ctx->touch&&ctx->touch->touched))return APP_REDRAW_NONE;
    // 手势识别器只在甩动或轻点时才发事件，「拖动过容差但不算甩动」的慢拖松开时什么都不发。
    // 所以松手不能靠事件判断：on_tick 在手指按住时已经返回，走到这里就说明刚松开。
    // The recogniser only emits on a swipe or a tap, so a slow drag that released after the slop
    // produces nothing at all. Release cannot be read from events: on_tick returns while the finger
    // is down, so reaching this line means the finger just lifted.
    if(s_card_drag) {
        s_card_drag=false;
        home_book_t *cards[HOME_CARDS];s_card_off_max=card_max_off(home_cards(cards));
        // 手指向左划为正，卡带跟着往左移；总位移直接就是偏移量，1:1。
        // A leftward drag is positive and moves the strip left; total travel maps 1:1 to offset.
        // 不再按手指行程换算：一次滑动固定翻一张卡。触摸上报的行程比手指实际划过的短很多，
        // 按行程换算永远算不准，固定一卡一步反而可预期。
        // No more travel arithmetic: one swipe advances exactly one card. The reported travel is much
        // shorter than the real finger path, so scaling it never lands where expected, while a fixed
        // one-card step always does.
        int next=strip_release_offset();
        if(next!=s_card_off) {
            s_card_off=next;s_card_menu=false;strip_paint(ctx);
            s_area=card_strip_area();return APP_REDRAW_AREA;
        }
    }
    // 松手后的惯性：按速度继续滑并按时间衰减，撞到两端立刻停。
    // Inertia after release: keep sliding at the measured velocity with exponential decay, stopping
    // at once on either end.
    // 设置页的做法不播惯性，这里保留同样的选择：分段推进后停在完整画面上。
    // The settings approach has no release inertia; the strip keeps that choice and rests on a
    // complete picture.
    if(s_view==HOME_ROOT&&!s_card_drag&&s_card_flick) {
        home_book_t *cards[HOME_CARDS];s_card_off_max=card_max_off(home_cards(cards));
        const int64_t now=esp_timer_get_time()/1000;
        int dt=(int)(now-s_card_flick_ms);
        if(dt<1)dt=1;
        if(dt>120)dt=120; // 慢帧也别一步跳太远，但别把惯性压扁 / cap the jump on slow frames without flattening the glide
        int offset=s_card_off+(int)((int64_t)s_card_flick*dt/1000);
        if(offset<0){offset=0;s_card_flick=0;}
        if(offset>s_card_off_max){offset=s_card_off_max;s_card_flick=0;}
        // 按时间衰减，0.998/ms 就是 iOS 的正常滚动减速率；按帧乘 0.8 的话 300ms 就停死了。
        // Decay in time, not per frame: 0.998 per ms is iOS's normal deceleration rate.
        // Multiplying by 0.8 per frame kills the glide within 300 ms.
        s_card_flick-=s_card_flick*dt/500;
        if(s_card_flick>-30&&s_card_flick<30)s_card_flick=0;
        s_card_flick_ms=(int)now;
        if(offset!=s_card_off){s_card_off=offset;s_card_menu=false;strip_paint(ctx);s_area=card_strip_area();return APP_REDRAW_AREA;}
    }
    if(s_view==HOME_COMPONENT&&s_component_pending)return APP_REDRAW_AREA;
    bool folder_preview=s_view==HOME_COMPONENT&&s_component_browse==APP_HOME_FOLDER;
    if(s_view==HOME_ROOT||folder_preview){
    if(s_scan_pending) {
        read_pico_sd_info_t info={0};if(read_pico_sd_get_info(&info)==ESP_ERR_NOT_FINISHED)return APP_REDRAW_NONE;
        s_scan_pending=false;
        if(info.mounted) {
            s_cover_generation=book_cover_jobs_begin();
            memset(&s_current,0,sizeof(s_current));memset(s_recent,0,sizeof(s_recent));s_cover_next=0;
            scan_books();restore_cover_cache();remember_scan_state(&info);avatar_prepare_state();s_folder_attempted=false;
            free(s_folder);s_folder=NULL;return APP_REDRAW_PAGE;
        }remember_scan_state(&info);
    }
    if(s_view==HOME_ROOT&&s_avatar_pending) {
        ui_avatar_state_t state=ui_avatar_poll(s_avatar_path,HOME_AVATAR_SIDE,s_avatar);
        s_avatar_pending=state==UI_AVATAR_PENDING;
        s_avatar_ready=state==UI_AVATAR_READY;
        if(s_avatar_ready&&s_view==HOME_ROOT){s_area=(EpdRect){HOME_AVATAR_X,HOME_AVATAR_Y,HOME_AVATAR_SIDE,HOME_AVATAR_SIDE};return APP_REDRAW_AREA;}
    }
    // 封面必须在这一块里准备：后面的动效时钟分支每 100ms 就 return，放在它下面永远轮不到。
    // Covers must be prepared here: the animated-clock branch below returns every 100 ms, so anything
    // placed after it would never run while a clock has motion.
    if(s_view==HOME_ROOT) {
        home_book_t *cards[HOME_CARDS];unsigned count=home_cards(cards);
        for(unsigned i=0;i<count;++i) {
            home_book_t *b=cards[i];
            if(!b->path[0]||b->cover)continue;
            int slot=b==&s_current?0:(int)(b-s_recent)+1;
            if(load_cover(b,slot)){s_area=card_strip_area();return APP_REDRAW_AREA;}
            if(s_cover_waiting)return APP_REDRAW_NONE;
            break;
        }
    }
    }
    if(s_view==HOME_COMPONENT&&s_component_pending)return APP_REDRAW_AREA;
    if(s_view==HOME_TIME){
        struct tm date;if(!date_now(&date))return APP_REDRAW_NONE;
        unsigned phase=clock_phase_now(&date);
        if(date.tm_yday!=s_date.tm_yday||date.tm_year!=s_date.tm_year){refresh_days();return APP_REDRAW_PAGE;}
        if(date_stamp(&date)!=s_time_stamp||(ui_home_clock_has_motion(clock_style(app_settings_home_component()))&&phase!=s_time_phase_shown&&clock_motion_due(ctx))){s_time_date=date;s_time_phase=phase;s_time_pending=true;return APP_REDRAW_AREA;}return APP_REDRAW_NONE;
    }
    if(visible_motion_clock()){
        struct tm date;if(date_now(&date)){
            unsigned phase=clock_phase_now(&date);
            if(date.tm_yday!=s_date.tm_yday||date.tm_year!=s_date.tm_year){refresh_days();return APP_REDRAW_PAGE;}
            if(clock_motion_due(ctx)&&(date_stamp(&date)!=s_widget_stamp||phase!=s_widget_phase_shown)){s_widget_date=date;s_widget_phase=phase;s_widget_pending=true;return APP_REDRAW_AREA;}
        }
    }
    if(ui_nav_clock_minute()!=s_clock_minute) {
        struct tm date;bool valid=date_now(&date);
        if(valid&&(!s_reading_days_valid||date.tm_yday!=s_date.tm_yday||date.tm_year!=s_date.tm_year)) {refresh_days();return APP_REDRAW_PAGE;}
        if(valid)s_date=date;
        s_clock_pending=true;s_area=(EpdRect){24,18,156,44};return APP_REDRAW_AREA;
    }
    // 只为正在预览的书夹准备封面；切到时钟即停止隐藏文件处理。
    // Prepare covers only for the visible folder preview; switching to a clock stops hidden media work.
    if(s_view==HOME_COMPONENT&&!folder_preview)return APP_REDRAW_NONE;
    if(s_view==HOME_GOAL)return APP_REDRAW_NONE;
    home_book_t *books[3];unsigned n=folder_books(books);
    if(s_cover_next<n+1) {
        unsigned i=s_cover_next++;home_book_t *b=i?books[i-1]:featured();
        if(b->path[0]&&!b->cover) {
            int slot=b==&s_current?0:(int)(b-s_recent)+1;
            if(load_cover(b,slot)&&!i&&s_view==HOME_ROOT){s_area=(EpdRect){60,278,176,240};return APP_REDRAW_AREA;}
            if(s_cover_waiting)--s_cover_next;
        }
        return APP_REDRAW_NONE;
    }
    bool needs_folder=(s_view==HOME_ROOT&&app_settings_home_component()==APP_HOME_FOLDER)||(s_view==HOME_COMPONENT&&s_component_browse==APP_HOME_FOLDER);
    if(needs_folder&&!s_folder&&!s_folder_attempted) {
        if(prepare_folder()){
            if(folder_preview){s_component_pending=true;s_area=(EpdRect){36,260,612,876};}
            else s_area=(EpdRect){24,604,320,350};
            return APP_REDRAW_AREA;
        }
        s_folder_attempted=true;
    }
    return APP_REDRAW_NONE;
}
static bool hit(EpdRect r,int x,int y) {return x>=r.x&&x<r.x+r.width&&y>=r.y&&y<r.y+r.height;}
// 续读卡本身不做按压反馈：整张卡重画一次封面代价太高，点击直接开书。
// Resume cards take no press feedback: repainting a whole cover is too costly, so a tap opens the book.
static bool home_band_clock_hit(int x,int y) {return hit((EpdRect){24,640+HOME_BAND_DY,320,365},x,y);}
static bool home_band_calendar_hit(int x,int y) {return hit((EpdRect){354,640+HOME_BAND_DY,294,365},x,y);}
static int card_at(int x,int y) {
    for(int i=0;i<HOME_CARDS;++i)if(hit(card_hit_rect(s_card_page,i),x,y))return i;
    return -1;
}
static EpdRect control(int x,int y) {
    if(s_view==HOME_ROOT) {
        if(hit((EpdRect){HOME_AVATAR_X,HOME_AVATAR_Y,HOME_AVATAR_SIDE,HOME_AVATAR_SIDE},x,y))
            return(EpdRect){HOME_AVATAR_X,HOME_AVATAR_Y,HOME_AVATAR_SIDE,HOME_AVATAR_SIDE};
    }else if(hit((EpdRect){36,78,60,60},x,y))return(EpdRect){0}; // 共享圆圈反馈负责题头返回，避免再叠错位框。/ Shared circle feedback owns header backs; do not add a shifted frame.
    if(s_view==HOME_COMPONENT){
        if(hit((EpdRect){36,472,64,64},x,y))return(EpdRect){36,472,64,64};
        if(hit((EpdRect){584,472,64,64},x,y))return(EpdRect){584,472,64,64};
    }
    if((s_view==HOME_COMPONENT||s_view==HOME_TIME)&&hit((EpdRect){36,1060,612,76},x,y))return(EpdRect){36,1060,612,76};
    if(s_view==HOME_GOAL) {
        if(hit((EpdRect){56,834,86,86},x,y))return(EpdRect){56,834,86,86};
        if(hit((EpdRect){542,834,86,86},x,y))return(EpdRect){542,834,86,86};
        if(hit((EpdRect){36,1090,612,72},x,y))return(EpdRect){36,1090,612,72};
    }
    return(EpdRect){0};
}
static app_redraw_t on_gesture(app_ctx_t *ctx,const ui_gesture_event_t *ev) {
    if(ev->type==UI_GESTURE_PRESS) {s_pressed=control(ev->x0,ev->y0);s_press_pending=s_pressed.width>0;if(s_press_pending){s_area=control_feedback_area(s_pressed);return APP_REDRAW_AREA;}return APP_REDRAW_NONE;}
    bool restore=s_press_pending;
    // 手指划过多少就偏移多少：拖动过程只记录、不推屏。每次 MOVE 重绘整条卡带（684×467）
    // 在墨水屏上要一两百毫秒，必然追不上手；改成一整段位移在松手时一次落位，只刷一帧完整画面。
    // The strip moves by however far the finger travelled. The drag only records, because repainting
    // the 684x467 band per move costs 100-200 ms on e-paper and can never keep up. The whole travel
    // is applied in one go on release, presenting a single complete frame.
    if(s_view==HOME_ROOT&&!s_card_menu&&ev->y0>=HOME_CARD_Y&&ev->y0<HOME_CARD_Y+HOME_CARD_H) {
        // 不接管 PRESS：轻点必须继续走原来的路径，接管了书就打不开了。
        // The press is left alone: a tap has to keep taking the original path, or books stop
        // opening. Tracking starts on the first move instead.
        if(ev->type==UI_GESTURE_MOVE) {
            if(!s_card_drag) {
                s_card_drag=true;s_card_from=s_card_off;
                s_card_last_x=ev->x0;s_card_to_x=ev->x;
            } else s_card_to_x=ev->x; // 只记位置，不画 / record only, never paint
            s_card_dir=(int)ev->x<(int)s_card_last_x?1:-1;
            return APP_REDRAW_NONE;
        }
        if(s_card_drag&&(ev->type==UI_GESTURE_SWIPE_L||ev->type==UI_GESTURE_SWIPE_R)) {
            s_card_to_x=ev->x; // 甩动事件带释放位置 / the swipe carries the release position
            s_card_dir=ev->type==UI_GESTURE_SWIPE_L?1:-1;
            return APP_REDRAW_NONE;
        }
    }
    if(ev->type==UI_GESTURE_MOVE) {if(s_press_pending&&!hit(s_pressed,ev->x,ev->y)){s_press_pending=false;s_area=control_feedback_area(s_pressed);return APP_REDRAW_AREA;}return APP_REDRAW_NONE;}
    s_press_pending=false;
    if(ev->type==UI_GESTURE_LONG_PRESS&&s_view==HOME_ROOT&&home_band_clock_hit(ev->x0,ev->y0)) {
        s_component_browse=app_settings_home_component();s_view=HOME_COMPONENT;s_save_error=false;return APP_REDRAW_PAGE;
    }
    if(s_card_drag&&ev->type==UI_GESTURE_CANCEL){s_card_drag=false;s_card_flick=0;return APP_REDRAW_NONE;}
    if(s_view==HOME_COMPONENT&&ev->y0>=250&&ev->y0<970){
        int direction=ev->type==UI_GESTURE_SWIPE_L?1:ev->type==UI_GESTURE_SWIPE_R?-1:0;
        if(ev->type==UI_GESTURE_TAP&&hit((EpdRect){36,472,64,64},ev->x0,ev->y0))direction=-1;
        if(ev->type==UI_GESTURE_TAP&&hit((EpdRect){584,472,64,64},ev->x0,ev->y0))direction=1;
        if(direction){unsigned index=component_index(s_component_browse);s_component_browse=s_components[(index+HOME_COMPONENT_OPTIONS+direction)%HOME_COMPONENT_OPTIONS];s_save_error=false;s_component_pending=true;s_widget_pending=false;s_area=(EpdRect){36,260,612,876};return APP_REDRAW_AREA;}
    }
    if(ev->type!=UI_GESTURE_TAP) {if(restore){s_area=control_feedback_area(s_pressed);return APP_REDRAW_AREA;}return APP_REDRAW_NONE;}
    int x=ev->x0,y=ev->y0;
    if(s_view!=HOME_ROOT&&hit((EpdRect){36,78,60,60},x,y)) {s_view=HOME_ROOT;s_save_error=false;return APP_REDRAW_PAGE;}
    if(s_view==HOME_COMPONENT) {
        if(hit((EpdRect){36,1060,612,76},x,y)) {
            s_save_error=s_component_browse!=app_settings_home_component()&&app_settings_set_home_component(s_component_browse)!=ESP_OK;
            if(!s_save_error) s_view=HOME_ROOT;
            return APP_REDRAW_PAGE;
        }
    }else if(s_view==HOME_TIME){
        if(hit((EpdRect){36,1060,612,76},x,y)){s_view=HOME_COMPONENT;s_component_browse=app_settings_home_component();s_save_error=false;return APP_REDRAW_PAGE;}
    }else if(s_view==HOME_GOAL) {
        uint8_t old=s_goal_draft;
        if(hit((EpdRect){56,834,86,86},x,y)&&s_goal_draft>5)s_goal_draft-=5;
        else if(hit((EpdRect){542,834,86,86},x,y)&&s_goal_draft<180)s_goal_draft+=5;
        else if(hit((EpdRect){36,1090,612,72},x,y)) {
            s_save_error=app_settings_set_home_goal_minutes(s_goal_draft)!=ESP_OK;if(!s_save_error) s_view=HOME_ROOT;
            return APP_REDRAW_PAGE;
        }else {const uint8_t presets[]={15,30,60};for(int i=0;i<3;++i)if(hit((EpdRect){36+i*214,970,184,56},x,y))s_goal_draft=presets[i];}
        if(old!=s_goal_draft){s_area=(EpdRect){36,198,612,830};return APP_REDRAW_AREA;}
    }else {
        int tab=ui_nav_hit(x,y);if(tab>=0){ui_nav_request(ctx,tab);return APP_REDRAW_NONE;}
        // “…”三个动作：标记已读完 / 在文件管理中显示 / 从继续阅读移除。
        // The "..." rows: mark finished, reveal in Files, remove from continue reading.
        if(s_card_menu) {
            const EpdRect menu=card_menu_rect(s_card_page,s_card_page);
            const int row=y-(HOME_MENU_TOP+HOME_MENU_PAD_V);
            home_book_t *book=focused_card();
            // 菜单第一行正好压在“…”下方，再点把手只能收起菜单，不能当成首行动作。
            // Row one sits right under "...", so a second tap on the handle may only close the menu
            // and must never count as the first action.
            const bool on_dots=hit(dots_hit_rect(s_card_page,s_card_page),x,y);
            s_card_menu=false;
            if(!on_dots&&book&&row>=0&&row<3*HOME_MENU_ROW_H&&hit(menu,x,y)) {
                const int action=row/HOME_MENU_ROW_H;
                if(action==0) {
                    // 标记成功后只改内存里的这份缓存，页面立刻少一本书；下一次进首页会重扫。
                    // After a successful mark only this in-memory cache changes, so the book leaves
                    // the strip at once; the next Home entry rescans anyway.
                    if(app_book_set_finished(book->path,true))book->finished=true;
                }else if(action==1) {
                    app_files_request_reveal(book->path);
                    ctx->request_app=app_files_page();
                    return APP_REDRAW_NONE;
                }else if(app_book_set_resume_hidden(book->path,true))book->resume_hidden=true;
            }
            return APP_REDRAW_PAGE;
        }
        if(hit(dots_hit_rect(s_card_page,s_card_page),x,y)){s_card_menu=true;return APP_REDRAW_PAGE;}
        int card=card_at(x,y);
        if(card>=0) {
            home_book_t *cards[HOME_CARDS];const unsigned count=home_cards(cards);
            if((unsigned)card<count&&cards[card]->path[0]&&app_book_request_open_from_home(cards[card]->path)){ui_nav_request(ctx,1);return APP_REDRAW_NONE;}
            if(restore){s_area=control_feedback_area(s_pressed);return APP_REDRAW_AREA;}
            return APP_REDRAW_NONE;
        }
        if(home_band_clock_hit(x,y)){
            if(app_settings_home_component()==APP_HOME_FOLDER){ui_nav_request(ctx,1);return APP_REDRAW_NONE;}
            s_time_trail.valid=false;s_view=HOME_TIME;s_time_pending=false;s_time_stamp=-1;refresh_days();return APP_REDRAW_PAGE;
        }
        if(home_band_calendar_hit(x,y)) {
            refresh_days();s_goal_draft=app_settings_home_goal_minutes();s_view=HOME_GOAL;s_save_error=false;return APP_REDRAW_PAGE;
        }
        if(hit((EpdRect){HOME_AVATAR_X,HOME_AVATAR_Y,HOME_AVATAR_SIDE,HOME_AVATAR_SIDE},x,y)){ui_nav_request(ctx,2);return APP_REDRAW_NONE;}
    }
    if(restore){s_area=control_feedback_area(s_pressed);return APP_REDRAW_AREA;}return APP_REDRAW_NONE;
}
static app_redraw_t on_key(app_ctx_t *ctx,int key) {(void)ctx;(void)key;if(s_view!=HOME_ROOT){s_view=HOME_ROOT;s_press_pending=false;return APP_REDRAW_PAGE;}return APP_REDRAW_NONE;}
static bool no_menu_handle(app_ctx_t *ctx) {(void)ctx;return false;}
static bool main_page_visible(app_ctx_t *ctx) {(void)ctx;return s_view==HOME_ROOT;}
static EpdRect area_hint(app_ctx_t *ctx) {(void)ctx;return s_area;}
static void home_exit(app_ctx_t *ctx){(void)ctx;book_cover_jobs_cancel(s_cover_generation);s_cover_generation=0;}
const app_desc_t app_dashboard={
    .title="首页 Home",.detail="继续阅读与月阅读记录",.enter_full=false,.owns_keys=true,.menu_handle_enabled=no_menu_handle,
    .main_page_visible=main_page_visible,.on_enter=on_enter,.on_exit=home_exit,.on_media_lost=on_media_lost,.on_media_ready=on_media_ready,
    .render=render,.present=present,.on_tick=on_tick,.on_gesture=on_gesture,.on_key=on_key,.area_hint=area_hint,
};
