/*
 * Copyright (c) 2022 Rockchip, Inc. All Rights Reserved.
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
#define LOG_TAG "ui_video_player.c"

#include "lvgl/porting/lv_port_indev.h"
#include "ui_common.h"
#include "ui_page_manager.h"
#include "ui_player.h"
#include "ui_resource_manager.h"
#include <linux/input.h>

#include "common.h"
#include "evdev.h"
#include "player.h"
#include "storage.h"
#include "video.h"

#include <limits.h>
#include <stdio.h>
#include <time.h>

extern lv_ft_info_t ttf_info_32;
/**********************
 *  STATIC PROTOTYPES
 **********************/
typedef enum {
	UI_PLAYER_STATE_IDLE = 0, /**< The player state before init . */
	UI_PLAYER_STATE_INIT,     /**< The player is in the initial state. It changes
	                                to the initial state after being SetDataSource. */
	UI_PLAYER_STATE_PREPARED, /**< The player is in the prepared state. */
	UI_PLAYER_STATE_PLAY,     /**< The player is in the playing state. */
	UI_PLAYER_STATE_PAUSE,    /**< The player is in the pause state. */
	UI_PLAYER_STATE_EOF,
	UI_PLAYER_STATE_ERR, /**< The player is in the err state. */
	UI_PLAYER_STATE_BUTT
} UI_PLAYER_STATUS_E;

typedef enum {
	UI_CTRL_ESC = 0,
	UI_CTRL_PLAY,
	UI_CTRL_PREV,
	UI_CTRL_NEXT,
	UI_CTRL_BUTT
} UI_CTRL_ID_E;

typedef struct {
	lv_group_t *group;

	lv_obj_t *top_bar_obj;

	lv_obj_t *esc_obj;
	lv_obj_t *esc_label_obj;

	lv_timer_t *timer;
	lv_obj_t *slider_obj;
	lv_obj_t *cur_time_obj;
	lv_obj_t *total_time_obj;
	lv_obj_t *play_obj;
	lv_obj_t *play_label_obj;

	player_file_info_t file_info;
	RK_PLAYER_EVENT event;
	volatile UI_PLAYER_STATUS_E state;
} ui_video_player_context_t;

/**********************
 *  STATIC VARIABLES
 **********************/
static RK_PLAYER_CONFIG_S g_player_handler;
static ui_video_player_context_t player_ctx;

/**********************
 *  GLOBAL VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void update_play_progress(void) {
	char str_time[32] = {0};
	uint32_t cur_time = 0;

	memset(str_time, 0, sizeof(str_time));

	rk_player_get_video_position(&g_player_handler, &cur_time);
	cur_time = cur_time / 1000 + ((cur_time % 1000) ? 1 : 0);

	if (false == lv_obj_has_state(player_ctx.slider_obj, LV_STATE_PRESSED))
		lv_slider_set_value(player_ctx.slider_obj, cur_time, LV_ANIM_OFF);

	sprintf(str_time, "%02u:%02u", cur_time / 60, cur_time % 60);
	lv_label_set_text(player_ctx.cur_time_obj, str_time);
}

static void player_event_handler(RK_PLAYER_EVENT enEvent) {
	switch (enEvent) {
	case RK_PLAYER_EOF:
		player_ctx.state = UI_PLAYER_STATE_EOF;
		LOG_INFO("+++++ RK_PLAYER_EVENT_EOF +++++\n");
		update_play_progress();
		rk_player_pause(&g_player_handler);
		lv_label_set_text(player_ctx.play_label_obj, LV_SYMBOL_PLAY);
		break;
	case RK_PLAYER_PLAY:
		player_ctx.state = UI_PLAYER_STATE_PLAY;
		LOG_INFO("+++++ RK_PLAYER_EVENT_PLAY +++++\n");
		lv_label_set_text(player_ctx.play_label_obj, LV_SYMBOL_PAUSE);
		break;
	case RK_PLAYER_PAUSE:
		player_ctx.state = UI_PLAYER_STATE_PAUSE;
		LOG_INFO("+++++ RK_PLAYER_EVENT_PAUSED +++++\n");
		lv_label_set_text(player_ctx.play_label_obj, LV_SYMBOL_PLAY);
		break;
	default:
		LOG_INFO("+++++ Unknown event(%d) +++++\n", enEvent);
		break;
	}
}

static void player_event_cb(RK_PLAYER_EVENT enEvent) { player_ctx.event = enEvent; }

static void slider_event_cb(lv_event_t * e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_target(e);

	if (code == LV_EVENT_VALUE_CHANGED) {
		uint32_t value = lv_slider_get_value(obj);
		rk_player_audio_seek(&g_player_handler, value * 1000);
		rk_player_video_seek(&g_player_handler, value * 1000);
		LOG_INFO("Seek to %d seconds\n", value);
	}
}

static void time_handler(lv_timer_t *timer) {
	if (RK_PLAYER_BUTT != player_ctx.event) {
		player_event_handler(player_ctx.event);
		player_ctx.event = RK_PLAYER_BUTT;
	}

	if (UI_PLAYER_STATE_PLAY == player_ctx.state)
		update_play_progress();
}

static void video_player_start(void) {
	uint32_t total_time = 0;
	const char *ptr = NULL;
	char str_time[32] = {0};
	int32_t ret = -1;

	ret = rk_player_set_file(&g_player_handler, player_ctx.file_info.path);
	if (ret) {
		LOG_ERROR("Switch video failed.\n");
		return;
	}

	lv_label_set_text(player_ctx.cur_time_obj, "00:00");
	lv_label_set_text(player_ctx.total_time_obj, "00:00");

	rk_player_get_video_duration(&g_player_handler, &total_time);
	total_time = total_time / 1000 + ((total_time % 1000) ? 1 : 0);
	if (total_time > 0) {
		sprintf(str_time, "%02d:%02d", total_time / 60, total_time % 60);
		lv_label_set_text(player_ctx.total_time_obj, str_time);
		lv_slider_set_range(player_ctx.slider_obj, 0, total_time);
	}

	if (rk_player_play(&g_player_handler))
		LOG_ERROR("Play video failed.\n");

	lv_timer_ready(player_ctx.timer);
}

static int32_t video_player_create(void) {
	int ret = 0;
	memset(&g_player_handler, 0, sizeof(&g_player_handler));
	rk_pause_preview();
	g_player_handler.event_cb = player_event_cb;
	g_player_handler.skip_init_vo = true;
	ret = rk_player_create(&g_player_handler);
	if (ret) {
		LOG_ERROR("create player failed!\n");
		return -1;
	}

	return 0;
}

static void video_player_destroy(void) {
	rk_player_destroy(&g_player_handler);
	rk_resume_preview();
}

static void menu_event_cb(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_target(e);
	UI_CTRL_ID_E ctrl_id = (UI_CTRL_ID_E)lv_event_get_user_data(e);

	if (code == LV_EVENT_CLICKED) {
		switch (ctrl_id) {
		case UI_CTRL_ESC:
			ui_page_pop_page();
			break;

		case UI_CTRL_PLAY:
			if (player_ctx.state == UI_PLAYER_STATE_PAUSE) {
				rk_player_play(&g_player_handler);
			} else if (player_ctx.state == UI_PLAYER_STATE_PLAY) {
				rk_player_pause(&g_player_handler);
			} else if (player_ctx.state == UI_PLAYER_STATE_EOF) {
				video_player_start();
			}
			break;

		default:
			break;
		}
	} else if (code == LV_EVENT_FOCUSED) {
		lv_obj_set_style_outline_color(obj, lv_palette_lighten(LV_PALETTE_LIGHT_BLUE, 5), 0);
		lv_obj_set_style_outline_opa(obj, LV_OPA_COVER, 0);
	} else if (code == LV_EVENT_DEFOCUSED) {
		lv_obj_set_style_outline_opa(obj, LV_OPA_TRANSP, 0);
	}
}

static void video_player_layout_ctrl(lv_obj_t *page_obj) {
	static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
	                               LV_GRID_TEMPLATE_LAST};
	static lv_coord_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};

	lv_obj_t *top_bar_obj = player_ctx.top_bar_obj;
	lv_obj_t *esc_obj = player_ctx.esc_obj;
	lv_obj_t *play_obj = player_ctx.play_obj;

	lv_obj_align(top_bar_obj, LV_ALIGN_TOP_MID, 0, 0);
	lv_obj_set_layout(top_bar_obj, LV_LAYOUT_GRID);
	lv_obj_set_style_grid_column_dsc_array(top_bar_obj, col_dsc, 0);
	lv_obj_set_style_grid_row_dsc_array(top_bar_obj, row_dsc, 0);
	lv_obj_set_grid_cell(esc_obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);

	lv_obj_align(play_obj, LV_ALIGN_CENTER, 0, 0);
}

static void video_player_create_ctrl(lv_obj_t *page_obj) {
	lv_obj_t *obj = NULL;
	lv_color_t text_color = lv_color_make(0xff, 0xff, 0xff);

	lv_obj_set_style_bg_opa(page_obj, LV_OPA_TRANSP, 0);
	lv_disp_set_bg_opa(NULL, LV_OPA_TRANSP);
	static lv_style_t style;
	lv_style_reset(&style);
	lv_style_init(&style);
	lv_style_set_pad_all(&style, 5);
	lv_style_set_outline_width(&style, 1);
	lv_style_set_outline_opa(&style, LV_OPA_TRANSP);

	static lv_style_t symbol_style;
	lv_style_reset(&symbol_style);
	lv_style_init(&symbol_style);
	lv_style_set_text_font(&symbol_style, &lv_font_montserrat_48);
	lv_style_set_text_color(&symbol_style, text_color);
	lv_style_set_text_align(&symbol_style, LV_TEXT_ALIGN_CENTER);

	player_ctx.top_bar_obj = obj = lv_obj_create(page_obj);
	lv_obj_set_size(obj, lv_pct(100), lv_pct(100));
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, 0);

	player_ctx.esc_obj = obj = lv_btn_create(player_ctx.top_bar_obj);
	lv_obj_add_style(obj, &style, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_add_event_cb(obj, menu_event_cb, LV_EVENT_FOCUSED, (void *)UI_CTRL_ESC);
	lv_obj_add_event_cb(obj, menu_event_cb, LV_EVENT_DEFOCUSED, (void *)UI_CTRL_ESC);
	lv_obj_add_event_cb(obj, menu_event_cb, LV_EVENT_CLICKED, (void *)UI_CTRL_ESC);
	player_ctx.esc_label_obj = lv_label_create(player_ctx.esc_obj);
	lv_label_set_text(player_ctx.esc_label_obj, LV_SYMBOL_CLOSE);
	lv_obj_add_style(player_ctx.esc_label_obj, &symbol_style, 0);

	player_ctx.play_obj = obj = lv_btn_create(page_obj);
	lv_obj_add_style(obj, &style, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_add_event_cb(obj, menu_event_cb, LV_EVENT_FOCUSED, (void *)UI_CTRL_PLAY);
	lv_obj_add_event_cb(obj, menu_event_cb, LV_EVENT_DEFOCUSED, (void *)UI_CTRL_PLAY);
	lv_obj_add_event_cb(obj, menu_event_cb, LV_EVENT_CLICKED, (void *)UI_CTRL_PLAY);
	player_ctx.play_label_obj = lv_label_create(player_ctx.play_obj);
	lv_label_set_text(player_ctx.play_label_obj, LV_SYMBOL_PLAY);
	lv_obj_add_style(player_ctx.play_label_obj, &symbol_style, 0);

	static const lv_style_prop_t props[] = {LV_STYLE_BG_COLOR, 0};
	static lv_style_transition_dsc_t transition_dsc;
	lv_style_transition_dsc_init(&transition_dsc, props, lv_anim_path_linear, 300, 0, NULL);

	static lv_style_t style_main;
	static lv_style_t style_indicator;
	static lv_style_t style_knob;
	static lv_style_t style_pressed_color;
	lv_style_reset(&style_main);
	lv_style_init(&style_main);
	lv_style_set_bg_opa(&style_main, LV_OPA_50);
	lv_style_set_bg_color(&style_main, lv_color_hex(0x4c5457));
	lv_style_set_radius(&style_main, LV_RADIUS_CIRCLE);
	lv_style_set_pad_ver(&style_main, -2); /*Makes the indicator larger*/

	lv_style_reset(&style_indicator);
	lv_style_init(&style_indicator);
	lv_style_set_bg_opa(&style_indicator, LV_OPA_COVER);
	lv_style_set_bg_color(&style_indicator, lv_color_hex(0x00eaeb));
	lv_style_set_radius(&style_indicator, LV_RADIUS_CIRCLE);
	lv_style_set_transition(&style_indicator, &transition_dsc);

	lv_style_reset(&style_knob);
	lv_style_init(&style_knob);
	lv_style_set_bg_opa(&style_knob, LV_OPA_COVER);
	lv_style_set_bg_color(&style_knob, lv_color_hex(0xffffff));
	lv_style_set_border_color(&style_knob, lv_color_hex(0xffffff));
	lv_style_set_border_width(&style_knob, 32);
	lv_style_set_radius(&style_knob, LV_RADIUS_CIRCLE);
	lv_style_set_pad_all(&style_knob, 6); /*Makes the knob larger*/
	lv_style_set_transition(&style_knob, &transition_dsc);

	lv_style_reset(&style_pressed_color);
	lv_style_init(&style_pressed_color);
	lv_style_set_bg_color(&style_pressed_color, lv_color_hex(0x00eaeb));

	/* Create a slider and add the style */
	player_ctx.slider_obj = obj = lv_slider_create(page_obj);
	lv_obj_remove_style_all(obj); /*Remove the styles coming from the theme*/
	lv_obj_add_style(obj, &style_main, LV_PART_MAIN);
	lv_obj_add_style(obj, &style_indicator, LV_PART_INDICATOR);
	lv_obj_add_style(obj, &style_pressed_color, LV_PART_INDICATOR | LV_STATE_PRESSED);
	lv_obj_add_style(obj, &style_knob, LV_PART_KNOB);
	lv_obj_add_style(obj, &style_pressed_color, LV_PART_KNOB | LV_STATE_PRESSED);
	lv_obj_set_size(obj, lv_pct(50), 32);
	lv_obj_align(obj, LV_ALIGN_BOTTOM_MID, 0, -50);
	lv_obj_add_event_cb(obj, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

	player_ctx.cur_time_obj = obj = lv_label_create(page_obj);
	lv_obj_set_style_text_font(obj, &lv_font_montserrat_48, 0);
	lv_obj_set_style_text_color(obj, text_color, 0);
	lv_obj_align_to(obj, player_ctx.slider_obj, LV_ALIGN_OUT_LEFT_MID, -35, 0);
	lv_label_set_text(obj, "00:00");

	player_ctx.total_time_obj = obj = lv_label_create(page_obj);
	lv_obj_set_style_text_font(obj, &lv_font_montserrat_48, 0);
	lv_obj_set_style_text_color(obj, text_color, 0);
	lv_obj_align_to(obj, player_ctx.slider_obj, LV_ALIGN_OUT_RIGHT_MID, 15, 0);
	lv_label_set_text(obj, "00:00");

	player_ctx.timer = lv_timer_create(time_handler, 500, NULL);
	lv_timer_ready(player_ctx.timer);
}

static void video_player_destroy_ctrl(void) {
	if (player_ctx.timer) {
		lv_timer_del(player_ctx.timer);
		player_ctx.timer = NULL;
	}

	if (player_ctx.top_bar_obj) {
		ui_common_remove_style_all(player_ctx.top_bar_obj);
		lv_obj_del(player_ctx.top_bar_obj);
		player_ctx.top_bar_obj = NULL;
	}

	if (player_ctx.slider_obj) {
		ui_common_remove_style_all(player_ctx.slider_obj);
		lv_obj_del(player_ctx.slider_obj);
		player_ctx.slider_obj = NULL;
	}

	if (player_ctx.cur_time_obj) {
		ui_common_remove_style_all(player_ctx.cur_time_obj);
		lv_obj_del(player_ctx.cur_time_obj);
		player_ctx.cur_time_obj = NULL;
	}

	if (player_ctx.total_time_obj) {
		ui_common_remove_style_all(player_ctx.total_time_obj);
		lv_obj_del(player_ctx.total_time_obj);
		player_ctx.total_time_obj = NULL;
	}

	if (player_ctx.play_obj) {
		ui_common_remove_style_all(player_ctx.play_obj);
		lv_obj_del(player_ctx.play_obj);
		player_ctx.play_obj = NULL;
	}
}

static void video_player_param_init(lv_obj_t *page_obj) {
	memset(&player_ctx, 0, sizeof(player_ctx));

	player_file_info_t *file_info = (player_file_info_t *)lv_obj_get_user_data(page_obj);
	memcpy(&player_ctx.file_info, file_info, sizeof(player_ctx.file_info));
}

static void video_player_add_indev(void) {
	lv_group_t *group = lv_port_indev_group_create();
	if (NULL == group)
		return;

	lv_group_add_obj(group, player_ctx.esc_obj);
	lv_group_add_obj(group, player_ctx.play_obj);

	player_ctx.group = group;
}

static void video_player_delete_indev(void) {
	if (NULL != player_ctx.group) {
		lv_port_indev_group_destroy(player_ctx.group);
		player_ctx.group = NULL;
	}
}

static void video_player_page_create(lv_obj_t *page_obj) {
	LOG_INFO("enter\n");
	video_player_param_init(page_obj);
	video_player_create_ctrl(page_obj);
	video_player_layout_ctrl(page_obj);
}

static void video_player_page_enter(lv_obj_t *page_obj) {
	LOG_INFO("enter\n");
	video_player_add_indev();
	video_player_create();
	video_player_start();
}

static void video_player_page_exit(lv_obj_t *page_obj) {
	LOG_INFO("enter\n");
	video_player_delete_indev();
	video_player_destroy();
}

static void video_player_page_destroy(lv_obj_t *page_obj) {
	LOG_INFO("enter\n");
	video_player_destroy_ctrl();
}

static UI_PAGE_HANDLER_T video_player_page = {.name = "video_player",
                                              .init = NULL,
                                              .create = video_player_page_create,
                                              .enter = video_player_page_enter,
                                              .destroy = video_player_page_destroy,
                                              .exit = video_player_page_exit};

UI_PAGE_REGISTER(video_player_page)
