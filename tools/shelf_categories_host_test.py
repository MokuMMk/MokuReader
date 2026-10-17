"""Actual shelf folder selection/page memory with bounded allocation and path regressions.
SPDX-License-Identifier: Apache-2.0
"""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parents[1];s=(root/'main/apps/app_book.c').read_text()
def function(name):
 m=re.search(r'^static [^\n]+\b'+name+r'\([^;{}]*\)\s*\{',s,re.M);assert m,name
 at=s.index('{',m.start());i=at+1;depth=1;quote=None;escape=False
 while depth:
  c=s[i]
  if quote:
   if escape:escape=False
   elif c=='\\':escape=True
   elif c==quote:quote=None
  elif c in "\"'":quote=c
  elif c=='{':depth+=1
  elif c=='}':depth-=1
  i+=1
 return s[m.start():i]
unit=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "book_shelf_categories.h"
#define BOOK_STORE_PATH_MAX 288
#define SHELF_CATEGORY_LIMIT 128
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define SHELF 0
static bool fail_alloc;
static void *heap_caps_realloc(void *p,size_t bytes,int caps){assert(caps==3);return fail_alloc?NULL:realloc(p,bytes);}
static void *heap_caps_calloc(size_t count,size_t bytes,int caps){assert(caps==3);return fail_alloc?NULL:calloc(count,bytes);}
static void copy_text(char *to,size_t cap,const char *from){snprintf(to,cap,"%s",from);}
static const char *books_root="/sdcard/books";
static const char *app_settings_books_dir(void){return books_root;}
static int style;
static int app_settings_shelf_style(void){return style;}
static int shelf_rows(void){return style==5?4:9;}
static void invalidate_covers(void){}
typedef struct{int leaf;}app_ctx_t;
typedef struct{char path[288];int pages[2],count;bool available;}shelf_category_t;
static shelf_category_t *s_categories;
static int s_category_count,s_category_capacity,s_category_selected,s_category_menu_page;
static char s_category_root[288];
static int s_view=SHELF;
static bool s_scan_pending,s_shelf_cache_valid,s_recent_sort;
static int s_count,s_visible_count,s_filter;
static bool s_category_finished;
static int s_finished_count,s_finished_pages[2];
typedef struct{char path[288],name[256];bool removed,search_match,is_flash,favorite,finished;uint32_t recent;}shelf_entry_t;
static shelf_entry_t s_shelf[40];
typedef struct{int x,y,width,height;}EpdRect;
#define SHELF_ACTION_SIDE 75
#define SHELF_ACTION_Y 74
#define SHELF_CATEGORY_ROWS 7
#define CATEGORY_MENU_W 304
#define CATEGORY_MENU_TOP SHELF_ACTION_Y
#define CATEGORY_MENU_PAD_V 27
#define CATEGORY_MENU_PAD_H 25
#define CATEGORY_ROW_H 53
#define CATEGORY_FIXED_ROWS 1
'''
for name in ('shelf_category_add','shelf_category_init','shelf_category_name','shelf_category_matches','shelf_category_remember','shelf_category_restore','shelf_categories_recount','shelf_matches','compare_books','sort_shelf','shelf_category_rect','shelf_category_at','shelf_category_available','category_rows_visible','category_footer_visible','shelf_category_box','category_row_rect','category_choice_rect','category_footer_rect'):
 unit+=function(name)+'\n'
unit+=r'''
int main(void){
 char child[288];assert(book_shelf_child("/sdcard/books/","/sdcard/books/文学/深层/a.epub",child,sizeof(child))&&!strcmp(child,"文学"));
 assert(!book_shelf_child("/sdcard/books","/sdcard/books/a.epub",child,sizeof(child)));
 assert(!book_shelf_child("/sdcard/books","/sdcard/books2/文学/a.epub",child,sizeof(child)));
 assert(!book_shelf_child("/sdcard/books","/sdcard/books/.hidden/a.epub",child,sizeof(child)));
 assert(!book_shelf_child("/sdcard/books","/sdcard/book",child,sizeof(child)));
 assert(!book_shelf_child("/sdcard/books","/sdcard/books/文学/a.epub",child,3));
 assert(book_shelf_saved_page(100,10,9)==1&&book_shelf_saved_page(-1,10,9)==0&&book_shelf_saved_page(2,0,9)==0);
 fail_alloc=true;shelf_category_init();assert(!s_categories&&shelf_category_matches("anything"));fail_alloc=false;shelf_category_init();assert(s_category_count==2);
 int literature=shelf_category_add("/sdcard/books/文学");int empty=shelf_category_add("/sdcard/books/空目录");assert(literature==2&&empty==3);
 assert(shelf_category_add("/sdcard/books/文学")==literature);
 for(int i=0;i<24;++i){snprintf(s_shelf[i].path,288,i<18?"/sdcard/books/文学/nested/%d.epub":"/sdcard/books/%d.epub",i);snprintf(s_shelf[i].name,256,"b%02d",i);s_shelf[i].search_match=true;}
 snprintf(s_shelf[23].path,288,"/sdcard/book/legacy.epub");s_count=24;shelf_categories_recount();
 assert(s_categories[0].count==24&&s_categories[1].count==6&&s_categories[literature].count==18&&s_categories[empty].count==0);
 assert(!strcmp(shelf_category_name(literature),"文学")&&!strcmp(shelf_category_name(1),"未分类"));
 // 分类弹窗第一行固定是“已读完”：位置在弹窗顶行，下面才是分类；分类翻页不动它。
 // The category popup's first row is the fixed "finished" item at the top, with the categories
 // below it; paging the categories never moves the fixed row.
 assert(shelf_category_rect().x==449&&shelf_category_rect().y==74);
 s_category_menu_page=0;
 EpdRect box=shelf_category_box();
 assert(box.x==220&&box.y==74&&!category_footer_visible());
 assert(shelf_category_available()==4);
 assert(box.height==2*CATEGORY_MENU_PAD_V+(1+shelf_category_available())*CATEGORY_ROW_H);
 assert(category_row_rect(0).y==box.y+CATEGORY_MENU_PAD_V);
 assert(category_choice_rect(0).y==category_row_rect(0).y+CATEGORY_ROW_H);
 assert(category_choice_rect(1).y==category_row_rect(0).y+2*CATEGORY_ROW_H);
 assert(shelf_category_at(0)==0&&shelf_category_at(1)==1&&shelf_category_at(2)==literature&&shelf_category_at(3)==empty);
 app_ctx_t ctx={0};sort_shelf(&ctx);ctx.leaf=2;shelf_category_remember(&ctx);
 s_category_selected=literature;sort_shelf(&ctx);shelf_category_restore(&ctx);assert(s_visible_count==18&&ctx.leaf==0);
 ctx.leaf=1;shelf_category_remember(&ctx);s_category_selected=0;sort_shelf(&ctx);shelf_category_restore(&ctx);assert(ctx.leaf==2);
 s_category_selected=literature;sort_shelf(&ctx);shelf_category_restore(&ctx);assert(ctx.leaf==1);
 style=5;shelf_category_restore(&ctx);assert(!ctx.leaf);ctx.leaf=3;shelf_category_remember(&ctx);style=0;shelf_category_restore(&ctx);assert(ctx.leaf==1);
 s_category_selected=empty;sort_shelf(&ctx);shelf_category_restore(&ctx);assert(!s_visible_count&&!ctx.leaf);
 s_category_selected=1;sort_shelf(&ctx);assert(s_visible_count==6);
 s_filter=2;sort_shelf(&ctx);assert(!s_visible_count);s_filter=0;
 s_category_selected=literature;sort_shelf(&ctx);s_shelf[0].search_match=false;sort_shelf(&ctx);assert(s_visible_count==17);
 // 已读完只进顶部固定项：普通分类少两本，固定项计数等于标记数，选中后只看已读完。
 // Finished feeds only the popup's fixed row: ordinary counts drop by two, the fixed count equals
 // the marks, and selecting it shows nothing else. 一条搜索不匹配的记录仍占一个坑。
 // One record is still search-mismatched and stays out of every view.
 s_category_selected=0;s_category_finished=false;
 s_shelf[0].finished=true;s_shelf[23].finished=true;shelf_categories_recount();
 assert(s_finished_count==2&&s_categories[0].count==22&&s_categories[1].count==5);
 sort_shelf(&ctx);assert(s_visible_count==21);
 s_category_finished=true;sort_shelf(&ctx);assert(s_visible_count==2&&ctx.leaf==0);
 ctx.leaf=1;shelf_category_remember(&ctx);
 s_category_finished=false;sort_shelf(&ctx);shelf_category_restore(&ctx);assert(ctx.leaf==2);
 s_category_finished=true;sort_shelf(&ctx);shelf_category_restore(&ctx);assert(ctx.leaf==0);
 s_shelf[0].finished=false;shelf_categories_recount();sort_shelf(&ctx);assert(s_visible_count==1);
 s_shelf[0].finished=true;s_shelf[23].finished=false;sort_shelf(&ctx);assert(s_visible_count==1);
 s_category_finished=false;shelf_categories_recount();sort_shelf(&ctx);assert(s_visible_count==22);
 s_category_selected=empty;s_categories[empty].available=false;shelf_categories_recount();assert(!s_category_selected);
 fail_alloc=true;while(s_category_count<s_category_capacity)shelf_category_add("/not-a-real-path");assert(shelf_category_add("/new/folder")==-1);fail_alloc=false;
 for(int i=0;i<140;++i){char name[288];snprintf(name,sizeof(name),"/sdcard/books/folder-%d",i);shelf_category_add(name);}
 assert(s_category_count==128&&shelf_category_add("/overflow")==-1);
 // 分类多到需要翻页：固定项仍占弹窗第一行，页码行只加在底部，翻页不改变固定项位置。
 // More categories than one page: the fixed row keeps the top slot, the pager only adds a bottom
 // row, and paging never moves the fixed row.
 assert(category_footer_visible());
 s_category_menu_page=0;
 EpdRect many=shelf_category_box();
 assert(many.x==220&&many.y==74);
 assert(many.height==2*CATEGORY_MENU_PAD_V+(1+SHELF_CATEGORY_ROWS+1)*CATEGORY_ROW_H);
 assert(category_row_rect(0).y==many.y+CATEGORY_MENU_PAD_V);
 assert(category_footer_rect().y==category_choice_rect(category_rows_visible()-1).y+CATEGORY_ROW_H);
 s_category_menu_page=1;
 assert(category_row_rect(0).y==many.y+CATEGORY_MENU_PAD_V);
 assert(shelf_category_box().height==many.height);
 s_category_menu_page=0;
 books_root="/sdcard/newbooks";shelf_category_init();assert(s_category_count==2&&!s_category_selected);free(s_categories);
 puts("PASS: native folder grouping, root/path boundaries, empty folders, per-category/layout pages, fixed finished row, popup paging geometry, filtering, shrink clamp, bounded OOM fallback and changed root");
}
'''
# Avoid a duplicate path in the fill-to-capacity loop.
unit=unit.replace('while(s_category_count<s_category_capacity)shelf_category_add("/not-a-real-path");','while(s_category_count<s_category_capacity){char path[288];snprintf(path,sizeof(path),"/fill/%d",s_category_count);shelf_category_add(path);}')
with tempfile.TemporaryDirectory() as directory:
 c=Path(directory)/'shelf.c';binary=c.with_suffix('');c.write_text(unit)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root/'main/book'),str(c),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
