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

#include "disp.h"

/**********************
 *  STATIC PROTOTYPES
 **********************/
typedef struct {
	char suffix[32];
	disp_ops_t *ops;
} disp_ops_list;

/**********************
 *  GLOBAL VARIABLES
 **********************/
#if defined(DRAW_UI_BY_VO)
extern disp_ops_t rk_ui_ops;
#else
extern disp_ops_t drm_ops;
#endif
/**********************
 *  STATIC VARIABLES
 **********************/
static disp_ops_list ops_list[] = {
#if defined(DRAW_UI_BY_VO)
    {"RK_UI", &rk_ui_ops},
#else
	{"DRM", &drm_ops},
#endif
};

static disp_ops_t *cur_ops = NULL;

/**********************
 *      MACROS
 **********************/

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/*Initialize your display and the required peripherals.*/
int32_t disp_init(const char *dev_name, lv_disp_rot_t rotate_disp) {
	if (NULL != dev_name && 0 < strlen(dev_name)) {
		uint32_t i = 0;
		uint32_t ops_num = sizeof(ops_list) / sizeof(ops_list[0]);
		for (i = 0; i < ops_num; i++) {
			if (0 == strcmp(dev_name, ops_list[i].suffix)) {
				cur_ops = ops_list[i].ops;
				break;
			}
		}

		if (i == ops_num)
			cur_ops = ops_list[0].ops; /* default first */
	} else {
		cur_ops = ops_list[0].ops; /* default first */
	}

	if (NULL != cur_ops && NULL != cur_ops->init)
		return cur_ops->init(rotate_disp);

	return -1;
}

void disp_exit(void) {
	if (NULL != cur_ops && NULL != cur_ops->exit)
		cur_ops->exit();
}

void disp_get_sizes(lv_coord_t *width, lv_coord_t *height, uint32_t *dpi) {
	if (NULL != cur_ops && NULL != cur_ops->get_sizes)
		cur_ops->get_sizes(width, height, dpi);
}

/*Flush the content of the internal buffer the specific area on the display
 *You can use DMA or any hardware acceleration to do this operation in the background but
 *'lv_disp_flush_ready()' has to be called when finished.*/
void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
	/*The most simple case (but also the slowest) to put all pixels to the screen one-by-one*/
	if (NULL != cur_ops && NULL != cur_ops->flush)
		cur_ops->flush(drv, area, color_p);
}
