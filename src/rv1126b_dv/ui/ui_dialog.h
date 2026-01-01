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

#ifndef _UI_DIALOG_H_
#define _UI_DIALOG_H_

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

lv_obj_t *ui_dialog_create(const char *head_title, const char *content);
void ui_dialog_destroy(void);
typedef void (*UI_TASK_TYPE)(void *);
int ui_start_async_task(UI_TASK_TYPE func, void *func_arg, UI_TASK_TYPE dtor, void *dtor_arg);
bool ui_async_task_is_running(void);

typedef void (*BTN_CB_TYPE)(void *);

typedef struct ui_option_entry {
	const char *name;
	BTN_CB_TYPE cb;
	void *userdata;
} ui_option_entry_t;

void create_select_box(const char *title, const ui_option_entry_t *option_array, int option_num, int focus_idx);

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif