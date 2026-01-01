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

#ifndef _UI_FORMAT_DIALOG_H_
#define _UI_FORMAT_DIALOG_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*FORMAT_DIALOG_CB)(void);

void ui_format_dialog_create(void);
void ui_format_dialog_destory(void);

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif
