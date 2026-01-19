// Copyright 2025 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern "C" {
#include "video.h"
#include "uvc_control.h"
#include "venc.h"
}
#include "opencv2/core.hpp"
#include "task/yolo26.h"
#include "draw/cv_draw.hpp"
#include "rga/rga.h"
#include "rga/im2d.h"
#include "rga/im2d_type.h"
#include "rga/im2d_buffer.h"

#include <thread>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "video.c"

#define RKISP_MAINPATH 0
#define RKISP_SELFPATH 1
#define RKISP_FBCPATH 2
#define VIDEO_PIPE_0 0
#define VIDEO_PIPE_1 1
#define VIDEO_PIPE_2 2
#define JPEG_VENC_CHN 3
#define VPSS_ROTATE 6
#define VPSS_GRP_ID VPSS_MAX_CHN_NUM
#define VI_PIPE_ID VI_MAX_CHN_NUM
#define RED_COLOR 0x0000FF
#define BLUE_COLOR 0xFF0000

#define RV1126B_VOP_LAYER_CLUSTER1 1

#define RTSP_URL_0 "/live/0"
#define RTSP_URL_1 "/live/1"
#define RTSP_URL_2 "/live/2"
#define RTMP_URL_0 "rtmp://127.0.0.1:1935/live/mainstream"
#define RTMP_URL_1 "rtmp://127.0.0.1:1935/live/substream"
#define RTMP_URL_2 "rtmp://127.0.0.1:1935/live/thirdstream"

int pipe_id_ = 0;
int g_vi_chn_id = 0;
int g_vi_for_venc_0_id = 3;
int g_vi_for_npu_ivs_id = 4;
int g_vi_for_venc_1_id = 2;
int g_vi_for_vo_chn_id = 5;

static int take_photo_one = 0;
static RK_BOOL enable_jpeg, enable_venc_0, enable_venc_1, enable_npu, enable_wrap, enable_ivs,
    enable_rtmp, enable_rtsp,enable_fec;
int g_enable_vo, g_vo_dev_id, g_vo_layer_id;
static int g_video_run_ = 1;
static int cycle_snapshot_flag = 0;
static const char *tmp_output_data_type = "H.264";
static const char *tmp_rc_mode;
static const char *tmp_h264_profile;
static const char *tmp_smart;
static const char *tmp_gop_mode;
static const char *tmp_rc_quality;
static const char *distortion_correction;
static std::thread venc_thread_0, venc_thread_1, venc_thread_2, jpeg_venc_thread_id,
	get_vpss_2_send_npu_thread, get_vi_2_send_thread,
	cycle_snapshot_thread_id, get_vi_thread_id;

static MPP_CHN_S vi_chn, vpss_in_chn, vi_for_vo_chn, vo_chn, vpss_out_chn[4], venc_chn, ivs_chn,
    gdc_chn;
typedef enum rkCOLOR_INDEX_E {
	RGN_COLOR_LUT_INDEX_0 = 0,
	RGN_COLOR_LUT_INDEX_1 = 1,
} COLOR_INDEX_E;

static RK_U8 rgn_color_lut_0_left_value[4] = {0x03, 0xf, 0x3f, 0xff};
static RK_U8 rgn_color_lut_0_right_value[4] = {0xc0, 0xf0, 0xfc, 0xff};
static RK_U8 rgn_color_lut_1_left_value[4] = {0x02, 0xa, 0x2a, 0xaa};
static RK_U8 rgn_color_lut_1_right_value[4] = {0x80, 0xa0, 0xa8, 0xaa};
RK_S32 draw_rect_2bpp(RK_U8 *buffer, RK_U32 width, RK_U32 height, int rgn_x, int rgn_y, int rgn_w,
                      int rgn_h, int line_pixel, COLOR_INDEX_E color_index) {
	int i;
	RK_U8 *ptr = buffer;
	RK_U8 value = 0;
	if (color_index == RGN_COLOR_LUT_INDEX_0)
		value = 0xff;
	if (color_index == RGN_COLOR_LUT_INDEX_1)
		value = 0xaa;

	if (line_pixel > 4) {
		printf("line_pixel %d > 4, not support\n", line_pixel);
		return -1;
	}

	// printf("YUV %dx%d, rgn (%d,%d,%d,%d), line pixel %d\n", width, height, rgn_x, rgn_y, rgn_w,
	// rgn_h, line_pixel); draw top line
	ptr += (width * rgn_y + rgn_x) >> 2;
	for (i = 0; i < line_pixel; i++) {
		memset(ptr, value, (rgn_w + 3) >> 2);
		ptr += width >> 2;
	}
	// draw letft/right line
	for (i = 0; i < (rgn_h - line_pixel * 2); i++) {
		if (color_index == RGN_COLOR_LUT_INDEX_1) {
			*ptr = rgn_color_lut_1_left_value[line_pixel - 1];
			*(ptr + ((rgn_w + 3) >> 2)) = rgn_color_lut_1_right_value[line_pixel - 1];
		} else {
			*ptr = rgn_color_lut_0_left_value[line_pixel - 1];
			*(ptr + ((rgn_w + 3) >> 2)) = rgn_color_lut_0_right_value[line_pixel - 1];
		}
		ptr += width >> 2;
	}
	// draw bottom line
	for (i = 0; i < line_pixel; i++) {
		memset(ptr, value, (rgn_w + 3) >> 2);
		ptr += width >> 2;
	}
	return 0;
}

static void *test_get_vi(void *arg) {
	printf("#Start %s thread, arg:%p\n", __func__, arg);
	VIDEO_FRAME_INFO_S stViFrame;
	VI_CHN_STATUS_S stChnStatus;
	int loopCount = 0;
	int ret = 0;
	int get_vi_chn_id = g_vi_for_venc_0_id;
	if (enable_fec)
		get_vi_chn_id = g_vi_chn_id;
	while (g_video_run_) {
		// 5.get the frame
		ret = RK_MPI_VI_GetChnFrame(pipe_id_, get_vi_chn_id, &stViFrame, 1000);
		if (ret == RK_SUCCESS) {
			void *data = RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk);
			// LOG_ERROR("RK_MPI_VI_GetChnFrame ok:data %p loop:%d seq:%d pts:%" PRId64 " ms\n",
			//(unsigned char*)data,
			//           loopCount, stViFrame.stVFrame.u32TimeRef, stViFrame.stVFrame.u64PTS /
			//           1000);
			if (rk_param_get_int("video.source:enable_uvc", 0))
				uvc_read_camera_buffer(
				   (unsigned char*)data, -1, stViFrame.stVFrame.u32Width * stViFrame.stVFrame.u32Height * 1.5,
				    NULL, 0);
			// 7.release the frame
			ret = RK_MPI_VI_ReleaseChnFrame(pipe_id_, get_vi_chn_id, &stViFrame);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("RK_MPI_VI_ReleaseChnFrame fail %x\n", ret);
			}
			loopCount++;
		} else {
			LOG_ERROR("RK_MPI_VI_GetChnFrame timeout %x\n", ret);
		}
	}

	return 0;
}

static void *rkipc_get_venc_0(void *arg) {
	printf("#Start %s thread, arg:%p\n", __func__, arg);
	VENC_STREAM_S stFrame;
	VI_CHN_STATUS_S stChnStatus;
	int loopCount = 0;
	int ret = 0;
	// FILE *fp = fopen("/data/venc.h265", "wb");
	stFrame.pstPack = (VENC_PACK_S*)malloc(sizeof(VENC_PACK_S));
	int enable_uvc = rk_param_get_int("video.source:enable_uvc", 0);

	while (g_video_run_) {
		// 5.get the frame
		ret = RK_MPI_VENC_GetStream(VIDEO_PIPE_0, &stFrame, 2500);
		if (ret == RK_SUCCESS) {
			void *data = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
			// fwrite(data, 1, stFrame.pstPack->u32Len, fp);
			// fflush(fp);
			// LOG_INFO("Count:%d, Len:%d, PTS is %" PRId64", enH264EType is %d\n", loopCount,
			// stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
			// stFrame.pstPack->DataType.enH264EType);
			if (enable_uvc)
				uvc_read_camera_buffer(data, -1, stFrame.pstPack->u32Len, NULL, 0);

			rkipc_rtsp_write_video_frame(0,(unsigned char*)data, stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS);
			if ((stFrame.pstPack->DataType.enH264EType == H264E_NALU_IDRSLICE) ||
			    (stFrame.pstPack->DataType.enH264EType == H264E_NALU_ISLICE) ||
			    (stFrame.pstPack->DataType.enH265EType == H265E_NALU_IDRSLICE) ||
			    (stFrame.pstPack->DataType.enH265EType == H265E_NALU_ISLICE)) {
				rk_storage_write_video_frame(0,(unsigned char*)data, stFrame.pstPack->u32Len,
				                             stFrame.pstPack->u64PTS, 1);
				rk_rtmp_write_video_frame(0,(unsigned char*)data, stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
				                          1);
			} else {
				rk_storage_write_video_frame(0,(unsigned char*)data, stFrame.pstPack->u32Len,
				                             stFrame.pstPack->u64PTS, 0);
				rk_rtmp_write_video_frame(0,(unsigned char*)data, stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
				                          0);
			}
			// 7.release the frame
			ret = RK_MPI_VENC_ReleaseStream(VIDEO_PIPE_0, &stFrame);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("RK_MPI_VENC_ReleaseStream fail %x\n", ret);
			}
			loopCount++;
		} else {
			LOG_ERROR("RK_MPI_VENC_GetStream timeout %x\n", ret);
		}
	}
	if (stFrame.pstPack)
		free(stFrame.pstPack);
	// if (fp)
	// fclose(fp);

	return 0;
}

static void *rkipc_get_venc_1(void *arg) {
	printf("#Start %s thread, arg:%p\n", __func__, arg);
	VENC_STREAM_S stFrame;
	VI_CHN_STATUS_S stChnStatus;
	int loopCount = 0;
	int ret = 0;
	stFrame.pstPack =(VENC_PACK_S*)malloc(sizeof(VENC_PACK_S));

	while (g_video_run_) {
		// 5.get the frame
		ret = RK_MPI_VENC_GetStream(VIDEO_PIPE_1, &stFrame, 2500);
		if (ret == RK_SUCCESS) {
			void *data = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
			// LOG_INFO("Count:%d, Len:%d, PTS is %" PRId64", enH264EType is %d\n", loopCount,
			// stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
			// stFrame.pstPack->DataType.enH264EType);
			rkipc_rtsp_write_video_frame(1,(unsigned char*)data, stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS);
			if ((stFrame.pstPack->DataType.enH264EType == H264E_NALU_IDRSLICE) ||
			    (stFrame.pstPack->DataType.enH264EType == H264E_NALU_ISLICE) ||
			    (stFrame.pstPack->DataType.enH265EType == H265E_NALU_IDRSLICE) ||
			    (stFrame.pstPack->DataType.enH265EType == H265E_NALU_ISLICE)) {
				rk_storage_write_video_frame(1,(unsigned char*)data, stFrame.pstPack->u32Len,
				                             stFrame.pstPack->u64PTS, 1);
				rk_rtmp_write_video_frame(1,(unsigned char*)data, stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
				                          1);
			} else {
				rk_storage_write_video_frame(1,(unsigned char*)data, stFrame.pstPack->u32Len,
				                             stFrame.pstPack->u64PTS, 0);
				rk_rtmp_write_video_frame(1,(unsigned char*)data, stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
				                          0);
			}
			// 7.release the frame
			ret = RK_MPI_VENC_ReleaseStream(VIDEO_PIPE_1, &stFrame);
			if (ret != RK_SUCCESS)
				LOG_ERROR("RK_MPI_VENC_ReleaseStream fail %x\n", ret);
			loopCount++;
		} else {
			LOG_ERROR("RK_MPI_VENC_GetStream timeout %x\n", ret);
		}
	}
	if (stFrame.pstPack)
		free(stFrame.pstPack);

	return 0;
}

static void *rkipc_get_venc_2(void *arg) {
	printf("#Start %s thread, arg:%p\n", __func__, arg);
	VENC_STREAM_S stFrame;
	VI_CHN_STATUS_S stChnStatus;
	int loopCount = 0;
	int ret = 0;
	stFrame.pstPack = (VENC_PACK_S*)malloc(sizeof(VENC_PACK_S));

	while (g_video_run_) {
		// 5.get the frame
		ret = RK_MPI_VENC_GetStream(VIDEO_PIPE_2, &stFrame, 2500);
		if (ret == RK_SUCCESS) {
			void *data = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
			// LOG_INFO("Count:%d, Len:%d, PTS is %" PRId64", enH264EType is %d\n", loopCount,
			// stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
			// stFrame.pstPack->DataType.enH264EType);
			rkipc_rtsp_write_video_frame(2,(unsigned char*)data, stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS);
			if ((stFrame.pstPack->DataType.enH264EType == H264E_NALU_IDRSLICE) ||
			    (stFrame.pstPack->DataType.enH264EType == H264E_NALU_ISLICE) ||
			    (stFrame.pstPack->DataType.enH265EType == H265E_NALU_IDRSLICE) ||
			    (stFrame.pstPack->DataType.enH265EType == H265E_NALU_ISLICE)) {
				rk_storage_write_video_frame(2,(unsigned char*)data, stFrame.pstPack->u32Len,
				                             stFrame.pstPack->u64PTS, 1);
				rk_rtmp_write_video_frame(2,(unsigned char*)data, stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
				                          1);
			} else {
				rk_storage_write_video_frame(2,(unsigned char*)data, stFrame.pstPack->u32Len,
				                             stFrame.pstPack->u64PTS, 0);
				rk_rtmp_write_video_frame(2,(unsigned char*)data, stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
				                          0);
			}
			// 7.release the frame
			ret = RK_MPI_VENC_ReleaseStream(VIDEO_PIPE_2, &stFrame);
			if (ret != RK_SUCCESS)
				LOG_ERROR("RK_MPI_VENC_ReleaseStream fail %x\n", ret);
			loopCount++;
		} else {
			LOG_ERROR("RK_MPI_VENC_GetStream timeout %x\n", ret);
		}
	}
	if (stFrame.pstPack)
		free(stFrame.pstPack);

	return 0;
}

static void *rkipc_get_jpeg(void *arg) {
	printf("#Start %s thread, arg:%p\n", __func__, arg);
	VENC_STREAM_S stFrame;
	VI_CHN_STATUS_S stChnStatus;
	int loopCount = 0;
	int ret = 0;
	char file_name[128] = {0};
	const char *file_path = rk_param_get_string("storage:file_path", "/userdata");
	stFrame.pstPack = (VENC_PACK_S*)malloc(sizeof(VENC_PACK_S));

	while (g_video_run_) {
		usleep(300 * 1000);
		if (!take_photo_one)
			continue;
		// 5.get the frame
		ret = RK_MPI_VENC_GetStream(JPEG_VENC_CHN, &stFrame, 1000);
		if (ret == RK_SUCCESS) {
			void *data = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
			LOG_INFO("Count:%d, Len:%d, PTS is %" PRId64 ", enH264EType is %d\n", loopCount,
			         stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
			         stFrame.pstPack->DataType.enH264EType);
			// uvc_read_camera_buffer(data, -1, stFrame.pstPack->u32Len, NULL, 0);
			// save jpeg file
			time_t t = time(NULL);
			struct tm tm = *localtime(&t);
			snprintf(file_name, 128, "%s/%d%02d%02d%02d%02d%02d.jpeg", file_path, tm.tm_year + 1900,
			         tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
			LOG_INFO("file_name is %s\n", file_name);
			FILE *fp = fopen(file_name, "wb");
			fwrite(data, 1, stFrame.pstPack->u32Len, fp);
			fflush(fp);
			fclose(fp);
			take_photo_one = 0;
			// 7.release the frame
			ret = RK_MPI_VENC_ReleaseStream(JPEG_VENC_CHN, &stFrame);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("RK_MPI_VENC_ReleaseStream fail %x\n", ret);
			}
			loopCount++;
		} else {
			LOG_ERROR("RK_MPI_VENC_GetStream timeout %x\n", ret);
		}
		// usleep(33 * 1000);
	}
	if (stFrame.pstPack)
		free(stFrame.pstPack);

	return 0;
}

int rkipc_rtmp_init() {
	int ret = 0;
	ret |= rk_rtmp_init(0, RTMP_URL_0);
	ret |= rk_rtmp_init(1, RTMP_URL_1);
	ret |= rk_rtmp_init(2, RTMP_URL_2);

	return ret;
}

int rkipc_rtmp_deinit() {
	int ret = 0;
	ret |= rk_rtmp_deinit(0);
	ret |= rk_rtmp_deinit(1);
	ret |= rk_rtmp_deinit(2);

	return ret;
}

int rkipc_vi_dev_init() {
	LOG_INFO("%s\n", __func__);
	int ret = 0;
	VI_DEV_ATTR_S stDevAttr;
	VI_DEV_BIND_PIPE_S stBindPipe;
	memset(&stDevAttr, 0, sizeof(stDevAttr));
	memset(&stBindPipe, 0, sizeof(stBindPipe));
	// 0. get dev config status
	ret = RK_MPI_VI_GetDevAttr(pipe_id_, &stDevAttr);
	if (ret == RK_ERR_VI_NOT_CONFIG) {
		// 0-1.config dev
		ret = RK_MPI_VI_SetDevAttr(pipe_id_, &stDevAttr);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("RK_MPI_VI_SetDevAttr %x\n", ret);
			return -1;
		}
	} else {
		LOG_ERROR("RK_MPI_VI_SetDevAttr already\n");
	}
	// 1.get dev enable status
	ret = RK_MPI_VI_GetDevIsEnable(pipe_id_);
	if (ret != RK_SUCCESS) {
		// 1-2.enable dev
		ret = RK_MPI_VI_EnableDev(pipe_id_);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("RK_MPI_VI_EnableDev %x\n", ret);
			return -1;
		}
		// 1-3.bind dev/pipe
		stBindPipe.u32Num = pipe_id_;
		stBindPipe.PipeId[0] = pipe_id_;
		ret = RK_MPI_VI_SetDevBindPipe(pipe_id_, &stBindPipe);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("RK_MPI_VI_SetDevBindPipe %x\n", ret);
			return -1;
		}
	} else {
		LOG_ERROR("RK_MPI_VI_EnableDev already\n");
	}

	if (!enable_fec) {
		VI_PARAM_MOD_S stModParam;
		memset(&stModParam, 0, sizeof(stModParam));
		stModParam.enViModType = VI_EXT_CHN_MODE;
		stModParam.stExtChnParam.mirrorCmsc = 0; // 1 is for vpss, 0 is for vi ext
		stModParam.stExtChnParam.extChn[0] = 0;
		stModParam.stExtChnParam.extChn[1] = 0;
		stModParam.stExtChnParam.extChn[2] = 0;
		stModParam.stExtChnParam.extChn[3] = 0;
		ret = RK_MPI_VI_SetModParam(&stModParam);
		if (ret)
			LOG_ERROR("RK_MPI_VI_SetModParam fail:%#X\n", ret);

		memset(&stModParam, 0, sizeof(stModParam));
		stModParam.enViModType = VI_EXT_CHN_MODE;
		ret = RK_MPI_VI_GetModParam(&stModParam);
		if (ret)
			LOG_ERROR("RK_MPI_VI_GetModParam fail:%#X\n", ret);

		LOG_INFO("vi mod:%d mirror:%d ext_chn_mode:%d ext_chn1_mode:%d"
		         "ext_chn2_mode:%d ext_chn3_mode:%d\n",
		         stModParam.enViModType, stModParam.stExtChnParam.mirrorCmsc,
		         stModParam.stExtChnParam.extChn[0], stModParam.stExtChnParam.extChn[1],
		         stModParam.stExtChnParam.extChn[2], stModParam.stExtChnParam.extChn[3]);
	}

	return 0;
}

int rkipc_vi_dev_deinit() {
	RK_MPI_VI_DisableDev(pipe_id_);

	return 0;
}

int rkipc_vi_gdc_vpss_init() {
	int ret;
	int video_width = rk_param_get_int("video.0:width", -1);
	int video_height = rk_param_get_int("video.0:height", -1);
	int rotation = rk_param_get_int("video.source:rotation", 0);
	int buf_cnt = 3;
	tmp_output_data_type = rk_param_get_string("video.0:output_data_type", NULL);

	// VI
	VI_CHN_ATTR_S vi_chn_attr;
	memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
	vi_chn_attr.stIspOpt.u32BufCount = buf_cnt;
	vi_chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
	vi_chn_attr.stIspOpt.stMaxSize.u32Width = rk_param_get_int("video.0:max_width", 2560);
	vi_chn_attr.stIspOpt.stMaxSize.u32Height = rk_param_get_int("video.0:max_height", 1440);
	vi_chn_attr.stSize.u32Width = video_width;
	vi_chn_attr.stSize.u32Height = video_height;
	vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
	if (!strcmp(tmp_output_data_type, "NV12"))
		vi_chn_attr.u32Depth = 1;
	if (rk_param_get_int("video.source:enable_compress", 0))
		vi_chn_attr.enVideoFormat = VIDEO_FORMAT_TILE_4x4;
	else
		vi_chn_attr.enVideoFormat = VIDEO_FORMAT_LINEAR;
	vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE;
	ret = RK_MPI_VI_SetChnAttr(pipe_id_, g_vi_chn_id, &vi_chn_attr);
	if (ret) {
		LOG_ERROR("ERROR: create VI error! ret=%d\n", ret);
		return ret;
	}
	ret = RK_MPI_VI_EnableChn(pipe_id_, g_vi_chn_id);
	if (ret) {
		LOG_ERROR("ERROR: create VI error! ret=%d\n", ret);
		return ret;
	}
	if (!strcmp(tmp_output_data_type, "NV12"))
		get_vi_thread_id = std::thread(test_get_vi, nullptr);

	// GDC
	GDC_CHN_ATTR_S stAttr = {0};
	GDC_UPDATE_ATTR_S stUpdateAttr {};
	stAttr.u32MaxInQueue = 3;
	stAttr.u32MaxOutQueue = 3;
	stAttr.s32Depth = 0;
	stAttr.s32DstWidth = rk_param_get_int("video.0:width", -1);
	stAttr.s32DstHeight = rk_param_get_int("video.0:height", -1);
	stAttr.enDstPixelFormat = RK_FMT_YUV420SP;
	if (rk_param_get_int("video.source:enable_compress", 0)) {
		stAttr.enDstCompMode = COMPRESS_RFBC_64x4;
	} else {
		stAttr.enDstCompMode = COMPRESS_MODE_NONE;
	}

	if (!strcmp(distortion_correction, "DIS")) {
		stAttr.enMode = GDC_CHN_MODE_DIS;
		const char *dis_file = rk_param_get_string("isp.0.enhancement:dis_file", NULL);
		if (dis_file) {
			LOG_INFO("dis_file is %s\n", dis_file);
			memcpy(stAttr.cfgFile, dis_file, strlen(dis_file));
		} else {
			LOG_ERROR("dis_file is NULL\n");
			return -1;
		}
	} else {
		stAttr.enMode = GDC_CHN_MODE_FEC;
		stAttr.stFecAttr.s32InFourcc = 0;
		stAttr.stFecAttr.s32OutFourcc = 0;
		stAttr.stFecAttr.s32BorderMode = 0;
		stAttr.stFecAttr.s32CrossBufMode = 0;
		stAttr.stFecAttr.stBgVal.s32BgY = 255;
		stAttr.stFecAttr.stBgVal.s32BgU = 100;
		stAttr.stFecAttr.stBgVal.s32BgV = 0;

		const char *fec_ini_file = rk_param_get_string("isp.0.enhancement:fec_ini_file", NULL);
		if (fec_ini_file) {
			LOG_INFO("fec_ini_file is %s\n", fec_ini_file);
			stUpdateAttr.enMode = GDC_CHN_MODE_FEC;
			RK_MPI_GDC_GetAttrFromFile(&stUpdateAttr, fec_ini_file);
			stAttr.stFecAttr.enFecMode = GDC_FEC_UPDATE_MESH_ONLINE;
			stAttr.stFecAttr.dLightCenter[0] = stUpdateAttr.stFecAttr.stOnlineCfg.dLightCenter[0];
			stAttr.stFecAttr.dLightCenter[1] = stUpdateAttr.stFecAttr.stOnlineCfg.dLightCenter[1];
			for (int i = 0; i < 4; i++) {
				stAttr.stFecAttr.dCoeff[i] = stUpdateAttr.stFecAttr.stOnlineCfg.dCoeff[i];
			}
			// stAttr.stFecAttr.s32CorrectLevel  = stUpdateAttr.stFecAttr.stOnlineCfg.s32CorrectLevel;
			stAttr.stFecAttr.s32CorrectLevel =
				rk_param_get_int("isp.0.enhancement:fec_level", 0) * 2.55;
			stAttr.stFecAttr.enStyle = stUpdateAttr.stFecAttr.stOnlineCfg.enStyle;
		} else {
			LOG_ERROR("fec_ini_file is NULL\n");
			return -1;
		}
		stAttr.stFecAttr.enDirection = GDC_FEC_CORRECT_DIRECTION_XY;
		stAttr.stFecAttr.enStyle = GDC_FEC_KEEP_ASPECT_RATIO_REDUCE_FOV;
	}
	ret = RK_MPI_GDC_CreateChn(0, &stAttr);

	// VPSS
	VPSS_CHN VpssChn[VPSS_MAX_CHN_NUM] = {VPSS_CHN0, VPSS_CHN1, VPSS_CHN2, VPSS_CHN3};
	VPSS_GRP VpssGrp = 0;
	VPSS_GRP_ATTR_S stVpssGrpAttr;
	VPSS_CHN_ATTR_S stVpssChnAttr[VPSS_MAX_CHN_NUM];
	memset(&stVpssGrpAttr, 0, sizeof(stVpssGrpAttr));
	memset(&stVpssChnAttr[0], 0, sizeof(stVpssChnAttr[0]));
	memset(&stVpssChnAttr[1], 0, sizeof(stVpssChnAttr[1]));
	memset(&stVpssChnAttr[2], 0, sizeof(stVpssChnAttr[2]));
	memset(&stVpssChnAttr[3], 0, sizeof(stVpssChnAttr[3]));
	stVpssGrpAttr.u32MaxW = 4096;
	stVpssGrpAttr.u32MaxH = 4096;
	stVpssGrpAttr.enPixelFormat = RK_FMT_YUV420SP;
	stVpssGrpAttr.stFrameRate.s32SrcFrameRate = -1;
	stVpssGrpAttr.stFrameRate.s32DstFrameRate = -1;
	if (rk_param_get_int("video.source:enable_compress", 0))
		stVpssGrpAttr.enCompressMode = COMPRESS_RFBC_64x4;
	else
		stVpssGrpAttr.enCompressMode = COMPRESS_MODE_NONE;

	const char *vpss_proc_dev = rk_param_get_string("video.source:vpss_proc_dev", "vpss");
	if (!strcmp(vpss_proc_dev, "gpu")) {
		stVpssGrpAttr.enVProcDev = VIDEO_PROC_DEV_GPU;
	} else if (!strcmp(vpss_proc_dev, "rga")) {
		stVpssGrpAttr.enVProcDev = VIDEO_PROC_DEV_RGA;
	} else {
		stVpssGrpAttr.enVProcDev = VIDEO_PROC_DEV_VPSS;
	}
	ret = RK_MPI_VPSS_CreateGrp(VpssGrp, &stVpssGrpAttr);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VPSS_CreateGrp error! ret is %#x\n", ret);
		return ret;
	}

	if (enable_venc_0) {
		stVpssChnAttr[0].enChnMode = VPSS_CHN_MODE_AUTO;
		stVpssChnAttr[0].enDynamicRange = DYNAMIC_RANGE_SDR8;
		stVpssChnAttr[0].enPixelFormat = RK_FMT_YUV420SP;
		stVpssChnAttr[0].stFrameRate.s32SrcFrameRate = -1;
		stVpssChnAttr[0].stFrameRate.s32DstFrameRate = -1;
		stVpssChnAttr[0].u32Width = rk_param_get_int("video.0:width", -1);
		stVpssChnAttr[0].u32Height = rk_param_get_int("video.0:height", -1);
		if (rk_param_get_int("video.source:enable_compress", 0))
			stVpssChnAttr[0].enCompressMode = COMPRESS_RFBC_64x4;
		else
			stVpssChnAttr[0].enCompressMode = COMPRESS_MODE_NONE;
		stVpssChnAttr[0].u32FrameBufCnt = 2;
		ret = RK_MPI_VPSS_SetChnAttr(VpssGrp, VpssChn[0], &stVpssChnAttr[0]);
		if (ret != RK_SUCCESS)
			LOG_ERROR("0: RK_MPI_VPSS_SetChnAttr error! ret is %#x\n", ret);
		ret = RK_MPI_VPSS_EnableChn(VpssGrp, VpssChn[0]);
		if (ret != RK_SUCCESS)
			LOG_ERROR("0: RK_MPI_VPSS_EnableChn error! ret is %#x\n", ret);
	}
	if (enable_venc_1) {
		stVpssChnAttr[1].enChnMode = VPSS_CHN_MODE_AUTO;
		stVpssChnAttr[1].enDynamicRange = DYNAMIC_RANGE_SDR8;
		stVpssChnAttr[1].enPixelFormat = RK_FMT_YUV420SP;
		stVpssChnAttr[1].stFrameRate.s32SrcFrameRate = -1;
		stVpssChnAttr[1].stFrameRate.s32DstFrameRate = -1;
		stVpssChnAttr[1].u32Width = rk_param_get_int("video.1:width", 0);
		stVpssChnAttr[1].u32Height = rk_param_get_int("video.1:height", 0);
		stVpssChnAttr[1].enCompressMode = COMPRESS_MODE_NONE;
		stVpssChnAttr[1].u32FrameBufCnt = 2;
		ret = RK_MPI_VPSS_SetChnAttr(VpssGrp, VpssChn[1], &stVpssChnAttr[1]);
		if (ret != RK_SUCCESS)
			LOG_ERROR("1: RK_MPI_VPSS_SetChnAttr error! ret is %#x\n", ret);
		ret = RK_MPI_VPSS_EnableChn(VpssGrp, VpssChn[1]);
		if (ret != RK_SUCCESS)
			LOG_ERROR("1: RK_MPI_VPSS_EnableChn error! ret is %#x\n", ret);
	}
	if (enable_npu || enable_ivs) {
		stVpssChnAttr[2].enChnMode = VPSS_CHN_MODE_AUTO;
		stVpssChnAttr[2].enDynamicRange = DYNAMIC_RANGE_SDR8;
		stVpssChnAttr[2].enPixelFormat = RK_FMT_YUV420SP;
		stVpssChnAttr[2].stFrameRate.s32SrcFrameRate = -1;
		stVpssChnAttr[2].stFrameRate.s32DstFrameRate = -1;
		stVpssChnAttr[2].u32Width = rk_param_get_int("video.2:width", 0);
		stVpssChnAttr[2].u32Height = rk_param_get_int("video.2:height", 0);
		stVpssChnAttr[2].enCompressMode = COMPRESS_MODE_NONE;
		stVpssChnAttr[2].u32FrameBufCnt = 2;
		if (enable_npu) {
			stVpssChnAttr[2].u32Depth = 1;
			stVpssChnAttr[2].u32FrameBufCnt = 3;
		}
		ret = RK_MPI_VPSS_SetChnAttr(VpssGrp, VpssChn[2], &stVpssChnAttr[2]);
		if (ret != RK_SUCCESS)
			LOG_ERROR("2: RK_MPI_VPSS_SetChnAttr error! ret is %#x\n", ret);
		ret = RK_MPI_VPSS_EnableChn(VpssGrp, VpssChn[2]);
		if (ret != RK_SUCCESS)
			LOG_ERROR("2: RK_MPI_VPSS_EnableChn error! ret is %#x\n", ret);
	}
	if (g_enable_vo) {
		stVpssChnAttr[3].enChnMode = VPSS_CHN_MODE_AUTO;
		stVpssChnAttr[3].enDynamicRange = DYNAMIC_RANGE_SDR8;
		stVpssChnAttr[3].enPixelFormat = RK_FMT_YUV420SP;
		stVpssChnAttr[3].stFrameRate.s32SrcFrameRate = -1;
		stVpssChnAttr[3].stFrameRate.s32DstFrameRate = -1;
		stVpssChnAttr[3].u32Width = 1920;
		stVpssChnAttr[3].u32Height = 1080;
		stVpssChnAttr[3].enCompressMode = COMPRESS_MODE_NONE;
		stVpssChnAttr[3].u32FrameBufCnt = 3;
		ret = RK_MPI_VPSS_SetChnAttr(VpssGrp, VpssChn[3], &stVpssChnAttr[3]);
		if (ret != RK_SUCCESS)
			LOG_ERROR("3: RK_MPI_VPSS_SetChnAttr error! ret is %#x\n", ret);
		ret = RK_MPI_VPSS_EnableChn(VpssGrp, VpssChn[3]);
		if (ret != RK_SUCCESS)
			LOG_ERROR("3: RK_MPI_VPSS_EnableChn error! ret is %#x\n", ret);
	}
	ret = RK_MPI_VPSS_EnableBackupFrame(VpssGrp);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VPSS_EnableBackupFrame error! ret is %#x\n", ret);
		return ret;
	}
	RK_MPI_VPSS_SetVProcDev(VpssGrp, VIDEO_PROC_DEV_VPSS);
	ret = RK_MPI_VPSS_StartGrp(VpssGrp);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VPSS_StartGrp error! ret is %#x\n", ret);
		return ret;
	}
	for (int i = 0; i < 4; i++) {
		vpss_out_chn[i].enModId = RK_ID_VPSS;
		vpss_out_chn[i].s32DevId = 0;
		vpss_out_chn[i].s32ChnId = i;
	}

	vi_chn.enModId = RK_ID_VI;
	vi_chn.s32DevId = 0;
	vi_chn.s32ChnId = g_vi_chn_id;
	gdc_chn.enModId = RK_ID_GDC;
	gdc_chn.s32DevId = 0;
	gdc_chn.s32ChnId = 0;
	vpss_in_chn.enModId = RK_ID_VPSS;
	vpss_in_chn.s32DevId = 0;
	vpss_in_chn.s32ChnId = 0;

	ret = RK_MPI_SYS_Bind(&vi_chn, &gdc_chn);
	if (ret)
		LOG_ERROR("Bind VI and GDC error! ret=%#x\n", ret);
	ret = RK_MPI_SYS_Bind(&gdc_chn, &vpss_in_chn);
	if (ret)
		LOG_ERROR("Bind GDC and VPSS error! ret=%#x\n", ret);

	return ret;
}

int rkipc_vi_gdc_vpss_deinit() {
	int ret;
	ret = RK_MPI_SYS_UnBind(&gdc_chn, &vpss_in_chn);
	if (ret)
		LOG_ERROR("UnBind GDC and VPSS error! ret=%#x\n", ret);
	vi_chn.enModId = RK_ID_VI;
	vi_chn.s32DevId = 0;
	vi_chn.s32ChnId = g_vi_chn_id;
	gdc_chn.enModId = RK_ID_GDC;
	gdc_chn.s32DevId = 0;
	gdc_chn.s32ChnId = 0;
	ret = RK_MPI_SYS_UnBind(&vi_chn, &gdc_chn);
	if (ret)
		LOG_ERROR("UnBind VI and GDC error! ret=%#x\n", ret);
	// VPSS
	VPSS_CHN VpssChn[VPSS_MAX_CHN_NUM] = {VPSS_CHN0, VPSS_CHN1, VPSS_CHN2, VPSS_CHN3};
	VPSS_GRP VpssGrp = 0;
	ret |= RK_MPI_VPSS_StopGrp(VpssGrp);
	if (enable_venc_0)
		ret |= RK_MPI_VPSS_DisableChn(VpssGrp, VpssChn[0]);
	if (enable_venc_1)
		ret |= RK_MPI_VPSS_DisableChn(VpssGrp, VpssChn[1]);
	if (enable_npu || enable_ivs)
		ret |= RK_MPI_VPSS_DisableChn(VpssGrp, VpssChn[2]);
	if (g_enable_vo)
		ret |= RK_MPI_VPSS_DisableChn(VpssGrp, VpssChn[3]);
	ret |= RK_MPI_VPSS_DisableBackupFrame(VpssGrp);
	ret |= RK_MPI_VPSS_DestroyGrp(VpssGrp);
	// GDC
	ret |= RK_MPI_GDC_DestroyChn(0);
	// VI
	ret |= RK_MPI_VI_DisableChn(pipe_id_, g_vi_chn_id);

	return ret;
}

int rkipc_vi_ext_init() {
	int ret = 0;
	VI_CHN_ATTR_S vi_chn_attr;
	tmp_output_data_type = rk_param_get_string("video.0:output_data_type", NULL);
	if (enable_venc_0) {
		int video_width = rk_param_get_int("video.0:width", -1);
		int video_height = rk_param_get_int("video.0:height", -1);
		int video_max_width = rk_param_get_int("video.0:max_width", -1);
		int video_max_height = rk_param_get_int("video.0:max_height", -1);
		int buffer_line = rk_param_get_int("video.source:buffer_line", video_max_height / 4);
		if (buffer_line < 128)
			buffer_line = video_max_height;
		memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
		vi_chn_attr.stIspOpt.u32BufCount = rk_param_get_int("video.0:input_buffer_count", 3);
		vi_chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
		vi_chn_attr.stIspOpt.stMaxSize.u32Width = rk_param_get_int("video.0:max_width", 2560);
		vi_chn_attr.stIspOpt.stMaxSize.u32Height = rk_param_get_int("video.0:max_height", 1440);
		vi_chn_attr.stSize.u32Width = rk_param_get_int("video.0:width", 2560);
		vi_chn_attr.stSize.u32Height = rk_param_get_int("video.0:height", 1440);
		if (!strcmp(tmp_output_data_type, "NV12"))
			vi_chn_attr.u32Depth = 1;
		vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
		if (rk_param_get_int("video.source:enable_compress", 0))
			vi_chn_attr.enCompressMode = COMPRESS_RFBC_64x4;
		else
			vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE;
		ret = RK_MPI_VI_SetChnAttr(pipe_id_, g_vi_for_venc_0_id, &vi_chn_attr);
		if (ret) {
			LOG_ERROR("ERROR: create VI error! ret=%d\n", ret);
			return ret;
		}

		VI_CHN_BUF_WRAP_S stViWrap;
		memset(&stViWrap, 0, sizeof(VI_CHN_BUF_WRAP_S));
		if (enable_wrap) {
			if (buffer_line < 128 || buffer_line > video_max_height) {
				LOG_ERROR("wrap mode buffer line must between [128, H], set as video_max_height\n");
				buffer_line = video_max_height;
			}
			stViWrap.bEnable = enable_wrap;
			stViWrap.u32BufLine = buffer_line;
			stViWrap.u32WrapBufferSize = stViWrap.u32BufLine * video_max_width * 3 / 2;
			LOG_INFO("set vi channel wrap line: %d, wrapBuffSize = %d\n", stViWrap.u32BufLine,
			         stViWrap.u32WrapBufferSize);
			RK_MPI_VI_SetChnWrapBufAttr(pipe_id_, g_vi_for_venc_0_id, &stViWrap);
		}

		ret = RK_MPI_VI_EnableChn(pipe_id_, g_vi_for_venc_0_id);
		if (ret) {
			LOG_ERROR("ERROR: create VI error! ret=%d\n", ret);
			return ret;
		}
		if (!strcmp(tmp_output_data_type, "NV12"))
			get_vi_thread_id = std::thread(test_get_vi, nullptr);
	}

	if (enable_venc_1) {
		memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
		vi_chn_attr.stIspOpt.u32BufCount = rk_param_get_int("video.1:input_buffer_count", 3);
		vi_chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
		vi_chn_attr.stIspOpt.stMaxSize.u32Width = rk_param_get_int("video.1:max_width", 704);
		vi_chn_attr.stIspOpt.stMaxSize.u32Height = rk_param_get_int("video.1:max_height", 576);
		vi_chn_attr.stSize.u32Width = rk_param_get_int("video.1:width", 2560);
		vi_chn_attr.stSize.u32Height = rk_param_get_int("video.1:height", 1440);
		vi_chn_attr.u32Depth = 0;
		vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
		vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE;
		ret = RK_MPI_VI_SetChnAttr(pipe_id_, g_vi_for_venc_1_id, &vi_chn_attr);
		ret |= RK_MPI_VI_EnableChn(pipe_id_, g_vi_for_venc_1_id);
		if (ret) {
			LOG_ERROR("ERROR: create VI error! ret=%d\n", ret);
			return ret;
		}
	}

	if (enable_npu || enable_ivs) {
		memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
		vi_chn_attr.stIspOpt.u32BufCount = 2;
		if (enable_npu) // ensure vi and ivs have two buffer ping-pong
			vi_chn_attr.stIspOpt.u32BufCount += 1;
		vi_chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
		vi_chn_attr.stIspOpt.stMaxSize.u32Width = rk_param_get_int("video.2:max_width", 960);
		vi_chn_attr.stIspOpt.stMaxSize.u32Height = rk_param_get_int("video.2:max_height", 540);
		vi_chn_attr.stSize.u32Width = rk_param_get_int("video.2:width", 2560);
		vi_chn_attr.stSize.u32Height = rk_param_get_int("video.2:height", 1440);
		vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
		vi_chn_attr.enCompressMode = COMPRESS_MODE_NONE;
		vi_chn_attr.u32Depth = 0;
		if (enable_npu)
			vi_chn_attr.u32Depth += 1;
		ret = RK_MPI_VI_SetChnAttr(pipe_id_, g_vi_for_npu_ivs_id, &vi_chn_attr);
		if (ret) {
			LOG_ERROR("ERROR: create VI error! ret=%d\n", ret);
			return ret;
		}
		ret = RK_MPI_VI_EnableChn(pipe_id_, g_vi_for_npu_ivs_id);
		if (ret) {
			LOG_ERROR("ERROR: create VI error! ret=%d\n", ret);
			return ret;
		}
	}

	if (g_enable_vo) {
		memset(&vi_chn_attr, 0, sizeof(vi_chn_attr));
		vi_chn_attr.stIspOpt.u32BufCount = 3;
		vi_chn_attr.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
		vi_chn_attr.stSize.u32Width = 1920;
		vi_chn_attr.stSize.u32Height = 1080;
		vi_chn_attr.enPixelFormat = RK_FMT_YUV420SP;
		vi_chn_attr.u32Depth = 0;
		ret = RK_MPI_VI_SetChnAttr(pipe_id_, g_vi_for_vo_chn_id, &vi_chn_attr);
		ret |= RK_MPI_VI_EnableChn(pipe_id_, g_vi_for_vo_chn_id);
		if (ret) {
			LOG_ERROR("ERROR: create VI error! ret=%d\n", ret);
			return ret;
		}
	}

	return ret;
}

int rkipc_vi_ext_deinit() {
	int ret = 0;
	if (enable_venc_0) {
		ret = RK_MPI_VI_DisableChn(pipe_id_, g_vi_for_venc_0_id);
		if (ret)
			LOG_ERROR("ERROR: Destroy VI error! ret=%#x\n", ret);
	}
	if (enable_venc_1) {
		ret = RK_MPI_VI_DisableChn(pipe_id_, g_vi_for_venc_1_id);
		if (ret)
			LOG_ERROR("ERROR: Destroy VI error! ret=%#x\n", ret);
	}
	if (enable_npu || enable_ivs) {
		ret = RK_MPI_VI_DisableChn(pipe_id_, g_vi_for_npu_ivs_id);
		if (ret)
			LOG_ERROR("ERROR: Destroy VI error! ret=%#x\n", ret);
	}
	if (g_enable_vo) {
		ret = RK_MPI_VI_DisableChn(pipe_id_, g_vi_for_vo_chn_id);
		if (ret) {
			LOG_ERROR("ERROR: RK_MPI_VI_DisableChn VI error! ret=%x\n", ret);
			return -1;
		}
	}
	return ret;
}

int rkipc_pipe_0_init() {
	int ret;
	int video_width = rk_param_get_int("video.0:width", -1);
	int video_height = rk_param_get_int("video.0:height", -1);
	int video_max_width = rk_param_get_int("video.0:max_width", -1);
	int video_max_height = rk_param_get_int("video.0:max_height", -1);
	int rotation = rk_param_get_int("video.source:rotation", 0);

	// VENC
	VENC_CHN_ATTR_S venc_chn_attr;
	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
	tmp_output_data_type = rk_param_get_string("video.0:output_data_type", NULL);
	tmp_rc_mode = rk_param_get_string("video.0:rc_mode", NULL);
	tmp_h264_profile = rk_param_get_string("video.0:h264_profile", NULL);
	if ((tmp_output_data_type == NULL) || (tmp_rc_mode == NULL)) {
		LOG_ERROR("tmp_output_data_type or tmp_rc_mode is NULL\n");
		return -1;
	}
	LOG_DEBUG("tmp_output_data_type is %s, tmp_rc_mode is %s, tmp_h264_profile is %s\n",
	          tmp_output_data_type, tmp_rc_mode, tmp_h264_profile);
	if (!strcmp(tmp_output_data_type, "H.264")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_AVC;
		if (!strcmp(tmp_h264_profile, "high"))
			venc_chn_attr.stVencAttr.u32Profile = 100;
		else if (!strcmp(tmp_h264_profile, "main"))
			venc_chn_attr.stVencAttr.u32Profile = 77;
		else if (!strcmp(tmp_h264_profile, "baseline"))
			venc_chn_attr.stVencAttr.u32Profile = 66;
		else
			LOG_ERROR("tmp_h264_profile is %s\n", tmp_h264_profile);
		if (!strcmp(tmp_rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
			venc_chn_attr.stRcAttr.stH264Cbr.u32Gop = rk_param_get_int("video.0:gop", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate = rk_param_get_int("video.0:max_rate", 0);
			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen =
			    rk_param_get_int("video.0:dst_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum =
			    rk_param_get_int("video.0:dst_frame_rate_num", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen =
			    rk_param_get_int("video.0:src_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum =
			    rk_param_get_int("video.0:src_frame_rate_num", -1);
		} else {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
			venc_chn_attr.stRcAttr.stH264Vbr.u32Gop = rk_param_get_int("video.0:gop", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32BitRate = rk_param_get_int("video.0:mid_rate", 0);
			venc_chn_attr.stRcAttr.stH264Vbr.u32MaxBitRate =
			    rk_param_get_int("video.0:max_rate", 0);
			venc_chn_attr.stRcAttr.stH264Vbr.u32MinBitRate =
			    rk_param_get_int("video.0:min_rate", 0);
			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateDen =
			    rk_param_get_int("video.0:dst_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateNum =
			    rk_param_get_int("video.0:dst_frame_rate_num", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateDen =
			    rk_param_get_int("video.0:src_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateNum =
			    rk_param_get_int("video.0:src_frame_rate_num", -1);
		}
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_HEVC;
		if (!strcmp(tmp_rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
			venc_chn_attr.stRcAttr.stH265Cbr.u32Gop = rk_param_get_int("video.0:gop", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate = rk_param_get_int("video.0:max_rate", 0);
			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen =
			    rk_param_get_int("video.0:dst_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum =
			    rk_param_get_int("video.0:dst_frame_rate_num", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateDen =
			    rk_param_get_int("video.0:src_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateNum =
			    rk_param_get_int("video.0:src_frame_rate_num", -1);
		} else {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;
			venc_chn_attr.stRcAttr.stH265Vbr.u32Gop = rk_param_get_int("video.0:gop", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32BitRate = rk_param_get_int("video.0:mid_rate", 0);
			venc_chn_attr.stRcAttr.stH265Vbr.u32MaxBitRate =
			    rk_param_get_int("video.0:max_rate", 0);
			venc_chn_attr.stRcAttr.stH265Vbr.u32MinBitRate =
			    rk_param_get_int("video.0:min_rate", 0);
			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateDen =
			    rk_param_get_int("video.0:dst_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateNum =
			    rk_param_get_int("video.0:dst_frame_rate_num", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateDen =
			    rk_param_get_int("video.0:src_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateNum =
			    rk_param_get_int("video.0:src_frame_rate_num", -1);
		}
	} else {
		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
		return -1;
	}
	tmp_smart = rk_param_get_string("video.0:smart", NULL);
	tmp_gop_mode = rk_param_get_string("video.0:gop_mode", NULL);
	if (!strcmp(tmp_gop_mode, "normalP")) {
		venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
	} else if (!strcmp(tmp_gop_mode, "smartP")) {
		venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_SMARTP;
		venc_chn_attr.stGopAttr.s32VirIdrLen = rk_param_get_int("video.0:smartp_viridrlen", 25);
		venc_chn_attr.stGopAttr.u32MaxLtrCount = 1; // long-term reference frame ltr is fixed to 1
	} else if (!strcmp(tmp_gop_mode, "tsvc4")) {
		venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_TSVC4;
	}
	// venc_chn_attr.stGopAttr.u32GopSize = rk_param_get_int("video.0:gop", -1);
	if (rk_param_get_int("video.source:0", 0) == 2)
		venc_chn_attr.stVencAttr.enPixelFormat = RK_FMT_YUV422SP;
	else
		venc_chn_attr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
	venc_chn_attr.stVencAttr.u32MaxPicWidth = rk_param_get_int("video.0:max_width", 2560);
	venc_chn_attr.stVencAttr.u32MaxPicHeight = rk_param_get_int("video.0:max_height", 1440);
	venc_chn_attr.stVencAttr.u32PicWidth = video_width;
	venc_chn_attr.stVencAttr.u32PicHeight = video_height;
	venc_chn_attr.stVencAttr.u32VirWidth = video_width;
	venc_chn_attr.stVencAttr.u32VirHeight = video_height;
	venc_chn_attr.stVencAttr.u32StreamBufCnt = rk_param_get_int("video.0:buffer_count", 4);
	venc_chn_attr.stVencAttr.u32BufSize = rk_param_get_int("video.0:buffer_size", 1843200);
	// venc_chn_attr.stVencAttr.u32Depth = 1;
	ret = RK_MPI_VENC_CreateChn(VIDEO_PIPE_0, &venc_chn_attr);
	if (ret) {
		LOG_ERROR("ERROR: create VENC error! ret=%#x\n", ret);
		return -1;
	}
	rk_video_reset_frame_rate(VIDEO_PIPE_0);

	if (!strcmp(tmp_smart, "open"))
		RK_MPI_VENC_EnableSvc(VIDEO_PIPE_0, RK_TRUE);

	if (rk_param_get_int("video.0:enable_motion_deblur", 0)) {
		ret = RK_MPI_VENC_EnableMotionDeblur(VIDEO_PIPE_0, RK_TRUE);
		if (ret)
			LOG_ERROR("RK_MPI_VENC_EnableMotionDeblur error! ret=%#x\n", ret);
	}
	if (rk_param_get_int("video.0:enable_motion_static_switch", 0)) {
		ret = RK_MPI_VENC_EnableMotionStaticSwitch(VIDEO_PIPE_0, RK_TRUE);
		if (ret)
			LOG_ERROR("RK_MPI_VENC_EnableMotionStaticSwitch error! ret=%#x\n", ret);
	}

	// VENC_RC_PARAM_S h265_RcParam;
	// RK_MPI_VENC_GetRcParam(VIDEO_PIPE_0, &h265_RcParam);
	// h265_RcParam.s32FirstFrameStartQp = 26;
	// h265_RcParam.stParamH265.u32StepQp = 8;
	// h265_RcParam.stParamH265.u32MaxQp = 51;
	// h265_RcParam.stParamH265.u32MinQp = 10;
	// h265_RcParam.stParamH265.u32MaxIQp = 46;
	// h265_RcParam.stParamH265.u32MinIQp = 24;
	// h265_RcParam.stParamH265.s32DeltIpQp = -4;
	// RK_MPI_VENC_SetRcParam(VIDEO_PIPE_0, &h265_RcParam);

	tmp_rc_quality = rk_param_get_string("video.0:rc_quality", NULL);
	VENC_RC_PARAM_S venc_rc_param;
	RK_MPI_VENC_GetRcParam(VIDEO_PIPE_0, &venc_rc_param);
	if (!strcmp(tmp_output_data_type, "H.264")) {
		if (!strcmp(tmp_rc_quality, "highest")) {
			venc_rc_param.stParamH264.u32MinQp = 10;
		} else if (!strcmp(tmp_rc_quality, "higher")) {
			venc_rc_param.stParamH264.u32MinQp = 15;
		} else if (!strcmp(tmp_rc_quality, "high")) {
			venc_rc_param.stParamH264.u32MinQp = 20;
		} else if (!strcmp(tmp_rc_quality, "medium")) {
			venc_rc_param.stParamH264.u32MinQp = 25;
		} else if (!strcmp(tmp_rc_quality, "low")) {
			venc_rc_param.stParamH264.u32MinQp = 30;
		} else if (!strcmp(tmp_rc_quality, "lower")) {
			venc_rc_param.stParamH264.u32MinQp = 35;
		} else {
			venc_rc_param.stParamH264.u32MinQp = 40;
		}
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		if (!strcmp(tmp_rc_quality, "highest")) {
			venc_rc_param.stParamH265.u32MinQp = 10;
		} else if (!strcmp(tmp_rc_quality, "higher")) {
			venc_rc_param.stParamH265.u32MinQp = 15;
		} else if (!strcmp(tmp_rc_quality, "high")) {
			venc_rc_param.stParamH265.u32MinQp = 20;
		} else if (!strcmp(tmp_rc_quality, "medium")) {
			venc_rc_param.stParamH265.u32MinQp = 25;
		} else if (!strcmp(tmp_rc_quality, "low")) {
			venc_rc_param.stParamH265.u32MinQp = 30;
		} else if (!strcmp(tmp_rc_quality, "lower")) {
			venc_rc_param.stParamH265.u32MinQp = 35;
		} else {
			venc_rc_param.stParamH265.u32MinQp = 40;
		}
	} else {
		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
		return -1;
	}
	RK_MPI_VENC_SetRcParam(VIDEO_PIPE_0, &venc_rc_param);

	VENC_CHN_BUF_WRAP_S stVencChnBufWrap;
	memset(&stVencChnBufWrap, 0, sizeof(stVencChnBufWrap));
	if (enable_wrap) {
		stVencChnBufWrap.bEnable = enable_wrap;
		stVencChnBufWrap.u32BufLine =
		    rk_param_get_int("video.source:buffer_line", video_max_height);
		if (stVencChnBufWrap.u32BufLine < 128)
			stVencChnBufWrap.u32BufLine = video_max_height;
		RK_MPI_VENC_SetChnBufWrapAttr(VIDEO_PIPE_0, &stVencChnBufWrap);
	}

	VENC_CHN_REF_BUF_SHARE_S stVencChnRefBufShare;
	memset(&stVencChnRefBufShare, 0, sizeof(VENC_CHN_REF_BUF_SHARE_S));
	stVencChnRefBufShare.bEnable = (RK_BOOL)rk_param_get_int("video.0:enable_refer_buffer_share", RK_FALSE);
	RK_MPI_VENC_SetChnRefBufShareAttr(VIDEO_PIPE_0, &stVencChnRefBufShare);
	if (rotation == 0) {
		RK_MPI_VENC_SetChnRotation(VIDEO_PIPE_0, ROTATION_0);
	} else if (rotation == 90) {
		RK_MPI_VENC_SetChnRotation(VIDEO_PIPE_0, ROTATION_90);
	} else if (rotation == 180) {
		RK_MPI_VENC_SetChnRotation(VIDEO_PIPE_0, ROTATION_180);
	} else if (rotation == 270) {
		RK_MPI_VENC_SetChnRotation(VIDEO_PIPE_0, ROTATION_270);
	}

	const char *gray_scale_mode = NULL;
	int video_full_range_flag = 0;
	rk_isp_get_gray_scale_mode(0, &gray_scale_mode);
	if (!strcmp(gray_scale_mode, "[16-235]"))
		video_full_range_flag = 0;
	else
		video_full_range_flag = 1;
	if (!strcmp(tmp_output_data_type, "H.264")) {
		VENC_H264_VUI_S pstH264Vui;
		RK_MPI_VENC_GetH264Vui(VIDEO_PIPE_0, &pstH264Vui);
		pstH264Vui.stVuiVideoSignal.video_full_range_flag = video_full_range_flag;
		RK_MPI_VENC_SetH264Vui(VIDEO_PIPE_0, &pstH264Vui);
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		VENC_H265_VUI_S pstH265Vui;
		RK_MPI_VENC_GetH265Vui(VIDEO_PIPE_0, &pstH265Vui);
		pstH265Vui.stVuiVideoSignal.video_full_range_flag = video_full_range_flag;
		RK_MPI_VENC_SetH265Vui(VIDEO_PIPE_0, &pstH265Vui);
	}

	rkipc_set_advanced_venc_params(VIDEO_PIPE_0);

	VENC_RECV_PIC_PARAM_S stRecvParam;
	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	stRecvParam.s32RecvPicNum = -1;
	RK_MPI_VENC_StartRecvFrame(VIDEO_PIPE_0, &stRecvParam);
	if (strcmp(tmp_output_data_type, "NV12")) {
		venc_thread_0 = std::thread(rkipc_get_venc_0, nullptr);
		// bind
		vi_chn.enModId = RK_ID_VI;
		vi_chn.s32DevId = 0;
		vi_chn.s32ChnId = g_vi_for_venc_0_id;
		venc_chn.enModId = RK_ID_VENC;
		venc_chn.s32DevId = 0;
		venc_chn.s32ChnId = VIDEO_PIPE_0;
		if (!enable_fec) {
			ret = RK_MPI_SYS_Bind(&vi_chn, &venc_chn);
			if (ret)
				LOG_ERROR("Bind VI and VENC error! ret=%#x\n", ret);
		} else {
			ret = RK_MPI_SYS_Bind(&vpss_out_chn[0], &venc_chn);
			if (ret)
				LOG_ERROR("Bind VPSS and VENC error! ret=%#x\n", ret);
		}
	}

	return 0;
}

int rkipc_pipe_0_deinit() {
	int ret;
	// unbind
	vi_chn.enModId = RK_ID_VI;
	vi_chn.s32DevId = 0;
	vi_chn.s32ChnId = g_vi_for_venc_0_id;
	venc_chn.enModId = RK_ID_VENC;
	venc_chn.s32DevId = 0;
	venc_chn.s32ChnId = VIDEO_PIPE_0;
	if (!enable_fec) {
		ret = RK_MPI_SYS_UnBind(&vi_chn, &venc_chn);
		if (ret)
			LOG_ERROR("Unbind VI and VENC error! ret=%#x\n", ret);
	} else {
		ret = RK_MPI_SYS_UnBind(&vpss_out_chn[0], &venc_chn);
		if (ret)
			LOG_ERROR("Unbind VPSS and VENC error! ret=%#x\n", ret);
	}
	// VENC
	ret = RK_MPI_VENC_StopRecvFrame(VIDEO_PIPE_0);
	ret |= RK_MPI_VENC_DestroyChn(VIDEO_PIPE_0);
	if (ret)
		LOG_ERROR("ERROR: Destroy VENC error! ret=%#x\n", ret);
	else
		LOG_DEBUG("RK_MPI_VENC_DestroyChn success\n");

	return 0;
}

int rkipc_pipe_1_init() {
	int ret;
	int video_width = rk_param_get_int("video.1:width", 1920);
	int video_height = rk_param_get_int("video.1:height", 1080);
	int rotation = rk_param_get_int("video.source:rotation", 0);

	// VENC
	VENC_CHN_ATTR_S venc_chn_attr;
	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
	tmp_output_data_type = rk_param_get_string("video.1:output_data_type", NULL);
	tmp_rc_mode = rk_param_get_string("video.1:rc_mode", NULL);
	tmp_h264_profile = rk_param_get_string("video.1:h264_profile", NULL);
	if ((tmp_output_data_type == NULL) || (tmp_rc_mode == NULL)) {
		LOG_ERROR("tmp_output_data_type or tmp_rc_mode is NULL\n");
		return -1;
	}
	LOG_DEBUG("tmp_output_data_type is %s, tmp_rc_mode is %s, tmp_h264_profile is %s\n",
	          tmp_output_data_type, tmp_rc_mode, tmp_h264_profile);
	if (!strcmp(tmp_output_data_type, "H.264")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_AVC;
		if (!strcmp(tmp_h264_profile, "high"))
			venc_chn_attr.stVencAttr.u32Profile = 100;
		else if (!strcmp(tmp_h264_profile, "main"))
			venc_chn_attr.stVencAttr.u32Profile = 77;
		else if (!strcmp(tmp_h264_profile, "baseline"))
			venc_chn_attr.stVencAttr.u32Profile = 66;
		else
			LOG_ERROR("tmp_h264_profile is %s\n", tmp_h264_profile);
		if (!strcmp(tmp_rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
			venc_chn_attr.stRcAttr.stH264Cbr.u32Gop = rk_param_get_int("video.1:gop", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate = rk_param_get_int("video.1:max_rate", 0);
			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen =
			    rk_param_get_int("video.1:dst_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum =
			    rk_param_get_int("video.1:dst_frame_rate_num", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen =
			    rk_param_get_int("video.1:src_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum =
			    rk_param_get_int("video.1:src_frame_rate_num", -1);
		} else {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
			venc_chn_attr.stRcAttr.stH264Vbr.u32Gop = rk_param_get_int("video.1:gop", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32BitRate = rk_param_get_int("video.1:mid_rate", 0);
			venc_chn_attr.stRcAttr.stH264Vbr.u32MaxBitRate =
			    rk_param_get_int("video.1:max_rate", 0);
			venc_chn_attr.stRcAttr.stH264Vbr.u32MinBitRate =
			    rk_param_get_int("video.1:min_rate", 0);
			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateDen =
			    rk_param_get_int("video.1:dst_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateNum =
			    rk_param_get_int("video.1:dst_frame_rate_num", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateDen =
			    rk_param_get_int("video.1:src_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateNum =
			    rk_param_get_int("video.1:src_frame_rate_num", -1);
		}
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_HEVC;
		if (!strcmp(tmp_rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
			venc_chn_attr.stRcAttr.stH265Cbr.u32Gop = rk_param_get_int("video.1:gop", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate = rk_param_get_int("video.1:max_rate", 0);
			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen =
			    rk_param_get_int("video.1:dst_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum =
			    rk_param_get_int("video.1:dst_frame_rate_num", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateDen =
			    rk_param_get_int("video.1:src_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateNum =
			    rk_param_get_int("video.1:src_frame_rate_num", -1);
		} else {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;
			venc_chn_attr.stRcAttr.stH265Vbr.u32Gop = rk_param_get_int("video.1:gop", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32BitRate = rk_param_get_int("video.1:mid_rate", 0);
			venc_chn_attr.stRcAttr.stH265Vbr.u32MaxBitRate =
			    rk_param_get_int("video.1:max_rate", 0);
			venc_chn_attr.stRcAttr.stH265Vbr.u32MinBitRate =
			    rk_param_get_int("video.1:min_rate", 0);
			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateDen =
			    rk_param_get_int("video.1:dst_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateNum =
			    rk_param_get_int("video.1:dst_frame_rate_num", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateDen =
			    rk_param_get_int("video.1:src_frame_rate_den", -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateNum =
			    rk_param_get_int("video.1:src_frame_rate_num", -1);
		}
	} else {
		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
		return -1;
	}
	tmp_smart = rk_param_get_string("video.1:smart", NULL);
	tmp_gop_mode = rk_param_get_string("video.1:gop_mode", NULL);
	if (!strcmp(tmp_gop_mode, "normalP")) {
		venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
	} else if (!strcmp(tmp_gop_mode, "smartP")) {
		venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_SMARTP;
		venc_chn_attr.stGopAttr.s32VirIdrLen = rk_param_get_int("video.1:smartp_viridrlen", 25);
		venc_chn_attr.stGopAttr.u32MaxLtrCount = 1; // long-term reference frame ltr is fixed to 1
	} else if (!strcmp(tmp_gop_mode, "tsvc4")) {
		venc_chn_attr.stGopAttr.enGopMode = VENC_GOPMODE_TSVC4;
	}
	// venc_chn_attr.stGopAttr.u32GopSize = rk_param_get_int("video.1:gop", -1);

	venc_chn_attr.stVencAttr.u32MaxPicWidth = rk_param_get_int("video.1:max_width", 704);
	venc_chn_attr.stVencAttr.u32MaxPicHeight = rk_param_get_int("video.1:max_height", 576);
	venc_chn_attr.stVencAttr.u32PicWidth = video_width;
	venc_chn_attr.stVencAttr.u32PicHeight = video_height;
	venc_chn_attr.stVencAttr.u32VirWidth = video_width;
	venc_chn_attr.stVencAttr.u32VirHeight = video_height;
	venc_chn_attr.stVencAttr.u32StreamBufCnt = rk_param_get_int("video.1:buffer_count", 4);
	venc_chn_attr.stVencAttr.u32BufSize = rk_param_get_int("video.1:buffer_size", 202752);
	// venc_chn_attr.stVencAttr.u32Depth = 1;
	if (rk_param_get_int("video.source:1", 0) == 2)
		venc_chn_attr.stVencAttr.enPixelFormat = RK_FMT_YUV422SP;
	else
		venc_chn_attr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
	ret = RK_MPI_VENC_CreateChn(VIDEO_PIPE_1, &venc_chn_attr);
	if (ret) {
		LOG_ERROR("ERROR: create VENC error! ret=%#x\n", ret);
		return -1;
	}
	rk_video_reset_frame_rate(VIDEO_PIPE_1);

	if (!strcmp(tmp_smart, "open"))
		RK_MPI_VENC_EnableSvc(VIDEO_PIPE_1, RK_TRUE);

	tmp_rc_quality = rk_param_get_string("video.1:rc_quality", NULL);
	VENC_RC_PARAM_S venc_rc_param;
	RK_MPI_VENC_GetRcParam(VIDEO_PIPE_1, &venc_rc_param);
	if (!strcmp(tmp_output_data_type, "H.264")) {
		if (!strcmp(tmp_rc_quality, "highest")) {
			venc_rc_param.stParamH264.u32MinQp = 10;
		} else if (!strcmp(tmp_rc_quality, "higher")) {
			venc_rc_param.stParamH264.u32MinQp = 15;
		} else if (!strcmp(tmp_rc_quality, "high")) {
			venc_rc_param.stParamH264.u32MinQp = 20;
		} else if (!strcmp(tmp_rc_quality, "medium")) {
			venc_rc_param.stParamH264.u32MinQp = 25;
		} else if (!strcmp(tmp_rc_quality, "low")) {
			venc_rc_param.stParamH264.u32MinQp = 30;
		} else if (!strcmp(tmp_rc_quality, "lower")) {
			venc_rc_param.stParamH264.u32MinQp = 35;
		} else {
			venc_rc_param.stParamH264.u32MinQp = 40;
		}
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		if (!strcmp(tmp_rc_quality, "highest")) {
			venc_rc_param.stParamH265.u32MinQp = 10;
		} else if (!strcmp(tmp_rc_quality, "higher")) {
			venc_rc_param.stParamH265.u32MinQp = 15;
		} else if (!strcmp(tmp_rc_quality, "high")) {
			venc_rc_param.stParamH265.u32MinQp = 20;
		} else if (!strcmp(tmp_rc_quality, "medium")) {
			venc_rc_param.stParamH265.u32MinQp = 25;
		} else if (!strcmp(tmp_rc_quality, "low")) {
			venc_rc_param.stParamH265.u32MinQp = 30;
		} else if (!strcmp(tmp_rc_quality, "lower")) {
			venc_rc_param.stParamH265.u32MinQp = 35;
		} else {
			venc_rc_param.stParamH265.u32MinQp = 40;
		}
	} else {
		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
		return -1;
	}
	RK_MPI_VENC_SetRcParam(VIDEO_PIPE_1, &venc_rc_param);

	VENC_CHN_REF_BUF_SHARE_S stVencChnRefBufShare;
	memset(&stVencChnRefBufShare, 0, sizeof(VENC_CHN_REF_BUF_SHARE_S));
	stVencChnRefBufShare.bEnable = (RK_BOOL)rk_param_get_int("video.1:enable_refer_buffer_share", RK_FALSE);
	RK_MPI_VENC_SetChnRefBufShareAttr(VIDEO_PIPE_1, &stVencChnRefBufShare);
	if (rotation == 0) {
		RK_MPI_VENC_SetChnRotation(VIDEO_PIPE_1, ROTATION_0);
	} else if (rotation == 90) {
		RK_MPI_VENC_SetChnRotation(VIDEO_PIPE_1, ROTATION_90);
	} else if (rotation == 180) {
		RK_MPI_VENC_SetChnRotation(VIDEO_PIPE_1, ROTATION_180);
	} else if (rotation == 270) {
		RK_MPI_VENC_SetChnRotation(VIDEO_PIPE_1, ROTATION_270);
	}

	const char *gray_scale_mode = NULL;
	int video_full_range_flag = 0;
	rk_isp_get_gray_scale_mode(0, &gray_scale_mode);
	if (!strcmp(gray_scale_mode, "[16-235]"))
		video_full_range_flag = 0;
	else
		video_full_range_flag = 1;
	if (!strcmp(tmp_output_data_type, "H.264")) {
		VENC_H264_VUI_S pstH264Vui;
		RK_MPI_VENC_GetH264Vui(VIDEO_PIPE_1, &pstH264Vui);
		pstH264Vui.stVuiVideoSignal.video_full_range_flag = video_full_range_flag;
		RK_MPI_VENC_SetH264Vui(VIDEO_PIPE_1, &pstH264Vui);
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		VENC_H265_VUI_S pstH265Vui;
		RK_MPI_VENC_GetH265Vui(VIDEO_PIPE_1, &pstH265Vui);
		pstH265Vui.stVuiVideoSignal.video_full_range_flag = video_full_range_flag;
		RK_MPI_VENC_SetH265Vui(VIDEO_PIPE_1, &pstH265Vui);
	}

	rkipc_set_advanced_venc_params(VIDEO_PIPE_1);

	VENC_RECV_PIC_PARAM_S stRecvParam;
	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	stRecvParam.s32RecvPicNum = -1;
	RK_MPI_VENC_StartRecvFrame(VIDEO_PIPE_1, &stRecvParam);
	venc_thread_1 = std::thread(rkipc_get_venc_1, nullptr);

	// pthread_create(&vi_thread_1, NULL, rkipc_get_vi_draw_send_venc, NULL);
	// bind
	vi_chn.enModId = RK_ID_VI;
	vi_chn.s32DevId = 0;
	vi_chn.s32ChnId = g_vi_for_venc_1_id;
	venc_chn.enModId = RK_ID_VENC;
	venc_chn.s32DevId = 0;
	venc_chn.s32ChnId = VIDEO_PIPE_1;
	if (!enable_fec) {
		ret = RK_MPI_SYS_Bind(&vi_chn, &venc_chn);
		if (ret)
			LOG_ERROR("Bind VI and VENC error! ret=%#x\n", ret);
	} else {
		ret = RK_MPI_SYS_Bind(&vpss_out_chn[1], &venc_chn);
		if (ret)
			LOG_ERROR("Bind VPSS and VENC error! ret=%#x\n", ret);
	}

	return 0;
}

int rkipc_pipe_1_deinit() {
	int ret;
	// unbind
	vi_chn.enModId = RK_ID_VI;
	vi_chn.s32DevId = 0;
	vi_chn.s32ChnId = g_vi_for_venc_1_id;
	venc_chn.enModId = RK_ID_VENC;
	venc_chn.s32DevId = 0;
	venc_chn.s32ChnId = VIDEO_PIPE_1;
	if (!enable_fec) {
		ret = RK_MPI_SYS_UnBind(&vi_chn, &venc_chn);
		if (ret)
			LOG_ERROR("Unbind VI and VENC error! ret=%#x\n", ret);
	} else {
		ret = RK_MPI_SYS_UnBind(&vpss_out_chn[1], &venc_chn);
		if (ret)
			LOG_ERROR("Unbind VPSS and VENC error! ret=%#x\n", ret);
	}
	// VENC
	ret = RK_MPI_VENC_StopRecvFrame(VIDEO_PIPE_1);
	ret |= RK_MPI_VENC_DestroyChn(VIDEO_PIPE_1);
	if (ret)
		LOG_ERROR("ERROR: Destroy VENC error! ret=%#x\n", ret);
	else
		LOG_DEBUG("RK_MPI_VENC_DestroyChn success\n");

	return 0;
}

static void *rkipc_get_vi_2_send(void *arg) {
	LOG_DEBUG("#Start %s thread, arg:%p\n", __func__, arg);
	int width = rk_param_get_int("video.0:width", -1);
	int height = rk_param_get_int("video.0:height", -1);
	prctl(PR_SET_NAME, "RkipcGetVi2", 0, 0, 0);
	int ret;
	int32_t loopCount = 0;
	VIDEO_FRAME_INFO_S stViFrame;
	int npu_cycle_time_ms = 1000 / rk_param_get_int("video.source:npu_fps", 10);

  // 初始化
  Yolo26 yolo;
	std::vector<Detection> objects;
  // 加载模型
  yolo.LoadModel("./yolo26s.rknn");

	long long before_time, cost_time;
	while (g_video_run_) {
		before_time = rkipc_get_curren_time_ms();
		if (!enable_fec)
			ret = RK_MPI_VI_GetChnFrame(pipe_id_, g_vi_for_npu_ivs_id, &stViFrame, 1000);
		else
			ret = RK_MPI_VPSS_GetChnFrame(0, 2, &stViFrame, 1000);
		if (ret == RK_SUCCESS) {
			void *data = RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk);

			// 1126b 32bit rga only support fd

			int32_t fd = RK_MPI_MB_Handle2Fd(stViFrame.stVFrame.pMbBlk);
			cv::Mat src_img = cv::Mat::zeros(height, width, CV_8UC3);

			rga_buffer_t yuv_buffer = wrapbuffer_fd(fd, width, height, RK_FORMAT_YCbCr_420_SP, width, height);
			rga_buffer_t rgb_buffer = wrapbuffer_virtualaddr((void*)src_img.data, width, height, RK_FORMAT_RGB_888, width, height);

			ret = imcheck(yuv_buffer, rgb_buffer, {}, {});

			if (ret != IM_STATUS_NOERROR) {
				LOG_ERROR("%d imcheck fail %s \n", ret, imStrError((IM_STATUS)ret));
			}

			imflip(yuv_buffer, rgb_buffer, IM_HAL_TRANSFORM_FLIP_H);
			if (ret != IM_STATUS_NOERROR) {
				LOG_ERROR("%d imcheck fail %s \n", ret, imStrError((IM_STATUS)ret));
			}


			yolo.Run(src_img, objects);
    	// 绘制框，显示结果
      DrawDetections(src_img, objects);

			imcopy(rgb_buffer, yuv_buffer);
			RK_MPI_VENC_SendFrame(VIDEO_PIPE_0, &stViFrame, 1000);

			if (!enable_fec)
				ret = RK_MPI_VI_ReleaseChnFrame(pipe_id_, g_vi_for_npu_ivs_id, &stViFrame);
			else
				ret = RK_MPI_VPSS_ReleaseChnFrame(0, 2, &stViFrame);
			if (ret != RK_SUCCESS)
				LOG_ERROR("RK_MPI_VI or VPSS_ReleaseChnFrame fail %x\n", ret);
			loopCount++;
		} else {
			LOG_ERROR("RK_MPI_VI or VPSS_GetChnFrame timeout %x\n", ret);
			sleep(1);
		}
		cost_time = rkipc_get_curren_time_ms() - before_time;
		if ((cost_time > 0) && (cost_time < npu_cycle_time_ms))
			usleep((npu_cycle_time_ms - cost_time) * 1000);
	}
	return NULL;
}

static void *rkipc_ivs_get_results(void *arg) {
	LOG_DEBUG("#Start %s thread, arg:%p\n", __func__, arg);
	prctl(PR_SET_NAME, "RkipcGetIVS", 0, 0, 0);
	int ret, i;
	IVS_RESULT_INFO_S stResults;
	int resultscount = 0;
	int count = 0;
	int md = rk_param_get_int("ivs:md", 0);
	int od = rk_param_get_int("ivs:od", 0);
	int width = rk_param_get_int("video.2:width", 960);
	int height = rk_param_get_int("video.2:height", 540);
	int md_area_threshold = width * height * 0.3;

	while (g_video_run_) {
		ret = RK_MPI_IVS_GetResults(0, &stResults, 1000);
		if (ret >= 0) {
			resultscount++;
			if (md == 1) {
				if (stResults.pstResults->stMdInfo.u32Square > md_area_threshold) {
					LOG_INFO("MD: md_area is %d, md_area_threshold is %d\n",
					         stResults.pstResults->stMdInfo.u32Square, md_area_threshold);
				}
			}
			if (od == 1) {
				if (stResults.s32ResultNum > 0) {
					if (stResults.pstResults->stOdInfo.u32Flag)
						LOG_INFO("OD flag:%d\n", stResults.pstResults->stOdInfo.u32Flag);
				}
			}
			RK_MPI_IVS_ReleaseResults(0, &stResults);
		} else {
			LOG_ERROR("get chn %d fail %d\n", 0, ret);
			usleep(50000llu);
		}
	}
	return NULL;
}

static void *rkipc_cycle_snapshot(void *arg) {
	LOG_INFO("start %s thread, arg:%p\n", __func__, arg);
	prctl(PR_SET_NAME, "RkipcCycleSnapshot", 0, 0, 0);

	while (g_video_run_ && cycle_snapshot_flag) {
		usleep(rk_param_get_int("video.jpeg:snapshot_interval_ms", 1000) * 1000);
		rk_take_photo();
	}
	LOG_INFO("exit %s thread, arg:%p\n", __func__, arg);

	return 0;
}

int rkipc_pipe_jpeg_init() {
	// jpeg resolution same to video.0
	int ret;
	int video_width = rk_param_get_int("video.jpeg:width", 1920);
	int video_height = rk_param_get_int("video.jpeg:height", 1080);
	int video_max_height = rk_param_get_int("video.0:max_height", -1);
	int rotation = rk_param_get_int("video.source:rotation", 0);
	// VENC[3] init
	VENC_CHN_ATTR_S jpeg_chn_attr;
	memset(&jpeg_chn_attr, 0, sizeof(jpeg_chn_attr));
	jpeg_chn_attr.stVencAttr.enType = RK_VIDEO_ID_JPEG;
	jpeg_chn_attr.stVencAttr.enPixelFormat = RK_FMT_YUV420SP;
	jpeg_chn_attr.stVencAttr.u32MaxPicWidth = rk_param_get_int("video.0:max_width", 2560);
	jpeg_chn_attr.stVencAttr.u32MaxPicHeight = rk_param_get_int("video.0:max_height", 1440);
	jpeg_chn_attr.stVencAttr.u32PicWidth = video_width;
	jpeg_chn_attr.stVencAttr.u32PicHeight = video_height;
	jpeg_chn_attr.stVencAttr.u32VirWidth = video_width;
	jpeg_chn_attr.stVencAttr.u32VirHeight = video_height;
	jpeg_chn_attr.stVencAttr.u32StreamBufCnt = 2;
	jpeg_chn_attr.stVencAttr.u32BufSize = rk_param_get_int("video.jpeg:jpeg_buffer_size", 204800);
	// jpeg_chn_attr.stVencAttr.u32Depth = 1;
	ret = RK_MPI_VENC_CreateChn(JPEG_VENC_CHN, &jpeg_chn_attr);
	if (ret) {
		LOG_ERROR("ERROR: create VENC error! ret=%d\n", ret);
		return -1;
	}
	VENC_JPEG_PARAM_S stJpegParam;
	memset(&stJpegParam, 0, sizeof(stJpegParam));
	stJpegParam.u32Qfactor = rk_param_get_int("video.jpeg:jpeg_qfactor", 70);
	RK_MPI_VENC_SetJpegParam(JPEG_VENC_CHN, &stJpegParam);
	if (rotation == 0) {
		RK_MPI_VENC_SetChnRotation(JPEG_VENC_CHN, ROTATION_0);
	} else if (rotation == 90) {
		RK_MPI_VENC_SetChnRotation(JPEG_VENC_CHN, ROTATION_90);
	} else if (rotation == 180) {
		RK_MPI_VENC_SetChnRotation(JPEG_VENC_CHN, ROTATION_180);
	} else if (rotation == 270) {
		RK_MPI_VENC_SetChnRotation(JPEG_VENC_CHN, ROTATION_270);
	}

	VENC_CHN_BUF_WRAP_S stVencChnBufWrap;
	memset(&stVencChnBufWrap, 0, sizeof(stVencChnBufWrap));
	if (enable_wrap) {
		stVencChnBufWrap.bEnable = enable_wrap;
		stVencChnBufWrap.u32BufLine =
		    rk_param_get_int("video.source:buffer_line", video_max_height / 4);
		if (stVencChnBufWrap.u32BufLine < 128)
			stVencChnBufWrap.u32BufLine = video_max_height;
		RK_MPI_VENC_SetChnBufWrapAttr(JPEG_VENC_CHN, &stVencChnBufWrap);
	}

	VENC_COMBO_ATTR_S stComboAttr;
	memset(&stComboAttr, 0, sizeof(VENC_COMBO_ATTR_S));
	stComboAttr.bEnable = RK_TRUE;
	stComboAttr.s32ChnId = VIDEO_PIPE_0;
	RK_MPI_VENC_SetComboAttr(JPEG_VENC_CHN, &stComboAttr);

	VENC_RECV_PIC_PARAM_S stRecvParam;
	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	stRecvParam.s32RecvPicNum = 1;
	RK_MPI_VENC_StartRecvFrame(JPEG_VENC_CHN,
	                           &stRecvParam); // must, for no streams callback running failed

	jpeg_venc_thread_id = std::thread(rkipc_get_jpeg, nullptr);
	if (rk_param_get_int("video.jpeg:enable_cycle_snapshot", 0)) {
		cycle_snapshot_flag = 1;
		cycle_snapshot_thread_id = std::thread(rkipc_cycle_snapshot, nullptr);
	}

	return ret;
}

int rkipc_pipe_jpeg_deinit() {
	int ret = 0;
	ret = RK_MPI_VENC_StopRecvFrame(JPEG_VENC_CHN);
	ret |= RK_MPI_VENC_DestroyChn(JPEG_VENC_CHN);
	if (ret)
		LOG_ERROR("ERROR: Destroy VENC error! ret=%#x\n", ret);
	else
		LOG_INFO("RK_MPI_VENC_DestroyChn success\n");

	return ret;
}

int rkipc_pipe_vi_vo_init() {
	int ret = 0;

	// VO init
	VO_PUB_ATTR_S VoPubAttr;
	VO_VIDEO_LAYER_ATTR_S stLayerAttr;
	VO_CSC_S VideoCSC;
	VO_CHN_ATTR_S VoChnAttr;
	RK_U32 u32DispBufLen;
	memset(&VoPubAttr, 0, sizeof(VO_PUB_ATTR_S));
	memset(&stLayerAttr, 0, sizeof(VO_VIDEO_LAYER_ATTR_S));
	memset(&VideoCSC, 0, sizeof(VO_CSC_S));
	memset(&VoChnAttr, 0, sizeof(VoChnAttr));

	VoPubAttr.enIntfType = VO_INTF_MIPI;
	VoPubAttr.enIntfSync = VO_OUTPUT_DEFAULT;

	ret = RK_MPI_VO_SetPubAttr(g_vo_dev_id, &VoPubAttr);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VO_SetPubAttr %x\n", ret);
		return ret;
	}
	LOG_INFO("RK_MPI_VO_SetPubAttr success\n");

	ret = RK_MPI_VO_Enable(g_vo_dev_id);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VO_Enable err is %x\n", ret);
		return ret;
	}
	LOG_INFO("RK_MPI_VO_Enable success\n");

	ret = RK_MPI_VO_GetLayerDispBufLen(g_vo_layer_id, &u32DispBufLen);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("Get display buf len failed with error code %d!\n", ret);
		return ret;
	}
	LOG_INFO("Get g_vo_layer_id %d disp buf len is %d.\n", g_vo_layer_id, u32DispBufLen);
	u32DispBufLen = 2;
	ret = RK_MPI_VO_SetLayerDispBufLen(g_vo_layer_id, u32DispBufLen);
	if (ret != RK_SUCCESS) {
		return ret;
	}
	LOG_INFO("Agin Get g_vo_layer_id %d disp buf len is %d.\n", g_vo_layer_id, u32DispBufLen);

	/* get vo attribute*/
	ret = RK_MPI_VO_GetPubAttr(g_vo_dev_id, &VoPubAttr);
	if (ret) {
		LOG_ERROR("RK_MPI_VO_GetPubAttr fail!\n");
		return ret;
	}
	LOG_INFO("RK_MPI_VO_GetPubAttr success\n");
	if ((VoPubAttr.stSyncInfo.u16Hact == 0) || (VoPubAttr.stSyncInfo.u16Vact == 0)) {
		VoPubAttr.stSyncInfo.u16Hact = 1080;
		VoPubAttr.stSyncInfo.u16Vact = 1920;
	}

	stLayerAttr.stDispRect.s32X = 0;
	stLayerAttr.stDispRect.s32Y = 0;
	stLayerAttr.stDispRect.u32Width = VoPubAttr.stSyncInfo.u16Hact;
	stLayerAttr.stDispRect.u32Height = VoPubAttr.stSyncInfo.u16Vact;
	stLayerAttr.stImageSize.u32Width = VoPubAttr.stSyncInfo.u16Hact;
	stLayerAttr.stImageSize.u32Height = VoPubAttr.stSyncInfo.u16Vact;
	LOG_INFO("stLayerAttr W=%d, H=%d, format is %d\n", stLayerAttr.stDispRect.u32Width,
	         stLayerAttr.stDispRect.u32Height, stLayerAttr.enPixFormat);

	stLayerAttr.u32DispFrmRt = 30;
	stLayerAttr.enPixFormat = RK_FMT_RGB888;
	VideoCSC.enCscMatrix = VO_CSC_MATRIX_IDENTITY;
	VideoCSC.u32Contrast = 50;
	VideoCSC.u32Hue = 50;
	VideoCSC.u32Luma = 50;
	VideoCSC.u32Satuature = 50;
	RK_S32 u32VoChn = 0;

	/*bind layer0 to device hd0*/
	ret = RK_MPI_VO_BindLayer(g_vo_layer_id, g_vo_dev_id, VO_LAYER_MODE_GRAPHIC);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VO_BindLayer g_vo_layer_id = %d error\n", g_vo_layer_id);
		return ret;
	}
	LOG_INFO("RK_MPI_VO_BindLayer success\n");

	ret = RK_MPI_VO_SetLayerAttr(g_vo_layer_id, &stLayerAttr);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VO_SetLayerAttr g_vo_layer_id = %d error\n", g_vo_layer_id);
		return ret;
	}
	LOG_INFO("RK_MPI_VO_SetLayerAttr success\n");

	ret = RK_MPI_VO_SetLayerSpliceMode(g_vo_layer_id, VO_SPLICE_MODE_RGA);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VO_SetLayerSpliceMode g_vo_layer_id = %d error\n", g_vo_layer_id);
		return ret;
	}
	LOG_INFO("RK_MPI_VO_SetLayerSpliceMode success\n");

	ret = RK_MPI_VO_EnableLayer(g_vo_layer_id);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VO_EnableLayer g_vo_layer_id = %d error\n", g_vo_layer_id);
		return ret;
	}
	LOG_INFO("RK_MPI_VO_EnableLayer success\n");

	ret = RK_MPI_VO_SetLayerCSC(g_vo_layer_id, &VideoCSC);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VO_SetLayerCSC error\n");
		return ret;
	}
	LOG_INFO("RK_MPI_VO_SetLayerCSC success\n");

	VoChnAttr.bDeflicker = RK_FALSE;
	VoChnAttr.u32Priority = 1;
	VoChnAttr.stRect.s32X = 0;
	VoChnAttr.stRect.s32Y = 0;
	VoChnAttr.stRect.u32Width = stLayerAttr.stDispRect.u32Width;
	VoChnAttr.stRect.u32Height = stLayerAttr.stDispRect.u32Height;
	if (VoPubAttr.enIntfType == VO_INTF_MIPI)
		VoChnAttr.enRotation = ROTATION_90;
	ret = RK_MPI_VO_SetChnAttr(g_vo_layer_id, 0, &VoChnAttr);

	ret = RK_MPI_VO_EnableChn(g_vo_layer_id, u32VoChn);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("create %d layer %d ch vo failed!\n", g_vo_layer_id, u32VoChn);
		return ret;
	}
	LOG_INFO("RK_MPI_VO_EnableChn success\n");

	vi_for_vo_chn.enModId = RK_ID_VI;
	vi_for_vo_chn.s32DevId = 0;
	vi_for_vo_chn.s32ChnId = g_vi_for_vo_chn_id;

	vo_chn.enModId = RK_ID_VO;
	vo_chn.s32DevId = g_vo_layer_id;
	vo_chn.s32ChnId = 0;

	if (!enable_fec) {
		ret = RK_MPI_SYS_Bind(&vi_for_vo_chn, &vo_chn);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("vi and vo bind error! ret=%#x\n", ret);
			return ret;
		}
	} else {
		ret = RK_MPI_SYS_Bind(&vpss_out_chn[3], &vo_chn);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("vpss and vo bind error! ret=%#x\n", ret);
			return ret;
		}
	}

	return 0;
}

int rkipc_pipe_vi_vo_deinit() {
	int ret;
	RK_S32 u32VoChn = 0;
	if (!enable_fec) {
		ret = RK_MPI_SYS_UnBind(&vi_for_vo_chn, &vo_chn);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("vi and vo unbind error! ret=%#x\n", ret);
			return ret;
		}
	} else {
		ret = RK_MPI_SYS_UnBind(&vpss_out_chn[3], &vo_chn);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("vpss and vo unbind error! ret=%#x\n", ret);
			return ret;
		}
	}

	ret = RK_MPI_VO_DisableChn(g_vo_layer_id, u32VoChn);
	if (ret) {
		LOG_ERROR("RK_MPI_VO_DisableChn failed, ret is %#x\n", ret);
		return -1;
	}
	ret = RK_MPI_VO_DisableLayer(g_vo_layer_id);
	if (ret) {
		LOG_ERROR("RK_MPI_VO_DisableLayer failed, ret is %#x\n", ret);
		return -1;
	}
	ret = RK_MPI_VO_Disable(g_vo_dev_id);
	if (ret) {
		LOG_ERROR("RK_MPI_VO_Disable failed, ret is %#x\n", ret);
		return -1;
	}
	ret = RK_MPI_VO_UnBindLayer(g_vo_layer_id, g_vo_dev_id);
	if (ret) {
		LOG_ERROR("RK_MPI_VO_UnBindLayer failed, ret is %#x\n", ret);
		return -1;
	}
	ret = RK_MPI_VO_CloseFd();
	if (ret) {
		LOG_ERROR("RK_MPI_VO_CloseFd failed, ret is %#x\n", ret);
		return -1;
	}

	return 0;
}

// export API
int rk_video_get_gop(int stream_id, int *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:gop", stream_id);
	*value = rk_param_get_int(entry, -1);

	return 0;
}

int rk_video_set_gop(int stream_id, int value) {
	char entry[128] = {'\0'};
	VENC_CHN_ATTR_S venc_chn_attr;
	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
	RK_MPI_VENC_GetChnAttr(stream_id, &venc_chn_attr);
	snprintf(entry, 127, "video.%d:output_data_type", stream_id);
	tmp_output_data_type = rk_param_get_string(entry, "H.264");
	snprintf(entry, 127, "video.%d:rc_mode", stream_id);
	tmp_rc_mode = rk_param_get_string(entry, "CBR");
	if (!strcmp(tmp_output_data_type, "H.264")) {
		if (!strcmp(tmp_rc_mode, "CBR"))
			venc_chn_attr.stRcAttr.stH264Cbr.u32Gop = value;
		else
			venc_chn_attr.stRcAttr.stH264Vbr.u32Gop = value;
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		if (!strcmp(tmp_rc_mode, "CBR"))
			venc_chn_attr.stRcAttr.stH265Cbr.u32Gop = value;
		else
			venc_chn_attr.stRcAttr.stH265Vbr.u32Gop = value;
	} else {
		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
		return -1;
	}
	RK_MPI_VENC_SetChnAttr(stream_id, &venc_chn_attr);
	snprintf(entry, 127, "video.%d:gop", stream_id);
	rk_param_set_int(entry, value);

	return 0;
}

int rk_video_get_max_rate(int stream_id, int *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:max_rate", stream_id);
	*value = rk_param_get_int(entry, -1);

	return 0;
}

int rk_video_set_max_rate(int stream_id, int value) {
	VENC_CHN_ATTR_S venc_chn_attr;
	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
	RK_MPI_VENC_GetChnAttr(stream_id, &venc_chn_attr);
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:output_data_type", stream_id);
	tmp_output_data_type = rk_param_get_string(entry, "H.264");
	snprintf(entry, 127, "video.%d:rc_mode", stream_id);
	tmp_rc_mode = rk_param_get_string(entry, "CBR");
	if (!strcmp(tmp_output_data_type, "H.264")) {
		if (!strcmp(tmp_rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate = value;
		} else {
			venc_chn_attr.stRcAttr.stH264Vbr.u32MinBitRate = value / 3;
			venc_chn_attr.stRcAttr.stH264Vbr.u32BitRate = value / 3 * 2;
			venc_chn_attr.stRcAttr.stH264Vbr.u32MaxBitRate = value;
		}
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		if (!strcmp(tmp_rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate = value;
		} else {
			venc_chn_attr.stRcAttr.stH265Vbr.u32MinBitRate = value / 3;
			venc_chn_attr.stRcAttr.stH265Vbr.u32BitRate = value / 3 * 2;
			venc_chn_attr.stRcAttr.stH265Vbr.u32MaxBitRate = value;
		}
	} else {
		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
		return -1;
	}
	RK_MPI_VENC_SetChnAttr(stream_id, &venc_chn_attr);
	snprintf(entry, 127, "video.%d:max_rate", stream_id);
	rk_param_set_int(entry, value);
	snprintf(entry, 127, "video.%d:mid_rate", stream_id);
	rk_param_set_int(entry, value / 3 * 2);
	snprintf(entry, 127, "video.%d:min_rate", stream_id);
	rk_param_set_int(entry, value / 3);

	return 0;
}

int rk_video_get_RC_mode(int stream_id, const char **value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:rc_mode", stream_id);
	*value = rk_param_get_string(entry, "CBR");

	return 0;
}

int rk_video_set_RC_mode(int stream_id, const char *value) {
	char entry_output_data_type[128] = {'\0'};
	char entry_gop[128] = {'\0'};
	char entry_max_rate[128] = {'\0'};
	char entry_dst_frame_rate_den[128] = {'\0'};
	char entry_dst_frame_rate_num[128] = {'\0'};
	char entry_src_frame_rate_den[128] = {'\0'};
	char entry_src_frame_rate_num[128] = {'\0'};
	char entry_rc_mode[128] = {'\0'};
	snprintf(entry_output_data_type, 127, "video.%d:output_data_type", stream_id);
	snprintf(entry_gop, 127, "video.%d:gop", stream_id);
	snprintf(entry_max_rate, 127, "video.%d:max_rate", stream_id);
	snprintf(entry_dst_frame_rate_den, 127, "video.%d:dst_frame_rate_den", stream_id);
	snprintf(entry_dst_frame_rate_num, 127, "video.%d:dst_frame_rate_num", stream_id);
	snprintf(entry_src_frame_rate_den, 127, "video.%d:src_frame_rate_den", stream_id);
	snprintf(entry_src_frame_rate_num, 127, "video.%d:src_frame_rate_num", stream_id);
	snprintf(entry_rc_mode, 127, "video.%d:rc_mode", stream_id);

	VENC_CHN_ATTR_S venc_chn_attr;
	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
	RK_MPI_VENC_GetChnAttr(stream_id, &venc_chn_attr);
	tmp_output_data_type = rk_param_get_string(entry_output_data_type, "H.264");
	if (!strcmp(tmp_output_data_type, "H.264")) {
		if (!strcmp(value, "CBR")) {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
			venc_chn_attr.stRcAttr.stH264Cbr.u32Gop = rk_param_get_int(entry_gop, -1);
			venc_chn_attr.stRcAttr.stH264Cbr.u32BitRate = rk_param_get_int(entry_max_rate, -1);
			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen =
			    rk_param_get_int(entry_dst_frame_rate_den, -1);
			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum =
			    rk_param_get_int(entry_dst_frame_rate_num, -1);
			venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateDen =
			    rk_param_get_int(entry_src_frame_rate_den, -1);
			venc_chn_attr.stRcAttr.stH264Cbr.u32SrcFrameRateNum =
			    rk_param_get_int(entry_src_frame_rate_num, -1);
		} else {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H264VBR;
			venc_chn_attr.stRcAttr.stH264Vbr.u32Gop = rk_param_get_int(entry_gop, -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32BitRate = rk_param_get_int(entry_max_rate, -1);
			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateDen =
			    rk_param_get_int(entry_dst_frame_rate_den, -1);
			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateNum =
			    rk_param_get_int(entry_dst_frame_rate_num, -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateDen =
			    rk_param_get_int(entry_src_frame_rate_den, -1);
			venc_chn_attr.stRcAttr.stH264Vbr.u32SrcFrameRateNum =
			    rk_param_get_int(entry_src_frame_rate_num, -1);
		}
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		if (!strcmp(value, "CBR")) {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
			venc_chn_attr.stRcAttr.stH265Cbr.u32Gop = rk_param_get_int(entry_gop, -1);
			venc_chn_attr.stRcAttr.stH265Cbr.u32BitRate = rk_param_get_int(entry_max_rate, -1);
			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen =
			    rk_param_get_int(entry_dst_frame_rate_den, -1);
			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum =
			    rk_param_get_int(entry_dst_frame_rate_num, -1);
			venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateDen =
			    rk_param_get_int(entry_src_frame_rate_den, -1);
			venc_chn_attr.stRcAttr.stH265Cbr.u32SrcFrameRateNum =
			    rk_param_get_int(entry_src_frame_rate_num, -1);
		} else {
			venc_chn_attr.stRcAttr.enRcMode = VENC_RC_MODE_H265VBR;
			venc_chn_attr.stRcAttr.stH265Vbr.u32Gop = rk_param_get_int(entry_gop, -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32BitRate = rk_param_get_int(entry_max_rate, -1);
			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateDen =
			    rk_param_get_int(entry_dst_frame_rate_den, -1);
			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateNum =
			    rk_param_get_int(entry_dst_frame_rate_num, -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateDen =
			    rk_param_get_int(entry_src_frame_rate_den, -1);
			venc_chn_attr.stRcAttr.stH265Vbr.u32SrcFrameRateNum =
			    rk_param_get_int(entry_src_frame_rate_num, -1);
		}
	} else {
		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
		return -1;
	}
	RK_MPI_VENC_SetChnAttr(stream_id, &venc_chn_attr);
	rk_param_set_string(entry_rc_mode, value);

	return 0;
}

int rk_video_get_output_data_type(int stream_id, const char **value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:output_data_type", stream_id);
	*value = rk_param_get_string(entry, "H.265");

	return 0;
}

int rk_video_set_output_data_type(int stream_id, const char *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:output_data_type", stream_id);
	rk_param_set_string(entry, value);
	rk_video_restart();

	return 0;
}

int rk_video_get_rc_quality(int stream_id, const char **value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:rc_quality", stream_id);
	*value = rk_param_get_string(entry, "high");

	return 0;
}

int rk_video_set_rc_quality(int stream_id, const char *value) {
	char entry_rc_quality[128] = {'\0'};
	char entry_output_data_type[128] = {'\0'};

	snprintf(entry_rc_quality, 127, "video.%d:rc_quality", stream_id);
	snprintf(entry_output_data_type, 127, "video.%d:output_data_type", stream_id);
	tmp_output_data_type = rk_param_get_string(entry_output_data_type, "H.264");

	VENC_RC_PARAM_S venc_rc_param;
	RK_MPI_VENC_GetRcParam(stream_id, &venc_rc_param);
	if (!strcmp(tmp_output_data_type, "H.264")) {
		if (!strcmp(value, "highest")) {
			venc_rc_param.stParamH264.u32MinQp = 10;
		} else if (!strcmp(value, "higher")) {
			venc_rc_param.stParamH264.u32MinQp = 15;
		} else if (!strcmp(value, "high")) {
			venc_rc_param.stParamH264.u32MinQp = 20;
		} else if (!strcmp(value, "medium")) {
			venc_rc_param.stParamH264.u32MinQp = 25;
		} else if (!strcmp(value, "low")) {
			venc_rc_param.stParamH264.u32MinQp = 30;
		} else if (!strcmp(value, "lower")) {
			venc_rc_param.stParamH264.u32MinQp = 35;
		} else {
			venc_rc_param.stParamH264.u32MinQp = 40;
		}
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		if (!strcmp(value, "highest")) {
			venc_rc_param.stParamH265.u32MinQp = 10;
		} else if (!strcmp(value, "higher")) {
			venc_rc_param.stParamH265.u32MinQp = 15;
		} else if (!strcmp(value, "high")) {
			venc_rc_param.stParamH265.u32MinQp = 20;
		} else if (!strcmp(value, "medium")) {
			venc_rc_param.stParamH265.u32MinQp = 25;
		} else if (!strcmp(value, "low")) {
			venc_rc_param.stParamH265.u32MinQp = 30;
		} else if (!strcmp(value, "lower")) {
			venc_rc_param.stParamH265.u32MinQp = 35;
		} else {
			venc_rc_param.stParamH265.u32MinQp = 40;
		}
	} else {
		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
		return -1;
	}
	RK_MPI_VENC_SetRcParam(stream_id, &venc_rc_param);
	rk_param_set_string(entry_rc_quality, value);

	return 0;
}

int rk_video_get_smart(int stream_id, const char **value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:smart", stream_id);
	*value = rk_param_get_string(entry, "close");

	return 0;
}

int rk_video_set_smart(int stream_id, const char *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:smart", stream_id);
	rk_param_set_string(entry, value);
	rk_video_restart();

	return 0;
}

int rk_video_get_gop_mode(int stream_id, const char **value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:gop_mode", stream_id);
	*value = rk_param_get_string(entry, "close");

	return 0;
}

int rk_video_set_gop_mode(int stream_id, const char *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:gop_mode", stream_id);
	rk_param_set_string(entry, value);
	rk_video_restart();

	return 0;
}

int rk_video_get_stream_type(int stream_id, const char **value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:stream_type", stream_id);
	*value = rk_param_get_string(entry, "mainStream");

	return 0;
}

int rk_video_set_stream_type(int stream_id, const char *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:stream_type", stream_id);
	rk_param_set_string(entry, value);

	return 0;
}

int rk_video_get_h264_profile(int stream_id, const char **value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:h264_profile", stream_id);
	*value = rk_param_get_string(entry, "high");

	return 0;
}

int rk_video_set_h264_profile(int stream_id, const char *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:h264_profile", stream_id);
	rk_param_set_string(entry, value);
	rk_video_restart();

	return 0;
}

int rk_video_get_resolution(int stream_id, char **value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:width", stream_id);
	int width = rk_param_get_int(entry, 0);
	snprintf(entry, 127, "video.%d:height", stream_id);
	int height = rk_param_get_int(entry, 0);
	sprintf(*value, "%d*%d", width, height);

	return 0;
}

int rk_video_set_resolution(int stream_id, const char *value) {
	char entry[128] = {'\0'};
	int width, height;

	sscanf(value, "%d*%d", &width, &height);
	LOG_INFO("value is %s, width is %d, height is %d\n", value, width, height);
	snprintf(entry, 127, "video.%d:width", stream_id);
	rk_param_set_int(entry, width);
	snprintf(entry, 127, "video.%d:height", stream_id);
	rk_param_set_int(entry, height);
	if (enable_jpeg && (stream_id == 0)) {
		snprintf(entry, 127, "video.jpeg:width");
		rk_param_set_int(entry, width);
		snprintf(entry, 127, "video.jpeg:height");
		rk_param_set_int(entry, height);
	}
	rk_video_restart();

	return 0;
}

int rk_video_get_frame_rate(int stream_id, char **value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:dst_frame_rate_den", stream_id);
	int den = rk_param_get_int(entry, -1);
	snprintf(entry, 127, "video.%d:dst_frame_rate_num", stream_id);
	int num = rk_param_get_int(entry, -1);
	if (den == 1)
		sprintf(*value, "%d", num);
	else
		sprintf(*value, "%d/%d", num, den);

	return 0;
}

int rk_video_set_frame_rate(int stream_id, const char *value) {
	char entry[128] = {'\0'};
	int den, num;
	if (strchr(value, '/') == NULL) {
		den = 1;
		sscanf(value, "%d", &num);
	} else {
		sscanf(value, "%d/%d", &num, &den);
	}
	LOG_INFO("num is %d, den is %d\n", num, den);

	VENC_CHN_ATTR_S venc_chn_attr;
	memset(&venc_chn_attr, 0, sizeof(venc_chn_attr));
	RK_MPI_VENC_GetChnAttr(stream_id, &venc_chn_attr);
	snprintf(entry, 127, "video.%d:output_data_type", stream_id);
	tmp_output_data_type = rk_param_get_string(entry, "H.264");
	snprintf(entry, 127, "video.%d:rc_mode", stream_id);
	tmp_rc_mode = rk_param_get_string(entry, "CBR");
	if (!strcmp(tmp_output_data_type, "H.264")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_AVC;
		if (!strcmp(tmp_rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateDen = den;
			venc_chn_attr.stRcAttr.stH264Cbr.fr32DstFrameRateNum = num;
		} else {
			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateDen = den;
			venc_chn_attr.stRcAttr.stH264Vbr.fr32DstFrameRateNum = num;
		}
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		venc_chn_attr.stVencAttr.enType = RK_VIDEO_ID_HEVC;
		if (!strcmp(tmp_rc_mode, "CBR")) {
			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateDen = den;
			venc_chn_attr.stRcAttr.stH265Cbr.fr32DstFrameRateNum = num;
		} else {
			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateDen = den;
			venc_chn_attr.stRcAttr.stH265Vbr.fr32DstFrameRateNum = num;
		}
	} else {
		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
		return -1;
	}
	RK_MPI_VENC_SetChnAttr(stream_id, &venc_chn_attr);

	snprintf(entry, 127, "video.%d:dst_frame_rate_den", stream_id);
	rk_param_set_int(entry, den);
	snprintf(entry, 127, "video.%d:dst_frame_rate_num", stream_id);
	rk_param_set_int(entry, num);

	return 0;
}

int rk_video_reset_frame_rate(int stream_id) {
	int ret = 0;
	char *value = (char *)malloc(20);
	ret |= rk_video_get_frame_rate(stream_id, &value);
	ret |= rk_video_set_frame_rate(stream_id, value);
	free(value);

	return 0;
}

int rk_video_get_frame_rate_in(int stream_id, char **value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.%d:src_frame_rate_den", stream_id);
	int den = rk_param_get_int(entry, -1);
	snprintf(entry, 127, "video.%d:src_frame_rate_num", stream_id);
	int num = rk_param_get_int(entry, -1);
	if (den == 1)
		sprintf(*value, "%d", num);
	else
		sprintf(*value, "%d/%d", num, den);

	return 0;
}

int rk_video_set_frame_rate_in(int stream_id, const char *value) {
	char entry[128] = {'\0'};
	int den, num;
	if (strchr(value, '/') == NULL) {
		den = 1;
		sscanf(value, "%d", &num);
	} else {
		sscanf(value, "%d/%d", &num, &den);
	}
	LOG_INFO("num is %d, den is %d\n", num, den);
	snprintf(entry, 127, "video.%d:src_frame_rate_den", stream_id);
	rk_param_set_int(entry, den);
	snprintf(entry, 127, "video.%d:src_frame_rate_num", stream_id);
	rk_param_set_int(entry, num);
	rk_video_restart();

	return 0;
}

// jpeg
int rk_video_get_enable_cycle_snapshot(int *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.jpeg:enable_cycle_snapshot");
	*value = rk_param_get_int(entry, -1);

	return 0;
}

int rk_video_set_enable_cycle_snapshot(int value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.jpeg:enable_cycle_snapshot");
	rk_param_set_int(entry, value);

	return 0;
}

int rk_video_get_image_quality(int *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.jpeg:jpeg_qfactor");
	*value = rk_param_get_int(entry, -1);

	return 0;
}

int rk_video_set_image_quality(int value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.jpeg:jpeg_qfactor");
	rk_param_set_int(entry, value);

	return 0;
}

int rk_video_get_snapshot_interval_ms(int *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.jpeg:snapshot_interval_ms");
	*value = rk_param_get_int(entry, 0);

	return 0;
}

int rk_video_set_snapshot_interval_ms(int value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.jpeg:snapshot_interval_ms");
	rk_param_set_int(entry, value);

	return 0;
}

int rk_video_get_jpeg_resolution(char **value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.jpeg:width");
	int width = rk_param_get_int(entry, 0);
	snprintf(entry, 127, "video.jpeg:height");
	int height = rk_param_get_int(entry, 0);
	sprintf(*value, "%d*%d", width, height);

	return 0;
}

int rk_video_set_jpeg_resolution(const char *value) {
	int width, height, ret;
	char entry[128] = {'\0'};
	sscanf(value, "%d*%d", &width, &height);
	snprintf(entry, 127, "video.jpeg:width");
	rk_param_set_int(entry, width);
	snprintf(entry, 127, "video.jpeg:height");
	rk_param_set_int(entry, height);

	return 0;
}

int rk_take_photo() {
	LOG_INFO("start\n");
	VENC_RECV_PIC_PARAM_S stRecvParam;
	memset(&stRecvParam, 0, sizeof(VENC_RECV_PIC_PARAM_S));
	stRecvParam.s32RecvPicNum = 1;
	RK_MPI_VENC_StartRecvFrame(JPEG_VENC_CHN, &stRecvParam);
	take_photo_one = 1;

	return 0;
}

int rk_roi_set(roi_data_s *roi_data) {
	// LOG_INFO("id is %d\n", id);
	int ret = 0;
	int venc_chn = 0;
	VENC_ROI_ATTR_S pstRoiAttr;
	pstRoiAttr.u32Index = roi_data->id;
	pstRoiAttr.bEnable = (RK_BOOL)roi_data->enabled;
	pstRoiAttr.bAbsQp = RK_FALSE;
	pstRoiAttr.bIntra = RK_FALSE;
	pstRoiAttr.stRect.s32X = roi_data->position_x;
	pstRoiAttr.stRect.s32Y = roi_data->position_y;
	pstRoiAttr.stRect.u32Width = roi_data->width;
	pstRoiAttr.stRect.u32Height = roi_data->height;
	switch (roi_data->quality_level) {
	case 6:
		pstRoiAttr.s32Qp = -16;
		break;
	case 5:
		pstRoiAttr.s32Qp = -14;
		break;
	case 4:
		pstRoiAttr.s32Qp = -12;
		break;
	case 3:
		pstRoiAttr.s32Qp = -10;
		break;
	case 2:
		pstRoiAttr.s32Qp = -8;
		break;
	case 1:
	default:
		pstRoiAttr.s32Qp = -6;
	}

	if (!strcmp(roi_data->stream_type, "mainStream") &&
	    rk_param_get_int("video.source:enable_venc_0", 0)) {
		venc_chn = 0;
	} else if (!strcmp(roi_data->stream_type, "subStream") &&
	           rk_param_get_int("video.source:enable_venc_1", 0)) {
		venc_chn = 1;
	} else if (!strcmp(roi_data->stream_type, "thirdStream") &&
	           rk_param_get_int("video.source:enable_venc_2", 0)) {
		venc_chn = 2;
	} else {
		LOG_DEBUG("%s is not exit\n", roi_data->stream_type);
		return -1;
	}

	ret = RK_MPI_VENC_SetRoiAttr(venc_chn, &pstRoiAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_VENC_SetRoiAttr to venc %d failed with %#x\n", venc_chn, ret);
		return RK_FAILURE;
	}
	LOG_DEBUG("RK_MPI_VENC_SetRoiAttr to venc %d success\n", venc_chn);

	return ret;
}

int rk_region_clip_set(int venc_chn, region_clip_data_s *region_clip_data) {
	int ret = 0;
	VENC_CHN_PARAM_S stParam;

	RK_MPI_VENC_GetChnParam(venc_chn, &stParam);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_VENC_GetChnParam to venc failed with %#x\n", ret);
		return RK_FAILURE;
	}
	LOG_INFO("RK_MPI_VENC_GetChnParam to venc success\n");
	LOG_INFO("venc_chn is %d\n", venc_chn);
	if (region_clip_data->enabled)
		stParam.stCropCfg.enCropType = VENC_CROP_ONLY;
	else
		stParam.stCropCfg.enCropType = VENC_CROP_NONE;
	stParam.stCropCfg.stCropRect.s32X = region_clip_data->position_x;
	stParam.stCropCfg.stCropRect.s32Y = region_clip_data->position_y;
	stParam.stCropCfg.stCropRect.u32Width = region_clip_data->width;
	stParam.stCropCfg.stCropRect.u32Height = region_clip_data->height;
	ret = RK_MPI_VENC_SetChnParam(venc_chn, &stParam);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_VENC_SetChnParam to venc failed with %#x\n", ret);
		return RK_FAILURE;
	}
	LOG_INFO("RK_MPI_VENC_SetChnParam to venc success\n");

	return ret;
}

int rk_video_get_rotation(int *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.source:rotation");
	*value = rk_param_get_int(entry, 0);

	return 0;
}

int rk_video_set_rotation(int value) {
	LOG_INFO("value is %d\n", value);
	int rotation = 0;
	int ret = 0;
	char entry[128] = {'\0'};
	snprintf(entry, 127, "video.source:rotation");
	rk_param_set_int(entry, value);
	rk_video_restart();

	return 0;
}

int rk_video_init() {
	LOG_INFO("begin\n");
	int ret = 0;
	enable_jpeg =   (RK_BOOL)rk_param_get_int("video.source:enable_jpeg", 1);
	enable_venc_0 = (RK_BOOL)rk_param_get_int("video.source:enable_venc_0", 1);
	enable_venc_1 = (RK_BOOL)rk_param_get_int("video.source:enable_venc_1", 1);
	enable_rtsp =   (RK_BOOL)rk_param_get_int("video.source:enable_rtsp", 1);
	enable_rtmp =   (RK_BOOL)rk_param_get_int("video.source:enable_rtmp", 1);
	enable_npu =    (RK_BOOL)rk_param_get_int("video.source:enable_npu", 0);
	enable_ivs =    (RK_BOOL)rk_param_get_int("video.source:enable_ivs", 1);
	enable_wrap =   (RK_BOOL)rk_param_get_int("video.source:enable_wrap", 0);
	distortion_correction =
	    rk_param_get_string("isp.0.enhancement:distortion_correction", "close");
	if (!strcmp(distortion_correction, "FEC") || !strcmp(distortion_correction, "DIS"))
		enable_fec = RK_TRUE;
	else
		enable_fec = RK_FALSE;
	LOG_INFO("enable_jpeg is %d, enable_venc_0 is %d, enable_venc_1 is %d, enable_fec is %d\n",
	         enable_jpeg, enable_venc_0, enable_venc_1, enable_fec);

	pipe_id_ = rk_param_get_int("video.source:camera_id", 0);
	g_vi_chn_id = rk_param_get_int("video.source:vi_chn_id", 0);
	g_enable_vo = rk_param_get_int("video.source:enable_vo", 1);
	g_vo_dev_id = rk_param_get_int("video.source:vo_dev_id", 3);
	g_vo_layer_id = rk_param_get_int("video.source:vo_layer_id", 0);
	LOG_INFO("g_vi_chn_id is %d, g_enable_vo is %d, g_vo_dev_id is %d, g_vo_layer_id is %d\n",
	         g_vi_chn_id, g_enable_vo, g_vo_dev_id, g_vo_layer_id);
	g_video_run_ = 1;
	ret |= rkipc_vi_dev_init();
	if (enable_fec)
		ret |= rkipc_vi_gdc_vpss_init();
	else
		ret |= rkipc_vi_ext_init();
	if (g_enable_vo)
		ret |= rkipc_pipe_vi_vo_init();
	if (enable_venc_0)
		ret |= rkipc_pipe_0_init();
	if (enable_venc_1)
		ret |= rkipc_pipe_1_init();
	if (enable_jpeg)
		ret |= rkipc_pipe_jpeg_init();
	rk_roi_set_callback_register(rk_roi_set);
	rk_roi_set_all();
	rk_region_clip_set_callback_register(rk_region_clip_set);
	rk_region_clip_set_all();
	if (enable_rtsp)
		ret |= rkipc_rtsp_init(RTSP_URL_0, RTSP_URL_1, RTSP_URL_2);
	if (enable_rtmp) {
		sleep(6); // wait for fcgi and nginx
		ret |= rkipc_rtmp_init();
	}
	LOG_INFO("over\n");

	return ret;
}

int rk_video_deinit() {
	LOG_INFO("%s\n", __func__);
	g_video_run_ = 0;
	int ret = 0;
	rk_region_clip_set_callback_register(NULL);
	rk_roi_set_callback_register(NULL);
	if (g_enable_vo)
		ret |= rkipc_pipe_vi_vo_deinit();
	if (enable_venc_0) {
		if (venc_thread_0.joinable())
			venc_thread_0.join();
		ret |= rkipc_pipe_0_deinit();
	}
	if (enable_venc_1) {
		if (venc_thread_1.joinable())
			venc_thread_1.join();
		ret |= rkipc_pipe_1_deinit();
	}
	if (enable_jpeg) {
		if (rk_param_get_int("video.jpeg:enable_cycle_snapshot", 0)) {
			cycle_snapshot_flag = 0;
			if (cycle_snapshot_thread_id.joinable())
				cycle_snapshot_thread_id.join();
		}
		if (jpeg_venc_thread_id.joinable())
			jpeg_venc_thread_id.join();
		ret |= rkipc_pipe_jpeg_deinit();
	}
	if (enable_fec)
		ret |= rkipc_vi_gdc_vpss_deinit();
	else
		ret |= rkipc_vi_ext_deinit();
	ret |= rkipc_vi_dev_deinit();
	if (enable_rtmp)
		ret |= rkipc_rtmp_deinit();
	if (enable_rtsp)
		ret |= rkipc_rtsp_deinit();

	return ret;
}

extern char *rkipc_iq_file_path_;
int rk_video_restart() {
	int ret;
	ret = rk_storage_deinit();
	ret |= rk_video_deinit();
	ret |= rk_isp_deinit(0);
	ret |= rk_isp_init(0, rkipc_iq_file_path_);
	ret |= rk_video_init();
	ret |= rk_storage_init();

	return ret;
}
