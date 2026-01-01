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
#define LOG_TAG "ui_playlist.c"

#include <linux/input.h>

#include "common.h"
#include "evdev.h"
#include "filelist.h"
#include "lvgl/porting/lv_port_indev.h"
#include "storage.h"
#include "ui_common.h"
#include "ui_page_manager.h"
#include "ui_player.h"
#include "ui_resource_manager.h"
#include "video.h"

#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct {
	lv_group_t *group;
	lv_obj_t *menu_obj;
	lv_obj_t *list_obj;
	lv_obj_t *exit_obj;
	lv_obj_t *exit_label_obj;

	const char *lable;
	lv_img_dsc_t *icon_lable;
	lv_obj_t *cur_btn;
	struct file_list list;
	RK_MODE_E mode;
} ui_playlist_context_t;

static lv_style_t style_btn;
static lv_style_t style_btn_chk;
static lv_style_t style_title;
static lv_style_t style_lable;
static lv_style_t style_time;

static ui_playlist_context_t playlist_ctx;
static int32_t orig_pos;
static bool switch_from_player;

extern lv_ft_info_t ttf_info_32;

#define LINE_NUM 20
#define LIST_ITEM_HEIGHT 100

static void btn_click_event_cb(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_target(e);
	size_t track_id = (size_t)lv_obj_get_user_data(obj);

	if (code == LV_EVENT_CLICKED) {
		player_file_info_t file_info;
		memset(&file_info, 0, sizeof(file_info));

		if (track_id < playlist_ctx.list.count) {
			snprintf(file_info.path, sizeof(file_info.path), "%s/%s", playlist_ctx.list.path,
			         playlist_ctx.list.files[track_id].name);
			if (playlist_ctx.mode != RK_PHOTO_MODE)
				ui_page_push_page("video_player", &file_info);
			else
				ui_page_push_page("photo_player", (void *)(track_id));
			switch_from_player = true;
		}
	}
}

static lv_obj_t *add_list_btn(lv_obj_t *parent, uint32_t track_id) {
	const char *title = playlist_ctx.list.files[track_id].name;
	const char *lable = playlist_ctx.lable;

	lv_color_t text_color = lv_color_make(0xff, 0xff, 0xff);
	static lv_style_t symbol_style;
	lv_style_reset(&symbol_style);
	lv_style_init(&symbol_style);
	lv_style_set_text_font(&symbol_style, &lv_font_montserrat_48);
	lv_style_set_text_color(&symbol_style, text_color);
	lv_style_set_text_align(&symbol_style, LV_TEXT_ALIGN_CENTER);

	lv_obj_t *btn = lv_obj_create(parent);
	lv_obj_remove_style_all(btn);
	lv_obj_set_size(btn, lv_pct(100), LIST_ITEM_HEIGHT);
	lv_obj_set_pos(btn, 0, LIST_ITEM_HEIGHT * track_id);
	lv_obj_set_user_data(btn, (void *)(track_id));
	lv_obj_add_style(btn, &style_btn, 0);
	lv_obj_add_style(btn, &style_btn_chk, LV_STATE_FOCUSED);
	lv_obj_add_event_cb(btn, btn_click_event_cb, LV_EVENT_CLICKED, NULL);

	lv_obj_t *icon = lv_label_create(btn);
	lv_obj_set_style_pad_hor(icon, 10, 0);
	if (playlist_ctx.mode == RK_PHOTO_MODE)
		lv_label_set_text(icon, LV_SYMBOL_IMAGE);
	else
		lv_label_set_text(icon, LV_SYMBOL_VIDEO);
	lv_obj_add_style(icon, &symbol_style, 0);
	lv_obj_set_grid_cell(icon, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 0, 2);

	lv_obj_t *title_obj = lv_label_create(btn);
	lv_label_set_text(title_obj, title);
	lv_obj_set_grid_cell(title_obj, LV_GRID_ALIGN_START, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
	lv_obj_add_style(title_obj, &style_title, 0);

	lv_obj_t *label_obj = lv_label_create(btn);
	lv_label_set_text(label_obj, lable);
	lv_obj_set_grid_cell(label_obj, LV_GRID_ALIGN_START, 1, 1, LV_GRID_ALIGN_CENTER, 1, 1);
	lv_obj_add_style(label_obj, &style_lable, 0);

	return btn;
}

static void return_event_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_target(e);

	if (code == LV_EVENT_CLICKED) {
		ui_page_pop_page();
	}
}

static void scroll_event_cb(lv_event_t *e) {
	lv_obj_t *child = NULL;
	int32_t step_size = 0;
	int32_t new_pos = 0;
	lv_obj_t *obj = lv_event_get_target(e);

	if (obj->spec_attr->scroll.y < 0)
		new_pos = (lv_obj_get_scroll_y(obj) + LIST_ITEM_HEIGHT - 1) / LIST_ITEM_HEIGHT;

	if (new_pos < (LINE_NUM >> 1))
		return;

	step_size = new_pos - orig_pos;
	if (0 == step_size)
		return;

	if (step_size > 0) {
		int32_t actual_step_size = 0;
		size_t track_id = 0;

		child = lv_obj_get_child(obj, -1);
		track_id = (size_t)lv_obj_get_user_data(child);
		while (++track_id < playlist_ctx.list.count && actual_step_size < step_size) {
			actual_step_size++;
			add_list_btn(playlist_ctx.list_obj, track_id);
		}

		if (0 != actual_step_size)
			orig_pos += actual_step_size;

		while ((actual_step_size--) > 0) {
			child = lv_obj_get_child(obj, 0);
			ui_common_remove_style_all(child);
			lv_obj_del(child);
		}

		return;
	} else {
		int32_t actual_step_size = 0;
		size_t track_id = 0;

		child = lv_obj_get_child(obj, 0);
		track_id = (size_t)lv_obj_get_user_data(child);

		while (--track_id >= 0 && actual_step_size > step_size) {
			actual_step_size--;
			child = add_list_btn(playlist_ctx.list_obj, track_id);
			lv_obj_move_to_index(child, 0);
		}

		if (0 != actual_step_size)
			orig_pos += actual_step_size;

		while ((actual_step_size++) < 0) {
			child = lv_obj_get_child(obj, -1);
			ui_common_remove_style_all(child);
			lv_obj_del(child);
		}

		return;
	}
}

static void playlist_layout_ctrl(lv_obj_t *page_obj) {
	lv_obj_t *obj = NULL;
	static const lv_coord_t page_cols[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
	static const lv_coord_t page_rows[] = {LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};

	obj = page_obj;
	lv_obj_set_grid_dsc_array(obj, page_cols, page_rows);
	lv_obj_set_style_layout(obj, LV_LAYOUT_GRID, 0);
	lv_obj_set_grid_cell(playlist_ctx.menu_obj, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 0,
	                     1);
	lv_obj_set_grid_cell(playlist_ctx.list_obj, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH,
	                     1, 1);

	static const lv_coord_t menu_cols[] = {LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT,
	                                       LV_GRID_FR(3),   LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
	static const lv_coord_t menu_rows[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};

	obj = playlist_ctx.menu_obj;
	lv_obj_set_grid_dsc_array(obj, menu_cols, menu_rows);
	lv_obj_set_style_layout(obj, LV_LAYOUT_GRID, 0);
}

static void playlist_create_ctrl(lv_obj_t *page_obj) {
	lv_obj_t *obj = NULL;

	static const lv_coord_t grid_cols[] = {LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_CONTENT,
	                                       LV_GRID_TEMPLATE_LAST};
	static const lv_coord_t grid_rows[] = {LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};

	lv_color_t text_color = lv_color_make(0xff, 0xff, 0xff);
	static lv_style_t symbol_style;
	lv_style_reset(&symbol_style);
	lv_style_init(&symbol_style);
	lv_style_set_text_font(&symbol_style, &lv_font_montserrat_48);
	lv_style_set_text_color(&symbol_style, text_color);
	lv_style_set_text_align(&symbol_style, LV_TEXT_ALIGN_CENTER);
	lv_style_set_pad_all(&symbol_style, 5);

	obj = page_obj;
	lv_obj_remove_style_all(obj);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(obj, lv_color_hex(0xFF04171D), 0);

	playlist_ctx.menu_obj = obj = lv_obj_create(page_obj);
	lv_obj_set_size(obj, lv_pct(100), lv_pct(10));
	lv_obj_set_style_bg_color(obj, lv_color_hex(0xFF04171D), 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_set_style_radius(obj, 0, 0);

	playlist_ctx.exit_obj = lv_btn_create(playlist_ctx.menu_obj);
	lv_obj_remove_style_all(playlist_ctx.exit_obj);
	lv_obj_set_width(playlist_ctx.exit_obj, lv_pct(10));
	lv_obj_set_style_bg_opa(playlist_ctx.exit_obj, LV_OPA_TRANSP, 0);
	lv_obj_add_event_cb(playlist_ctx.exit_obj, return_event_handler, LV_EVENT_CLICKED, NULL);
	playlist_ctx.exit_label_obj = lv_label_create(playlist_ctx.exit_obj);
	lv_obj_remove_style_all(playlist_ctx.exit_label_obj);
	lv_label_set_text(playlist_ctx.exit_label_obj, LV_SYMBOL_LEFT);
	lv_obj_add_style(playlist_ctx.exit_label_obj, &symbol_style, 0);

	static lv_style_t style_scrollbar;
	lv_style_reset(&style_scrollbar);
	lv_style_init(&style_scrollbar);
	lv_style_set_width(&style_scrollbar, 4);
	lv_style_set_bg_opa(&style_scrollbar, LV_OPA_COVER);
	lv_style_set_bg_color(&style_scrollbar, lv_color_hex3(0xeee));
	lv_style_set_radius(&style_scrollbar, LV_RADIUS_CIRCLE);
	lv_style_set_pad_right(&style_scrollbar, 4);

	/*Create an empty transparent container*/
	playlist_ctx.list_obj = obj = lv_obj_create(page_obj);
	lv_obj_remove_style_all(obj);
	lv_obj_set_width(obj, LV_HOR_RES);
	lv_obj_add_style(obj, &style_scrollbar, LV_PART_SCROLLBAR);
	lv_obj_add_event_cb(obj, scroll_event_cb, LV_EVENT_SCROLL, NULL);

	lv_style_reset(&style_btn);
	lv_style_init(&style_btn);
	lv_style_set_bg_opa(&style_btn, LV_OPA_TRANSP);
	lv_style_set_grid_column_dsc_array(&style_btn, grid_cols);
	lv_style_set_grid_row_dsc_array(&style_btn, grid_rows);
	lv_style_set_grid_row_align(&style_btn, LV_GRID_ALIGN_CENTER);
	lv_style_set_layout(&style_btn, LV_LAYOUT_GRID);
	lv_style_set_pad_right(&style_btn, 20);

	lv_style_reset(&style_btn_chk);
	lv_style_init(&style_btn_chk);
	lv_style_set_bg_opa(&style_btn_chk, LV_OPA_COVER);
	lv_style_set_bg_color(&style_btn_chk, lv_color_hex(0x4c4965));

	lv_style_reset(&style_title);
	lv_style_init(&style_title);
	lv_style_set_text_font(&style_title, ttf_info_32.font);
	lv_style_set_text_color(&style_title, lv_color_hex(0xffffff));

	memset(&style_lable, 0, sizeof(style_lable));
	lv_style_reset(&style_lable);
	lv_style_init(&style_lable);
	lv_style_set_text_font(&style_lable, ttf_info_32.font);
	lv_style_set_text_color(&style_lable, lv_color_hex(0xb1b0be));

	memset(&style_time, 0, sizeof(style_time));
	lv_style_reset(&style_time);
	lv_style_init(&style_time);
	lv_style_set_text_color(&style_time, lv_color_hex(0xffffff));
}

static void playlist_add_indev(void) {
	lv_group_t *group = lv_port_indev_group_create();
	if (NULL == group)
		return;
	lv_group_add_obj(group, playlist_ctx.exit_obj);

	playlist_ctx.group = group;
}

static void playlist_delete_indev(void) {
	if (NULL != playlist_ctx.group) {
		lv_port_indev_group_destroy(playlist_ctx.group);
		playlist_ctx.group = NULL;
	}
}

static void playlist_destroy_ctrl(lv_obj_t *page_obj) {
	if (NULL != playlist_ctx.exit_obj) {
		ui_common_remove_style_all(playlist_ctx.exit_obj);
		lv_obj_del(playlist_ctx.exit_obj);
		playlist_ctx.exit_obj = NULL;
	}
	if (NULL != playlist_ctx.menu_obj) {
		ui_common_remove_style_all(playlist_ctx.menu_obj);
		lv_obj_del(playlist_ctx.menu_obj);
		playlist_ctx.menu_obj = NULL;
	}

	if (NULL != playlist_ctx.list_obj) {
		ui_common_remove_style_all(playlist_ctx.list_obj);
		lv_obj_del(playlist_ctx.list_obj);
		playlist_ctx.list_obj = NULL;
	}

	ui_common_remove_style_all(page_obj);
}

static int32_t playlist_param_init(lv_obj_t *page_obj) {
	uint32_t index = 0;

	const char *folder_name = NULL;
	playlist_ctx.cur_btn = NULL;

	orig_pos = (LINE_NUM >> 1);

	playlist_ctx.mode = (RK_MODE_E)lv_obj_get_user_data(page_obj);
	if (playlist_ctx.mode == RK_VIDEO_MODE) {
		folder_name = "/mnt/sdcard/video0";
		playlist_ctx.lable = "录像回放";
	} else if (playlist_ctx.mode == RK_PHOTO_MODE) {
		folder_name = "/mnt/sdcard/photo";
		playlist_ctx.lable = "照片";
	} else if (playlist_ctx.mode == RK_SLOW_MOTION_MODE) {
		folder_name = "/mnt/sdcard/slowmotion";
		playlist_ctx.lable = "慢动作回放";
	} else if (playlist_ctx.mode == RK_TIME_LAPSE_MODE) {
		folder_name = "/mnt/sdcard/timelapse";
		playlist_ctx.lable = "延时摄影";
	}
	if (rk_get_file_list(folder_name, &playlist_ctx.list) != 0) {
		LOG_ERROR("get file list %s failed\n", folder_name);
		return -1;
	}

	return 0;
}

static void playlist_param_deinit(void) {
	rk_free_file_list(&playlist_ctx.list);
	memset(&playlist_ctx, 0, sizeof(playlist_ctx));
}

static int32_t playlist_load_list(void) {
	uint32_t track_id = 0;
	for (track_id = 0; (track_id < playlist_ctx.list.count) && (track_id < LINE_NUM); track_id++)
		add_list_btn(playlist_ctx.list_obj, track_id);
	return 0;
}

static void playlist_page_create(lv_obj_t *page_obj) {
	LOG_INFO("enter\n");
	playlist_param_init(page_obj);

	playlist_create_ctrl(page_obj);
	playlist_layout_ctrl(page_obj);

	playlist_load_list();
}

static void playlist_page_enter(lv_obj_t *page_obj) {
	LOG_INFO("enter\n");
	playlist_add_indev();
}

static void playlist_page_exit(lv_obj_t *page_obj) {
	LOG_INFO("enter\n");
	playlist_delete_indev();
}

static void playlist_page_destroy(lv_obj_t *page_obj) {
	LOG_INFO("enter\n");
	playlist_destroy_ctrl(page_obj);
	playlist_param_deinit();
}

static UI_PAGE_HANDLER_T playlist_page = {.name = "playlist",
                                          .init = NULL,
                                          .create = playlist_page_create,
                                          .enter = playlist_page_enter,
                                          .destroy = playlist_page_destroy,
                                          .exit = playlist_page_exit};

UI_PAGE_REGISTER(playlist_page)
