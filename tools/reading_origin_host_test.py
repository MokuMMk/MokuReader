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
typedef struct {int now_ms,leaf;} app_ctx_t;
typedef enum {APP_REDRAW_NONE,APP_REDRAW_AREA,APP_REDRAW_PAGE,APP_REDRAW_FULL} app_redraw_t;
enum {HOME_ROOT,HOME_COMPONENT,HOME_GOAL,HOME_TIME,READING,SHELF};
enum {UI_GESTURE_PRESS,UI_GESTURE_MOVE,UI_GESTURE_TAP,UI_GESTURE_LONG_PRESS,UI_GESTURE_SWIPE_L,UI_GESTURE_SWIPE_R,UI_GESTURE_SWIPE_U};
typedef struct {int type,x0,y0,x,y;} ui_gesture_event_t;
typedef int app_home_component_t;
#define APP_HOME_FOLDER 7
#define HOME_COMPONENT_OPTIONS 2
#define UI_LOCK_WIDTH 684
#define ESP_OK 0
static app_home_component_t s_components[]={0,7};
static int s_view=HOME_ROOT,s_component_browse,s_goal_draft,s_time_stamp;
static bool s_save_error,s_component_pending,s_widget_pending,s_time_pending;
static struct {bool valid;} s_time_trail;
static EpdRect s_area;
typedef struct {char path[32];bool has_progress;uint8_t *cover;uint16_t cover_width,cover_height;bool cover_fast;} home_book_t;
static int drawn;
static uint8_t ui_contrast_gray(uint8_t v){return v;}
static uint8_t ui_image_dither_gray(uint8_t v,int x,int y){(void)x;(void)y;return v;}
static void epd_draw_pixel(int x,int y,uint8_t g,uint8_t *fb){(void)x;(void)y;(void)g;(void)fb;++drawn;}
static void ui_fill_round_rect(uint8_t *fb,EpdRect r,int radius,uint8_t g){(void)fb;(void)r;(void)radius;(void)g;}
static home_book_t s_current,s_recent[6];
static int open_calls,nav=-1;static bool fail_open;
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
                ['draw_card_cover','folder_books','hit','home_band_clock_hit','home_band_calendar_hit','card_at','control','on_gesture'])
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
 // 左右滑动换书：开机停在第一本，左滑看右边下一本，到头不再前进。
 s_view=HOME_ROOT;s_card_page=0;s_card_menu=false;nav=-1;
 ui_gesture_event_t swipe=(ui_gesture_event_t){UI_GESTURE_SWIPE_L,HOME_CARD_X+120,HOME_CARD_Y+120,HOME_CARD_X+40,HOME_CARD_Y+120};
 assert(on_gesture(&ctx,&swipe)==APP_REDRAW_AREA&&s_card_page==1);
 assert(s_area.x==0&&s_area.y==HOME_CARD_Y-8&&s_area.width==UI_LOCK_WIDTH&&s_area.height==HOME_CARD_H+16);
 assert(on_gesture(&ctx,&swipe)==APP_REDRAW_AREA&&s_card_page==2);
 assert(on_gesture(&ctx,&swipe)==APP_REDRAW_NONE&&s_card_page==2);
 swipe.type=UI_GESTURE_SWIPE_R;assert(on_gesture(&ctx,&swipe)==APP_REDRAW_AREA&&s_card_page==1);
 assert(on_gesture(&ctx,&swipe)==APP_REDRAW_AREA&&s_card_page==0);
 assert(on_gesture(&ctx,&swipe)==APP_REDRAW_NONE&&s_card_page==0);
 // 卡带外的滑动不动页码。/ A swipe outside the strip never pages.
 s_view=HOME_ROOT;s_card_page=0;
 assert(gesture_at(UI_GESTURE_SWIPE_L,342,HOME_CARD_Y-40,&ctx)==APP_REDRAW_NONE&&s_card_page==0);
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
 // 打开失败时不切页、不消费点击。/ A failed open switches no page and consumes nothing.
 s_view=HOME_ROOT;s_card_page=0;nav=-1;fail_open=true;open_calls=0;
 assert(gesture_at(UI_GESTURE_TAP,HOME_CARD_X+60,HOME_CARD_Y+60,&ctx)==APP_REDRAW_NONE);
 assert(nav==-1&&open_calls==1);fail_open=false;open_calls=0;
 // 没有书时不占位，也不越界翻页。/ With no book the strip holds no placeholder and never over-pages.
 s_current.has_progress=true;s_recent[0].has_progress=false;s_recent[1].has_progress=false;
 s_view=HOME_ROOT;s_card_page=0;nav=-1;
 assert(gesture_at(UI_GESTURE_TAP,HOME_CARD_X+60,HOME_CARD_Y+60,&ctx)==APP_REDRAW_NONE&&nav==1);
 s_current.has_progress=false;
 assert(gesture_at(UI_GESTURE_TAP,HOME_CARD_X+60,HOME_CARD_Y+60,&ctx)==APP_REDRAW_NONE&&nav==1&&open_calls==1);
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
 printf("PASS: %d card hit points, strip paging clamped at both ends, dots menu open/consume, no-feedback cards and failed-open guard; clock/calendar/avatar moves; screen/key shared return routes retain home or shelf category/page\n",points);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='kiiko-reading-origin-') as folder:
 c=Path(folder)/'test.c';binary=Path(folder)/'test';c.write_text(unit)
 subprocess.run(['cc','-I'+str(root),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-g','-fsanitize=address,undefined',str(c),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
