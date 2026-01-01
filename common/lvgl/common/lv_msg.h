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

#ifndef _LV_COMMON_MSG_H_
#define _LV_COMMON_MSG_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*msg_handler_t)(int32_t msg, void *wparam, void *lparam);

int32_t lv_msg_init(void);
int32_t lv_msg_deinit(void);
void *lv_msg_register_handler(msg_handler_t handler);
void lv_msg_unregister_handler(void *obj);
int32_t lv_send_msg(void *obj, int32_t msg, void *wparam, void *lparam);
int32_t lv_post_msg(void *obj, int32_t msg, void *wparam, void *lparam);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif //_LV_MSG_H_
