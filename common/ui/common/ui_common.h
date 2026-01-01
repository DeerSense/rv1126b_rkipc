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

#ifndef _UI_COMMON_H_
#define _UI_COMMON_H_

#include "lvgl/porting/lv_port_indev.h"
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void ui_common_remove_style_all(lv_obj_t *obj);
int32_t ui_common_set_screen_brightness(uint32_t brightness);

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif
