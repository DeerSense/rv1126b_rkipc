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

#include "lvgl/common/lv_msg.h"
#include "lvgl/porting/lv_port_disp.h"
#include "lvgl/porting/lv_port_file.h"
#include "lvgl/porting/lv_port_indev.h"

#include "ui_common.h"
#include "ui_ctrl.h"
#include "ui_page_manager.h"
#include "ui_resource_manager.h"

#include <pthread.h>
#include <unistd.h>

#define LVGL_TICK 5

void ui_loop(void) {
	ui_ctl_proc();
	lv_task_handler();
	usleep(LVGL_TICK * 1000); /*Sleep for 5 millisecond*/
}

int32_t ui_init(void) {

	/*Initialize LittlevGL*/
	lv_init();
	lv_port_disp_init(LV_DISP_ROT_90);
	lv_port_indev_init(LV_DISP_ROT_90);
	lv_port_fs_init();

	ui_resource_load();

	lv_msg_init();

	/*Load preview interface*/
	ui_page_init(true);
	ui_page_push_page("main", NULL);

	return 0;
}

void ui_deinit(void) {
	ui_page_deinit();

	ui_resource_release();

	lv_msg_deinit();

	// lv_port_disp_deinit();

#if LV_ENABLE_GC || !LV_MEM_CUSTOM
	lv_deinit();
#endif
}