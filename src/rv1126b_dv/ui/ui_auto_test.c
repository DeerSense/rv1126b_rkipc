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
#define LOG_TAG "ui_test.c"

#define _GNU_SOURCE
#include <stdio.h>
#include <linux/input.h>
#include <time.h>
#include <unistd.h>
#include "audio.h"
#include "common.h"
#include "evdev.h"
#include "lvgl/porting/lv_port_indev.h"
#include "player.h"
#include "storage.h"
#include "ui_common.h"
#include "ui_dialog.h"
#include "ui_dialog_format.h"
#include "ui_dialog_storage.h"
#include "ui_page_manager.h"
#include "ui_player.h"
#include "ui_resource_manager.h"
#include "video.h"

/**********************
 *  STATIC PROTOTYPES
 **********************/
typedef struct {
	lv_obj_t *container_obj;

	lv_obj_t *exit_obj;
	lv_obj_t *exit_label_obj;
	lv_obj_t *rtsp_enable_obj;
	lv_obj_t *restart_test_obj;
	lv_obj_t *restart_test_label_obj;
	lv_obj_t *mode_test_obj;
	lv_obj_t *mode_test_label_obj;
	lv_obj_t *video_test_obj;
	lv_obj_t *video_test_label_obj;
	lv_obj_t *photo_test_obj;
	lv_obj_t *photo_test_label_obj;
	lv_obj_t *player_test_obj;
	lv_obj_t *player_test_label_obj;
	lv_obj_t *sleep_test_obj;
	lv_obj_t *sleep_test_label_obj;
	lv_obj_t *all_test_obj;
	lv_obj_t *all_test_label_obj;
	lv_group_t *group;

} UI_AUTO_TEST_CONTROL_S;

typedef enum {
	TEST_TYPE_RESTART = 0,
	TEST_TYPE_MODE_SWITCH,
	TEST_TYPE_VIDEO,
	TEST_TYPE_PHOTO,
	TEST_TYPE_PLAYER,
	TEST_TYPE_SLEEP,
	TEST_TYPE_ALL,
} UI_TEST_TYPE;

static UI_AUTO_TEST_CONTROL_S auto_test_ctrl;

extern lv_ft_info_t ttf_info_32;

static void return_event_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_target(e);

	if (code == LV_EVENT_CLICKED) {
		ui_page_pop_page();
	}
}

static lv_timer_t *test_timer = NULL;
static lv_obj_t *test_dialog = NULL;
static lv_obj_t *test_status_obj = NULL;
static int test_total_count = 0;
static int test_total_success_count = 0;
static int test_total_fail_count = 0;
static char test_result_msg[128];
static int test_type = TEST_TYPE_RESTART;

static pthread_t test_task_id;

static void create_test_dialog(const char *title) {
	lv_obj_t *obj, *obj_bg, *context_obj;
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

	test_dialog = obj = lv_obj_create(lv_scr_act());
	lv_obj_set_size(obj, lv_pct(100), lv_pct(100));
	lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_bg_color(obj, color, 0);
	lv_obj_set_style_border_color(obj, color, 0);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_50, 0);

	obj_bg = obj = lv_obj_create(obj);
	lv_obj_set_size(obj, lv_pct(50), lv_pct(40));
	lv_obj_set_style_bg_color(obj, dialog_color, 0);
	lv_obj_set_style_border_color(obj, dialog_color, 0);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_80, 0);
	lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);
	lv_obj_add_style(obj, &style, 0);

	obj = lv_label_create(obj_bg);
	lv_obj_set_style_text_font(obj, ttf_info_32.font, 0);
	lv_obj_set_style_text_color(obj, text_color, 0);
	lv_obj_set_size(obj, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(obj, title);
	lv_label_set_long_mode(obj, LV_LABEL_LONG_SCROLL_CIRCULAR);

	obj = lv_label_create(obj_bg);
	lv_label_set_text(obj, LV_SYMBOL_WARNING);
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
	lv_obj_set_flex_grow(obj, 1);

	text_color.full = 0xffffffff;
	test_status_obj = obj = lv_label_create(context_obj);
	lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
	lv_obj_set_style_text_font(obj, ttf_info_32.font, 0);
	lv_obj_set_style_text_color(obj, text_color, 0);
	lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(obj, lv_pct(100));
}

static void test_timer_func(lv_timer_t *timer) {
	if (test_total_count == (test_total_success_count + test_total_fail_count) && test_total_count > 0) {
		if (test_status_obj) {
			lv_obj_del(test_status_obj);
			test_status_obj = NULL;
		}
		if (test_dialog) {
			lv_obj_del(test_dialog);
			test_dialog = NULL;
		}
		lv_timer_del(test_timer);
		pthread_join(test_task_id, NULL);
		test_timer = NULL;
		test_total_count = 0;
		test_total_success_count = 0;
		test_total_fail_count = 0;
		test_task_id = 0;
		ui_dialog_create("INFO", test_result_msg);
	}
	if (test_status_obj) {
		static char info[64];
		snprintf(info, sizeof(info), "TOTAL: %d, SUCCESS: %d, FAIL: %d",
		         test_total_count, test_total_success_count, test_total_fail_count);
		lv_label_set_text(test_status_obj, info);
	}
}

static int clean_directory(const char* dir_name, bool need_del_self) {
	DIR *dir = opendir(dir_name);
	if (dir == NULL) {
		LOG_ERROR("open directory %s failed\n", dir_name);
		return -1;
	}

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;

		char file_path[512];
		snprintf(file_path, sizeof(file_path), "%s/%s", dir_name, entry->d_name);

		if (entry->d_type == DT_DIR) {
			clean_directory(file_path, true);
		} else {
			unlink(file_path);
		}
	}

	closedir(dir);

	if (need_del_self) {
		rmdir(dir_name);
	}
	return 0;
}

static void video_test_routine(int fd) {
	int ret = 0, len = 0;
	int count = rk_param_get_int("auto_test:video_test_loop_count", 30);
	int success_count = 0, fail_count = 0;
	char msg[256] = {'\0'};
	rk_set_mode(RK_VIDEO_MODE);
	for (int i = 0; i < count; i++) {
		ret = rk_video_start_record();
		if (ret != 0) {
			++test_total_fail_count;
			++fail_count;
			memset(msg, 0, sizeof(msg));
			len = snprintf(msg, sizeof(msg), "[VIDEO TEST] loop %d failed, rk_video_start_record failed %#X\n"
					, i, ret);
			write(fd, msg, len);
			fsync(fd);
			LOG_ERROR("[VIDEO TEST] loop %d test failed, rk_video_start_record failed %#X\n", i, ret);
			continue;
		}
		usleep(5000 * 1000);
		ret = rk_video_stop_record();
		if (ret != 0) {
			++test_total_fail_count;
			++fail_count;
			memset(msg, 0, sizeof(msg));
			len = snprintf(msg, sizeof(msg), "[VIDEO TEST] loop %d failed, rk_video_stop_record failed %#X\n"
					, i, ret);
			write(fd, msg, len);
			fsync(fd);
			LOG_ERROR("[VIDEO TEST] loop %d test failed, rk_video_stop_record failed %#X\n", i, ret);
			continue;
		}
		if (i > 0 && (i % 100) == 0) {
			clean_directory("/mnt/sdcard/video0", false);
		}
		++test_total_success_count;
		++success_count;
		LOG_ERROR("[VIDEO TEST] loop %d test success\n", i);
	}
	memset(msg, 0, sizeof(msg));
	len = snprintf(msg, sizeof(msg), "[VIDEO TEST] success count %d, failed count %d\n"
		, success_count, fail_count);
	write(fd, msg, len);
	fsync(fd);
	clean_directory("/mnt/sdcard/video0", false);
}

static void photo_test_routine(int fd) {
	int count = rk_param_get_int("auto_test:photo_test_loop_count", 30);
	int ret = 0, len = 0, success_count = 0, fail_count = 0;
	char file_name[256] = {'\0'};
	char msg[256] = {'\0'};
	rk_set_mode(RK_PHOTO_MODE);
	for (int i = 0; i < count; i++) {
		ret = rk_photo_start();
		if (ret != 0) {
			++test_total_fail_count;
			++fail_count;
			memset(msg, 0, sizeof(msg));
			len = snprintf(msg, sizeof(msg), "[PHOTO TEST] loop %d failed, rk_photo_start failed %#X\n"
					, i, ret);
			write(fd, msg, len);
			fsync(fd);
			LOG_ERROR("[PHOTO TEST] loop %d test failed, rk_photo_start failed %#X\n", i, ret);
			continue;
		}
		while (rk_photo_get_done_num() != rk_photo_get_max_num()) {
			sleep(1);
			LOG_ERROR("[PHOTO TEST] wait for all photo done\n");
		}
		ret = rk_photo_stop();
		if (ret != 0) {
			++test_total_fail_count;
			++fail_count;
			memset(msg, 0, sizeof(msg));
			len = snprintf(msg, sizeof(msg), "[PHOTO TEST] loop %d failed, rk_photo_stop failed %#X\n"
					, i, ret);
			write(fd, msg, len);
			fsync(fd);
			LOG_ERROR("[PHOTO TEST] loop %d test failed, rk_photo_stop failed %#X\n", i, ret);
			continue;
		}
		if (i > 0 && (i % 500) == 0) {
			clean_directory("/mnt/sdcard/photo", false);
		}
		++test_total_success_count;
		++success_count;
		LOG_ERROR("[PHOTO TEST] loop %d test success\n", i);
	}
	memset(msg, 0, sizeof(msg));
	len = snprintf(msg, sizeof(msg), "[VIDEO TEST] success count %d, failed count %d\n"
		, success_count, fail_count);
	write(fd, msg, len);
	fsync(fd);
	clean_directory("/mnt/sdcard/photo", false);
}

static void restart_test_routine(int fd) {
	int count = rk_param_get_int("auto_test:restart_test_loop_count", 30);
	int ret = 0, len = 0, success_count = 0, fail_count = 0;
	char msg[256] = {'\0'};
	for (int i = 0; i < count; i++) {
		ret = rk_video_restart();
		if (ret != 0) {
			++test_total_fail_count;
			++fail_count;
			memset(msg, 0, sizeof(msg));
			len = snprintf(msg, sizeof(msg), "[RESTART TEST] loop %d failed, rk_video_restart failed %#X\n"
					, i, ret);
			write(fd, msg, len);
			fsync(fd);
			LOG_ERROR("[RESTART TEST] loop %d test failed %#X\n", i, ret);
			continue;
		}
		++test_total_success_count;
		++success_count;
		LOG_ERROR("[RESTART TEST] loop %d test success\n", i);
	}
	memset(msg, 0, sizeof(msg));
	len = snprintf(msg, sizeof(msg), "[RESTART TEST] success count %d, failed count %d\n"
		, success_count, fail_count);
	write(fd, msg, len);
	fsync(fd);
}

static void mode_switch_test_routine(int fd) {
	int count = rk_param_get_int("auto_test:mode_test_loop_count", 30);
	int ret = 0, mode = RK_PHOTO_MODE, len = 0, success_count = 0, fail_count = 0;
	char msg[256] = {'\0'};
	for (int i = 0; i < count; i++) {
		mode = i % 2;
		ret = rk_set_mode(mode);
		if (ret != 0) {
			++test_total_fail_count;
			++fail_count;
			memset(msg, 0, sizeof(msg));
			len = snprintf(msg, sizeof(msg), "[MODE TEST] loop %d failed, rk_set_mode failed %#X\n"
					, i, ret);
			write(fd, msg, len);
			fsync(fd);
			LOG_ERROR("[MODE TEST] loop %d test failed %#X\n", i, ret);
			continue;
		}
		++test_total_success_count;
		++success_count;
		LOG_ERROR("[MODE TEST] loop %d test success\n", i);
	}
	memset(msg, 0, sizeof(msg));
	len = snprintf(msg, sizeof(msg), "[MODE TEST] success count %d, failed count %d\n"
		, success_count, fail_count);
	write(fd, msg, len);
	fsync(fd);
}

static void player_test_routine(int fd) {
	int count = rk_param_get_int("auto_test:player_test_loop_count", 30);
	int ret = 0, len = 0, success_count = 0, fail_count = 0;
	const char *file_path = rk_param_get_string("auto_test:player_test_file_path", NULL);
	char msg[256] = {'\0'};
	if (!file_path) {
		LOG_ERROR("[PLAYER TEST] file path is NULL, please set auto_test:player_test_file_path\n");
		return ;
	}
	for (int i = 0; i < count; i++) {
		RK_PLAYER_CONFIG_S config = {};
		uint32_t duration = 0, seek_time = 0;
		config.skip_init_vo = true;
		ret = rk_pause_preview();
		if (ret != 0) {
			++test_total_fail_count;
			++fail_count;
			memset(msg, 0, sizeof(msg));
			len = snprintf(msg, sizeof(msg), "[PLAYER TEST] loop %d failed, rk_pause_preview failed %#X\n"
					, i, ret);
			write(fd, msg, len);
			fsync(fd);
			LOG_ERROR("[PLAYER TEST] loop %d test failed %#X\n", i, ret);
			continue;
		}
		ret = rk_player_create(&config);
		if (ret != 0) {
			++test_total_fail_count;
			++fail_count;
			memset(msg, 0, sizeof(msg));
			len = snprintf(msg, sizeof(msg), "[PLAYER TEST] loop %d failed, rk_player_create failed %#X\n"
					, i, ret);
			write(fd, msg, len);
			fsync(fd);
			LOG_ERROR("[PLAYER TEST] loop %d test failed %#X\n", i, ret);
			goto __player_test_failed;
		}
		ret = rk_player_set_file(&config, file_path);
		if (ret != 0) {
			++test_total_fail_count;
			++fail_count;
			memset(msg, 0, sizeof(msg));
			len = snprintf(msg, sizeof(msg), "[PLAYER TEST] loop %d failed, rk_player_set_file failed %#X\n"
					, i, ret);
			write(fd, msg, len);
			fsync(fd);
			LOG_ERROR("[PLAYER TEST] loop %d test failed %#X\n", i, ret);
			goto __player_test_failed;
		}
		rk_player_get_video_duration(&config, &duration);
		// INFO: Just for test, make sure the player channel has the highest priority
		RK_MPI_VO_SetLayerPriority(0, 1);
		RK_MPI_VO_SetLayerPriority(1, 0);
		ret = rk_player_play(&config);
		if (ret != 0) {
			++test_total_fail_count;
			++fail_count;
			memset(msg, 0, sizeof(msg));
			len = snprintf(msg, sizeof(msg), "[PLAYER TEST] loop %d failed, rk_player_play failed %#X\n"
					, i, ret);
			write(fd, msg, len);
			fsync(fd);
			LOG_ERROR("[PLAYER TEST] loop %d test failed %#X\n", i, ret);
			goto __player_test_failed;
		}
		for (int j = 0; j < 5; ++j) {
			seek_time = random() % duration;
			ret = rk_player_audio_seek(&config, seek_time);
			ret |= rk_player_video_seek(&config, seek_time);
			if (ret != 0) {
				++test_total_fail_count;
				++fail_count;
				memset(msg, 0, sizeof(msg));
				len = snprintf(msg, sizeof(msg), "[PLAYER TEST] loop %d failed, player seek failed %#X\n"
						, i, ret);
				write(fd, msg, len);
				fsync(fd);
				LOG_ERROR("[PLAYER TEST] loop %d test failed %#X\n", i, ret);
				goto __player_test_failed;
			}
			sleep(4);
		}
		++test_total_success_count;
		++success_count;
		LOG_ERROR("[PLAYER TEST] loop %d test success\n", i);
__player_test_failed:
		RK_MPI_VO_SetLayerPriority(0, 0);
		RK_MPI_VO_SetLayerPriority(1, 1);
		rk_player_destroy(&config);
		rk_resume_preview();
	}
	memset(msg, 0, sizeof(msg));
	len = snprintf(msg, sizeof(msg), "[PLAYER TEST] success count %d, failed count %d\n"
		, success_count, fail_count);
	write(fd, msg, len);
	fsync(fd);
}

static void sleep_test_routine(int fd) {
	int count = rk_param_get_int("auto_test:sleep_test_loop_count", 30);
	int ret = 0, len = 0, success_count = 0, fail_count = 0;
	char msg[256] = {'\0'};
	// set sleep time
	system("io -4 0x20834310 32768");
	for (int i = 0; i < count; i++) {
		rk_enter_sleep();
		sleep(5);
		++test_total_success_count;
		++success_count;
		LOG_ERROR("[SLEEP TEST] loop %d test success\n", i);
	}
	memset(msg, 0, sizeof(msg));
	len = snprintf(msg, sizeof(msg), "[SLEEP TEST] success count %d, failed count %d\n"
		, success_count, fail_count);
	write(fd, msg, len);
	fsync(fd);
}

static void* test_func(void *arg) {
	int ret = 0, fd = -1;
	char file_name[256] = {'\0'};
	char thread_name[16] = {'\0'};
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);
	snprintf(thread_name, sizeof(thread_name), "dv_test_%d", gettid());
	prctl(PR_SET_NAME, thread_name, 0, 0, 0);
	LOG_INFO("enter %s, test_type: %d\n", __func__, test_type);
	if (test_type == TEST_TYPE_RESTART) {
		test_total_count = rk_param_get_int("auto_test:restart_test_loop_count", 30);
		snprintf(file_name, sizeof(file_name)
			, "/userdata/test_result_restart_%d%02d%02d%02d%02d%02d.txt"
			, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	} else if (test_type == TEST_TYPE_MODE_SWITCH) {
		test_total_count = rk_param_get_int("auto_test:mode_test_loop_count", 30);
		snprintf(file_name, sizeof(file_name)
			, "/userdata/test_result_mode_%d%02d%02d%02d%02d%02d.txt"
			, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	} else if (test_type == TEST_TYPE_VIDEO) {
		test_total_count = rk_param_get_int("auto_test:video_test_loop_count", 30);
		snprintf(file_name, sizeof(file_name)
			, "/userdata/test_result_video_%d%02d%02d%02d%02d%02d.txt"
			, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	} else if (test_type == TEST_TYPE_PHOTO) {
		test_total_count = rk_param_get_int("auto_test:photo_test_loop_count", 30);
		snprintf(file_name, sizeof(file_name)
			, "/userdata/test_result_photo_%d%02d%02d%02d%02d%02d.txt"
			, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	} else if (test_type == TEST_TYPE_PLAYER) {
		test_total_count = rk_param_get_int("auto_test:player_test_loop_count", 30);
		snprintf(file_name, sizeof(file_name)
			, "/userdata/test_result_player_%d%02d%02d%02d%02d%02d.txt"
			, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	} else if (test_type == TEST_TYPE_SLEEP) {
		test_total_count = rk_param_get_int("auto_test:sleep_test_loop_count", 30);
		snprintf(file_name, sizeof(file_name)
			, "/userdata/test_result_sleep_%d%02d%02d%02d%02d%02d.txt"
			, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	} else if (test_type == TEST_TYPE_ALL) {
		test_total_count = 0;
		test_total_count += rk_param_get_int("auto_test:restart_test_loop_count", 30);
		test_total_count += rk_param_get_int("auto_test:mode_test_loop_count", 30);
		test_total_count += rk_param_get_int("auto_test:video_test_loop_count", 30);
		test_total_count += rk_param_get_int("auto_test:photo_test_loop_count", 30);
		snprintf(file_name, sizeof(file_name)
			, "/userdata/test_result_all_%d%02d%02d%02d%02d%02d.txt"
			, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	}
	fd = open(file_name, O_RDWR | O_CREAT, 0666);
	if (fd < 0) {
		LOG_ERROR("open %s failed\n", file_name);
		return NULL;
	}
	if (test_type == TEST_TYPE_RESTART) {
		restart_test_routine(fd);
	} else if (test_type == TEST_TYPE_MODE_SWITCH) {
		mode_switch_test_routine(fd);
	} else if (test_type == TEST_TYPE_VIDEO) {
		video_test_routine(fd);
	} else if (test_type == TEST_TYPE_PHOTO) {
		photo_test_routine(fd);
	} else if (test_type == TEST_TYPE_PLAYER) {
		player_test_routine(fd);
	} else if (test_type == TEST_TYPE_SLEEP) {
		sleep_test_routine(fd);
	} else if (test_type == TEST_TYPE_ALL) {
		restart_test_routine(fd);
		mode_switch_test_routine(fd);
		video_test_routine(fd);
		photo_test_routine(fd);
	}
	memset(test_result_msg, 0, sizeof(test_result_msg));
	if (test_type == TEST_TYPE_RESTART) {
		snprintf(test_result_msg, sizeof(test_result_msg), "反初始化测试完成, SUCCESS: %d, FAIL: %d",
		         test_total_success_count, test_total_fail_count);
	} else if (test_type == TEST_TYPE_MODE_SWITCH) {
		snprintf(test_result_msg, sizeof(test_result_msg), "模式切换测试完成, SUCCESS: %d, FAIL: %d",
		         test_total_success_count, test_total_fail_count);
	} else if (test_type == TEST_TYPE_VIDEO) {
		snprintf(test_result_msg, sizeof(test_result_msg), "视频录制测试完成, SUCCESS: %d, FAIL: %d",
		         test_total_success_count, test_total_fail_count);
	} else if (test_type == TEST_TYPE_PHOTO) {
		snprintf(test_result_msg, sizeof(test_result_msg), "拍照测试完成, SUCCESS: %d, FAIL: %d",
		         test_total_success_count, test_total_fail_count);
	} else if (test_type == TEST_TYPE_PLAYER) {
		snprintf(test_result_msg, sizeof(test_result_msg), "播放测试完成, SUCCESS: %d, FAIL: %d",
		         test_total_success_count, test_total_fail_count);
	} else if (test_type == TEST_TYPE_SLEEP) {
		snprintf(test_result_msg, sizeof(test_result_msg), "休眠唤醒测试完成, SUCCESS: %d, FAIL: %d",
		         test_total_success_count, test_total_fail_count);
	} else if (test_type == TEST_TYPE_ALL) {
		snprintf(test_result_msg, sizeof(test_result_msg), "全流程测试完成, SUCCESS: %d, FAIL: %d",
		         test_total_success_count, test_total_fail_count);
	}
	close(fd);
	LOG_INFO("test thread %s exit\n", thread_name);
	return NULL;
}

static void event_handler(lv_event_t *e) {
	lv_event_code_t code = lv_event_get_code(e);
	lv_obj_t *obj = lv_event_get_target(e);
	if (code == LV_EVENT_CLICKED) {
		if (obj == auto_test_ctrl.restart_test_obj) {
			create_test_dialog("反初始化测试");
			test_timer = lv_timer_create(test_timer_func, 500, NULL);
			test_type = TEST_TYPE_RESTART;
			pthread_create(&test_task_id, NULL, test_func, NULL);
		} else if (obj == auto_test_ctrl.mode_test_obj) {
			create_test_dialog("模式切换测试");
			test_timer = lv_timer_create(test_timer_func, 500, NULL);
			test_type = TEST_TYPE_MODE_SWITCH;
			pthread_create(&test_task_id, NULL, test_func, NULL);
		} else if (obj == auto_test_ctrl.video_test_obj) {
			create_test_dialog("视频测试");
			test_timer = lv_timer_create(test_timer_func, 500, NULL);
			test_type = TEST_TYPE_VIDEO;
			pthread_create(&test_task_id, NULL, test_func, NULL);
		} else if (obj == auto_test_ctrl.photo_test_obj) {
			create_test_dialog("拍照测试");
			test_timer = lv_timer_create(test_timer_func, 500, NULL);
			test_type = TEST_TYPE_PHOTO;
			pthread_create(&test_task_id, NULL, test_func, NULL);
		} else if (obj == auto_test_ctrl.player_test_obj) {
			create_test_dialog("播放测试");
			test_timer = lv_timer_create(test_timer_func, 500, NULL);
			test_type = TEST_TYPE_PLAYER;
			pthread_create(&test_task_id, NULL, test_func, NULL);
		} else if (obj == auto_test_ctrl.sleep_test_obj) {
			create_test_dialog("休眠唤醒测试");
			test_timer = lv_timer_create(test_timer_func, 500, NULL);
			test_type = TEST_TYPE_SLEEP;
			pthread_create(&test_task_id, NULL, test_func, NULL);
		} else if (obj == auto_test_ctrl.all_test_obj) {
			create_test_dialog("全流程测试");
			test_timer = lv_timer_create(test_timer_func, 500, NULL);
			test_type = TEST_TYPE_ALL;
			pthread_create(&test_task_id, NULL, test_func, NULL);
		} else if (obj == auto_test_ctrl.exit_obj) {
			ui_page_pop_page();
		} else {
			LOG_ERROR("unknown event\n");
		}
	}
}

static void auto_test_layout(lv_obj_t *page_obj) {
	lv_obj_set_flex_flow(auto_test_ctrl.container_obj, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(auto_test_ctrl.container_obj, LV_FLEX_ALIGN_START,
	                      LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_scrollbar_mode(auto_test_ctrl.container_obj, LV_SCROLLBAR_MODE_OFF);
}

static void auto_test_create_ctrl(lv_obj_t *page_obj) {
	lv_color_t bg_color = lv_color_hex(0xFF04171D);
	lv_obj_t *cont_obj = NULL, *obj = NULL;

	lv_disp_set_bg_opa(NULL, LV_OPA_TRANSP);
	lv_obj_set_style_bg_opa(page_obj, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(page_obj, bg_color, 0);

	auto_test_ctrl.exit_obj = lv_btn_create(page_obj);
	lv_obj_set_style_bg_opa(auto_test_ctrl.exit_obj, LV_OPA_TRANSP, 0);
	lv_obj_add_event_cb(auto_test_ctrl.exit_obj, return_event_handler, LV_EVENT_CLICKED, NULL);
	auto_test_ctrl.exit_label_obj = lv_label_create(auto_test_ctrl.exit_obj);

	auto_test_ctrl.container_obj = cont_obj = lv_obj_create(page_obj);
	lv_obj_set_size(cont_obj, lv_pct(100), lv_pct(100));
	lv_obj_set_style_bg_opa(cont_obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_opa(cont_obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_radius(cont_obj, 0, 0);

	auto_test_ctrl.restart_test_obj = obj = lv_obj_create(cont_obj);
	lv_obj_set_size(obj, lv_pct(100), lv_pct(10));
	lv_obj_add_event_cb(obj, event_handler, LV_EVENT_CLICKED, NULL);
	auto_test_ctrl.restart_test_label_obj = lv_label_create(obj);

	auto_test_ctrl.mode_test_obj = obj = lv_obj_create(cont_obj);
	lv_obj_set_size(obj, lv_pct(100), lv_pct(10));
	lv_obj_add_event_cb(obj, event_handler, LV_EVENT_CLICKED, NULL);
	auto_test_ctrl.mode_test_label_obj = lv_label_create(obj);

	auto_test_ctrl.video_test_obj = obj = lv_obj_create(cont_obj);
	lv_obj_set_size(obj, lv_pct(100), lv_pct(10));
	lv_obj_add_event_cb(obj, event_handler, LV_EVENT_CLICKED, NULL);
	auto_test_ctrl.video_test_label_obj = lv_label_create(obj);

	auto_test_ctrl.photo_test_obj = obj = lv_obj_create(cont_obj);
	lv_obj_set_size(obj, lv_pct(100), lv_pct(10));
	lv_obj_add_event_cb(obj, event_handler, LV_EVENT_CLICKED, NULL);
	auto_test_ctrl.photo_test_label_obj = lv_label_create(obj);

	auto_test_ctrl.player_test_obj = obj = lv_obj_create(cont_obj);
	lv_obj_set_size(obj, lv_pct(100), lv_pct(10));
	lv_obj_add_event_cb(obj, event_handler, LV_EVENT_CLICKED, NULL);
	auto_test_ctrl.player_test_label_obj = lv_label_create(obj);

	auto_test_ctrl.sleep_test_obj = obj = lv_obj_create(cont_obj);
	lv_obj_set_size(obj, lv_pct(100), lv_pct(10));
	lv_obj_add_event_cb(obj, event_handler, LV_EVENT_CLICKED, NULL);
	auto_test_ctrl.sleep_test_label_obj = lv_label_create(obj);

	auto_test_ctrl.all_test_obj = obj = lv_obj_create(cont_obj);
	lv_obj_set_size(obj, lv_pct(100), lv_pct(10));
	lv_obj_add_event_cb(obj, event_handler, LV_EVENT_CLICKED, NULL);
	auto_test_ctrl.all_test_label_obj = lv_label_create(obj);
}

static void auto_test_set_text() {
	static lv_style_t symbol_style, symbol_focus_style, option_style, option_focus_style;
	// INFO: make sure to get the right heigh of obj
	lv_obj_update_layout(auto_test_ctrl.restart_test_obj);
	int font_height = lv_font_get_line_height(ttf_info_32.font);
	int option_height = lv_obj_get_height(auto_test_ctrl.restart_test_obj);
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

	lv_obj_remove_style_all(auto_test_ctrl.exit_obj);
	lv_label_set_text(auto_test_ctrl.exit_label_obj, LV_SYMBOL_LEFT);
	lv_obj_add_style(auto_test_ctrl.exit_obj, &symbol_style, 0);
	lv_obj_add_style(auto_test_ctrl.exit_obj, &symbol_focus_style, LV_STATE_FOCUSED);

	// INFO: set align to the left of the parent after set text
	lv_obj_align_to(auto_test_ctrl.container_obj
		, auto_test_ctrl.exit_obj, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);

	lv_obj_add_style(auto_test_ctrl.restart_test_obj, &option_style, 0);
	lv_obj_add_style(auto_test_ctrl.restart_test_obj, &option_focus_style,
	                 LV_PART_MAIN | LV_STATE_FOCUSED);
	lv_label_set_text(auto_test_ctrl.restart_test_label_obj, "反初始化测试");

	lv_obj_add_style(auto_test_ctrl.mode_test_obj, &option_style, 0);
	lv_obj_add_style(auto_test_ctrl.mode_test_obj, &option_focus_style,
	                 LV_PART_MAIN | LV_STATE_FOCUSED);
	lv_label_set_text(auto_test_ctrl.mode_test_label_obj, "模式切换测试");

	lv_obj_add_style(auto_test_ctrl.video_test_obj, &option_style, 0);
	lv_obj_add_style(auto_test_ctrl.video_test_obj, &option_focus_style,
	                 LV_PART_MAIN | LV_STATE_FOCUSED);
	lv_label_set_text(auto_test_ctrl.video_test_label_obj, "视频测试");

	lv_obj_add_style(auto_test_ctrl.photo_test_obj, &option_style, 0);
	lv_obj_add_style(auto_test_ctrl.photo_test_obj, &option_focus_style,
	                 LV_PART_MAIN | LV_STATE_FOCUSED);
	lv_label_set_text(auto_test_ctrl.photo_test_label_obj, "拍照测试");

	lv_obj_add_style(auto_test_ctrl.player_test_obj, &option_style, 0);
	lv_obj_add_style(auto_test_ctrl.player_test_obj, &option_focus_style,
	                 LV_PART_MAIN | LV_STATE_FOCUSED);
	lv_label_set_text(auto_test_ctrl.player_test_label_obj, "播放测试");

	lv_obj_add_style(auto_test_ctrl.sleep_test_obj, &option_style, 0);
	lv_obj_add_style(auto_test_ctrl.sleep_test_obj, &option_focus_style,
	                 LV_PART_MAIN | LV_STATE_FOCUSED);
	lv_label_set_text(auto_test_ctrl.sleep_test_label_obj, "休眠唤醒测试");

	lv_obj_add_style(auto_test_ctrl.all_test_obj, &option_style, 0);
	lv_obj_add_style(auto_test_ctrl.all_test_obj, &option_focus_style,
	                 LV_PART_MAIN | LV_STATE_FOCUSED);
	lv_label_set_text(auto_test_ctrl.all_test_label_obj, "全部测试");
}

static void auto_test_destroy_ctrl(void) {
	if (NULL != auto_test_ctrl.all_test_obj) {
		ui_common_remove_style_all(auto_test_ctrl.all_test_obj);
		lv_obj_del(auto_test_ctrl.all_test_obj);
		auto_test_ctrl.all_test_obj = NULL;
	}
	if (NULL != auto_test_ctrl.sleep_test_obj) {
		ui_common_remove_style_all(auto_test_ctrl.sleep_test_obj);
		lv_obj_del(auto_test_ctrl.sleep_test_obj);
		auto_test_ctrl.sleep_test_obj = NULL;
	}
	if (NULL != auto_test_ctrl.player_test_obj) {
		ui_common_remove_style_all(auto_test_ctrl.player_test_obj);
		lv_obj_del(auto_test_ctrl.player_test_obj);
		auto_test_ctrl.player_test_obj = NULL;
	}
	if (NULL != auto_test_ctrl.photo_test_obj) {
		ui_common_remove_style_all(auto_test_ctrl.photo_test_obj);
		lv_obj_del(auto_test_ctrl.photo_test_obj);
		auto_test_ctrl.photo_test_obj = NULL;
	}
	if (NULL != auto_test_ctrl.video_test_obj) {
		ui_common_remove_style_all(auto_test_ctrl.video_test_obj);
		lv_obj_del(auto_test_ctrl.video_test_obj);
		auto_test_ctrl.video_test_obj = NULL;
	}
	if (NULL != auto_test_ctrl.restart_test_obj) {
		ui_common_remove_style_all(auto_test_ctrl.restart_test_obj);
		lv_obj_del(auto_test_ctrl.restart_test_obj);
		auto_test_ctrl.restart_test_obj = NULL;
	}
	if (NULL != auto_test_ctrl.mode_test_obj) {
		ui_common_remove_style_all(auto_test_ctrl.mode_test_obj);
		lv_obj_del(auto_test_ctrl.mode_test_obj);
		auto_test_ctrl.mode_test_obj = NULL;
	}
	if (NULL != auto_test_ctrl.exit_obj) {
		ui_common_remove_style_all(auto_test_ctrl.exit_obj);
		lv_obj_del(auto_test_ctrl.exit_obj);
		auto_test_ctrl.exit_obj = NULL;
	}
	if (NULL != auto_test_ctrl.container_obj) {
		ui_common_remove_style_all(auto_test_ctrl.container_obj);
		lv_obj_del(auto_test_ctrl.container_obj);
		auto_test_ctrl.container_obj = NULL;
	}
}

static void auto_test_page_create(lv_obj_t *page_obj) {

	auto_test_create_ctrl(page_obj);
	auto_test_layout(page_obj);
	auto_test_set_text();
}

static void auto_test_add_indev(void) {
	lv_group_t *group = lv_port_indev_group_create();
	if (NULL == group)
		return;
	lv_group_add_obj(group, auto_test_ctrl.exit_obj);
	lv_group_add_obj(group, auto_test_ctrl.restart_test_obj);
	lv_group_add_obj(group, auto_test_ctrl.mode_test_obj);
	lv_group_add_obj(group, auto_test_ctrl.video_test_obj);
	lv_group_add_obj(group, auto_test_ctrl.photo_test_obj);
	lv_group_add_obj(group, auto_test_ctrl.player_test_obj);
	lv_group_add_obj(group, auto_test_ctrl.sleep_test_obj);
	lv_group_add_obj(group, auto_test_ctrl.all_test_obj);
	auto_test_ctrl.group = group;
}

static void auto_test_delete_indev(void) {
	if (NULL != auto_test_ctrl.group) {
		lv_port_indev_group_destroy(auto_test_ctrl.group);
		auto_test_ctrl.group = NULL;
	}
}

static void auto_test_page_enter(lv_obj_t *page_obj) { auto_test_add_indev(); }

static void auto_test_page_exit(lv_obj_t *page_obj) {}

static void auto_test_page_destroy(lv_obj_t *page_obj) {
	auto_test_delete_indev();
	auto_test_destroy_ctrl();
}

static UI_PAGE_HANDLER_T auto_test_page = {.name = "auto_test",
                                                .init = NULL,
                                                .create = auto_test_page_create,
                                                .enter = auto_test_page_enter,
                                                .destroy = auto_test_page_destroy,
                                                .exit = auto_test_page_exit};

UI_PAGE_REGISTER(auto_test_page)
