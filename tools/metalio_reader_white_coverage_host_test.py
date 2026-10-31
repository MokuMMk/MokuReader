#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
"""中文：应用裁剪仅使用PR公开API，不能引入其他底层或波形。/ English: Reader crops use only the selected PR public APIs."""
from pathlib import Path
import subprocess,tempfile
from home_cover_cache_host_test import function
ROOT=Path(__file__).resolve().parents[1]
u=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
typedef struct{int x,y,width,height;}EpdRect;
typedef int EpdWaveform;typedef int EpdiyHighlevelState;
enum EpdDrawMode{MODE_GL16=5};enum EpdDrawError{EPD_DRAW_SUCCESS,EPD_DRAW_OTHER_ERROR};
typedef struct{EpdiyHighlevelState *hl;}app_ctx_t;
#define UI_LOCK_WIDTH 684
#define UI_LOCK_HEIGHT 1216
static EpdWaveform wave;static EpdRect current;
static int fulls,diffs;static enum EpdDrawError error;
static EpdRect reader_area(void){return current;}
static const EpdWaveform *display_reader_soft_turn_waveform(void){return &wave;}
static enum EpdDrawError update_display_area_full_with(EpdiyHighlevelState*h,const EpdWaveform*w,enum EpdDrawMode m,EpdRect a){(void)h;assert(w==&wave&&m==MODE_GL16&&a.width>0);++fulls;return error;}
static enum EpdDrawError update_display_area_diff_with(EpdiyHighlevelState*h,const EpdWaveform*w,enum EpdDrawMode m,EpdRect a){(void)h;assert(w==&wave&&m==MODE_GL16&&a.width>0);++diffs;return error;}
'''
for n in ['reader_present_crop','reader_soft_present']:u+=function(n,ROOT/'main/apps/app_book.c')+'\n'
u+=r'''
int main(void){int hl=0;app_ctx_t ctx={&hl};EpdRect layouts[]={{0,176,684,900},{0,52,684,1116},{0,0,684,1204}};
 for(int i=0;i<3;++i)for(int fail=0;fail<2;++fail){current=layouts[i];fulls=diffs=0;error=fail?EPD_DRAW_OTHER_ERROR:EPD_DRAW_SUCCESS;
  assert(reader_soft_present(&ctx,current)==error&&fulls==1&&!diffs);
  assert(reader_soft_present(&ctx,(EpdRect){24,700,600,70})==error&&fulls==1&&diffs==1);
 }
 puts("PASS: all3 current reader layouts use PR full-area/difference APIs and preserve errors, with no replacement waveform");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(u)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
