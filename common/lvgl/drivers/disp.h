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

#ifndef LV_DRV_DISP_H
#define LV_DRV_DISP_H

#include <limits.h>
#include <lvgl.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	int32_t (*init)(lv_disp_rot_t rotate_disp);
	void (*exit)(void);
	void (*get_sizes)(lv_coord_t *width, lv_coord_t *height, uint32_t *dpi);
	void (*flush)(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p);
} disp_ops_t;

int32_t disp_init(const char *dev_name, lv_disp_rot_t rotate_disp);
void disp_exit(void);
void disp_get_sizes(lv_coord_t *width, lv_coord_t *height, uint32_t *dpi);
void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif