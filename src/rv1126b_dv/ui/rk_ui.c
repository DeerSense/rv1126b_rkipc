/*
 * Copyright (c) 2023 Rockchip, Inc. All Rights Reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *	 http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "rk_ui.c"

#define DRAW_USE_CPU 0

#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <rga/im2d.h>
#include <rga/rga.h>
#include "common.h"
#include "disp.h"
#include <lvgl.h>

#include <rk_mpi_mb.h>
#include <rk_mpi_mmz.h>
#include <rk_mpi_rgn.h>
#include <rk_mpi_sys.h>
#include <rk_mpi_tde.h>
#include <rk_mpi_venc.h>
#include <rk_mpi_vi.h>
#include <rk_mpi_vo.h>
#include <rk_mpi_vpss.h>

#define DIV_ROUND_UP(n, d) (((n) + (d)-1) / (d))

#define UPALIGNED_16(x) (((x) + 15) & ~15)
#define DOWNALIGNED_16(x) ((x) & ~15)
#define UPALIGNED_64(x) (((x) + 63) & ~63)

#define VO_DEV_ID 0
#define VO_UI_LAYER 1
#define VO_UI_CHN 0
#define VO_UI_FPS 10
#define VO_UI_LAYER_PRIORITY 1

struct disp_dev {
	lv_disp_rot_t rot;
	VIDEO_FRAME_INFO_S display_bufs[2];
	VIDEO_FRAME_INFO_S *active_buf;
	VIDEO_FRAME_INFO_S *free_buf;
	uint32_t width, height;
	uint32_t mm_width, mm_height;
} disp_dev;

static int32_t rk_disp_setup(void) {
	int32_t ret;
	VO_VIDEO_LAYER_ATTR_S vo_layer_attr;
	VO_CSC_S vo_csc;
	VO_CHN_ATTR_S vo_chn_attr;
	uint32_t disp_buf_len;
	uint32_t display_width = rk_param_get_int("display:width", 240);
	uint32_t display_height = rk_param_get_int("display:height", 320);
	uint32_t display_layer = VO_UI_LAYER;
	int display_dev_id = VO_DEV_ID;
	int display_chn_id = VO_UI_CHN;
	uint32_t fps = VO_UI_FPS;
	const char *intf_type = rk_param_get_string("display:intf_type", "LCD");

	memset(&vo_layer_attr, 0, sizeof(VO_VIDEO_LAYER_ATTR_S));
	memset(&vo_csc, 0, sizeof(VO_CSC_S));
	memset(&vo_chn_attr, 0, sizeof(vo_chn_attr));

	RK_MPI_VO_GetLayerDispBufLen(display_layer, &disp_buf_len);
	LOG_INFO("Get display_layer %d disp buf len is %d.\n", display_layer, disp_buf_len);
	disp_buf_len = 2;
	ret = RK_MPI_VO_SetLayerDispBufLen(display_layer, disp_buf_len);
	LOG_INFO("Agin Get display_layer %d disp buf len is %d.\n", display_layer, disp_buf_len);

	vo_layer_attr.stDispRect.s32X = 0;
	vo_layer_attr.stDispRect.s32Y = 0;
	vo_layer_attr.stDispRect.u32Width = display_width;
	vo_layer_attr.stDispRect.u32Height = display_height;
	vo_layer_attr.stImageSize.u32Width = display_width;
	vo_layer_attr.stImageSize.u32Height = display_height;
	LOG_INFO("vo_layer_attr W=%d, H=%d\n", vo_layer_attr.stDispRect.u32Width,
			 vo_layer_attr.stDispRect.u32Height);

	// INFO: Not use RGA/GPU to composite, just send frame to VOP
	vo_layer_attr.bBypassFrame = true;
	vo_layer_attr.u32DispFrmRt = fps;
	vo_layer_attr.enPixFormat = RK_FMT_RGB888;
	vo_csc.enCscMatrix = VO_CSC_MATRIX_IDENTITY;
	vo_csc.u32Contrast = 50;
	vo_csc.u32Hue = 50;
	vo_csc.u32Luma = 50;
	vo_csc.u32Satuature = 50;

	/*bind layer0 to device hd0*/
	ret = RK_MPI_VO_BindLayer(display_layer, display_dev_id, VO_LAYER_MODE_CURSOR);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("failed to bind layer %#X\n", ret);
		return ret;
	}
	ret = RK_MPI_VO_SetLayerAttr(display_layer, &vo_layer_attr);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("failed to set layer attr\n");
		return ret;
	}
	ret = RK_MPI_VO_SetLayerSpliceMode(display_layer, VO_SPLICE_MODE_RGA);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("failed to set layer splice mode %#X\n", ret);
		return ret;
	}
	ret = RK_MPI_VO_SetLayerPriority(display_layer, VO_UI_LAYER_PRIORITY);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("failed to set layer priority %#X\n", ret);
		return ret;
	}
	ret = RK_MPI_VO_EnableLayer(display_layer);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("failed to enable layer %#X\n", ret);
		return ret;
	}
	ret = RK_MPI_VO_SetLayerCSC(display_layer, &vo_csc);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("failed to set layer csc\n");
		return ret;
	}

	vo_chn_attr.bDeflicker = RK_FALSE;
	vo_chn_attr.u32Priority = 0;
	vo_chn_attr.stRect.s32X = 0;
	vo_chn_attr.stRect.s32Y = 0;
	vo_chn_attr.stRect.u32Width = vo_layer_attr.stDispRect.u32Width;
	vo_chn_attr.stRect.u32Height = vo_layer_attr.stDispRect.u32Height;
	vo_chn_attr.enRotation = 0;
	ret = RK_MPI_VO_SetChnAttr(display_layer, display_chn_id, &vo_chn_attr);
	ret = RK_MPI_VO_EnableChn(display_layer, display_chn_id);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("create %d layer %d ch vo failed %#X!\n", display_layer, display_chn_id, ret);
		return ret;
	}
	LOG_INFO("rk_disp: ui created successfully.\n");

	return 0;
}

static void rk_disp_teardown(void) {
	int ret = 0;
	uint32_t display_layer = VO_UI_LAYER;
	int display_chn_id = VO_UI_CHN;
	int display_dev_id = VO_DEV_ID;
	ret = RK_MPI_VO_DisableChn(display_layer, display_chn_id);
	if (ret) {
		LOG_ERROR("RK_MPI_VO_DisableChn failed, ret is %#x\n", ret);
		return;
	}
	ret = RK_MPI_VO_DisableLayer(display_layer);
	if (ret) {
		LOG_ERROR("RK_MPI_VO_DisableLayer failed, ret is %#x\n", ret);
		return;
	}
	ret = RK_MPI_VO_Disable(display_dev_id);
	if (ret) {
		LOG_ERROR("RK_MPI_VO_Disable failed, ret is %#x\n", ret);
		return;
	}
	ret = RK_MPI_VO_UnBindLayer(display_layer, display_dev_id);
	if (ret) {
		LOG_ERROR("RK_MPI_VO_UnBindLayer failed, ret is %#x\n", ret);
		return;
	}
	LOG_INFO("rk_disp: ui destroyed successfully.\n");
}

static int32_t rk_disp_setup_buffers(void) {
	int32_t ret;
	uint32_t i, size;
	void *blk = NULL, *viraddr = NULL;
	uint32_t format = 0;

	if (LV_COLOR_DEPTH == 32) {
		format = RK_FMT_BGRA8888;
	} else {
		format = 0;
		LOG_ERROR("drm_flush rga not supported format\n");
		return -1;
	}

	size = disp_dev.width * disp_dev.height * (LV_COLOR_SIZE / 8);

	for (int i = 0; i < 2; ++i) {
		ret = RK_MPI_MMZ_Alloc(&blk, size, RK_MMZ_ALLOC_CACHEABLE);
		if (0 != ret) {
			LOG_ERROR("alloc buf size %d failed %#X!\n", size, ret);
			return ret;
		}
		disp_dev.display_bufs[i].stVFrame.enPixelFormat = format;
		disp_dev.display_bufs[i].stVFrame.pMbBlk = blk;
		disp_dev.display_bufs[i].stVFrame.u32Width = disp_dev.width;
		disp_dev.display_bufs[i].stVFrame.u32VirWidth = disp_dev.width;
		disp_dev.display_bufs[i].stVFrame.u32Height = disp_dev.height;
		disp_dev.display_bufs[i].stVFrame.u32VirHeight = disp_dev.height;
		viraddr = RK_MPI_MB_Handle2VirAddr(blk);
		memset(viraddr, 0x00, size);
		LOG_INFO("mb blk %p, width %d, height %d, viraddr %p\n",
				 disp_dev.display_bufs[i].stVFrame.pMbBlk,
				 disp_dev.display_bufs[i].stVFrame.u32Width,
				 disp_dev.display_bufs[i].stVFrame.u32Height,
				 viraddr);
	}
	disp_dev.active_buf = &disp_dev.display_bufs[0];
	disp_dev.free_buf = &disp_dev.display_bufs[1];
	return 0;
}

static void rk_disp_teardown_buffers(void) {
	for (int i = 0; i < 2; ++i) {
		LOG_INFO("free mb blk %p\n", disp_dev.display_bufs[i].stVFrame.pMbBlk);
		RK_MPI_MMZ_Free(disp_dev.display_bufs[i].stVFrame.pMbBlk);
		memset(&disp_dev.display_bufs[i], 0, sizeof(VIDEO_FRAME_INFO_S));
	}
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
int32_t rk_disp_init(lv_disp_rot_t rotate_disp) {
	int32_t ret;

	memset(&disp_dev, 0, sizeof(disp_dev));
	disp_dev.width = rk_param_get_int("display:width", 1080);
	disp_dev.height = rk_param_get_int("display:height", 1920);
	ret = rk_disp_setup();
	if (0 != ret) {
		LOG_ERROR("rk_disp_setup failed");
		return -1;
	}

	ret = rk_disp_setup_buffers();
	if (0 != ret) {
		LOG_ERROR("Allocating display buffer failed\n");
		goto err;
	}

	disp_dev.rot = rotate_disp;

	return 0;
err:
	rk_disp_teardown();

	return -1;
}

void rk_disp_exit(void) {
	rk_disp_teardown_buffers();
	rk_disp_teardown();
}

static void draw_buf_rotate_90(lv_color_t *color_p, const lv_area_t *area, lv_color_t *dst_buf,
							   lv_coord_t canvas_w, lv_coord_t canvas_h) {
	lv_coord_t area_w = (area->x2 - area->x1 + 1);
	lv_coord_t area_h = (area->y2 - area->y1 + 1);
	uint32_t initial_i = area->x1 * canvas_w + (canvas_w - area->y1 - 1);
	for (lv_coord_t y = 0; y < area_h; y++) {
		uint32_t i = initial_i - y;
		for (lv_coord_t x = 0; x < area_w; x++) {
			dst_buf[i] = *(color_p++);
			i += canvas_w;
		}
	}
}

static void rga_copy(VIDEO_FRAME_INFO_S *src_frame, VIDEO_FRAME_INFO_S *dst_frame) {
	int ret = 0;
	int src_width = src_frame->stVFrame.u32Width;
	int src_height = src_frame->stVFrame.u32Height;
	int src_format = RK_FORMAT_BGRA_8888;
	int dst_width = dst_frame->stVFrame.u32Width;
	int dst_height = dst_frame->stVFrame.u32Height;
	int dst_format = RK_FORMAT_BGRA_8888;
	int src_fd = RK_MPI_MB_Handle2Fd(src_frame->stVFrame.pMbBlk);
	int dst_fd = RK_MPI_MB_Handle2Fd(dst_frame->stVFrame.pMbBlk);
	rga_buffer_t src_img = {}, dst_img = {};
	rga_buffer_handle_t src_handle = 0, dst_handle = 0;
	im_handle_param_t src_handle_param = {
		.width = src_width,
		.height = src_height,
		.format = RK_FORMAT_BGRA_8888,
	};
	im_handle_param_t dst_handle_param = {
		.width = dst_width,
		.height = dst_height,
		.format = RK_FORMAT_BGRA_8888,
	};
	src_handle = importbuffer_fd(src_fd, &src_handle_param);
	dst_handle = importbuffer_fd(dst_fd, &dst_handle_param);
	if (src_handle == 0 || dst_handle == 0) {
		LOG_ERROR("importbuffer failed!\n");
		return ;
	}

	src_img = wrapbuffer_handle(src_handle, src_width, src_height, src_format);
	dst_img = wrapbuffer_handle(dst_handle, dst_width, dst_height, dst_format);

	ret = imcopy(src_img, dst_img);
	if (ret == IM_STATUS_SUCCESS) {
		LOG_DEBUG("imcopy success!\n");
	} else {
		LOG_ERROR("imcopy failed! %s\n", imStrError(ret));
	}

	if (src_handle)
		releasebuffer_handle(src_handle);
	if (dst_handle)
		releasebuffer_handle(dst_handle);
}

static void rga_copy_area(VIDEO_FRAME_INFO_S *dst_frame, const lv_area_t *area,
						lv_color_t *color_p, int rotation) {
	int ret = 0;
	int fg_width, fg_height, fg_format;
	int bg_width, bg_height, bg_format;
	int bg_fd = RK_MPI_MB_Handle2Fd(dst_frame->stVFrame.pMbBlk);
	void *fg_viraddr = color_p;
	int usage = IM_SYNC;
	im_rect bg_rect = {}, fg_rect = {}, dummy_rect = {};
	rga_buffer_t fg_img = {}, bg_img = {}, dummy_img = {};
	rga_buffer_handle_t fg_handle, bg_handle;
	im_handle_param_t fg_handle_param, bg_handle_param;

	fg_width = area->x2 - area->x1 + 1;
	fg_height = area->y2 - area->y1 + 1;
	fg_format = RK_FORMAT_BGRA_8888;

	bg_width = dst_frame->stVFrame.u32Width;
	bg_height = dst_frame->stVFrame.u32Height;
	bg_format = RK_FORMAT_BGRA_8888;

	fg_handle_param.width = fg_width;
	fg_handle_param.height = fg_height;
	fg_handle_param.format = fg_format;
	fg_handle = importbuffer_virtualaddr(fg_viraddr, &fg_handle_param);

	bg_handle_param.width = bg_width;
	bg_handle_param.height = bg_height;
	bg_handle_param.format = bg_format;
	bg_handle = importbuffer_fd(bg_fd, &bg_handle_param);
	if (fg_handle == 0 || bg_handle == 0) {
		LOG_ERROR("importbuffer failed!\n");
		return ;
	}

	fg_img = wrapbuffer_handle(fg_handle, fg_width, fg_height, fg_format);
	bg_img = wrapbuffer_handle(bg_handle, bg_width, bg_height, bg_format);

	if (rotation == ROTATION_90 || rotation == ROTATION_270) {
		bg_rect.x = (bg_width - area->y1 - fg_height);
		bg_rect.y = area->x1;
		bg_rect.width = fg_height;
		bg_rect.height = fg_width;
		usage |= IM_HAL_TRANSFORM_ROT_90;
	} else {
		bg_rect.x = area->x1;
		bg_rect.y = area->y1;
		bg_rect.width = fg_width;
		bg_rect.height = fg_height;
	}
	LOG_DEBUG("bg_rect x %d y %d w %d h %d\n", bg_rect.x, bg_rect.y, bg_rect.width, bg_rect.height);
	ret = imcheck(fg_img, bg_img, dummy_rect, bg_rect, usage);
	if (ret == IM_STATUS_NOERROR) {
		LOG_DEBUG("imcheck success!\n");
	} else {
		LOG_ERROR("imcheck failed! %s\n", imStrError(ret));
		releasebuffer_handle(fg_handle);
		releasebuffer_handle(bg_handle);
		return;
	}

	ret = improcess(fg_img, bg_img, dummy_img, dummy_rect, bg_rect, dummy_rect, usage);
	if (ret == IM_STATUS_SUCCESS) {
		LOG_DEBUG("improcess success!\n");
	} else {
		LOG_ERROR("improcess failed! %s\n", imStrError(ret));
	}

	if (fg_handle)
		releasebuffer_handle(fg_handle);
	if (bg_handle)
		releasebuffer_handle(bg_handle);
}

#if DRAW_USE_CPU
void rk_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
	int ret = 0;
	lv_coord_t area_width = (area->x2 - area->x1 + 1);
	lv_coord_t area_height = (area->y2 - area->y1 + 1);
	uint32_t display_layer = VO_UI_LAYER;
	int display_chn_id = VO_UI_CHN;
	VIDEO_FRAME_INFO_S *dst_frame = disp_dev.free_buf;

	LOG_DEBUG("x %d:%d y %d:%d area_width %d area_height %d\n", area->x1, area->x2, area->y1,
			  area->y2, area_width, area_height);

	void *dst_virtual_addr =
	    RK_MPI_MB_Handle2VirAddr(dst_frame->stVFrame.pMbBlk);
	void *src_virtual_addr = RK_MPI_MB_Handle2VirAddr(disp_dev.active_buf->stVFrame.pMbBlk);
	memcpy(dst_virtual_addr, src_virtual_addr, disp_dev.width * disp_dev.height * (LV_COLOR_SIZE / 8));
	if (disp_dev.rot == LV_DISP_ROT_90 || disp_dev.rot == LV_DISP_ROT_270) {
		draw_buf_rotate_90(color_p, area, dst_virtual_addr, disp_dev.width, disp_dev.height);
	} else {
		for (uint32_t y = 0, i = area->y1; i <= area->y2; ++i, ++y) {
			memcpy((uint8_t *)dst_virtual_addr +
			           (area->x1 + disp_dev.width * i) * (LV_COLOR_SIZE / 8),
			       (uint8_t *)color_p + (area_width * (LV_COLOR_SIZE / 8) * y),
			       area_width * (LV_COLOR_SIZE / 8));
		}
	}
	RK_MPI_SYS_MmzFlushCache(dst_frame->stVFrame.pMbBlk, RK_FALSE);
	ret = RK_MPI_VO_SendFrame(display_layer, display_chn_id, dst_frame, 1000);
	if (ret != 0) {
		LOG_ERROR("RK_MPI_VO_SendFrame [%d,%d] failed %#X\n", display_layer, display_chn_id, ret);
	}
	// swap buffer
	void *tmp = disp_dev.free_buf;
	disp_dev.free_buf = disp_dev.active_buf;
	disp_dev.active_buf = tmp;
	lv_disp_flush_ready(disp_drv);
}
#else
void rk_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
	int ret = 0;
	lv_coord_t area_width = (area->x2 - area->x1 + 1);
	lv_coord_t area_height = (area->y2 - area->y1 + 1);
	int rotation = rk_param_get_int("display:rotation", 1);
	uint32_t display_layer = VO_UI_LAYER;
	int display_chn_id = VO_UI_CHN;
	VIDEO_FRAME_INFO_S *dst_frame = disp_dev.free_buf;

	LOG_DEBUG("x %d:%d y %d:%d area_width %d area_height %d\n", area->x1, area->x2, area->y1,
			  area->y2, area_width, area_height);

	rga_copy(disp_dev.active_buf, disp_dev.free_buf);
	rga_copy_area(disp_dev.free_buf, area, color_p, rotation);
	ret = RK_MPI_VO_SendFrame(display_layer, display_chn_id, dst_frame, 1000);
	if (ret != 0) {
		LOG_ERROR("RK_MPI_VO_SendFrame [%d,%d] failed %#X\n", display_layer, display_chn_id, ret);
	}
	// swap buffer
	void *tmp = disp_dev.free_buf;
	disp_dev.free_buf = disp_dev.active_buf;
	disp_dev.active_buf = tmp;
	lv_disp_flush_ready(disp_drv);
	// INFO: Flush layer to make sure the display is updated.
	if (rotation == ROTATION_0 || rotation == ROTATION_180) {
		if (disp_dev.width == area_width &&
		    disp_dev.height == area_height) {
			RK_MPI_VO_SetLayerFlush(display_layer);
		}
	} else {
		if (disp_dev.height == area_width &&
		    disp_dev.width == area_height) {
			RK_MPI_VO_SetLayerFlush(display_layer);
		}
	}
}
#endif // DRAW_USE_CPU

void rk_disp_get_sizes(lv_coord_t *width, lv_coord_t *height, uint32_t *dpi) {
	if (width)
		*width = disp_dev.width;

	if (height)
		*height = disp_dev.height;

	if (dpi && disp_dev.mm_width)
		*dpi = DIV_ROUND_UP(disp_dev.width * 25400, disp_dev.mm_width * 1000);

	return;
}

disp_ops_t rk_ui_ops = {
	rk_disp_init,
	rk_disp_exit,
	rk_disp_get_sizes,
	rk_disp_flush,
};