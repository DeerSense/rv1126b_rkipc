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
#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "ui_dialog.c"

#include "ui_dialog.h"
#include "lvgl/porting/lv_port_indev.h"
#include "ui_common.h"
#include "common.h"
#include <pthread.h>

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/
extern lv_ft_info_t ttf_info_32;

static lv_obj_t *dialog_obj = NULL;
static lv_group_t *dialog_group = NULL;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
static void close_event_handler(lv_event_t *e);

void ui_dialog_destroy(void) {
	if (dialog_group) {
		lv_port_indev_group_destroy(dialog_group);
		dialog_group = NULL;
	}
	if (NULL != dialog_obj) {
		ui_common_remove_style_all(dialog_obj);
		lv_obj_del(dialog_obj);
		dialog_obj = NULL;
	}
}

lv_obj_t *ui_dialog_create(const char *head_title, const char *content) {
	ui_dialog_destroy();

	lv_obj_t *obj = NULL;
	lv_obj_t *obj_bg = NULL;
	lv_color_t bg_color;
	lv_color_t text_color;

	dialog_group = lv_port_indev_group_create();
	if (dialog_group == NULL)
		return NULL;

	text_color = lv_color_make(0XFF, 0XFF, 0XFF);
	bg_color = lv_color_hex(0xFF000000L);

	dialog_obj = obj = lv_obj_create(lv_scr_act());
	lv_obj_set_size(obj, lv_pct(100), lv_pct(100));
	lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_bg_color(obj, bg_color, 0);
	lv_obj_set_style_border_color(obj, bg_color, 0);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_50, 0);
	lv_obj_add_event_cb(obj, close_event_handler, LV_EVENT_CLICKED, dialog_obj);
	lv_obj_add_event_cb(obj, close_event_handler, LV_EVENT_FOCUSED, dialog_obj);
	lv_obj_add_event_cb(obj, close_event_handler, LV_EVENT_DEFOCUSED, dialog_obj);
	lv_group_add_obj(dialog_group, obj);

	bg_color = lv_color_hex(0xFF222D30);
	obj_bg = obj = lv_obj_create(obj);
	lv_obj_set_size(obj, lv_pct(50), lv_pct(40));
	lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_bg_color(obj, bg_color, 0);
	lv_obj_set_style_border_color(obj, bg_color, 0);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_80, 0);
	lv_obj_add_event_cb(obj, close_event_handler, LV_EVENT_CLICKED, dialog_obj);
	lv_obj_add_event_cb(obj, close_event_handler, LV_EVENT_FOCUSED, dialog_obj);
	lv_obj_add_event_cb(obj, close_event_handler, LV_EVENT_DEFOCUSED, dialog_obj);
	lv_group_add_obj(dialog_group, obj);
	lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN_WRAP);
	lv_obj_set_style_flex_main_place(obj, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
	lv_obj_set_style_flex_cross_place(obj, LV_FLEX_ALIGN_CENTER, 0);

	obj = lv_label_create(obj_bg);
	lv_label_set_text(obj, head_title);
	lv_label_set_long_mode(obj, LV_LABEL_LONG_SCROLL_CIRCULAR);
	lv_obj_set_style_text_font(obj, ttf_info_32.font, 0);
	lv_obj_set_style_text_color(obj, text_color, 0);
	lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_size(obj, LV_PCT(100), LV_SIZE_CONTENT);

	obj = lv_label_create(obj_bg);
	lv_label_set_text(obj, LV_SYMBOL_WARNING);
	lv_obj_set_style_text_font(obj, &lv_font_montserrat_48, 0);
	lv_obj_set_style_text_color(obj, lv_palette_main(LV_PALETTE_YELLOW), 0);
	lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_align(obj, LV_ALIGN_TOP_RIGHT, 0, 0);

	obj = lv_obj_create(obj_bg);
	lv_obj_set_width(obj, lv_pct(100));
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_TOP, 0);
	lv_obj_add_event_cb(obj, close_event_handler, LV_EVENT_CLICKED, dialog_obj);
	lv_obj_add_event_cb(obj, close_event_handler, LV_EVENT_FOCUSED, dialog_obj);
	lv_obj_add_event_cb(obj, close_event_handler, LV_EVENT_DEFOCUSED, dialog_obj);
	lv_group_add_obj(dialog_group, obj);
	lv_obj_set_flex_grow(obj, 1);

	obj = lv_label_create(obj);
	lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
	lv_label_set_text(obj, content);
	lv_obj_set_style_text_font(obj, ttf_info_32.font, 0);
	lv_obj_set_style_text_color(obj, text_color, 0);
	lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(obj, lv_pct(100));
	lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);

	return dialog_obj;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static void close_event_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	if (code == LV_EVENT_CLICKED) {
		ui_dialog_destroy();
	}
}

static struct {
	enum TASK_STATE {
		TASK_IDLE,
		TASK_RUNNING,
		TASK_DONE
	} task_state;
	pthread_t task_id;
	UI_TASK_TYPE func;
	void *func_arg;
	UI_TASK_TYPE dtor;
	void *dtor_arg;
	lv_obj_t *ani_obj;
	lv_obj_t *spin_obj;
} ui_async_task;

static void *task_wrap_func(void *arg) {
	LOG_INFO("start task\n");
	if (ui_async_task.func)
		ui_async_task.func(ui_async_task.func_arg);
	else
		LOG_ERROR("bad task!\n");
	LOG_INFO("stop_task\n");
	ui_async_task.task_state = TASK_DONE;
	return NULL;
}

static void task_timer(lv_timer_t *timer) {
	if (ui_async_task.task_state == TASK_DONE) {
		LOG_INFO("task done\n");
		pthread_join(ui_async_task.task_id, NULL);
		ui_async_task.task_state = TASK_IDLE;
		lv_timer_del(timer);
		if (ui_async_task.spin_obj) {
			lv_obj_del(ui_async_task.spin_obj);
			ui_async_task.spin_obj = NULL;
		}
		if (ui_async_task.ani_obj) {
			lv_obj_del(ui_async_task.ani_obj);
			ui_async_task.ani_obj = NULL;
		}
		ui_async_task.func = NULL;
		ui_async_task.func_arg = NULL;
		if (ui_async_task.dtor) {
			ui_async_task.dtor(ui_async_task.dtor_arg);
			LOG_INFO("dtor done\n");
		}
	}
}

int ui_start_async_task(UI_TASK_TYPE func, void *func_arg, UI_TASK_TYPE dtor, void *dtor_arg) {
	lv_color_t color;
	color.full = 0xFF000000;
	if (ui_async_task.task_state != TASK_IDLE) {
		LOG_ERROR("previous task is not finished\n");
		return -1;
	}

	ui_async_task.ani_obj = lv_obj_create(lv_scr_act());
	lv_obj_set_size(ui_async_task.ani_obj, LV_HOR_RES, LV_VER_RES);
	lv_obj_align(ui_async_task.ani_obj, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_bg_color(ui_async_task.ani_obj, color, 0);
	lv_obj_set_style_border_color(ui_async_task.ani_obj, color, 0);
	lv_obj_set_style_radius(ui_async_task.ani_obj, 0, 0);
	lv_obj_set_style_bg_opa(ui_async_task.ani_obj, LV_OPA_50, 0);

	ui_async_task.spin_obj = lv_spinner_create(ui_async_task.ani_obj, 1000, 200);
	lv_obj_center(ui_async_task.spin_obj);

	ui_async_task.func = func;
	ui_async_task.func_arg = func_arg;
	ui_async_task.dtor = dtor;
	ui_async_task.dtor_arg = dtor_arg;
	ui_async_task.task_state = TASK_RUNNING;
	pthread_create(&ui_async_task.task_id, NULL, task_wrap_func, NULL);
	lv_timer_t *timer = lv_timer_create(task_timer, 200, NULL);
	lv_timer_ready(timer);
}

bool ui_async_task_is_running(void) {
	return (ui_async_task.task_state == TASK_RUNNING);
}

static struct {
	lv_group_t *group;
	lv_obj_t *dialog;
	const char *title;
	const ui_option_entry_t *option_array;
	int option_num;
	int focus_idx;
} config_box;

static void destroy_select_box(void);

static void select_box_doing_func(void *arg) {
	size_t index = (size_t)arg;
	if (index < config_box.option_num) {
		BTN_CB_TYPE cb = config_box.option_array[index].cb;
		if (cb)
			cb(config_box.option_array[index].userdata);
	}
}

static void select_box_done_func(void *arg) {
	destroy_select_box();
}

static void btn_event_cb(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_target(e);
	lv_obj_t *parent = lv_obj_get_parent(obj);
	size_t index = lv_obj_get_index(obj);
	int child_idx = 0, child_cnt = 0;
	if (code == LV_EVENT_CLICKED) {
		if (obj == config_box.dialog) {
			destroy_select_box();
			return;
		}
		child_cnt = lv_obj_get_child_cnt(parent);
		while (child_idx < child_cnt) {
			lv_obj_t *child = lv_obj_get_child(parent, child_idx);
			if (!child)
				break;
			++child_idx;
			lv_obj_clear_state(child, LV_STATE_CHECKED);
		}
		ui_start_async_task(select_box_doing_func, (void *)index, select_box_done_func, NULL);
	}
}

static lv_obj_t *create_dialog(lv_obj_t *parent) {
	lv_color_t text_color = lv_color_make(0XFF, 0XFF, 0XFF);
	lv_color_t dialog_color = {
		.full = 0xFF222D30,
	};
	static lv_style_t style;
	lv_style_reset(&style);
	lv_style_init(&style);
	lv_style_set_text_font(&style, ttf_info_32.font);
	lv_style_set_text_color(&style, text_color);
	lv_style_set_text_align(&style, LV_TEXT_ALIGN_CENTER);

	lv_obj_t *dialog = lv_obj_create(parent);
	lv_obj_set_style_bg_color(dialog, dialog_color, 0);
	lv_obj_set_style_bg_opa(dialog, LV_OPA_80, 0);
	lv_obj_set_style_border_color(dialog, dialog_color, 0);
	lv_obj_set_size(dialog, lv_pct(60), lv_pct(60));
	lv_obj_align(dialog, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_flex_flow(dialog, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(dialog, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	lv_obj_t *title = lv_label_create(parent);
	lv_label_set_text(title, config_box.title);
	lv_obj_add_style(title, &style, 0);
	lv_obj_align_to(title, dialog, LV_ALIGN_TOP_MID, 0, 20);

	return dialog;
}

static void create_buttons(lv_obj_t *parent, lv_group_t *g) {
	int select_btn_idx = 0;
	lv_obj_t *btn = NULL, *label = NULL;
	static lv_style_t btn_style, btn_focus_style;

	lv_style_reset(&btn_style);
	lv_style_init(&btn_style);
	lv_style_set_bg_color(&btn_style, lv_palette_lighten(LV_PALETTE_GREY, 3));
	lv_style_set_text_color(&btn_style, lv_color_hex(0x000000));
	lv_style_set_bg_opa(&btn_style, LV_OPA_COVER);
	lv_style_set_radius(&btn_style, 10);
	lv_style_set_pad_left(&btn_style, 16);
	lv_style_set_pad_top(&btn_style, 16);

	lv_style_reset(&btn_focus_style);
	lv_style_init(&btn_focus_style);
	lv_style_set_bg_color(&btn_focus_style, lv_palette_main(LV_PALETTE_GREY));
	lv_style_set_text_color(&btn_focus_style, lv_color_hex(0x000000));
	lv_style_set_bg_opa(&btn_focus_style, LV_OPA_COVER);
	lv_style_set_outline_opa(&btn_focus_style, LV_OPA_TRANSP);
	lv_style_set_radius(&btn_focus_style, 10);
	lv_style_set_pad_left(&btn_focus_style, 16);
	lv_style_set_pad_top(&btn_focus_style, 16);

	for (int i = 0; i < config_box.option_num; i++) {
		btn = lv_checkbox_create(parent);
		lv_obj_set_size(btn, lv_pct(80), lv_pct(15));
		lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
		lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, NULL);
		lv_checkbox_set_text(btn, config_box.option_array[i].name);
		lv_obj_set_style_text_font(btn, ttf_info_32.font, LV_PART_MAIN);
		lv_obj_add_style(btn, &btn_style, LV_PART_MAIN);
		lv_obj_add_style(btn, &btn_focus_style, LV_STATE_FOCUSED);

		lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_INDICATOR);
		lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
		lv_obj_set_style_text_align(btn, LV_TEXT_ALIGN_RIGHT, LV_PART_INDICATOR);

		lv_group_add_obj(g, btn);
	}
	select_btn_idx = config_box.focus_idx; // 0 is title obj
	LOG_DEBUG("default select index %d\n", select_btn_idx);
	btn = lv_obj_get_child(parent, select_btn_idx);
	lv_obj_add_state(btn, LV_STATE_CHECKED);
	lv_group_focus_obj(btn);
}

void create_select_box(const char *title, const ui_option_entry_t *option_array, int option_num, int focus_idx) {
	lv_color_t color = lv_color_hex(0xFF000000);
	lv_obj_t *dialog = NULL;
	if (!option_array || option_num <= 0 || focus_idx < 0 || focus_idx >= option_num || !title) {
		LOG_ERROR("invalid parameters\n");
		return;
	}
	LOG_DEBUG("create config box %s, option num %d\n", title, option_num);
	config_box.group = lv_port_indev_group_create();
	if (config_box.group == NULL)
		return;
	config_box.title = title;
	config_box.option_array = option_array;
	config_box.option_num = option_num;
	config_box.focus_idx = focus_idx;
	config_box.dialog = lv_obj_create(lv_scr_act());
	lv_obj_set_size(config_box.dialog, lv_pct(100), lv_pct(100));
	lv_obj_align(config_box.dialog, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_bg_color(config_box.dialog, color, 0);
	lv_obj_set_style_border_color(config_box.dialog, color, 0);
	lv_obj_set_style_radius(config_box.dialog, 0, 0);
	lv_obj_set_style_bg_opa(config_box.dialog, LV_OPA_50, 0);
	lv_obj_add_event_cb(config_box.dialog, btn_event_cb, LV_EVENT_CLICKED,
	                    NULL);
	lv_group_add_obj(config_box.group, config_box.dialog);

	dialog = create_dialog(config_box.dialog);
	create_buttons(dialog, config_box.group);
}

static void destroy_select_box(void) {
	if (config_box.group) {
		lv_port_indev_group_destroy(config_box.group);
		config_box.group = NULL;
	}
	if (config_box.dialog) {
		lv_obj_del(config_box.dialog);
		config_box.dialog = NULL;
	}
	memset(&config_box, 0, sizeof(config_box));
}