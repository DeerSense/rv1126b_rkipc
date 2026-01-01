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
#define LOG_TAG "ui_gallery.c"

#include <linux/input.h>

#include "common.h"
#include "evdev.h"
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

/**********************
 *  STATIC PROTOTYPES
 **********************/
typedef struct {
	lv_group_t *group;
	lv_obj_t *exit_obj;
	lv_obj_t *exit_label_obj;
	lv_obj_t *container_obj;
	lv_obj_t *video_btn_obj;
	lv_obj_t *video_icon_obj;
	lv_obj_t *video_label_obj;
	lv_obj_t *slowmotion_btn_obj;
	lv_obj_t *slowmotion_icon_obj;
	lv_obj_t *slowmotion_label_obj;
	lv_obj_t *timelapse_btn_obj;
	lv_obj_t *timelapse_icon_obj;
	lv_obj_t *timelapse_label_obj;
	lv_obj_t *photo_btn_obj;
	lv_obj_t *photo_icon_obj;
	lv_obj_t *photo_label_obj;
} ui_gallery_context_t;

extern lv_ft_info_t ttf_info_32;

static ui_gallery_context_t gallery_ctx;

static void return_event_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_target(e);

	if (code == LV_EVENT_CLICKED) {
		ui_page_pop_page();
	}
}

static void btn_event_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_target(e);
	RK_MODE_E mode = (RK_MODE_E)lv_event_get_user_data(e);
	if (code == LV_EVENT_CLICKED) {
		ui_page_push_page("playlist", (void *)mode);
	} else if (code == LV_EVENT_FOCUSED) {
		lv_obj_set_size(obj, lv_pct(40), lv_pct(40));
	} else if (code == LV_EVENT_DEFOCUSED) {
		lv_obj_set_size(obj, lv_pct(30), lv_pct(30));
	}
}

static void gallery_create_ctrl(lv_obj_t *page_obj) {
	lv_color_t bg_color = lv_color_hex(0xFF04171D);
	lv_obj_t *cont_obj = NULL, *btn_obj = NULL, *label_obj = NULL;

	lv_obj_set_style_bg_opa(page_obj, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(page_obj, bg_color, 0);

	gallery_ctx.exit_obj = lv_btn_create(page_obj);
	lv_obj_set_style_bg_opa(gallery_ctx.exit_obj, LV_OPA_TRANSP, 0);
	lv_obj_add_event_cb(gallery_ctx.exit_obj, return_event_handler, LV_EVENT_CLICKED, NULL);
	lv_obj_add_event_cb(gallery_ctx.exit_obj, return_event_handler, LV_EVENT_FOCUSED, NULL);
	lv_obj_add_event_cb(gallery_ctx.exit_obj, return_event_handler, LV_EVENT_DEFOCUSED, NULL);
	gallery_ctx.exit_label_obj = lv_label_create(gallery_ctx.exit_obj);

	gallery_ctx.container_obj = cont_obj = lv_obj_create(page_obj);
	lv_obj_set_size(cont_obj, lv_pct(100), lv_pct(100));
	lv_obj_set_style_bg_opa(gallery_ctx.container_obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_opa(cont_obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_radius(cont_obj, 0, 0);

	gallery_ctx.video_btn_obj = btn_obj = lv_btn_create(cont_obj);
	lv_obj_add_event_cb(btn_obj, btn_event_handler, LV_EVENT_CLICKED, (void *)RK_VIDEO_MODE);
	lv_obj_add_event_cb(btn_obj, btn_event_handler, LV_EVENT_FOCUSED, (void *)RK_VIDEO_MODE);
	lv_obj_add_event_cb(btn_obj, btn_event_handler, LV_EVENT_DEFOCUSED, (void *)RK_VIDEO_MODE);
	lv_obj_set_size(btn_obj, lv_pct(30), lv_pct(30));
	gallery_ctx.video_label_obj = label_obj = lv_label_create(btn_obj);
	gallery_ctx.video_icon_obj = lv_img_create(btn_obj);
	lv_obj_align(gallery_ctx.video_icon_obj, LV_ALIGN_CENTER, 0, 0);
	lv_img_set_src(gallery_ctx.video_icon_obj, icon_media_film);

	gallery_ctx.photo_btn_obj = btn_obj = lv_btn_create(cont_obj);
	lv_obj_add_event_cb(btn_obj, btn_event_handler, LV_EVENT_CLICKED, (void *)RK_PHOTO_MODE);
	lv_obj_add_event_cb(btn_obj, btn_event_handler, LV_EVENT_FOCUSED, (void *)RK_PHOTO_MODE);
	lv_obj_add_event_cb(btn_obj, btn_event_handler, LV_EVENT_DEFOCUSED, (void *)RK_PHOTO_MODE);
	lv_obj_set_size(btn_obj, lv_pct(30), lv_pct(30));
	gallery_ctx.photo_label_obj = label_obj = lv_label_create(btn_obj);
	gallery_ctx.photo_icon_obj = lv_img_create(btn_obj);
	lv_obj_align(gallery_ctx.photo_icon_obj, LV_ALIGN_CENTER, 0, 0);
	lv_img_set_src(gallery_ctx.photo_icon_obj, icon_media_photo);

	gallery_ctx.slowmotion_btn_obj = btn_obj = lv_btn_create(cont_obj);
	lv_obj_add_event_cb(btn_obj, btn_event_handler, LV_EVENT_CLICKED, (void *)RK_SLOW_MOTION_MODE);
	lv_obj_add_event_cb(btn_obj, btn_event_handler, LV_EVENT_FOCUSED, (void *)RK_SLOW_MOTION_MODE);
	lv_obj_add_event_cb(btn_obj, btn_event_handler, LV_EVENT_DEFOCUSED,
	                    (void *)RK_SLOW_MOTION_MODE);
	lv_obj_set_size(btn_obj, lv_pct(30), lv_pct(30));
	gallery_ctx.slowmotion_label_obj = label_obj = lv_label_create(btn_obj);
	gallery_ctx.slowmotion_icon_obj = lv_img_create(btn_obj);
	lv_obj_align(gallery_ctx.slowmotion_icon_obj, LV_ALIGN_CENTER, 0, 0);
	lv_img_set_src(gallery_ctx.slowmotion_icon_obj, icon_media_film);

	gallery_ctx.timelapse_btn_obj = btn_obj = lv_btn_create(cont_obj);
	lv_obj_add_event_cb(btn_obj, btn_event_handler, LV_EVENT_CLICKED, (void *)RK_TIME_LAPSE_MODE);
	lv_obj_add_event_cb(btn_obj, btn_event_handler, LV_EVENT_FOCUSED, (void *)RK_TIME_LAPSE_MODE);
	lv_obj_add_event_cb(btn_obj, btn_event_handler, LV_EVENT_DEFOCUSED, (void *)RK_TIME_LAPSE_MODE);
	lv_obj_set_size(btn_obj, lv_pct(30), lv_pct(30));
	gallery_ctx.timelapse_label_obj = label_obj = lv_label_create(btn_obj);
	gallery_ctx.timelapse_icon_obj = lv_img_create(btn_obj);
	lv_obj_align(gallery_ctx.timelapse_icon_obj, LV_ALIGN_CENTER, 0, 0);
	lv_img_set_src(gallery_ctx.timelapse_icon_obj, icon_media_film);
}

static void gallery_layout(void) {
	lv_obj_set_flex_flow(gallery_ctx.container_obj, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(gallery_ctx.container_obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
	                      LV_FLEX_ALIGN_CENTER);
	lv_obj_set_scrollbar_mode(gallery_ctx.container_obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_scroll_snap_x(gallery_ctx.container_obj, LV_SCROLL_SNAP_CENTER);
}

static void gallery_set_text(void) {
	static lv_style_t text_style, btn_style, btn_focus_style, symbol_style,
	    symbol_focus_style;
	lv_color_t text_color = lv_color_hex(0xFFFFFF);

	lv_style_reset(&btn_style);
	lv_style_init(&btn_style);
	lv_style_set_bg_opa(&btn_style, LV_OPA_COVER);
	lv_style_set_bg_color(&btn_style, lv_palette_darken(LV_PALETTE_GREY, 2));
	lv_style_set_radius(&btn_style, 5);

	lv_style_reset(&btn_focus_style);
	lv_style_init(&btn_focus_style);
	lv_style_set_bg_opa(&btn_focus_style, LV_OPA_COVER);
	lv_style_set_bg_color(&btn_focus_style, lv_palette_main(LV_PALETTE_GREY));
	lv_style_set_outline_opa(&btn_focus_style, LV_OPA_TRANSP);
	lv_style_set_radius(&btn_focus_style, 5);

	lv_style_reset(&text_style);
	lv_style_init(&text_style);
	lv_style_set_text_font(&text_style, ttf_info_32.font);
	lv_style_set_text_color(&text_style, text_color);
	lv_style_set_text_align(&text_style, LV_TEXT_ALIGN_CENTER);
	lv_style_set_align(&text_style, LV_ALIGN_BOTTOM_MID);

	lv_style_reset(&symbol_style);
	lv_style_init(&symbol_style);
	lv_style_set_bg_opa(&symbol_style, LV_OPA_TRANSP);
	lv_style_set_text_font(&symbol_style, &lv_font_montserrat_48);
	lv_style_set_text_color(&symbol_style, lv_color_hex(0xFFFFFF));
	lv_style_set_text_align(&symbol_style, LV_TEXT_ALIGN_LEFT);
	lv_style_set_outline_opa(&symbol_style, LV_OPA_TRANSP);
	lv_style_set_outline_width(&symbol_style, 0);
	lv_style_set_pad_all(&symbol_style, 30);

	lv_style_reset(&symbol_focus_style);
	lv_style_init(&symbol_focus_style);
	lv_style_set_bg_opa(&symbol_focus_style, LV_OPA_TRANSP);
	lv_style_set_text_font(&symbol_focus_style, &lv_font_montserrat_48);
	lv_style_set_text_color(&symbol_focus_style, lv_palette_main(LV_PALETTE_GREY));
	lv_style_set_text_align(&symbol_focus_style, LV_TEXT_ALIGN_LEFT);
	lv_style_set_outline_opa(&symbol_focus_style, LV_OPA_TRANSP);
	lv_style_set_outline_width(&symbol_focus_style, 0);
	lv_style_set_pad_all(&symbol_focus_style, 30);

	lv_obj_remove_style_all(gallery_ctx.exit_obj);
	lv_label_set_text(gallery_ctx.exit_label_obj, LV_SYMBOL_LEFT);
	lv_obj_add_style(gallery_ctx.exit_obj, &symbol_style, 0);
	lv_obj_add_style(gallery_ctx.exit_obj, &symbol_focus_style, LV_STATE_FOCUSED);

	// INFO: set align to the left of the parent after set text
	lv_obj_align_to(gallery_ctx.container_obj
		, gallery_ctx.exit_obj, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);

	lv_obj_add_style(gallery_ctx.video_btn_obj, &btn_style, 0);
	lv_obj_add_style(gallery_ctx.video_btn_obj, &btn_focus_style, LV_STATE_FOCUSED);
	lv_label_set_text(gallery_ctx.video_label_obj, "录像");
	lv_obj_add_style(gallery_ctx.video_label_obj, &text_style, 0);

	lv_obj_add_style(gallery_ctx.photo_btn_obj, &btn_style, 0);
	lv_obj_add_style(gallery_ctx.photo_btn_obj, &btn_focus_style, LV_STATE_FOCUSED);
	lv_label_set_text(gallery_ctx.photo_label_obj, "照片");
	lv_obj_add_style(gallery_ctx.photo_label_obj, &text_style, 0);

	lv_obj_add_style(gallery_ctx.slowmotion_btn_obj, &btn_style, 0);
	lv_obj_add_style(gallery_ctx.slowmotion_btn_obj, &btn_focus_style, LV_STATE_FOCUSED);
	lv_label_set_text(gallery_ctx.slowmotion_label_obj, "慢动作回放");
	lv_obj_add_style(gallery_ctx.slowmotion_label_obj, &text_style, 0);

	lv_obj_add_style(gallery_ctx.timelapse_btn_obj, &btn_style, 0);
	lv_obj_add_style(gallery_ctx.timelapse_btn_obj, &btn_focus_style, LV_STATE_FOCUSED);
	lv_label_set_text(gallery_ctx.timelapse_label_obj, "延时录像");
	lv_obj_add_style(gallery_ctx.timelapse_label_obj, &text_style, 0);
}

static void gallery_destroy_ctrl(void) {
	if (NULL != gallery_ctx.photo_btn_obj) {
		ui_common_remove_style_all(gallery_ctx.photo_btn_obj);
		lv_obj_del(gallery_ctx.photo_btn_obj);
		gallery_ctx.photo_btn_obj = NULL;
	}
	if (NULL != gallery_ctx.video_btn_obj) {
		ui_common_remove_style_all(gallery_ctx.video_btn_obj);
		lv_obj_del(gallery_ctx.video_btn_obj);
		gallery_ctx.video_btn_obj = NULL;
	}
	if (NULL != gallery_ctx.slowmotion_btn_obj) {
		ui_common_remove_style_all(gallery_ctx.slowmotion_btn_obj);
		lv_obj_del(gallery_ctx.slowmotion_btn_obj);
		gallery_ctx.slowmotion_btn_obj = NULL;
	}
	if (NULL != gallery_ctx.timelapse_btn_obj) {
		ui_common_remove_style_all(gallery_ctx.timelapse_btn_obj);
		lv_obj_del(gallery_ctx.timelapse_btn_obj);
		gallery_ctx.timelapse_btn_obj = NULL;
	}
	if (NULL != gallery_ctx.container_obj) {
		ui_common_remove_style_all(gallery_ctx.container_obj);
		lv_obj_del(gallery_ctx.container_obj);
		gallery_ctx.container_obj = NULL;
	}
	if (NULL != gallery_ctx.exit_obj) {
		ui_common_remove_style_all(gallery_ctx.exit_obj);
		lv_obj_del(gallery_ctx.exit_obj);
		gallery_ctx.exit_obj = NULL;
	}
}

static void gallery_add_indev(void) {
	lv_group_t *group = lv_port_indev_group_create();
	if (NULL == group)
		return;

	lv_group_add_obj(group, gallery_ctx.exit_obj);
	lv_group_add_obj(group, gallery_ctx.video_btn_obj);
	lv_group_add_obj(group, gallery_ctx.photo_btn_obj);
	lv_group_add_obj(group, gallery_ctx.slowmotion_btn_obj);
	lv_group_add_obj(group, gallery_ctx.timelapse_btn_obj);

	gallery_ctx.group = group;
}

static void gallery_delete_indev(void) {
	if (NULL != gallery_ctx.group) {
		lv_port_indev_group_destroy(gallery_ctx.group);
		gallery_ctx.group = NULL;
	}
}

static void gallery_page_create(lv_obj_t *page_obj) {

	gallery_create_ctrl(page_obj);
	gallery_set_text();
	gallery_layout();
}

static void gallery_page_enter(lv_obj_t *page_obj) { gallery_add_indev(); }

static void gallery_page_exit(lv_obj_t *page_obj) {}

static void gallery_page_destroy(lv_obj_t *page_obj) {
	gallery_delete_indev();
	gallery_destroy_ctrl();
}

static UI_PAGE_HANDLER_T gallery_page = {.name = "gallery",
                                         .init = NULL,
                                         .create = gallery_page_create,
                                         .enter = gallery_page_enter,
                                         .destroy = gallery_page_destroy,
                                         .exit = gallery_page_exit};

UI_PAGE_REGISTER(gallery_page)
