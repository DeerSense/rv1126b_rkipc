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

#ifndef _UI_GETTEXT_H_
#define _UI_GETTEST_H_

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { LANG_TYPE_CHINESE = 0, LANG_TYPE_ENGLISH, LANG_TYPE_MAX } LANG_TYPE_E;

const char *__string_by_index(int32_t index);
int32_t ui_language_load(void);
void ui_language_release(void);
int32_t ui_language_select(LANG_TYPE_E lang_type);

#define _T(index) __string_by_index(index)

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif