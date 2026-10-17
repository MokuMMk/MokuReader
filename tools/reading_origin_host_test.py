#!/usr/bin/env python3
"""中文：设计稿续读卡带的命中、左右滑动、卡片菜单与返回来源回归。
English: Design resume-strip hits, left/right paging, the card menu and reader return origins.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
root=Path(__file__).resolve().parents[1]
home=root/'main/apps/app_dashboard.c';book=root/'main/apps/app_book.c'
source=home.read_text()
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "main/ui/ui_nav_layout.h"
typedef struct {int x,y,width,height;} EpdRect;
typedef struct {int now_ms,leaf;} app_ctx_leaf_t;
typedef struct {int now_ms,leaf;uint8_t *fb;const void *request_app;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL} app_redraw_t;
enum {HOME_ROOT,HOME_COMPONENT,HOME_GOAL,HOME_TIME,READING,SHELF};
enum {UI_GESTURE_PRESS,UI_GESTURE_MOVE,UI_GESTURE_TAP,UI_GESTURE_LONG_PRESS,UI_GESTURE_SWIPE_L,UI_GESTURE_SWIPE_R,UI_GESTURE_SWIPE_U,UI_GESTURE_CANCEL};
typedef struct {int type,x0,y0,x,y;} ui_gesture_event_t;
typedef int app_home_component_t;
#define APP_HOME_FOLDER 7
#define HOME_COMPONENT_OPTIONS 2
#define UI_LOCK_WIDTH 684
#define UI_GRAY_WHITE 255
#define ESP_OK 0
static app_home_component_t s_components[]={0,7};
static int s_view=HOME_ROOT,s_component_browse,s_goal_draft,s_time_stamp;
static bool s_save_error,s_component_pending,s_widget_pending,s_time_pending;
static struct {bool valid;} s_time_trail;
static EpdRect s_area;
typedef struct {char path[32];bool has_progress;uint8_t *cover;uint16_t cover_width,cover_height;bool cover_fast;bool finished,resume_hidden;} home_book_t;
static int drawn;
static uint8_t ui_contrast_gray(uint8_t v){return v;}
static uint8_t ui_image_dither_gray(uint8_t v,int x,int y){(void)x;(void)y;return v;}
static void epd_draw_pixel(int x,int y,uint8_t g,uint8_t *fb){(void)x;(void)y;(void)g;(void)fb;++drawn;}
static void ui_fill_round_rect(uint8_t *fb,EpdRect r,int radius,uint8_t g){(void)fb;(void)r;(void)radius;(void)g;}
static void ui_fill_rect_fast(EpdRect r,uint8_t g,uint8_t *fb){(void)r;(void)g;(void)fb;}
static void render_card(uint8_t *fb,const home_book_t *item,int page,int index,bool focused){(void)fb;(void)item;(void)page;(void)index;(void)focused;}
static home_book_t s_current,s_recent[6];
static int open_calls,nav=-1;static bool fail_open;
static int finished_calls,resume_hidden_calls,reveal_calls;static bool action_fail;static char action_path[64];
static const struct app_desc_page {int unused;} test_files_page={0};
static const struct app_desc_page *app_files_page(void){return &test_files_page;}
static bool app_book_set_finished(const char *path,bool finished){assert(finished);++finished_calls;snprintf(action_path,sizeof(action_path),"%s",path);return !action_fail;}
static bool app_book_resume_hidden(const char *path){(void)path;return false;}
static bool app_book_set_resume_hidden(const char *path,bool hidden){assert(hidden);++resume_hidden_calls;snprintf(action_path,sizeof(action_path),"%s",path);return !action_fail;}
static void app_files_request_reveal(const char *path){++reveal_calls;snprintf(action_path,sizeof(action_path),"%s",path);}
static int app_settings_home_component(void){return 0;}
static int app_settings_set_home_component(int i){(void)i;return 0;}
static int app_settings_home_goal_minutes(void){return 30;}
static int app_settings_set_home_goal_minutes(int i){(void)i;return 0;}
static unsigned component_index(int c){(void)c;return 0;}
static void refresh_days(void){}
static int ui_nav_hit(int x,int y){(void)x;return y>=1096?2:-1;}
static void ui_nav_request(app_ctx_t *ctx,int tab){(void)ctx;nav=tab;}
static bool app_book_request_open_from_home(const char *path){assert(!strcmp(path,s_current.path));++open_calls;return !fail_open;}
'''
# 常量、卡带状态和小工具都从真源码里取，测试不再抄一份设计稿坐标。
# Constants, strip state and the small helpers come from the real source so the test
# never keeps a second copy of the design coordinates.
unit+=source[source.index('#define HOME_TITLE_X'):source.index('static bool s_reading_days_valid;')]
unit+='\n'
unit+='\n'.join(function(n,home) for n in
                ['draw_card_cover','folder_books','home_cards','focused_card','hit','home_band_clock_hit','home_band_calendar_hit','card_at','control','on_gesture'])
settings=(root/'main/settings.h').read_text();a=settings.index('typedef enum {\n    APP_READER_KEY_PREV');b=settings.index('} app_reader_key_action_t;',a)+len('} app_reader_key_action_t;');unit+=settings[a:b]
unit+=r'''
enum {READER_PANEL_NONE,READER_PANEL_TOOLS};
static bool s_reader_return_home;static int s_reader_panel,s_reader_slider,s_stats_activity_ms;
static struct {bool pending;} s_reader_tap;
static int saved,freed,restored,shelf_page=4,shelf_category=3;
static void save_progress(void){++saved;}
static void free_book(void){++freed;}
static void return_to_cached_shelf(app_ctx_t *ctx){++restored;ctx->leaf=shelf_page;assert(shelf_category==3);}
static app_redraw_t turn_page(app_ctx_t *ctx,int d){(void)ctx;(void)d;return APP_REDRAW_AREA;}
static app_redraw_t toggle_reader_fullscreen(app_ctx_t *ctx){(void)ctx;return APP_REDRAW_PAGE;}
static app_redraw_t reader_manual_refresh(app_ctx_t *ctx){(void)ctx;return APP_REDRAW_FULL;}
static void invalidate_prep(void){}
'''
unit+='\n'.join(function(n,book) for n in ['reader_return','reader_key_action','reader_key_label'])
unit+=r'''
static void gestures(int type,int x0,int y0,int x,int y,app_ctx_t *ctx){ui_gesture_event_t ev={type,x0,y0,x,y};on_gesture(ctx,&ev);}
static ui_gesture_event_t g_ev;
static app_redraw_t gesture_at(int type,int x,int y,app_ctx_t *ctx){g_ev=(ui_gesture_event_t){type,x,y,x,y};return on_gesture(ctx,&g_ev);}
int main(void){app_ctx_t ctx={0};
 int points=0;
 // 三本有进度的书撑起卡带；按住整卡不给反馈，点一下直接进书库开这本书。
 strcpy(s_current.path,"/sdcard/books/Novel/a.epub");s_current.has_progress=true;
 strcpy(s_recent[0].path,"/sdcard/books/Novel/b.epub");s_recent[0].has_progress=true;
 strcpy(s_recent[1].path,"/sdcard/books/Novel/c.epub");s_recent[1].has_progress=true;
 for(int y=HOME_CARD_Y+8;y<HOME_CARD_Y+HOME_CARD_H-8;y+=23)for(int x=HOME_CARD_X+8;x<HOME_CARD_X+HOME_CARD_W-8;x+=23){
  if(hit(dots_hit_rect(0,0),x,y))continue; // “…”有自己的目标 / "..." has its own target
  s_view=HOME_ROOT;s_card_page=0;s_card_menu=false;nav=-1;int old=open_calls;
  assert(gesture_at(UI_GESTURE_PRESS,x,y,&ctx)==APP_REDRAW_NONE);
  assert(!s_press_pending); // 整卡重画封面太贵，不做按压反馈 / no press feedback: a cover repaint is too costly
  assert(gesture_at(UI_GESTURE_TAP,x,y,&ctx)==APP_REDRAW_NONE);
  assert(open_calls==old+1&&nav==1&&!s_press_pending);++points;
 }
 // 左右滑动只记录方向，松手才落位；一次滑动固定翻一张卡，两端夹紧。
 // A swipe only records its direction and lands on release: one card per swipe, clamped at both ends.
 s_view=HOME_ROOT;s_card_page=0;s_card_menu=false;s_card_drag=false;s_card_off=0;s_card_flick=0;
 ui_gesture_event_t swipe=(ui_gesture_event_t){UI_GESTURE_SWIPE_L,HOME_CARD_X+120,HOME_CARD_Y+120,HOME_CARD_X+40,HOME_CARD_Y+120};
 assert(on_gesture(&ctx,&swipe)==APP_REDRAW_NONE&&!s_card_drag); // 没按住就不动 / no drag, no move
 ui_gesture_event_t drag=(ui_gesture_event_t){UI_GESTURE_MOVE,HOME_CARD_X+120,HOME_CARD_Y+120,HOME_CARD_X+80,HOME_CARD_Y+120};
 assert(on_gesture(&ctx,&drag)==APP_REDRAW_NONE&&s_card_drag&&s_card_from==0);
 assert(on_gesture(&ctx,&swipe)==APP_REDRAW_NONE&&s_card_dir==1);
 s_card_off_max=card_max_off(3);
 assert(strip_release_offset()==HOME_CARD_PITCH);
 s_card_from=HOME_CARD_PITCH;s_card_dir=1;
 assert(strip_release_offset()==s_card_off_max);
 s_card_from=s_card_off_max;s_card_dir=1;
 assert(strip_release_offset()==s_card_off_max);
 s_card_from=0;s_card_dir=-1;
 assert(strip_release_offset()==0);
 assert(card_max_off(1)==0&&card_max_off(0)==0);
 s_card_off=0;assert(card_focus(3)==0);
 s_card_off=HOME_CARD_PITCH*2;assert(card_focus(3)==2);
 s_card_off=s_card_off_max;assert(card_focus(3)==1);
 s_card_drag=false;s_card_flick=0;
 // 卡带外的滑动不动页码。/ A swipe outside the strip never pages.
 s_view=HOME_ROOT;s_card_page=0;s_card_off=0;
 assert(gesture_at(UI_GESTURE_SWIPE_L,342,HOME_CARD_Y-40,&ctx)==APP_REDRAW_NONE&&s_card_page==0&&!s_card_drag);
 // “…”菜单：点开，再点一下只收起，不打开书。
 s_card_menu=false;nav=-1;int old=open_calls;
 int dx=HOME_CARD_X+HOME_CARD_DOTS_CENTER_X,dy=HOME_CARD_ACTION_CENTER_Y;
 assert(hit(dots_hit_rect(0,0),dx,dy));
 assert(gesture_at(UI_GESTURE_TAP,dx,dy,&ctx)==APP_REDRAW_PAGE&&s_card_menu);
 assert(open_calls==old&&nav==-1);
 assert(gesture_at(UI_GESTURE_TAP,dx,dy,&ctx)==APP_REDRAW_PAGE&&!s_card_menu&&open_calls==old);
 // 菜单开着时点卡片本体也只收起菜单。/ While the menu is open a tap on the card only closes it.
 assert(gesture_at(UI_GESTURE_TAP,dx,dy,&ctx)==APP_REDRAW_PAGE&&s_card_menu);
 assert(gesture_at(UI_GESTURE_TAP,HOME_CARD_X+60,HOME_CARD_Y+60,&ctx)==APP_REDRAW_PAGE);
 assert(!s_card_menu&&open_calls==old);
 // 菜单贴在当前这枚“…”下方，并被屏幕右缘夹住。
 assert(card_menu_rect(0,0).x==HOME_CARD_X+HOME_CARD_DOTS_CENTER_X-13&&card_menu_rect(0,0).y==HOME_MENU_TOP);
 assert(card_menu_rect(0,2).x+card_menu_rect(0,2).width==UI_LOCK_WIDTH);
 assert(card_menu_rect(0,2).y+card_menu_rect(0,2).height<=UI_NAV_TOP);
 // “…”三行接上真实动作：标记已读完 / 在文件中显示 / 从继续阅读移除。
 // The three "..." rows act for real: mark finished, reveal in Files, hide from continue reading.
 s_view=HOME_ROOT;s_card_page=0;
 const EpdRect menu=card_menu_rect(0,0);
 const int mx=menu.x+120;
 const int row[3]={HOME_MENU_TOP+HOME_MENU_PAD_V+HOME_MENU_ROW_H/2,
                   HOME_MENU_TOP+HOME_MENU_PAD_V+HOME_MENU_ROW_H*3/2,
                   HOME_MENU_TOP+HOME_MENU_PAD_V+HOME_MENU_ROW_H*5/2};
 finished_calls=resume_hidden_calls=reveal_calls=0;action_path[0]=0;action_fail=false;
 s_card_menu=true;ctx.request_app=NULL;
 assert(gesture_at(UI_GESTURE_TAP,mx,row[0],&ctx)==APP_REDRAW_PAGE&&!s_card_menu);
 assert(finished_calls==1&&s_current.finished&&!strcmp(action_path,s_current.path));
 s_current.finished=false;
 s_card_menu=true;assert(gesture_at(UI_GESTURE_TAP,mx,row[1],&ctx)==APP_REDRAW_NONE);
 assert(reveal_calls==1&&ctx.request_app==app_files_page());
 s_card_menu=true;assert(gesture_at(UI_GESTURE_TAP,mx,row[2],&ctx)==APP_REDRAW_PAGE);
 assert(resume_hidden_calls==1&&s_current.resume_hidden);
 s_current.resume_hidden=false;
 // 保存失败时首页缓存不改，但菜单仍收起。/ A failed save keeps the cache and still closes the menu.
 action_fail=true;s_card_menu=true;finished_calls=0;
 assert(gesture_at(UI_GESTURE_TAP,mx,row[0],&ctx)==APP_REDRAW_PAGE&&!s_card_menu&&finished_calls==1&&!s_current.finished);
 action_fail=false;
 // 打开失败时不切页、不消费点击。/ A failed open switches no page and consumes nothing.
 s_view=HOME_ROOT;s_card_page=0;nav=-1;fail_open=true;open_calls=0;
 assert(gesture_at(UI_GESTURE_TAP,HOME_CARD_X+60,HOME_CARD_Y+60,&ctx)==APP_REDRAW_NONE);
 assert(nav==-1&&open_calls==1);fail_open=false;open_calls=0;
 // 没有进度时卡带用最近扫描到的书补齐，点卡片仍然开书；连路径都没有才空着。
 // Without progress the strip fills from the last scan and the card still opens; only with no path
 // at all does the strip stay empty.
 s_current.has_progress=true;s_recent[0].has_progress=false;s_recent[1].has_progress=false;
 s_view=HOME_ROOT;s_card_page=0;nav=-1;
 assert(gesture_at(UI_GESTURE_TAP,HOME_CARD_X+60,HOME_CARD_Y+60,&ctx)==APP_REDRAW_NONE&&nav==1);
 s_current.has_progress=false;
 assert(gesture_at(UI_GESTURE_TAP,HOME_CARD_X+60,HOME_CARD_Y+60,&ctx)==APP_REDRAW_NONE&&nav==1&&open_calls==2);
 memset(&s_current,0,sizeof(s_current));memset(s_recent,0,sizeof(s_recent));
 s_view=HOME_ROOT;s_card_page=0;nav=-1;
 assert(gesture_at(UI_GESTURE_TAP,HOME_CARD_X+60,HOME_CARD_Y+60,&ctx)==APP_REDRAW_NONE&&nav==-1&&open_calls==2);
 // 时钟块和日历块跟着卡带下移：长按进组件页，点击进月阅读记录。
 s_view=HOME_ROOT;s_card_page=2;
 int band_x=120,band_y=640+HOME_BAND_DY+120;
 assert(gesture_at(UI_GESTURE_LONG_PRESS,band_x,band_y,&ctx)==APP_REDRAW_PAGE&&s_view==HOME_COMPONENT);
 s_view=HOME_ROOT;
 assert(gesture_at(UI_GESTURE_TAP,400,640+HOME_BAND_DY+120,&ctx)==APP_REDRAW_PAGE&&s_view==HOME_GOAL);
 // 头像换到设计稿位置，点它进设置（底栏第 3 格）。
 s_view=HOME_ROOT;nav=-1;
 assert(gesture_at(UI_GESTURE_TAP,HOME_AVATAR_X+10,HOME_AVATAR_Y+10,&ctx)==APP_REDRAW_NONE&&nav==2);
 // 卡带之外、底栏之上仍是页面空白：既不换页也不开书。
 s_view=HOME_ROOT;nav=-1;int before=open_calls;
 assert(gesture_at(UI_GESTURE_TAP,20,700,&ctx)==APP_REDRAW_NONE);
 assert(nav==-1&&open_calls==before);
 // 头像按下给一圈选中反馈，移出就取消。
 // Pressing the avatar shows the selection ring; moving away cancels it.
 assert(gesture_at(UI_GESTURE_PRESS,HOME_AVATAR_X+10,HOME_AVATAR_Y+10,&ctx)==APP_REDRAW_AREA);
 assert(s_pressed.x==HOME_AVATAR_X&&s_pressed.width==HOME_AVATAR_SIDE);
 assert(gesture_at(UI_GESTURE_MOVE,HOME_AVATAR_X-90,HOME_AVATAR_Y+10,&ctx)==APP_REDRAW_AREA&&!s_press_pending);
 // 续读卡封面按 cover 裁切，取样必须整段落在封面缓存里：ASan 抓越界。
 // 这正是之前"主页封面全画不出来"的根因，所以单独回归。
 // The resume card centre-crops its cover, so every sample must stay inside the cover buffer;
 // ASan catches an overrun. This is exactly what made every Home cover blank before.
 for(unsigned i=0;i<6;++i){
  static const unsigned dims[6][2]={{523,735},{512,768},{800,600},{600,600},{300,900},{1200,400}};
  const unsigned w=dims[i][0],h=dims[i][1];
  uint8_t *pixels=malloc((size_t)w*h);
  assert(pixels);
  for(size_t n=0;n<(size_t)w*h;++n)pixels[n]=(uint8_t)((n*37u)&0xffu);
  s_current.cover=pixels;s_current.cover_width=(uint16_t)w;s_current.cover_height=(uint16_t)h;
  s_current.cover_fast=false;
  drawn=0;draw_card_cover(NULL,&s_current,card_rect(0,0));assert(drawn>0);
  s_current.cover_fast=true;drawn=0;draw_card_cover(NULL,&s_current,card_rect(0,0));assert(drawn>0);
  free(pixels);s_current.cover=NULL;s_current.cover_width=s_current.cover_height=0;
 }
 assert(!strcmp(reader_key_label(APP_READER_KEY_HOME),"返回主页"));
 for(int via_key=0;via_key<2;++via_key)for(int from_home=0;from_home<2;++from_home){
  s_view=READING;s_reader_panel=READER_PANEL_TOOLS;s_reader_return_home=from_home;
  nav=-1;ctx.leaf=99;int freed_before=freed;
  app_redraw_t result=via_key?reader_key_action(&ctx,APP_READER_KEY_HOME):reader_return(&ctx);
  if(from_home)assert(result==APP_REDRAW_NONE&&nav==0&&freed==freed_before);
  else assert(result==APP_REDRAW_PAGE&&nav==-1&&s_view==SHELF&&ctx.leaf==4&&freed==freed_before+1&&s_reader_panel==READER_PANEL_NONE);
 }
 assert(saved==4&&restored==2);
 printf("PASS: %d card hit points, strip release clamping at both ends, dots menu open/consume with mark-finished, show-in-files and hide-from-continue actions, no-feedback cards and failed-open guard; clock/calendar/avatar moves; screen/key shared return routes retain home or shelf category/page\n",points);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='kiiko-reading-origin-') as folder:
 c=Path(folder)/'test.c';binary=Path(folder)/'test';c.write_text(unit)
 subprocess.run(['cc','-I'+str(root),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-g','-fsanitize=address,undefined',str(c),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
