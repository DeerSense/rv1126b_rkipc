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

#ifndef _UI_PAGE_MANAGER_H_
#define _UI_PAGE_MANAGER_H_

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ui_page_handler_t {
	const char *name;
	void (*init)(lv_obj_t *page_obj);
	void (*create)(lv_obj_t *page_obj);
	void (*enter)(lv_obj_t *page_obj);
	void (*exit)(lv_obj_t *page_obj);
	void (*destroy)(lv_obj_t *page_obj);

	/* anim args */
	lv_anim_path_cb_t path_cb_in;
	lv_anim_path_cb_t path_cb_out;
	lv_anim_exec_xcb_t exec_cb;
} UI_PAGE_HANDLER_T;

void ui_page_register(UI_PAGE_HANDLER_T *page);

int32_t ui_page_init(bool small_mem);
void ui_page_deinit(void);
int32_t ui_page_pop_page(void);
int32_t ui_page_push_page(const char *name, void *user_data);
int32_t ui_page_switch_page(const char *name, void *user_data);

#define UI_PAGE_REGISTER(_page)                                                                    \
	static __attribute__((constructor)) void page_##_page##_register_() {                          \
		ui_page_register(&_page);                                                                  \
	}

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif