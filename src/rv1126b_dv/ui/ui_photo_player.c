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
#define LOG_TAG "ui_photo_player.c"

#include "evdev.h"
#include "lvgl/porting/lv_port_indev.h"
#include "ui/common/ui_common.h"
#include "ui/common/ui_page_manager.h"
#include "ui_player.h"
#include "ui_resource_manager.h"
#include <linux/input.h>

#include "common.h"
#include "filelist.h"
#include "player.h"
#include "storage.h"
#include "video.h"

#include <limits.h>
#include <stdio.h>
#include <time.h>

/**********************
 *  STATIC PROTOTYPES
 **********************/
typedef struct {
	player_file_info_t file_info;
	lv_img_dsc_t img_data;

	lv_obj_t *img_obj;
	lv_obj_t *esc_obj;
	lv_obj_t *esc_label_obj;
	lv_obj_t *prev_obj;
	lv_obj_t *prev_label_obj;
	lv_obj_t *next_obj;
	lv_obj_t *next_label_obj;
	lv_obj_t *name_obj;

	lv_group_t *group;
	size_t cur_file_index;
	struct file_list list;
} ui_photo_player_context_t;

typedef enum { UI_CTRL_ESC = 0, UI_CTRL_PREV, UI_CTRL_NEXT, UI_CTRL_BUTT } UI_CTRL_ID_E;

/**********************
 *  STATIC VARIABLES
 **********************/
static ui_photo_player_context_t photo_player_ctx;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**********************
 *   STATIC FUNCTIONS
 **********************/

static int32_t photo_play(void) {
	int ret = 0;
	const char *ptr = NULL;
	lv_img_dsc_t *img_data = &photo_player_ctx.img_data;
	lv_img_cache_invalidate_src(lv_img_get_src(photo_player_ctx.img_obj));
	if (img_data->data) {
		free((void *)img_data->data);
		img_data->data = NULL;
	}

	do {
		RK_PHOTO_CONFIG config;
		RK_PHOTO_DATA data;
		memset(&config, 0, sizeof(config));
		memset(&data, 0, sizeof(data));
		config.file_path = photo_player_ctx.file_info.path;
		config.output_width = 1920;
		config.output_height = 1080;
		config.output_format = RK_FMT_BGRA8888;
		ret = rk_player_get_photo(&config, &data);
		if (ret == 0) {
			img_data->header.always_zero = 0;
			img_data->header.w = data.width;
			img_data->header.h = data.height;
			img_data->data_size = data.size;
			img_data->header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
			img_data->data = malloc(data.size);
			memcpy((void *)img_data->data, data.data, data.size);
			lv_img_set_src(photo_player_ctx.img_obj, img_data);
			rk_player_release_photo(&data);
		}

	} while (0);

#ifdef DUMP_IMG
	static int seq = 0;
	char dump_file_path[256];
	snprintf(dump_file_path, sizeof(dump_file_path), "/tmp/seq_%d.rgb", seq++);
	FILE *file = fopen(dump_file_path, "w");
	fwrite(img_data->data, 1, data.size, file);
	fclose(file);
#endif

	ptr = strrchr(photo_player_ctx.file_info.path, '/');
	if (NULL != ptr)
		lv_label_set_text(photo_player_ctx.name_obj, ptr + 1);
	return 0;
}

static void menu_event_cb(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_target(e);
	UI_CTRL_ID_E ctrl_id = (UI_CTRL_ID_E)lv_event_get_user_data(e);
	uint32_t file_num = photo_player_ctx.list.count;
	uint32_t cur_index = photo_player_ctx.cur_file_index;

	if (code == LV_EVENT_CLICKED) {
		switch (ctrl_id) {
		case UI_CTRL_ESC:
			ui_page_pop_page();
			break;
		case UI_CTRL_PREV:
			if (cur_index > 0)
				photo_player_ctx.cur_file_index = cur_index - 1;
			else
				photo_player_ctx.cur_file_index = file_num - 1;
			snprintf(photo_player_ctx.file_info.path, sizeof(photo_player_ctx.file_info.path),
			         "%s/%s", photo_player_ctx.list.path,
			         photo_player_ctx.list.files[photo_player_ctx.cur_file_index].name);
			photo_play();
			break;
		case UI_CTRL_NEXT:
			photo_player_ctx.cur_file_index = (cur_index + 1) % file_num;
			snprintf(photo_player_ctx.file_info.path, sizeof(photo_player_ctx.file_info.path),
			         "%s/%s", photo_player_ctx.list.path,
			         photo_player_ctx.list.files[photo_player_ctx.cur_file_index].name);
			photo_play();
			break;
		default:
			break;
		}
	}
}

static void photo_player_layout_ctrl(lv_obj_t *page_obj) {}

static void photo_player_create_ctrl(lv_obj_t *page_obj) {
	lv_obj_t *obj = NULL;
	lv_color_t text_color = lv_color_make(0xff, 0xff, 0xff);
	lv_color_t bg_color = lv_color_make(0x04, 0x17, 0x1D);

	static lv_style_t style;
	lv_style_reset(&style);
	lv_style_init(&style);
	lv_style_set_pad_all(&style, 5);
	lv_style_set_outline_width(&style, 1);
	lv_style_set_outline_opa(&style, LV_OPA_TRANSP);

	static lv_style_t symbol_style, symbol_focus_style;
	lv_style_reset(&symbol_style);
	lv_style_init(&symbol_style);
	lv_style_set_bg_opa(&symbol_style, LV_OPA_TRANSP);
	lv_style_set_text_font(&symbol_style, &lv_font_montserrat_48);
	lv_style_set_text_color(&symbol_style, lv_color_hex(0xFFFFFF));
	lv_style_set_text_align(&symbol_style, LV_TEXT_ALIGN_LEFT);
	lv_style_set_outline_opa(&symbol_style, LV_OPA_TRANSP);
	lv_style_set_outline_width(&symbol_style, 0);
	lv_style_set_pad_all(&symbol_style, 5);

	lv_style_reset(&symbol_focus_style);
	lv_style_init(&symbol_focus_style);
	lv_style_set_bg_opa(&symbol_focus_style, LV_OPA_TRANSP);
	lv_style_set_text_font(&symbol_focus_style, &lv_font_montserrat_48);
	lv_style_set_text_color(&symbol_focus_style, lv_palette_darken(LV_PALETTE_GREY, 1));
	lv_style_set_text_align(&symbol_focus_style, LV_TEXT_ALIGN_LEFT);
	lv_style_set_outline_opa(&symbol_focus_style, LV_OPA_TRANSP);
	lv_style_set_outline_width(&symbol_focus_style, 0);
	lv_style_set_pad_all(&symbol_focus_style, 5);

	lv_obj_set_style_bg_color(page_obj, bg_color, 0);
	lv_obj_set_style_bg_opa(page_obj, LV_OPA_COVER, 0);

	photo_player_ctx.name_obj = obj = lv_label_create(page_obj);
	lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, 0);
	lv_obj_set_style_text_font(obj, &lv_font_montserrat_48, 0);
	lv_obj_set_style_text_color(obj, text_color, 0);
	lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);

	photo_player_ctx.img_obj = obj = lv_img_create(page_obj);
	lv_obj_set_size(obj, lv_pct(100), lv_pct(100));
	lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_align_to(obj, photo_player_ctx.name_obj, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

	photo_player_ctx.esc_obj = obj = lv_btn_create(page_obj);
	lv_obj_remove_style_all(obj);
	lv_obj_add_style(obj, &symbol_style, 0);
	lv_obj_add_style(obj, &symbol_focus_style, LV_STATE_FOCUSED);
	lv_obj_align(obj, LV_ALIGN_TOP_LEFT, 0, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_add_event_cb(obj, menu_event_cb, LV_EVENT_FOCUSED, (void *)UI_CTRL_ESC);
	lv_obj_add_event_cb(obj, menu_event_cb, LV_EVENT_DEFOCUSED, (void *)UI_CTRL_ESC);
	lv_obj_add_event_cb(obj, menu_event_cb, LV_EVENT_CLICKED, (void *)UI_CTRL_ESC);
	photo_player_ctx.esc_label_obj = lv_label_create(photo_player_ctx.esc_obj);
	lv_label_set_text(photo_player_ctx.esc_label_obj, LV_SYMBOL_CLOSE);

	photo_player_ctx.prev_obj = obj = lv_btn_create(page_obj);
	lv_obj_remove_style_all(obj);
	lv_obj_add_style(obj, &symbol_style, 0);
	lv_obj_add_style(obj, &symbol_focus_style, LV_STATE_FOCUSED);
	lv_obj_align(obj, LV_ALIGN_LEFT_MID, 0, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_add_event_cb(obj, menu_event_cb, LV_EVENT_FOCUSED, (void *)UI_CTRL_PREV);
	lv_obj_add_event_cb(obj, menu_event_cb, LV_EVENT_DEFOCUSED, (void *)UI_CTRL_PREV);
	lv_obj_add_event_cb(obj, menu_event_cb, LV_EVENT_CLICKED, (void *)UI_CTRL_PREV);
	photo_player_ctx.prev_label_obj = lv_label_create(photo_player_ctx.prev_obj);
	lv_label_set_text(photo_player_ctx.prev_label_obj, LV_SYMBOL_LEFT);

	photo_player_ctx.next_obj = obj = lv_btn_create(page_obj);
	lv_obj_remove_style_all(obj);
	lv_obj_add_style(obj, &symbol_style, 0);
	lv_obj_add_style(obj, &symbol_focus_style, LV_STATE_FOCUSED);
	lv_obj_align(obj, LV_ALIGN_RIGHT_MID, 0, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_add_event_cb(obj, menu_event_cb, LV_EVENT_FOCUSED, (void *)UI_CTRL_NEXT);
	lv_obj_add_event_cb(obj, menu_event_cb, LV_EVENT_DEFOCUSED, (void *)UI_CTRL_NEXT);
	lv_obj_add_event_cb(obj, menu_event_cb, LV_EVENT_CLICKED, (void *)UI_CTRL_NEXT);
	photo_player_ctx.next_label_obj = lv_label_create(photo_player_ctx.next_obj);
	lv_label_set_text(photo_player_ctx.next_label_obj, LV_SYMBOL_RIGHT);
}

static void photo_player_destroy_ctrl(void) {
	if (photo_player_ctx.esc_obj) {
		ui_common_remove_style_all(photo_player_ctx.esc_obj);
		lv_obj_del(photo_player_ctx.esc_obj);
		photo_player_ctx.esc_obj = NULL;
	}
	if (photo_player_ctx.prev_obj) {
		ui_common_remove_style_all(photo_player_ctx.prev_obj);
		lv_obj_del(photo_player_ctx.prev_obj);
		photo_player_ctx.prev_obj = NULL;
	}
	if (photo_player_ctx.next_obj) {
		ui_common_remove_style_all(photo_player_ctx.next_obj);
		lv_obj_del(photo_player_ctx.next_obj);
		photo_player_ctx.next_obj = NULL;
	}
	if (photo_player_ctx.name_obj) {
		ui_common_remove_style_all(photo_player_ctx.name_obj);
		lv_obj_del(photo_player_ctx.name_obj);
		photo_player_ctx.name_obj = NULL;
	}
	if (photo_player_ctx.img_obj) {
		ui_common_remove_style_all(photo_player_ctx.img_obj);
		lv_obj_del(photo_player_ctx.img_obj);
		photo_player_ctx.img_obj = NULL;
	}
	if (photo_player_ctx.img_data.data) {
		free((void *)photo_player_ctx.img_data.data);
		photo_player_ctx.img_data.data = NULL;
	}
}

static void photo_player_param_init(lv_obj_t *page_obj) {
	memset(&photo_player_ctx, 0, sizeof(photo_player_ctx));
	if (rk_get_file_list("/mnt/sdcard/photo", &photo_player_ctx.list) != 0) {
		LOG_WARN("File list not obtained.\n");
		return;
	}
	photo_player_ctx.cur_file_index = (size_t)lv_obj_get_user_data(page_obj);

	if (photo_player_ctx.cur_file_index < photo_player_ctx.list.count) {
		LOG_INFO("load file index %zu\n", photo_player_ctx.cur_file_index);
		snprintf(photo_player_ctx.file_info.path, sizeof(photo_player_ctx.file_info.path), "%s/%s",
		         photo_player_ctx.list.path,
		         photo_player_ctx.list.files[photo_player_ctx.cur_file_index].name);
	}
}

static void photo_player_add_indev(void) {
	lv_group_t *group = lv_port_indev_group_create();
	if (NULL == group)
		return;

	lv_group_add_obj(group, photo_player_ctx.esc_obj);
	lv_group_add_obj(group, photo_player_ctx.prev_obj);
	lv_group_add_obj(group, photo_player_ctx.next_obj);

	photo_player_ctx.group = group;
}

static void photo_player_param_deinit(void) { rk_free_file_list(&photo_player_ctx.list); }

static void photo_player_delete_indev(void) {
	if (NULL != photo_player_ctx.group) {
		lv_port_indev_group_destroy(photo_player_ctx.group);
		photo_player_ctx.group = NULL;
	}
}

static void photo_player_page_create(lv_obj_t *page_obj) {
	photo_player_param_init(page_obj);
	photo_player_create_ctrl(page_obj);
	photo_player_layout_ctrl(page_obj);
}

static void photo_player_page_enter(lv_obj_t *page_obj) {
	photo_player_add_indev();
	photo_play();
}

static void photo_player_page_exit(lv_obj_t *page_obj) { photo_player_delete_indev(); }

static void photo_player_page_destroy(lv_obj_t *page_obj) {
	photo_player_destroy_ctrl();
	photo_player_param_deinit();
}

static UI_PAGE_HANDLER_T photo_player_page = {.name = "photo_player",
                                              .init = NULL,
                                              .create = photo_player_page_create,
                                              .enter = photo_player_page_enter,
                                              .destroy = photo_player_page_destroy,
                                              .exit = photo_player_page_exit};

UI_PAGE_REGISTER(photo_player_page)
