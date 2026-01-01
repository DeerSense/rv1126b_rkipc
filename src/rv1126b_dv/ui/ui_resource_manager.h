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

#ifndef _UI_RES_MANAGE_H_
#define _UI_RES_MANAGE_H_

#include "lvgl.h"
#include "lvgl/common/lv_img_dec.h"

#ifdef __cplusplus
extern "C" {
#endif

extern lv_img_dsc_t *icon_record_stop;
extern lv_img_dsc_t *icon_record_start;
extern lv_img_dsc_t *icon_photo;
extern lv_img_dsc_t *icon_setting;
extern lv_img_dsc_t *icon_media;
extern lv_img_dsc_t *icon_record_stat_run;
extern lv_img_dsc_t *icon_record_stat_idle;

extern lv_img_dsc_t *icon_video_mode;
extern lv_img_dsc_t *icon_photo_mode;
extern lv_img_dsc_t *icon_slowmotion_mode;
extern lv_img_dsc_t *icon_storage;
extern lv_img_dsc_t *icon_format;
extern lv_img_dsc_t *icon_advanced_setup;
extern lv_img_dsc_t *icon_video_setup;
extern lv_img_dsc_t *icon_audio_setup;

extern lv_img_dsc_t *icon_media_photo;
extern lv_img_dsc_t *icon_media_film;

void ui_resource_release(void);
void ui_resource_load(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif
