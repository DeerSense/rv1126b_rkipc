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

#include "ui_dialog_format.h"
#include "evdev.h"
#include "lvgl/common/lv_msg.h"
#include "lvgl/porting/lv_port_indev.h"
#include "storage.h"
#include "ui_common.h"
#include "ui_dialog.h"
#include "ui_resource_manager.h"
#include <linux/input.h>

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_obj_t *format_obj = NULL;
static lv_obj_t *format_ani_obj = NULL;
static lv_group_t *format_group = NULL;
/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void ui_format_dialog_destory();

/**********************
 *   STATIC FUNCTIONS
 **********************/
extern lv_ft_info_t ttf_info_32;

static void format_dialog_destroy_event_cb(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_target(e);
	if (code == LV_EVENT_CLICKED)
		ui_format_dialog_destory();
}

static pthread_t format_task_id;
static bool format_task_done = false;
static int format_task_result = 0;

static void *format_task_func(void *arg) {
	LOG_INFO("start format\n");
	format_task_result = rk_storage_deinit();
	LOG_INFO("deinit storage done, ret = %#X\n", format_task_result);
	format_task_result = rk_storage_format();
	LOG_INFO("format storage done, ret = %#X\n", format_task_result);
	format_task_result = rk_storage_init();
	LOG_INFO("init storage done, ret = %#X\n", format_task_result);
	format_task_done = true;
}

static void format_timer_func(lv_timer_t *timer) {
	if (format_task_done) {
		pthread_join(format_task_id, NULL);
		if (format_task_result != 0)
			ui_dialog_create("WARNING", "格式化失败!");
		else
			ui_dialog_create("WARNING", "格式化成功!");
		lv_timer_del(timer);
		if (format_ani_obj) {
			lv_obj_del(format_ani_obj);
			format_ani_obj = NULL;
		}
		ui_format_dialog_destory();
	}
}

static void start_format(void) {
	lv_obj_t *obj;
	lv_color_t color;
	color.full = 0xFF000000;

	format_ani_obj = obj = lv_obj_create(lv_scr_act());
	lv_obj_set_size(obj, LV_HOR_RES, LV_VER_RES);
	lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_bg_color(obj, color, 0);
	lv_obj_set_style_border_color(obj, color, 0);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_50, 0);

	obj = lv_spinner_create(obj, 1000, 60);
	lv_obj_set_size(obj, 100, 100);
	lv_obj_center(obj);

	format_task_done = false;
	format_task_result = 0;
	pthread_create(&format_task_id, NULL, format_task_func, NULL);
	lv_timer_t *timer = lv_timer_create(format_timer_func, 200, NULL);
	lv_timer_ready(timer);
}

static void format_start_event_cb(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_target(e);
	if (code == LV_EVENT_CLICKED) {
		ui_format_dialog_destory();
		start_format();
	}
}

void ui_format_dialog_create(void) {
	if (format_obj)
		return;

	lv_obj_t *obj;
	lv_obj_t *obj_bg;
	lv_obj_t *context_obj;
	lv_color_t color;
	lv_color_t text_color;
	lv_color_t dialog_color = {
		.full = 0xFF222D30,
	};
	static lv_style_t dialog_style, btn_style, btn_style_focused, symbol_style;

	lv_style_reset(&dialog_style);
	lv_style_init(&dialog_style);
	lv_style_set_layout(&dialog_style, LV_LAYOUT_FLEX);
	lv_style_set_flex_flow(&dialog_style, LV_FLEX_FLOW_COLUMN_WRAP);
	lv_style_set_flex_main_place(&dialog_style, LV_FLEX_ALIGN_SPACE_EVENLY);
	lv_style_set_flex_cross_place(&dialog_style, LV_FLEX_ALIGN_CENTER);

	lv_style_reset(&btn_style);
	lv_style_init(&btn_style);
	lv_style_set_bg_color(&btn_style, lv_color_hex(0xFFFFFF));
	lv_style_set_text_color(&btn_style, lv_color_hex(0x000000));
	lv_style_set_bg_opa(&btn_style, LV_OPA_COVER);
	lv_style_set_pad_top(&btn_style, 0);
	lv_style_set_pad_bottom(&btn_style, 0);

	lv_style_reset(&btn_style_focused);
	lv_style_init(&btn_style_focused);
	lv_style_set_bg_color(&btn_style_focused, lv_palette_lighten(LV_PALETTE_BLUE, 3));
	lv_style_set_text_color(&btn_style_focused, lv_color_hex(0x000000));
	lv_style_set_bg_opa(&btn_style_focused, LV_OPA_COVER);
	lv_style_set_pad_top(&btn_style_focused, 0);
	lv_style_set_pad_bottom(&btn_style_focused, 0);

	lv_style_reset(&symbol_style);
	lv_style_init(&symbol_style);
	lv_style_set_text_font(&symbol_style, &lv_font_montserrat_48);
	lv_style_set_text_color(&symbol_style, lv_palette_darken(LV_PALETTE_GREY, 3));
	lv_style_set_text_align(&symbol_style, LV_TEXT_ALIGN_CENTER);

	format_group = lv_port_indev_group_create();
	if (format_group == NULL)
		return;

	color = lv_color_make(0X00, 0X00, 0X00);
	format_obj = obj = lv_obj_create(lv_scr_act());
	lv_obj_set_size(obj, lv_pct(100), lv_pct(100));
	lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_bg_color(obj, color, 0);
	lv_obj_set_style_border_color(obj, color, 0);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_50, 0);
	lv_obj_add_event_cb(obj, format_dialog_destroy_event_cb, LV_EVENT_CLICKED, NULL);
	lv_obj_add_event_cb(obj, format_dialog_destroy_event_cb, LV_EVENT_FOCUSED, NULL);
	lv_obj_add_event_cb(obj, format_dialog_destroy_event_cb, LV_EVENT_DEFOCUSED, NULL);

	obj_bg = obj = lv_obj_create(obj);
	lv_obj_set_size(obj, lv_pct(40), lv_pct(40));
	lv_obj_set_style_bg_color(obj, dialog_color, 0);
	lv_obj_set_style_border_color(obj, dialog_color, 0);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_80, 0);
	lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);
	lv_obj_add_style(obj, &dialog_style, 0);

	text_color = lv_color_make(0XFF, 0XFF, 0XFF);
	obj = lv_label_create(obj_bg);
	lv_obj_set_style_text_font(obj, ttf_info_32.font, 0);
	lv_obj_set_style_text_color(obj, text_color, 0);
	lv_obj_set_size(obj, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(obj, "确认格式化?");
	lv_label_set_long_mode(obj, LV_LABEL_LONG_SCROLL_CIRCULAR);

	context_obj = obj = lv_obj_create(obj_bg);
	lv_obj_set_width(obj, lv_pct(100));
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_TOP, 0);
	lv_obj_add_event_cb(obj, format_dialog_destroy_event_cb, LV_EVENT_CLICKED, NULL);
	lv_obj_set_flex_grow(obj, 1);

	obj = lv_btn_create(context_obj);
	lv_obj_add_style(obj, &btn_style, 0);
	lv_obj_add_style(obj, &btn_style_focused, LV_STATE_FOCUSED);
	lv_group_add_obj(format_group, obj);
	lv_obj_add_event_cb(obj, format_dialog_destroy_event_cb, LV_EVENT_CLICKED, (void *)0);
	lv_obj_add_event_cb(obj, format_dialog_destroy_event_cb, LV_EVENT_FOCUSED, NULL);
	lv_obj_add_event_cb(obj, format_dialog_destroy_event_cb, LV_EVENT_DEFOCUSED, NULL);
	lv_obj_set_size(obj, lv_pct(30), LV_SIZE_CONTENT);
	lv_obj_align(obj, LV_ALIGN_BOTTOM_LEFT, 0, 0);

	obj = lv_label_create(obj);
	lv_obj_add_style(obj, &symbol_style, 0);
	lv_label_set_text(obj, LV_SYMBOL_CLOSE);
	lv_obj_set_style_pad_left(obj, 60, 0);

	obj = lv_btn_create(context_obj);
	lv_group_add_obj(format_group, obj);
	lv_obj_add_style(obj, &btn_style, 0);
	lv_obj_add_style(obj, &btn_style_focused, LV_STATE_FOCUSED);
	lv_obj_add_event_cb(obj, format_start_event_cb, LV_EVENT_CLICKED, (void *)0);
	lv_obj_add_event_cb(obj, format_start_event_cb, LV_EVENT_FOCUSED, NULL);
	lv_obj_add_event_cb(obj, format_start_event_cb, LV_EVENT_DEFOCUSED, NULL);
	lv_obj_set_size(obj, lv_pct(30), LV_SIZE_CONTENT);
	lv_obj_align(obj, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

	obj = lv_label_create(obj);
	lv_obj_add_style(obj, &symbol_style, 0);
	lv_label_set_text(obj, LV_SYMBOL_OK);
	lv_obj_set_style_pad_left(obj, 60, 0);
}

void ui_format_dialog_destory() {
	if (format_group) {
		lv_port_indev_group_destroy(format_group);
		format_group = NULL;
	}
	if (format_obj) {
		ui_common_remove_style_all(format_obj);
		lv_obj_del(format_obj);
		format_obj = NULL;
	}
}
