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

#include "ui_ctrl.h"
#include "ui_common.h"
#include "ui_page_manager.h"

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
static volatile UI_CTRL_E ui_ctl_cmd = UI_CTRL_BUTT;

/**********************
 *  GLOBAL VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void ui_ctl_proc(void) {
	switch (ui_ctl_cmd) {
	case UI_CTRL_SWITCH_MAIN_PAGE:
		ui_page_switch_page("main", NULL);
		ui_ctl_cmd = UI_CTRL_BUTT;
		break;

	case UI_CTRL_SWITCH_PLAYLIST_PAGE:
		ui_page_switch_page("playlist", NULL);
		ui_ctl_cmd = UI_CTRL_BUTT;
		break;

	case UI_CTRL_SWITCH_SETTINGS_PAGE:
		ui_page_switch_page("settings", NULL);
		ui_ctl_cmd = UI_CTRL_BUTT;
		break;

	default:
		break;
	}
}

void ui_ctl(UI_CTRL_E cmd) { ui_ctl_cmd = cmd; }
/**********************
 *   STATIC FUNCTIONS
 **********************/