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

#include <limits.h>

#include "ui_resource_manager.h"

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/
#define ICON_RELEASE(icon_index)                                                                   \
	do {                                                                                           \
		if (NULL != icon_index) {                                                                  \
			lv_free_img(&icon_index);                                                              \
			icon_index = NULL;                                                                     \
		}                                                                                          \
	} while (0)

lv_ft_info_t ttf_info_32;
lv_img_dsc_t *icon_record_stop;
lv_img_dsc_t *icon_record_start;
lv_img_dsc_t *icon_photo;
lv_img_dsc_t *icon_setting;
lv_img_dsc_t *icon_media;
lv_img_dsc_t *icon_record_stat_run;
lv_img_dsc_t *icon_record_stat_idle;

lv_img_dsc_t *icon_video_mode;
lv_img_dsc_t *icon_photo_mode;
lv_img_dsc_t *icon_slowmotion_mode;
lv_img_dsc_t *icon_storage;
lv_img_dsc_t *icon_format;
lv_img_dsc_t *icon_advanced_setup;
lv_img_dsc_t *icon_video_setup;
lv_img_dsc_t *icon_audio_setup;

lv_img_dsc_t *icon_media_photo;
lv_img_dsc_t *icon_media_film;

static lv_img_dsc_t *ui_dec_icon(const char *const icon_name) {
	if (NULL == icon_name) {
		LV_LOG_ERROR("The icon named pointer is NULL.");
		return NULL;
	}

	char icon_path[PATH_MAX] = {0};

#ifdef BUILDROOT
	snprintf(icon_path, sizeof(icon_path), "%s%s", "/usr/share/res/ui/", icon_name);
#else
	snprintf(icon_path, sizeof(icon_path), "%s%s", "/oem/usr/share/res/ui/", icon_name);
#endif
	return lv_dec_img(icon_path);
}

static int32_t ui_font_init(lv_ft_info_t *info, uint32_t weight) {
	if (NULL == info || 0 == weight) {
		LV_LOG_ERROR("Invalid font configuration parameter.");
		return -1;
	}

#ifdef BUILDROOT
	info->name = "/usr/share/PuHuiTi.ttf";
#else
	info->name = "/oem/usr/share/PuHuiTi.ttf";
#endif
	info->weight = weight;
	info->style = FT_FONT_STYLE_NORMAL;

	if (!lv_ft_font_init(info)) {
		LV_LOG_ERROR("Failed to create font.");
		return -1;
	}

	return 0;
}


static void ui_font_release(void) {
	if (NULL != ttf_info_32.font) {
		lv_ft_font_destroy(ttf_info_32.font);
		ttf_info_32.font = NULL;
	}
}

static int32_t ui_font_load(void) {
	/*Create a font*/
	if (0 != ui_font_init(&ttf_info_32, 32))
		LV_LOG_ERROR("Content font initialization failed.");

	return 0;
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void ui_resource_release(void) {
	ICON_RELEASE(icon_record_stop);
	ICON_RELEASE(icon_record_start);
	ICON_RELEASE(icon_photo);
	ICON_RELEASE(icon_setting);
	ICON_RELEASE(icon_media);
	ICON_RELEASE(icon_record_stat_run);
	ICON_RELEASE(icon_record_stat_idle);
	ICON_RELEASE(icon_slowmotion_mode);
	ICON_RELEASE(icon_video_mode);
	ICON_RELEASE(icon_photo_mode);
	ICON_RELEASE(icon_storage);
	ICON_RELEASE(icon_format);
	ICON_RELEASE(icon_advanced_setup);
	ICON_RELEASE(icon_video_setup);
	ICON_RELEASE(icon_audio_setup);
	ICON_RELEASE(icon_media_photo);
	ICON_RELEASE(icon_media_film);
	ui_font_release();
}

void ui_resource_load(void) {
	ui_font_load();
	icon_record_stop = ui_dec_icon("icon_record_stop.png");
	icon_record_start = ui_dec_icon("icon_record_start.png");
	icon_photo = ui_dec_icon("icon_photo.png");
	icon_setting = ui_dec_icon("icon_setting.png");
	icon_media = ui_dec_icon("icon_media.png");
	icon_record_stat_run = ui_dec_icon("icon_record_stat_run.png");
	icon_record_stat_idle = ui_dec_icon("icon_record_stat_idle.png");
	icon_slowmotion_mode = ui_dec_icon("icon_slowmotion_mode.png");
	icon_video_mode = ui_dec_icon("icon_video_mode.png");
	icon_photo_mode = ui_dec_icon("icon_photo_mode.png");
	icon_storage = ui_dec_icon("icon_storage.png");
	icon_format = ui_dec_icon("icon_format.png");
	icon_advanced_setup = ui_dec_icon("icon_advanced_setup.png");
	icon_video_setup = ui_dec_icon("icon_video_setup.png");
	icon_audio_setup = ui_dec_icon("icon_audio_setup.png");
	icon_media_photo = ui_dec_icon("icon_media_photo.png");
	icon_media_film = ui_dec_icon("icon_media_film.png");
}
