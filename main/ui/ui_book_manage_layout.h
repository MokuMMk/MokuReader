/* SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0
 * 中文：书架管理列表、分页和底部操作独立占位，绘制和命中共用。
 * English: Separate shelf list, pager and actions, with identical paint and hit rectangles.
 */
#pragma once
#include "epdiy.h"
static inline EpdRect ui_book_manage_row(int i){return (EpdRect){40,334+i*130,604,130};}
static inline EpdRect ui_book_manage_action(int i){return (EpdRect){36+(i%3)*208,i<3?270:1096,196,i<3?48:96};}
_Static_assert(334+5*130<1014,"Shelf rows must finish before pager");
_Static_assert(1014+64+18<=1096,"Reserve space above shelf actions");
_Static_assert(1096+96<1216,"Shelf actions must stay inside screen");
