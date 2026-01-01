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

#include "ui_common.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *  GLOBAL VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void ui_common_remove_style_all(lv_obj_t *obj) {
	if (obj == NULL)
		return;
	lv_obj_t *child = NULL;
	uint32_t child_cnt = lv_obj_get_child_cnt(obj);
	for (uint32_t i = 0; i < child_cnt; i++) {
		child = lv_obj_get_child(obj, i);
		if (NULL == child)
			break;
		ui_common_remove_style_all(child);
	}
	lv_obj_remove_style_all(obj);
}

int32_t ui_common_set_screen_brightness(uint32_t brightness) {
	int32_t fd = open("/sys/class/backlight/backlight/brightness", O_WRONLY);
	if (-1 == fd)
		return -1;

	char brightness_value[4];
	snprintf(brightness_value, sizeof(brightness_value), "%d", brightness);

	ssize_t size = write(fd, brightness_value, sizeof(brightness_value) - 1);
	if (-1 == size) {
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/