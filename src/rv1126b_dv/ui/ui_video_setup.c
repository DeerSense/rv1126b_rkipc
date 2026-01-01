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
#define LOG_TAG "ui_video_setup.c"

#include "common.h"
#include "evdev.h"
#include "lvgl/porting/lv_port_indev.h"
#include "storage.h"
#include "ui_common.h"
#include "ui_dialog.h"
#include "ui_dialog_format.h"
#include "ui_dialog_storage.h"
#include "ui_page_manager.h"
#include "ui_player.h"
#include "ui_resource_manager.h"
#include "video.h"
#include <linux/input.h>

typedef struct {
	lv_obj_t *container_obj;

	lv_obj_t *exit_obj;
	lv_obj_t *exit_label_obj;

	lv_obj_t *eis_dialog;
	lv_group_t *eis_group;

	lv_obj_t *eis_obj;
	lv_obj_t *eis_label_obj;
	lv_obj_t *hdr_obj;
	lv_obj_t *hdr_label_obj;
	lv_obj_t *photo_obj;
	lv_obj_t *photo_label_obj;
	lv_obj_t *debug_obj;
	lv_obj_t *smart_ae_obj;
	lv_obj_t *enable_osd_obj;
	lv_obj_t *memc_obj;

	lv_group_t *group;

} UI_VIDEO_SETUP_CONTROL_S;
static UI_VIDEO_SETUP_CONTROL_S video_setup_ctrl;

extern lv_ft_info_t ttf_info_32;

static void return_event_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_target(e);
	if (code == LV_EVENT_CLICKED) {
		ui_page_pop_page();
	}
}

static void ui_dialog_create_func(void *msg) {
	ui_dialog_create("WARNING", (const char *)msg);
}

static void eis_config_cb(void *userdata) {
	int ret = 0;
	RK_EIS_MODE_E mode = (RK_MODE_E)(size_t)userdata;
	ret = rk_set_eis_mode(mode);
	if (ret != 0) {
		lv_async_call(ui_dialog_create_func, "切换EIS模式失败!");
	} else {
		lv_async_call(ui_dialog_create_func, "切换EIS模式成功!");
	}
}

static const ui_option_entry_t eis_options[] = {
	{"关闭防抖", eis_config_cb, (void *)RK_EIS_OFF},
	{"普通防抖", eis_config_cb, (void *)RK_NORMAL_STEADY},
	{"地平线防抖", eis_config_cb, (void *)RK_HORIZON_STEADY},
	{"畸变校正", eis_config_cb, (void *)RK_DISTORTION_CORRECTION},
};


static void eis_event_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_target(e);
	if (code == LV_EVENT_CLICKED) {
		create_select_box("EIS配置", eis_options, sizeof(eis_options) / sizeof(eis_options[0])
			, rk_get_eis_mode());
	}
}

static void memc_event_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_target(e);
	if (code == LV_EVENT_CLICKED) {
		if (lv_obj_has_state(obj, LV_STATE_CHECKED))
			rk_set_memc_mode(true);
		else
			rk_set_memc_mode(false);
	}
}

static void hdr_config_cb(void *userdata) {
	int ret = 0, len = 0;
	RK_HDR_MODE_E hdr_mode = (RK_HDR_MODE_E)(size_t)userdata;
	RK_EIS_MODE_E eis_mode = rk_get_eis_mode();
	static char msg[128] = {0};
	ret = rk_set_hdr(hdr_mode);
	hdr_mode = rk_get_hdr();
	memset(msg, 0, sizeof(msg));
	len = snprintf(msg, sizeof(msg), "切换模式%s,", ret != 0 ? "失败" : "成功");
	if (hdr_mode == RK_HDR_DAG_MODE)
		len += snprintf(msg + ret, sizeof(msg) - len, "当前模式: DAG HDR");
	else if (hdr_mode == RK_HDR_STAGGERED_MODE)
		len += snprintf(msg + ret, sizeof(msg) - len, "当前模式: STAGGERED HDR");
	else
		len += snprintf(msg + ret, sizeof(msg) - len, "当前模式: 线性模式");
	lv_async_call(ui_dialog_create_func, msg);
}

static const ui_option_entry_t hdr_options[] = {
	{"线性模式", hdr_config_cb, (void *)RK_LINEAR_MODE},
	{"DAG HDR", hdr_config_cb, (void *)RK_HDR_DAG_MODE},
	{"STAGGERED HDR", hdr_config_cb, (void *)RK_HDR_STAGGERED_MODE},
};

static void hdr_event_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_target(e);
	if (code == LV_EVENT_CLICKED) {
		create_select_box("HDR配置", hdr_options, sizeof(hdr_options) / sizeof(hdr_options[0])
			, rk_get_hdr());
	}
}

static void debug_event_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_target(e);
	if (code == LV_EVENT_CLICKED) {
		if (lv_obj_has_state(obj, LV_STATE_CHECKED))
			rk_set_eis_debug(true);
		else
			rk_set_eis_debug(false);
	}
}

static void smart_ae_event_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_target(e);
	if (code == LV_EVENT_CLICKED) {
		if (lv_obj_has_state(obj, LV_STATE_CHECKED))
			rk_set_smart_ae(true);
		else
			rk_set_smart_ae(false);
	}
}

static void osd_event_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_target(e);
	if (code == LV_EVENT_CLICKED) {
		if (lv_obj_has_state(obj, LV_STATE_CHECKED))
			rk_param_set_int("video.source:enable_osd", 1);
		else
			rk_param_set_int("video.source:enable_osd", 0);
	}
}

static void photo_config_cb(void *userdata) {
	int capture_num = (int)(size_t)userdata;
	rk_photo_set_max_num(capture_num);
}

static const ui_option_entry_t photo_options[] = {
	{"1F1S", photo_config_cb, (void *)1},
	{"5F1S", photo_config_cb, (void *)5},
	{"10F1S", photo_config_cb, (void *)10},
	{"20F2S", photo_config_cb, (void *)20},
};

static void photo_event_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_target(e);
	if (code == LV_EVENT_CLICKED) {
		int index = 0;
		int num = rk_photo_get_max_num();
		if (num == 1)
			index = 0;
		else if (num == 5)
			index = 1;
		else if (num == 10)
			index = 2;
		else if (num == 20)
			index = 3;
		create_select_box("拍照配置", photo_options, sizeof(photo_options) / sizeof(photo_options[0])
			, index);
	}
}

static void video_setup_layout(lv_obj_t *page_obj) {
	lv_obj_set_flex_flow(video_setup_ctrl.container_obj, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(video_setup_ctrl.container_obj, LV_FLEX_ALIGN_START,
	                      LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_scrollbar_mode(video_setup_ctrl.container_obj, LV_SCROLLBAR_MODE_OFF);
}

static void video_setup_create_ctrl(lv_obj_t *page_obj) {
	lv_color_t bg_color = lv_color_hex(0xFF04171D);
	lv_obj_t *cont_obj = NULL, *obj = NULL;

	lv_disp_set_bg_opa(NULL, LV_OPA_TRANSP);
	lv_obj_set_style_bg_opa(page_obj, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(page_obj, bg_color, 0);

	video_setup_ctrl.exit_obj = lv_btn_create(page_obj);
	lv_obj_set_style_bg_opa(video_setup_ctrl.exit_obj, LV_OPA_TRANSP, 0);
	lv_obj_add_event_cb(video_setup_ctrl.exit_obj, return_event_handler, LV_EVENT_CLICKED, NULL);
	video_setup_ctrl.exit_label_obj = lv_label_create(video_setup_ctrl.exit_obj);

	video_setup_ctrl.container_obj = cont_obj = lv_obj_create(page_obj);
	lv_obj_set_size(cont_obj, lv_pct(100), lv_pct(100));
	lv_obj_set_style_bg_opa(cont_obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_opa(cont_obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_radius(cont_obj, 0, 0);

	video_setup_ctrl.eis_obj = lv_btn_create(cont_obj);
	lv_obj_set_size(video_setup_ctrl.eis_obj, lv_pct(100), lv_pct(10));
	lv_obj_add_event_cb(video_setup_ctrl.eis_obj, eis_event_handler, LV_EVENT_CLICKED, NULL);
	video_setup_ctrl.eis_label_obj = lv_label_create(video_setup_ctrl.eis_obj);

	video_setup_ctrl.hdr_obj = lv_btn_create(cont_obj);
	lv_obj_set_size(video_setup_ctrl.hdr_obj, lv_pct(100), lv_pct(10));
	lv_obj_add_event_cb(video_setup_ctrl.hdr_obj, hdr_event_handler, LV_EVENT_CLICKED, NULL);
	video_setup_ctrl.hdr_label_obj = lv_label_create(video_setup_ctrl.hdr_obj);

	video_setup_ctrl.debug_obj = lv_checkbox_create(cont_obj);
	lv_obj_add_flag(video_setup_ctrl.debug_obj, LV_OBJ_FLAG_CHECKABLE);
	lv_obj_set_size(video_setup_ctrl.debug_obj, lv_pct(100), lv_pct(10));
	lv_obj_add_event_cb(video_setup_ctrl.debug_obj, debug_event_handler, LV_EVENT_CLICKED, NULL);

	video_setup_ctrl.smart_ae_obj = lv_checkbox_create(cont_obj);
	lv_obj_add_flag(video_setup_ctrl.smart_ae_obj, LV_OBJ_FLAG_CHECKABLE);
	lv_obj_set_size(video_setup_ctrl.smart_ae_obj, lv_pct(100), lv_pct(10));
	lv_obj_add_event_cb(video_setup_ctrl.smart_ae_obj, smart_ae_event_handler, LV_EVENT_CLICKED, NULL);

	video_setup_ctrl.enable_osd_obj = lv_checkbox_create(cont_obj);
	lv_obj_add_flag(video_setup_ctrl.enable_osd_obj, LV_OBJ_FLAG_CHECKABLE);
	lv_obj_set_size(video_setup_ctrl.enable_osd_obj, lv_pct(100), lv_pct(10));
	lv_obj_add_event_cb(video_setup_ctrl.enable_osd_obj, osd_event_handler, LV_EVENT_CLICKED, NULL);

	video_setup_ctrl.photo_obj = lv_btn_create(cont_obj);
	lv_obj_set_size(video_setup_ctrl.photo_obj, lv_pct(100), lv_pct(10));
	lv_obj_add_event_cb(video_setup_ctrl.photo_obj, photo_event_handler, LV_EVENT_CLICKED, NULL);
	video_setup_ctrl.photo_label_obj = lv_label_create(video_setup_ctrl.photo_obj);

	video_setup_ctrl.memc_obj = lv_checkbox_create(cont_obj);
	lv_obj_add_flag(video_setup_ctrl.memc_obj, LV_OBJ_FLAG_CHECKABLE);
	lv_obj_set_size(video_setup_ctrl.memc_obj, lv_pct(100), lv_pct(10));
	lv_obj_add_event_cb(video_setup_ctrl.memc_obj, memc_event_handler, LV_EVENT_CLICKED, NULL);
}

static void video_setup_set_text() {
	static lv_style_t symbol_style, symbol_focus_style, option_style, option_focus_style;
	// INFO: make sure to get the right heigh of obj
	lv_obj_update_layout(video_setup_ctrl.eis_obj);
	int font_height = lv_font_get_line_height(ttf_info_32.font);
	int option_height = lv_obj_get_height(video_setup_ctrl.eis_obj);
	// INFO: adjust the padding of text, because set alignment function is not work
	int pad = (option_height - font_height) / 2;
	lv_style_reset(&option_style);
	lv_style_init(&option_style);
	lv_style_set_bg_opa(&option_style, LV_OPA_COVER);
	lv_style_set_bg_color(&option_style, lv_palette_lighten(LV_PALETTE_GREY, 3));
	lv_style_set_text_color(&option_style, lv_color_hex(0x000000));
	lv_style_set_text_font(&option_style, ttf_info_32.font);
	lv_style_set_text_align(&option_style, LV_TEXT_ALIGN_CENTER);
	lv_style_set_radius(&option_style, 5);
	lv_style_set_border_opa(&option_style, LV_OPA_TRANSP);
	lv_style_set_pad_top(&option_style, pad);

	lv_style_reset(&option_focus_style);
	lv_style_init(&option_focus_style);
	lv_style_set_bg_opa(&option_focus_style, LV_OPA_COVER);
	lv_style_set_bg_color(&option_focus_style, lv_palette_main(LV_PALETTE_GREY));
	lv_style_set_text_color(&option_focus_style, lv_color_hex(0x000000));
	lv_style_set_text_align(&option_focus_style, LV_TEXT_ALIGN_CENTER);
	lv_style_set_text_font(&option_focus_style, ttf_info_32.font);
	lv_style_set_radius(&option_focus_style, 5);
	lv_style_set_border_opa(&option_focus_style, LV_OPA_TRANSP);
	lv_style_set_outline_opa(&option_focus_style, LV_OPA_TRANSP);
	lv_style_set_outline_width(&option_focus_style, 0);
	lv_style_set_align(&option_focus_style, LV_ALIGN_CENTER);
	lv_style_set_pad_top(&option_focus_style, pad);

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

	lv_obj_remove_style_all(video_setup_ctrl.exit_obj);
	lv_label_set_text(video_setup_ctrl.exit_label_obj, LV_SYMBOL_LEFT);
	lv_obj_add_style(video_setup_ctrl.exit_obj, &symbol_style, 0);
	lv_obj_add_style(video_setup_ctrl.exit_obj, &symbol_focus_style, LV_STATE_FOCUSED);

	// INFO: set align to the left of the parent after set text
	lv_obj_align_to(video_setup_ctrl.container_obj
		, video_setup_ctrl.exit_obj, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);

	lv_obj_add_style(video_setup_ctrl.eis_obj, &option_style, 0);
	lv_obj_add_style(video_setup_ctrl.eis_obj, &option_focus_style,
	                 LV_PART_MAIN | LV_STATE_FOCUSED);
	lv_label_set_text(video_setup_ctrl.eis_label_obj, "EIS模式");

	lv_obj_add_style(video_setup_ctrl.hdr_obj, &option_style, 0);
	lv_obj_add_style(video_setup_ctrl.hdr_obj, &option_focus_style,
	                 LV_PART_MAIN | LV_STATE_FOCUSED);
	lv_label_set_text(video_setup_ctrl.hdr_label_obj, "HDR模式");

	lv_checkbox_set_text(video_setup_ctrl.debug_obj, "开发者模式");
	lv_obj_add_style(video_setup_ctrl.debug_obj, &option_style, 0);
	lv_obj_add_style(video_setup_ctrl.debug_obj, &option_focus_style,
	                 LV_PART_MAIN | LV_STATE_FOCUSED);
	lv_obj_set_style_radius(video_setup_ctrl.debug_obj, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
	if (rk_get_eis_debug())
		lv_obj_add_state(video_setup_ctrl.debug_obj, LV_STATE_CHECKED);

	lv_checkbox_set_text(video_setup_ctrl.smart_ae_obj, "AE策略");
	lv_obj_add_style(video_setup_ctrl.smart_ae_obj, &option_style, 0);
	lv_obj_add_style(video_setup_ctrl.smart_ae_obj, &option_focus_style,
	                 LV_PART_MAIN | LV_STATE_FOCUSED);
	lv_obj_set_style_radius(video_setup_ctrl.smart_ae_obj, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
	if (rk_get_smart_ae())
		lv_obj_add_state(video_setup_ctrl.smart_ae_obj, LV_STATE_CHECKED);

	lv_checkbox_set_text(video_setup_ctrl.enable_osd_obj, "显示OSD");
	lv_obj_add_style(video_setup_ctrl.enable_osd_obj, &option_style, 0);
	lv_obj_add_style(video_setup_ctrl.enable_osd_obj, &option_focus_style,
	                 LV_PART_MAIN | LV_STATE_FOCUSED);
	lv_obj_set_style_radius(video_setup_ctrl.enable_osd_obj, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
	if (rk_param_get_int("video.source:enable_osd", 0))
		lv_obj_add_state(video_setup_ctrl.enable_osd_obj, LV_STATE_CHECKED);

	lv_obj_add_style(video_setup_ctrl.photo_obj, &option_style, 0);
	lv_obj_add_style(video_setup_ctrl.photo_obj, &option_focus_style,
	                 LV_PART_MAIN | LV_STATE_FOCUSED);
	lv_label_set_text(video_setup_ctrl.photo_label_obj, "连拍设置");

	lv_checkbox_set_text(video_setup_ctrl.memc_obj, "MEMC");
	lv_obj_add_style(video_setup_ctrl.memc_obj, &option_style, 0);
	lv_obj_add_style(video_setup_ctrl.memc_obj, &option_focus_style,
	                 LV_PART_MAIN | LV_STATE_FOCUSED);
	lv_obj_set_style_radius(video_setup_ctrl.memc_obj, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
	if (rk_get_memc_mode())
		lv_obj_add_state(video_setup_ctrl.memc_obj, LV_STATE_CHECKED);
	else
		lv_obj_clear_state(video_setup_ctrl.memc_obj, LV_STATE_CHECKED);
}

static void video_setup_destroy_ctrl(void) {
	if (NULL != video_setup_ctrl.eis_obj) {
		ui_common_remove_style_all(video_setup_ctrl.eis_obj);
		lv_obj_del(video_setup_ctrl.eis_obj);
		video_setup_ctrl.eis_obj = NULL;
	}
	if (NULL != video_setup_ctrl.hdr_obj) {
		ui_common_remove_style_all(video_setup_ctrl.hdr_obj);
		lv_obj_del(video_setup_ctrl.hdr_obj);
		video_setup_ctrl.hdr_obj = NULL;
	}
	if (NULL != video_setup_ctrl.debug_obj) {
		ui_common_remove_style_all(video_setup_ctrl.debug_obj);
		lv_obj_del(video_setup_ctrl.debug_obj);
		video_setup_ctrl.debug_obj = NULL;
	}
	if (NULL != video_setup_ctrl.smart_ae_obj) {
		ui_common_remove_style_all(video_setup_ctrl.smart_ae_obj);
		lv_obj_del(video_setup_ctrl.smart_ae_obj);
		video_setup_ctrl.smart_ae_obj = NULL;
	}
	if (NULL != video_setup_ctrl.enable_osd_obj) {
		ui_common_remove_style_all(video_setup_ctrl.enable_osd_obj);
		lv_obj_del(video_setup_ctrl.enable_osd_obj);
		video_setup_ctrl.enable_osd_obj = NULL;
	}
	if (NULL != video_setup_ctrl.photo_obj) {
		ui_common_remove_style_all(video_setup_ctrl.photo_obj);
		lv_obj_del(video_setup_ctrl.photo_obj);
		video_setup_ctrl.photo_obj = NULL;
	}
	if (NULL != video_setup_ctrl.exit_obj) {
		ui_common_remove_style_all(video_setup_ctrl.exit_obj);
		lv_obj_del(video_setup_ctrl.exit_obj);
		video_setup_ctrl.exit_obj = NULL;
	}
	if (NULL != video_setup_ctrl.memc_obj) {
		ui_common_remove_style_all(video_setup_ctrl.memc_obj);
		lv_obj_del(video_setup_ctrl.memc_obj);
		video_setup_ctrl.memc_obj = NULL;
	}
	if (NULL != video_setup_ctrl.container_obj) {
		ui_common_remove_style_all(video_setup_ctrl.container_obj);
		lv_obj_del(video_setup_ctrl.container_obj);
		video_setup_ctrl.container_obj = NULL;
	}
}

static void video_setup_page_create(lv_obj_t *page_obj) {

	video_setup_create_ctrl(page_obj);
	video_setup_layout(page_obj);
	video_setup_set_text();
}

static void video_setup_add_indev(void) {
	lv_group_t *group = lv_port_indev_group_create();
	if (NULL == group)
		return;

	lv_group_add_obj(group, video_setup_ctrl.exit_obj);
	lv_group_add_obj(group, video_setup_ctrl.eis_obj);
	lv_group_add_obj(group, video_setup_ctrl.hdr_obj);
	lv_group_add_obj(group, video_setup_ctrl.debug_obj);
	lv_group_add_obj(group, video_setup_ctrl.smart_ae_obj);
	lv_group_add_obj(group, video_setup_ctrl.enable_osd_obj);
	lv_group_add_obj(group, video_setup_ctrl.photo_obj);
	lv_group_add_obj(group, video_setup_ctrl.memc_obj);

	video_setup_ctrl.group = group;
}

static void video_setup_delete_indev(void) {
	if (NULL != video_setup_ctrl.group) {
		lv_port_indev_group_destroy(video_setup_ctrl.group);
		video_setup_ctrl.group = NULL;
	}
}

static void video_setup_page_enter(lv_obj_t *page_obj) { video_setup_add_indev(); }

static void video_setup_page_exit(lv_obj_t *page_obj) {}

static void video_setup_page_destroy(lv_obj_t *page_obj) {
	video_setup_delete_indev();
	video_setup_destroy_ctrl();
}

static UI_PAGE_HANDLER_T video_setup_page = {.name = "video_setup",
                                                .init = NULL,
                                                .create = video_setup_page_create,
                                                .enter = video_setup_page_enter,
                                                .destroy = video_setup_page_destroy,
                                                .exit = video_setup_page_exit};

UI_PAGE_REGISTER(video_setup_page)
