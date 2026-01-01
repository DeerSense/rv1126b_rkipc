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
#include "rk_mpi_mmz.h"

#include "lv_port_disp.h"
#include "lvgl/drivers/disp.h"

#include <stdint.h>
#include <stdlib.h>

static void *pMblk;
static lv_disp_t *disp_dev;

void lv_port_disp_init(lv_disp_rot_t rotate_disp) {
	lv_coord_t lcd_w, lcd_h;
	/*-------------------------
	 * Initialize your display
	 * -----------------------*/
#if defined(DRAW_UI_BY_VO)
	disp_init("RK_UI", rotate_disp);
#else
	disp_init(NULL, rotate_disp);
#endif
	disp_get_sizes(&lcd_w, &lcd_h, NULL);
	/*-----------------------------
	 * Create a buffer for drawing
	 *----------------------------*/

	/**
	 * LVGL requires a buffer where it internally draws the widgets.
	 * Later this buffer will passed to your display driver's `flush_cb` to copy its content to your
	 * display. The buffer has to be greater than 1 display row
	 *
	 * There are 3 buffering configurations:
	 * 1. Create ONE buffer:
	 *      LVGL will draw the display's content here and writes it to your display
	 *
	 * 2. Create TWO buffer:
	 *      LVGL will draw the display's content to a buffer and writes it your display.
	 *      You should use DMA to write the buffer's content to the display.
	 *      It will enable LVGL to draw the next part of the screen to the other buffer while
	 *      the data is being sent form the first buffer. It makes rendering and flushing parallel.
	 *
	 * 3. Double buffering
	 *      Set 2 screens sized buffers and set disp_drv.full_refresh = 1.
	 *      This way LVGL will always provide the whole rendered screen in `flush_cb`
	 *      and you only need to change the frame buffer's address.
	 */

	/* Example for 1) */
	static lv_disp_draw_buf_t draw_buf_dsc;
	lv_color_t *draw_buf = NULL;

	uint32_t size_in_px_cnt = lcd_w * lcd_h;

	if (0 !=
	    RK_MPI_MMZ_Alloc(&pMblk, (size_in_px_cnt * (LV_COLOR_SIZE / 8)), RK_MMZ_ALLOC_CACHEABLE))
		return;
	draw_buf = RK_MPI_MMZ_Handle2VirAddr(pMblk);

	lv_disp_draw_buf_init(&draw_buf_dsc, draw_buf, NULL,
	                      size_in_px_cnt); /*Initialize the display buffer*/

	/*-----------------------------------
	 * Register the display in LVGL
	 *----------------------------------*/
	static lv_disp_drv_t disp_drv; /*Descriptor of a display driver*/
	lv_disp_drv_init(&disp_drv);   /*Basic initialization*/

	/*Set up the functions to access to your display*/

	/*Set the resolution of the display*/
	if (rotate_disp == LV_DISP_ROT_NONE) {
		disp_drv.hor_res = lcd_w;
		disp_drv.ver_res = lcd_h;
		disp_drv.sw_rotate = 0;
		disp_drv.rotated = LV_DISP_ROT_NONE;
	} else if (rotate_disp == LV_DISP_ROT_180) {
		disp_drv.hor_res = lcd_w;
		disp_drv.ver_res = lcd_h;
		disp_drv.sw_rotate = 1;
		disp_drv.rotated = LV_DISP_ROT_180;
	} else if (rotate_disp == LV_DISP_ROT_90) {
		disp_drv.hor_res = lcd_h;
		disp_drv.ver_res = lcd_w;
		disp_drv.sw_rotate = 0;
		disp_drv.rotated = LV_DISP_ROT_NONE;
	} else if (rotate_disp == LV_DISP_ROT_270) {
		disp_drv.hor_res = lcd_h;
		disp_drv.ver_res = lcd_w;
		disp_drv.sw_rotate = 1;
		disp_drv.rotated = LV_DISP_ROT_180;
	}

	disp_drv.screen_transp = LV_COLOR_SCREEN_TRANSP;
	/*Used to copy the buffer's content to the display*/
	disp_drv.flush_cb = disp_flush;

	/*Set a display buffer*/
	disp_drv.draw_buf = &draw_buf_dsc;

	/*Required for Example 3)*/
	// disp_drv.full_refresh = 1;

	/* Fill a memory array with a color if you have GPU.
	 * Note that, in lv_conf.h you can enable GPUs that has built-in support in LVGL.
	 * But if you have a different GPU you can use with this callback.*/
	// disp_drv.gpu_fill_cb = gpu_fill;

	/*Finally register the driver*/
	disp_dev = lv_disp_drv_register(&disp_drv);
}

void lv_port_disp_deinit(void) {
	if (NULL != disp_dev) {
		lv_disp_remove(disp_dev);
		disp_dev = NULL;
	}

	if (NULL != pMblk) {
		RK_MPI_MMZ_Free(pMblk);
		pMblk = NULL;
	}

	disp_exit();
}
