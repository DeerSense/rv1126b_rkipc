/*
 * Copyright (c) 2023 Rockchip, Inc. All Rights Reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "lvgl/porting/lv_port_indev.h"
#include "storage.h"
#include "ui_common.h"
#include "ui_dialog.h"

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/
extern lv_ft_info_t ttf_info_32;

static lv_obj_t *storage_status_dialog_obj = NULL;
static lv_group_t *storage_group = NULL;
extern void *g_sd_phandle;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
static void storage_status_dialog_exit_cb(lv_event_t *e);

void ui_nosdcard_dialog_destroy(void) { ui_dialog_destroy(); }

void ui_noformat_dialog_destroy(void) { ui_dialog_destroy(); }

void ui_scansdcard_dialog_destroy(void) { ui_dialog_destroy(); }

void ui_nosdcard_dialog_create(void) { ui_dialog_create("存储状态", "没有SD卡"); }

void ui_noformat_dialog_create(void) { ui_dialog_create("存储状态", "SD卡未格式化"); }

void ui_scansdcard_dialog_create(void) { ui_dialog_create("存储状态", "SD卡扫描进行中"); }

void ui_storage_status_dialog_destroy(void) {
	if (storage_group) {
		lv_port_indev_group_destroy(storage_group);
		storage_group = NULL;
	}
	if (storage_status_dialog_obj) {
		ui_common_remove_style_all(storage_status_dialog_obj);
		lv_obj_del(storage_status_dialog_obj);
		storage_status_dialog_obj = NULL;
	}
}

void ui_storage_status_dialog_create(void) {
	char info[64];
	int32_t sto_free = 0;
	int32_t sto_total = 0;
	int32_t sto_use = 0;
	int32_t progress = 0;
	lv_obj_t *obj, *obj_bg, *context_obj;
	lv_obj_t *storage_string_obj, *storage_progress_bar_obj;
	lv_color_t color;
	lv_color_t text_color;
	lv_color_t dialog_color = {
		.full = 0xFF222D30,
	};
	static lv_style_t style;

	text_color.full = 0xffffffff;
	color.full = 0xFF000000;

	lv_style_reset(&style);
	lv_style_init(&style);
	lv_style_set_layout(&style, LV_LAYOUT_FLEX);
	lv_style_set_flex_flow(&style, LV_FLEX_FLOW_COLUMN_WRAP);
	lv_style_set_flex_main_place(&style, LV_FLEX_ALIGN_SPACE_EVENLY);
	lv_style_set_flex_cross_place(&style, LV_FLEX_ALIGN_CENTER);

	storage_group = lv_port_indev_group_create();
	if (storage_group == NULL)
		return;

	storage_status_dialog_obj = obj = lv_obj_create(lv_scr_act());
	lv_obj_set_size(obj, lv_pct(100), lv_pct(100));
	lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_bg_color(obj, color, 0);
	lv_obj_set_style_border_color(obj, color, 0);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_50, 0);
	lv_obj_add_event_cb(obj, storage_status_dialog_exit_cb, LV_EVENT_CLICKED,
	                    storage_status_dialog_obj);
	lv_group_add_obj(storage_group, obj);

	obj_bg = obj = lv_obj_create(obj);
	lv_obj_set_size(obj, lv_pct(50), lv_pct(40));
	lv_obj_set_style_bg_color(obj, dialog_color, 0);
	lv_obj_set_style_border_color(obj, dialog_color, 0);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_80, 0);
	lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);
	lv_obj_add_style(obj, &style, 0);
	lv_obj_add_event_cb(obj, storage_status_dialog_exit_cb, LV_EVENT_CLICKED,
	                    storage_status_dialog_obj);
	lv_group_add_obj(storage_group, obj);

	obj = lv_label_create(obj_bg);
	lv_obj_set_style_text_font(obj, ttf_info_32.font, 0);
	lv_obj_set_style_text_color(obj, text_color, 0);
	lv_obj_set_size(obj, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(obj, "存储状态");
	lv_label_set_long_mode(obj, LV_LABEL_LONG_SCROLL_CIRCULAR);

	obj = lv_label_create(obj_bg);
	lv_label_set_text(obj, LV_SYMBOL_SD_CARD);
	lv_obj_set_style_text_font(obj, &lv_font_montserrat_48, 0);
	lv_obj_set_style_text_color(obj, lv_palette_main(LV_PALETTE_BLUE), 0);
	lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_align(obj, LV_ALIGN_TOP_RIGHT, 0, 0);

	context_obj = obj = lv_obj_create(obj_bg);
	lv_obj_set_width(obj, lv_pct(100));
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_TOP, 0);
	lv_obj_add_style(obj, &style, 0);
	lv_obj_add_event_cb(obj, storage_status_dialog_exit_cb, LV_EVENT_CLICKED,
	                    storage_status_dialog_obj);
	lv_obj_set_flex_grow(obj, 1);
	lv_group_add_obj(storage_group, obj);

	text_color.full = 0xffffffff;
	storage_string_obj = obj = lv_label_create(context_obj);
	lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
	lv_obj_set_style_text_font(obj, ttf_info_32.font, 0);
	lv_obj_set_style_text_color(obj, text_color, 0);
	lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(obj, lv_pct(100));

	storage_progress_bar_obj = obj = lv_bar_create(context_obj);
	lv_obj_set_width(obj, lv_pct(100));
	rkipc_storage_get_capacity(&g_sd_phandle, &sto_total, &sto_free);
	sto_total = sto_total >> 20;
	sto_free = sto_free >> 20;
	sto_use = sto_total - sto_free;
	if (sto_total > 0)
		progress = sto_use * 100 / sto_total;
	if (progress > 100)
		progress = 100;
	snprintf(info, sizeof(info), "%s %dGB/%s %dGB", "已使用", sto_use, "总共", sto_total);
	if (storage_string_obj)
		lv_label_set_text(storage_string_obj, info);
	if (storage_progress_bar_obj)
		lv_bar_set_value(storage_progress_bar_obj, progress, LV_ANIM_OFF);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static void storage_status_dialog_exit_cb(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	if (code == LV_EVENT_CLICKED) {
		ui_storage_status_dialog_destroy();
	}
}
