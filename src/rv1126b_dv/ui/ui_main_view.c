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
#define LOG_TAG "ui_main_view.c"

#include <linux/input.h>

#include "common.h"
#include "evdev.h"
#include "lvgl/porting/lv_port_indev.h"
#include "storage.h"
#include "ui_common.h"
#include "ui_dialog_storage.h"
#include "ui_page_manager.h"
#include "ui_player.h"
#include "ui_resource_manager.h"
#include "video.h"

#include <stdio.h>
#include <time.h>

/**********************
 *  STATIC PROTOTYPES
 **********************/
typedef enum {
	MSG_PREVIEW_OPEN = 0,
	MSG_PREVIEW_CLOSE,
} UI_MAIN_VIEW_MSG_E;

typedef enum {
	CAPTURE_1P1S,
	CAPTURE_5P1S,
	CAPTURE_10P1S,
	CAPTURE_15P3S,
} CAPTURE_MODE;

typedef struct {
	uint32_t disp_cnt;
	uint32_t disp_index;
	rkipc_mount_status disk_state;
} UI_VIEW_CONTEXT_S;

typedef struct {
	lv_obj_t *rec_obj;
	lv_obj_t *status_bar;
	lv_obj_t *bat_status_obj;

	lv_obj_t *media_obj;
	lv_obj_t *setting_obj;
	lv_obj_t *record_obj;
	lv_obj_t *menu_bar;
	lv_timer_t *timer;
	lv_timer_t *capture_timer;
	lv_obj_t *time_bar;
	lv_obj_t *cap_obj;

	lv_group_t *group;

} UI_VIEW_CONTROL_S;

/**********************
 *  STATIC VARIABLES
 **********************/
static UI_VIEW_CONTEXT_S main_view_ctx;
static UI_VIEW_CONTROL_S main_view_ctrl;
static time_t record_start_time;
extern lv_ft_info_t ttf_info_32;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
static void media_event_handler(lv_event_t *e);
static void set_event_handler(lv_event_t *e);
static void record_event_handler(lv_event_t *e);

/**********************s
 *   STATIC FUNCTIONS
 **********************/
static bool rk_record_is_active() {
	int value = 0;
	rk_storage_record_statue_get(0, &value);
	return (value == 1);
}

static int get_bat_capacity(void) {
	char buf[64] = {'\0'};
	int fd = open("/sys/class/power_supply/cw221X-bat/capacity", O_RDONLY);
	int ret = 0, capacity = 100;
	if (fd >= 0) {
		ret = read(fd, buf, sizeof(buf));
		if (ret < 0)
			LOG_ERROR("read bat capacity failed %s\n", strerror(errno));
		else
			capacity = atoi(buf);
		close(fd);
	} else {
		LOG_ERROR("open bat capacity failed %s\n", strerror(errno));
	}
	return capacity;
}

static void timer_xcb(lv_timer_t *timer) {
	time_t timep;
	struct tm *p;
	static bool flag = true;

	time(&timep);
	if (rk_record_is_active()) {
		timep -= record_start_time;
		p = gmtime(&timep);
		lv_label_set_text_fmt(main_view_ctrl.time_bar, "%02d:%02d:%02d              ", p->tm_hour,
		                      p->tm_min, p->tm_sec);
		flag = !flag;
		lv_img_set_src(main_view_ctrl.rec_obj, flag ? icon_record_stat_run : icon_record_stat_idle);
	} else {
		p = gmtime(&timep);
		lv_label_set_text_fmt(main_view_ctrl.time_bar, "%4d-%02d-%02d %02d:%02d:%02d",
		                      1900 + p->tm_year, 1 + p->tm_mon, p->tm_mday, p->tm_hour, p->tm_min,
		                      p->tm_sec);
		lv_img_set_src(main_view_ctrl.rec_obj, icon_record_stat_idle);
	}
	lv_label_set_text(main_view_ctrl.bat_status_obj, LV_SYMBOL_BATTERY_FULL);
	if (!rk_param_get_int("video.source:enable_display", 1))
		RK_MPI_VO_SetLayerFlush(rk_param_get_int("display:layer_id", 0));
}

static void capture_xcb(lv_timer_t *timer) {
	int done_num = rk_photo_get_done_num();
	int total_num = rk_photo_get_max_num();
	if (done_num == total_num) {
		lv_label_set_text_fmt(main_view_ctrl.cap_obj, " ");
		// safe ops, just set the flag.
		lv_timer_pause(main_view_ctrl.capture_timer);
		rk_photo_stop();
	} else {
		lv_label_set_text_fmt(main_view_ctrl.cap_obj, "capture %2d/%2d", done_num, total_num);
	}
}

static void main_view_layout_ctrl(void) {
	lv_obj_t *obj = NULL;

	obj = main_view_ctrl.status_bar;
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, 0);
	lv_obj_set_style_flex_flow(obj, LV_FLEX_FLOW_ROW, 0);
	lv_obj_set_style_flex_main_place(obj, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
	lv_obj_set_style_flex_cross_place(obj, LV_FLEX_ALIGN_CENTER, 0);
	lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, 0);

	obj = main_view_ctrl.menu_bar;
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_layout(obj, LV_LAYOUT_FLEX, 0);
	lv_obj_set_style_flex_flow(obj, LV_FLEX_FLOW_ROW, 0);
	lv_obj_set_style_flex_main_place(obj, LV_FLEX_ALIGN_SPACE_EVENLY, 0);
	lv_obj_set_style_flex_cross_place(obj, LV_FLEX_ALIGN_CENTER, 0);
	lv_obj_align(obj, LV_ALIGN_BOTTOM_MID, 0, 0);
}

static void main_view_destroy_ctrl(void) {
	if (main_view_ctrl.capture_timer) {
		lv_timer_del(main_view_ctrl.capture_timer);
		main_view_ctrl.capture_timer = NULL;
	}
	if (main_view_ctrl.timer) {
		lv_timer_del(main_view_ctrl.timer);
		main_view_ctrl.timer = NULL;
	}
	if (main_view_ctrl.status_bar) {
		ui_common_remove_style_all(main_view_ctrl.status_bar);
		lv_obj_del(main_view_ctrl.status_bar);
		main_view_ctrl.status_bar = NULL;
	}

	if (main_view_ctrl.menu_bar) {
		ui_common_remove_style_all(main_view_ctrl.menu_bar);
		lv_obj_del(main_view_ctrl.menu_bar);
		main_view_ctrl.menu_bar = NULL;
	}
	if (main_view_ctrl.cap_obj) {
		lv_obj_del(main_view_ctrl.cap_obj);
		main_view_ctrl.cap_obj = NULL;
	}
}

static void capture_event_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_target(e);

	if (code == LV_EVENT_CLICKED) {
		switch (rkipc_storage_dev_mount_status_get()) {
		case DISK_UNMOUNTED:
			ui_nosdcard_dialog_create();
			break;
		case DISK_FORMAT_ERR:
		case DISK_NOT_FORMATTED:
			ui_noformat_dialog_create();
			break;
		case DISK_SCANNING:
			ui_scansdcard_dialog_create();
			break;
		case DISK_MOUNTED:
			lv_timer_resume(main_view_ctrl.capture_timer);
			rk_photo_start();
			break;
		default:
			break;
		}
	}
}

static void record_event_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_target(e);

	LOG_INFO("record_event_handler code %d\n", code);
	if (code == LV_EVENT_CLICKED) {
		switch (rkipc_storage_dev_mount_status_get()) {
		case DISK_UNMOUNTED:
			ui_nosdcard_dialog_create();
			break;
		case DISK_FORMAT_ERR:
		case DISK_NOT_FORMATTED:
			ui_noformat_dialog_create();
			break;
		case DISK_SCANNING:
			ui_scansdcard_dialog_create();
			break;
		case DISK_MOUNTED:
			if (rk_record_is_active()) {
				rk_video_stop_record();
				record_start_time = 0;
				lv_imgbtn_set_src(obj, LV_IMGBTN_STATE_RELEASED, NULL, icon_record_start, NULL);
				lv_imgbtn_set_src(obj, LV_IMGBTN_STATE_PRESSED, NULL, icon_record_start, NULL);
			} else {
				time(&record_start_time);
				rk_video_start_record();
				lv_imgbtn_set_src(obj, LV_IMGBTN_STATE_RELEASED, NULL, icon_record_stop, NULL);
				lv_imgbtn_set_src(obj, LV_IMGBTN_STATE_PRESSED, NULL, icon_record_stop, NULL);
			}
			break;
		default:
			break;
		}
	}
}

static void media_event_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_target(e);

	LOG_INFO("media_event_handler code %d\n", code);
	if (code == LV_EVENT_CLICKED) {
		switch (rkipc_storage_dev_mount_status_get()) {
		case DISK_UNMOUNTED:
			ui_nosdcard_dialog_create();
			break;
		case DISK_FORMAT_ERR:
		case DISK_NOT_FORMATTED:
			ui_noformat_dialog_create();
			break;
		case DISK_SCANNING:
			ui_scansdcard_dialog_create();
			break;
		case DISK_MOUNTED:
			ui_page_push_page("gallery", NULL);
		default:
			break;
		}
	}
}

static void set_event_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_target(e);

	if (code == LV_EVENT_CLICKED) {
		ui_page_push_page("settings", NULL);
	}
}

static void main_view_create_ctrl(lv_obj_t *page_obj) {
	lv_obj_set_style_bg_opa(page_obj, LV_OPA_TRANSP, 0);
	lv_disp_set_bg_opa(NULL, LV_OPA_TRANSP);

	main_view_ctrl.status_bar = lv_obj_create(page_obj);
	lv_obj_set_size(main_view_ctrl.status_bar, lv_pct(100), LV_SIZE_CONTENT);

	main_view_ctrl.rec_obj = lv_img_create(main_view_ctrl.status_bar);
	//lv_img_set_src(main_view_ctrl.rec_obj, index_icon_record_nor);

	main_view_ctrl.menu_bar = lv_obj_create(page_obj);
	lv_obj_set_size(main_view_ctrl.menu_bar, lv_pct(100), LV_SIZE_CONTENT);

	main_view_ctrl.media_obj = lv_imgbtn_create(main_view_ctrl.menu_bar);
	lv_imgbtn_set_src(main_view_ctrl.media_obj, LV_IMGBTN_STATE_RELEASED, NULL, icon_media,
	                  NULL);
	lv_imgbtn_set_src(main_view_ctrl.media_obj, LV_IMGBTN_STATE_PRESSED, NULL, icon_media,
	                  NULL);
	lv_obj_set_size(main_view_ctrl.media_obj, icon_media->header.w,
	                icon_media->header.h);
	lv_obj_add_event_cb(main_view_ctrl.media_obj, media_event_handler, LV_EVENT_CLICKED, NULL);

	main_view_ctrl.record_obj = lv_imgbtn_create(main_view_ctrl.menu_bar);
	if (rk_get_mode() == RK_PHOTO_MODE) {
		lv_imgbtn_set_src(main_view_ctrl.record_obj, LV_IMGBTN_STATE_RELEASED, NULL,
		                  icon_photo, NULL);
		lv_imgbtn_set_src(main_view_ctrl.record_obj, LV_IMGBTN_STATE_PRESSED, NULL,
		                  icon_photo, NULL);
		lv_obj_add_event_cb(main_view_ctrl.record_obj, capture_event_handler, LV_EVENT_CLICKED,
		                    NULL);
	} else {
		lv_imgbtn_set_src(main_view_ctrl.record_obj, LV_IMGBTN_STATE_RELEASED, NULL,
		                  icon_record_start, NULL);
		lv_imgbtn_set_src(main_view_ctrl.record_obj, LV_IMGBTN_STATE_PRESSED, NULL,
		                  icon_record_start, NULL);
		lv_obj_add_event_cb(main_view_ctrl.record_obj, record_event_handler, LV_EVENT_CLICKED,
		                    NULL);
		lv_obj_set_size(main_view_ctrl.record_obj, icon_record_start->header.w,
						icon_record_start->header.h);
	}

	main_view_ctrl.setting_obj = lv_imgbtn_create(main_view_ctrl.menu_bar);
	lv_imgbtn_set_src(main_view_ctrl.setting_obj, LV_IMGBTN_STATE_RELEASED, NULL, icon_setting,
	                  NULL);
	lv_imgbtn_set_src(main_view_ctrl.setting_obj, LV_IMGBTN_STATE_PRESSED, NULL, icon_setting,
	                  NULL);
	lv_obj_set_size(main_view_ctrl.setting_obj, icon_setting->header.w,
	                icon_setting->header.h);
	lv_obj_add_event_cb(main_view_ctrl.setting_obj, set_event_handler, LV_EVENT_CLICKED, NULL);
	lv_obj_add_event_cb(main_view_ctrl.setting_obj, set_event_handler, LV_EVENT_FOCUSED, NULL);
	lv_obj_add_event_cb(main_view_ctrl.setting_obj, set_event_handler, LV_EVENT_DEFOCUSED, NULL);

	main_view_ctrl.time_bar = lv_label_create(main_view_ctrl.status_bar);
	lv_obj_set_style_text_font(main_view_ctrl.time_bar, &lv_font_montserrat_48, 0);
	lv_obj_set_style_text_color(main_view_ctrl.time_bar, lv_color_hex(0xffffff), 0);
	lv_obj_set_style_width(main_view_ctrl.time_bar, lv_pct(60), 0);

	main_view_ctrl.bat_status_obj = lv_label_create(main_view_ctrl.status_bar);
	lv_obj_set_style_text_font(main_view_ctrl.bat_status_obj, &lv_font_montserrat_48, 0);
	lv_obj_set_style_text_color(main_view_ctrl.bat_status_obj, lv_color_hex(0xffffff), 0);

	main_view_ctrl.cap_obj = lv_label_create(page_obj);
	lv_obj_set_style_text_font(main_view_ctrl.cap_obj, &lv_font_montserrat_48, 0);
	lv_obj_set_style_text_color(main_view_ctrl.cap_obj, lv_color_hex(0xffffff), 0);
	lv_obj_set_style_width(main_view_ctrl.cap_obj, lv_pct(20), 0);
	lv_obj_align(main_view_ctrl.cap_obj, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_bg_opa(main_view_ctrl.cap_obj, LV_OPA_TRANSP, 0);
	lv_label_set_text_fmt(main_view_ctrl.cap_obj, " ");

	main_view_ctrl.timer = lv_timer_create(timer_xcb, 500, NULL);
	lv_timer_ready(main_view_ctrl.timer);

	main_view_ctrl.capture_timer = lv_timer_create(capture_xcb, 100, NULL);
	lv_timer_pause(main_view_ctrl.capture_timer);
}

static void main_view_page_create(lv_obj_t *page_obj) {
	main_view_ctx.disk_state = DISK_MOUNT_BUTT;
	main_view_ctx.disp_index = 0;
	main_view_create_ctrl(page_obj);
	main_view_layout_ctrl();
}

static void main_view_add_indev(void) {
	lv_group_t *group = lv_port_indev_group_create();
	if (NULL == group)
		return;

	lv_group_add_obj(group, main_view_ctrl.media_obj);
	lv_group_add_obj(group, main_view_ctrl.record_obj);
	lv_group_add_obj(group, main_view_ctrl.setting_obj);
	lv_group_focus_obj(main_view_ctrl.record_obj);

	main_view_ctrl.group = group;
}

static void main_view_delete_indev(void) {
	if (NULL != main_view_ctrl.group) {
		lv_port_indev_group_destroy(main_view_ctrl.group);
		main_view_ctrl.group = NULL;
	}
}

static void main_view_page_enter(lv_obj_t *page_obj) {
	main_view_add_indev();
}

static void main_view_page_exit(lv_obj_t *page_obj) {
	main_view_delete_indev();
	if (rk_record_is_active())
		rk_video_stop_record();
}

static void main_view_page_destroy(lv_obj_t *page_obj) { main_view_destroy_ctrl(); }

static UI_PAGE_HANDLER_T main_page = {.name = "main",
                                      .init = NULL,
                                      .create = main_view_page_create,
                                      .enter = main_view_page_enter,
                                      .destroy = main_view_page_destroy,
                                      .exit = main_view_page_exit};

UI_PAGE_REGISTER(main_page)
