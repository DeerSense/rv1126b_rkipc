// Copyright 2025 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern "C" {
#include "video.h"
#include "venc.h"
#include "osd.h"
}
#include "draw/cv_draw.hpp"
#include "engine/rknnPool.hpp"
#include "opencv2/core.hpp"
#include "rga/im2d.h"
#include "rga/im2d_buffer.h"
#include "rga/im2d_type.h"
#include "rga/rga.h"
#include "task/yolo26.h"

#include <thread>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "video.cc"

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
int g_vi_for_npu_id = 4;
int g_vi_for_venc_1_id = 2;
int g_vi_for_vo_chn_id = 5;

static int take_photo_one = 0;
static RK_BOOL enable_jpeg, enable_venc_0, enable_venc_1, enable_npu, enable_wrap, enable_ivs,
    enable_rtmp, enable_rtsp;
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
static std::thread venc_thread_0, venc_thread_1, venc_thread_2, jpeg_venc_thread_id, yolo26_thread,
    cycle_snapshot_thread_id, get_vi_thread_id, draw_nn_thread;

static MPP_CHN_S vi_chn, vpss_in_chn, vi_for_vo_chn, vo_chn, vpss_out_chn[4], venc_chn, ivs_chn,
    gdc_chn;

static void *test_get_vi(void *arg) {
	printf("#Start %s thread, arg:%p\n", __func__, arg);
	VIDEO_FRAME_INFO_S stViFrame;
	VI_CHN_STATUS_S stChnStatus;
	int loopCount = 0;
	int ret = 0;
	int get_vi_chn_id = g_vi_for_venc_0_id;
	while (g_video_run_) {
		// 5.get the frame
		ret = RK_MPI_VI_GetChnFrame(pipe_id_, get_vi_chn_id, &stViFrame, 1000);
		if (ret == RK_SUCCESS) {
			void *data = RK_MPI_MB_Handle2VirAddr(stViFrame.stVFrame.pMbBlk);
			// LOG_ERROR("RK_MPI_VI_GetChnFrame ok:data %p loop:%d seq:%d pts:%" PRId64 " ms\n",
			//(unsigned char*)data,
			//           loopCount, stViFrame.stVFrame.u32TimeRef, stViFrame.stVFrame.u64PTS /
			//           1000);
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
	stFrame.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));

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

			rkipc_rtsp_write_video_frame(0, (unsigned char *)data, stFrame.pstPack->u32Len,
			                             stFrame.pstPack->u64PTS);
			if ((stFrame.pstPack->DataType.enH264EType == H264E_NALU_IDRSLICE) ||
			    (stFrame.pstPack->DataType.enH264EType == H264E_NALU_ISLICE) ||
			    (stFrame.pstPack->DataType.enH265EType == H265E_NALU_IDRSLICE) ||
			    (stFrame.pstPack->DataType.enH265EType == H265E_NALU_ISLICE)) {
				rk_storage_write_video_frame(0, (unsigned char *)data, stFrame.pstPack->u32Len,
				                             stFrame.pstPack->u64PTS, 1);
				rk_rtmp_write_video_frame(0, (unsigned char *)data, stFrame.pstPack->u32Len,
				                          stFrame.pstPack->u64PTS, 1);
			} else {
				rk_storage_write_video_frame(0, (unsigned char *)data, stFrame.pstPack->u32Len,
				                             stFrame.pstPack->u64PTS, 0);
				rk_rtmp_write_video_frame(0, (unsigned char *)data, stFrame.pstPack->u32Len,
				                          stFrame.pstPack->u64PTS, 0);
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
	stFrame.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));

	while (g_video_run_) {
		// 5.get the frame
		ret = RK_MPI_VENC_GetStream(VIDEO_PIPE_1, &stFrame, 2500);
		if (ret == RK_SUCCESS) {
			void *data = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
			// LOG_INFO("Count:%d, Len:%d, PTS is %" PRId64", enH264EType is %d\n", loopCount,
			// stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
			// stFrame.pstPack->DataType.enH264EType);
			rkipc_rtsp_write_video_frame(1, (unsigned char *)data, stFrame.pstPack->u32Len,
			                             stFrame.pstPack->u64PTS);
			if ((stFrame.pstPack->DataType.enH264EType == H264E_NALU_IDRSLICE) ||
			    (stFrame.pstPack->DataType.enH264EType == H264E_NALU_ISLICE) ||
			    (stFrame.pstPack->DataType.enH265EType == H265E_NALU_IDRSLICE) ||
			    (stFrame.pstPack->DataType.enH265EType == H265E_NALU_ISLICE)) {
				rk_storage_write_video_frame(1, (unsigned char *)data, stFrame.pstPack->u32Len,
				                             stFrame.pstPack->u64PTS, 1);
				rk_rtmp_write_video_frame(1, (unsigned char *)data, stFrame.pstPack->u32Len,
				                          stFrame.pstPack->u64PTS, 1);
			} else {
				rk_storage_write_video_frame(1, (unsigned char *)data, stFrame.pstPack->u32Len,
				                             stFrame.pstPack->u64PTS, 0);
				rk_rtmp_write_video_frame(1, (unsigned char *)data, stFrame.pstPack->u32Len,
				                          stFrame.pstPack->u64PTS, 0);
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
	stFrame.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));

	while (g_video_run_) {
		// 5.get the frame
		ret = RK_MPI_VENC_GetStream(VIDEO_PIPE_2, &stFrame, 2500);
		if (ret == RK_SUCCESS) {
			void *data = RK_MPI_MB_Handle2VirAddr(stFrame.pstPack->pMbBlk);
			// LOG_INFO("Count:%d, Len:%d, PTS is %" PRId64", enH264EType is %d\n", loopCount,
			// stFrame.pstPack->u32Len, stFrame.pstPack->u64PTS,
			// stFrame.pstPack->DataType.enH264EType);
			rkipc_rtsp_write_video_frame(2, (unsigned char *)data, stFrame.pstPack->u32Len,
			                             stFrame.pstPack->u64PTS);
			if ((stFrame.pstPack->DataType.enH264EType == H264E_NALU_IDRSLICE) ||
			    (stFrame.pstPack->DataType.enH264EType == H264E_NALU_ISLICE) ||
			    (stFrame.pstPack->DataType.enH265EType == H265E_NALU_IDRSLICE) ||
			    (stFrame.pstPack->DataType.enH265EType == H265E_NALU_ISLICE)) {
				rk_storage_write_video_frame(2, (unsigned char *)data, stFrame.pstPack->u32Len,
				                             stFrame.pstPack->u64PTS, 1);
				rk_rtmp_write_video_frame(2, (unsigned char *)data, stFrame.pstPack->u32Len,
				                          stFrame.pstPack->u64PTS, 1);
			} else {
				rk_storage_write_video_frame(2, (unsigned char *)data, stFrame.pstPack->u32Len,
				                             stFrame.pstPack->u64PTS, 0);
				rk_rtmp_write_video_frame(2, (unsigned char *)data, stFrame.pstPack->u32Len,
				                          stFrame.pstPack->u64PTS, 0);
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
	stFrame.pstPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S));

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

	return 0;
}

int rkipc_vi_dev_deinit() {
	RK_MPI_VI_DisableDev(pipe_id_);

	return 0;
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
		ret = RK_MPI_VI_SetChnAttr(pipe_id_, g_vi_for_npu_id, &vi_chn_attr);
		if (ret) {
			LOG_ERROR("ERROR: create VI error! ret=%d\n", ret);
			return ret;
		}
		ret = RK_MPI_VI_EnableChn(pipe_id_, g_vi_for_npu_id);
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
		ret = RK_MPI_VI_DisableChn(pipe_id_, g_vi_for_npu_id);
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
	stVencChnRefBufShare.bEnable =
	    (RK_BOOL)rk_param_get_int("video.0:enable_refer_buffer_share", RK_FALSE);
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
		ret = RK_MPI_SYS_Bind(&vi_chn, &venc_chn);
		if (ret)
			LOG_ERROR("Bind VI and VENC error! ret=%#x\n", ret);
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
	ret = RK_MPI_SYS_UnBind(&vi_chn, &venc_chn);
	if (ret)
		LOG_ERROR("Unbind VI and VENC error! ret=%#x\n", ret);
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
	stVencChnRefBufShare.bEnable =
	    (RK_BOOL)rk_param_get_int("video.1:enable_refer_buffer_share", RK_FALSE);
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
	ret = RK_MPI_SYS_Bind(&vi_chn, &venc_chn);
	if (ret)
		LOG_ERROR("Bind VI and VENC error! ret=%#x\n", ret);

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
	ret = RK_MPI_SYS_UnBind(&vi_chn, &venc_chn);
	if (ret)
		LOG_ERROR("Unbind VI and VENC error! ret=%#x\n", ret);
	// VENC
	ret = RK_MPI_VENC_StopRecvFrame(VIDEO_PIPE_1);
	ret |= RK_MPI_VENC_DestroyChn(VIDEO_PIPE_1);
	if (ret)
		LOG_ERROR("ERROR: Destroy VENC error! ret=%#x\n", ret);
	else
		LOG_DEBUG("RK_MPI_VENC_DestroyChn success\n");

	return 0;
}


static void *yolo26_inference(void *arg) {
	LOG_DEBUG("#Start %s thread, arg:%p\n", __func__, arg);
	int width = rk_param_get_int("video.0:width", -1);
	int height = rk_param_get_int("video.0:height", -1);
	prctl(PR_SET_NAME, "RkipcGetVi2", 0, 0, 0);
	int ret;
	int32_t loopCount = 0;
	VIDEO_FRAME_INFO_S stViFrame;

	// 初始化
	rknnPool<Yolo26, cv::Mat, std::vector<Detection>> yolo26("./yolo26n.rknn", 4);
	yolo26.init();

	std::vector<Detection> objects;

	while (g_video_run_) {
		ret = RK_MPI_VI_GetChnFrame(pipe_id_, g_vi_for_npu_id, &stViFrame, 1000);
		if (ret == RK_SUCCESS) {

			int32_t fd = RK_MPI_MB_Handle2Fd(stViFrame.stVFrame.pMbBlk);
			cv::Mat src_img = cv::Mat::zeros(height, width, CV_8UC3);

			rga_buffer_t yuv_buffer =
			    wrapbuffer_fd(fd, width, height, RK_FORMAT_YCbCr_420_SP, width, height);
			rga_buffer_t rgb_buffer = wrapbuffer_virtualaddr((void *)src_img.data, width, height,
			                                                 RK_FORMAT_RGB_888, width, height);

			ret = imcheck(yuv_buffer, rgb_buffer, {}, {});

			if (ret != IM_STATUS_NOERROR) {
				LOG_ERROR("%d imcheck fail %s \n", ret, imStrError((IM_STATUS)ret));
			}

			// imflip(yuv_buffer, rgb_buffer, IM_HAL_TRANSFORM_FLIP_H);
			// if (ret != IM_STATUS_NOERROR) {
			// 	LOG_ERROR("%d imcheck fail %s \n", ret, imStrError((IM_STATUS)ret));
			// }
			imcopy(yuv_buffer, rgb_buffer);
			if (loopCount % 4 == 0) {
				// yolo.Run(src_img, objects);
				yolo26.put(src_img);
			}

			// getresult
			if (yolo26.get(objects) != 0) {

				DrawDetections(src_img, objects);
			}

			// imcopy(rgb_buffer, yuv_buffer);

			// ret = RK_MPI_VI_ReleaseChnFrame(pipe_id_, g_vi_for_npu_id, &stViFrame);
			if (ret != RK_SUCCESS)
				LOG_ERROR("RK_MPI_VI fail %x\n", ret);
			loopCount++;
		} else {
			LOG_ERROR("RK_MPI_VI or VPSS_GetChnFrame timeout %x\n", ret);
			sleep(1);
		}
	}
	return NULL;
}

int rkipc_yolo_init() {
	int ret = 0;
	yolo26_thread = std::thread(yolo26_inference, nullptr);
	return ret;
}
int rkipc_yolo_deinit() {
	int ret = 0;
	g_video_run_ = 0;
	if (yolo26_thread.joinable()) {
		yolo26_thread.join();
	}
	return ret;
}

int rkipc_osd_cover_create(int id, osd_data_s *osd_data) {
	LOG_INFO("id is %d\n", id);
	int ret = 0;
	RGN_HANDLE coverHandle = id;
	RGN_ATTR_S stCoverAttr;
	MPP_CHN_S stCoverChn;
	RGN_CHN_ATTR_S stCoverChnAttr;
	int video_width = rk_param_get_int("video.0:width", -1);
	int video_height = rk_param_get_int("video.0:height", -1);

	memset(&stCoverAttr, 0, sizeof(stCoverAttr));
	memset(&stCoverChnAttr, 0, sizeof(stCoverChnAttr));
	// create cover regions
	stCoverAttr.enType = COVER_RGN;
	ret = RK_MPI_RGN_Create(coverHandle, &stCoverAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_Create (%d) failed with %#x\n", coverHandle, ret);
		RK_MPI_RGN_Destroy(coverHandle);
		return RK_FAILURE;
	}
	LOG_INFO("The handle: %d, create success\n", coverHandle);


		stCoverChn.enModId = RK_ID_VI;
		stCoverChn.s32DevId = 0;
		stCoverChn.s32ChnId = VI_PIPE_ID;
	
	memset(&stCoverChnAttr, 0, sizeof(stCoverChnAttr));
	stCoverChnAttr.bShow = (RK_BOOL)osd_data->enable;
	stCoverChnAttr.enType = COVER_RGN;
	while (osd_data->origin_x + osd_data->width >= video_width) {
		osd_data->origin_x -= 8;
	}
	while (osd_data->origin_y + osd_data->height >= video_height) {
		osd_data->origin_y -= 8;
	}
	stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32X = osd_data->origin_x;
	stCoverChnAttr.unChnAttr.stCoverChn.stRect.s32Y = osd_data->origin_y;
	stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Width = osd_data->width;
	stCoverChnAttr.unChnAttr.stCoverChn.stRect.u32Height = osd_data->height;
	stCoverChnAttr.unChnAttr.stCoverChn.u32Color = 0xffffffff;
	stCoverChnAttr.unChnAttr.stCoverChn.u32Layer = id;
	LOG_INFO("cover region to chn success\n");
	ret = RK_MPI_RGN_AttachToChn(coverHandle, &stCoverChn, &stCoverChnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_AttachToChn (%d) failed with %#x\n", coverHandle, ret);
		return RK_FAILURE;
	}
	LOG_INFO("RK_MPI_RGN_AttachToChn to vi/vpss success\n");

	ret = RK_MPI_RGN_SetDisplayAttr(coverHandle, &stCoverChn, &stCoverChnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_SetDisplayAttr failed with 0x%#x\n", ret);
		return RK_FAILURE;
	}
	LOG_INFO("RK_MPI_RGN_SetDisplayAttr to vi/vpss success\n");

	return ret;
}

int rkipc_osd_cover_destroy(int id) {
	LOG_INFO("%s\n", __func__);
	int ret = 0;
	// Detach osd from chn
	MPP_CHN_S stMppChn;
	RGN_HANDLE RgnHandle = id;

		stMppChn.enModId = RK_ID_VI;
		stMppChn.s32DevId = 0;
		stMppChn.s32ChnId = VI_PIPE_ID;
	
	ret = RK_MPI_RGN_DetachFromChn(RgnHandle, &stMppChn);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_DetachFrmChn (%d) to vi/vpss failed with %#x\n", RgnHandle, ret);
	}
	LOG_INFO("RK_MPI_RGN_DetachFromChn to vi/vpss success\n");

	// destory region
	ret = RK_MPI_RGN_Destroy(RgnHandle);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_Destroy [%d] failed with %#x\n", RgnHandle, ret);
	}
	LOG_INFO("Destory handle:%d success\n", RgnHandle);

	return ret;
}

int rkipc_osd_mosaic_create(int id, osd_data_s *osd_data) {
	LOG_INFO("id is %d\n", id);
	int ret = 0;
	RGN_HANDLE mosaic_handle = id;
	RGN_ATTR_S mosaic_attr;
	MPP_CHN_S mosaic_chn;
	RGN_CHN_ATTR_S mosaic_chn_attr;

	memset(&mosaic_attr, 0, sizeof(mosaic_attr));
	memset(&mosaic_chn_attr, 0, sizeof(mosaic_chn_attr));
	// create mosaic regions
	mosaic_attr.enType = MOSAIC_RGN;
	ret = RK_MPI_RGN_Create(mosaic_handle, &mosaic_attr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_Create (%d) failed with %#x\n", mosaic_handle, ret);
		RK_MPI_RGN_Destroy(mosaic_handle);
		return RK_FAILURE;
	}
	LOG_INFO("The handle: %d, create success\n", mosaic_handle);



		mosaic_chn.enModId = RK_ID_VI;
		mosaic_chn.s32DevId = 0;
		mosaic_chn.s32ChnId = VI_PIPE_ID;
	
	memset(&mosaic_chn_attr, 0, sizeof(mosaic_chn_attr));
	mosaic_chn_attr.bShow = (RK_BOOL)osd_data->enable;
	mosaic_chn_attr.enType = MOSAIC_RGN;
	mosaic_chn_attr.unChnAttr.stMosaicChn.enMosaicType = AREA_RECT;
	mosaic_chn_attr.unChnAttr.stMosaicChn.enBlkSize = MOSAIC_BLK_SIZE_64;
	mosaic_chn_attr.unChnAttr.stMosaicChn.u32Layer = mosaic_handle;
	mosaic_chn_attr.unChnAttr.stMosaicChn.stRect.s32X = osd_data->origin_x;
	mosaic_chn_attr.unChnAttr.stMosaicChn.stRect.s32Y = osd_data->origin_y;
	mosaic_chn_attr.unChnAttr.stMosaicChn.stRect.u32Width = osd_data->width;
	mosaic_chn_attr.unChnAttr.stMosaicChn.stRect.u32Height = osd_data->height;
	LOG_INFO("mosaic region to chn success\n");
	ret = RK_MPI_RGN_AttachToChn(mosaic_handle, &mosaic_chn, &mosaic_chn_attr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_AttachToChn (%d) failed with %#x\n", mosaic_handle, ret);
		return RK_FAILURE;
	}
	LOG_INFO("RK_MPI_RGN_AttachToChn to vi/vpss success\n");
	ret = RK_MPI_RGN_SetDisplayAttr(mosaic_handle, &mosaic_chn, &mosaic_chn_attr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_SetDisplayAttr (%d) failed with 0x%#x\n", mosaic_handle, ret);
		return RK_FAILURE;
	}
	LOG_INFO("RK_MPI_RGN_SetDisplayAttr to vi/vpss success\n");

	return ret;
}

int rkipc_osd_mosaic_destroy(int id) {
	LOG_INFO("%s\n", __func__);
	int ret = 0;
	// Detach osd from chn
	MPP_CHN_S stMppChn;
	RGN_HANDLE RgnHandle = id;

		stMppChn.enModId = RK_ID_VI;
		stMppChn.s32DevId = 0;
		stMppChn.s32ChnId = VI_PIPE_ID;
	ret = RK_MPI_RGN_DetachFromChn(RgnHandle, &stMppChn);
	if (!ret)
		LOG_ERROR("RK_MPI_RGN_DetachFrmChn (%d) to vi/vpss failed with %#x\n", RgnHandle, ret);

	// destory region
	ret = RK_MPI_RGN_Destroy(RgnHandle);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_Destroy [%d] failed with %#x\n", RgnHandle, ret);
	}
	LOG_INFO("Destory handle:%d success\n", RgnHandle);

	return ret;
}

int rkipc_osd_bmp_create(int id, osd_data_s *osd_data) {
	LOG_INFO("id is %d\n", id);
	int ret = 0;
	RGN_HANDLE RgnHandle = id;
	RGN_ATTR_S stRgnAttr;
	MPP_CHN_S stMppChn;
	RGN_CHN_ATTR_S stRgnChnAttr;
	BITMAP_S stBitmap;

	// create overlay regions
	memset(&stRgnAttr, 0, sizeof(stRgnAttr));
	stRgnAttr.enType = OVERLAY_RGN;
	// stRgnAttr.unAttr.stOverlay.enVProcDev = VIDEO_PROC_DEV_RGA;
	stRgnAttr.unAttr.stOverlay.enPixelFmt = RK_FMT_ARGB8888;
	stRgnAttr.unAttr.stOverlay.stSize.u32Width = osd_data->width;
	stRgnAttr.unAttr.stOverlay.stSize.u32Height = osd_data->height;
	ret = RK_MPI_RGN_Create(RgnHandle, &stRgnAttr);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_Create (%d) failed with %#x\n", RgnHandle, ret);
		RK_MPI_RGN_Destroy(RgnHandle);
		return RK_FAILURE;
	}
	LOG_INFO("The handle: %d, create success\n", RgnHandle);

	// display overlay regions to venc groups
	memset(&stRgnChnAttr, 0, sizeof(stRgnChnAttr));
	stRgnChnAttr.bShow = (RK_BOOL)osd_data->enable;
	stRgnChnAttr.enType = OVERLAY_RGN;
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32X = osd_data->origin_x;
	stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y = osd_data->origin_y;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32BgAlpha = 128;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32FgAlpha = 128;
	stRgnChnAttr.unChnAttr.stOverlayChn.u32Layer = id;
	stMppChn.enModId = RK_ID_VENC;
	stMppChn.s32DevId = 0;

	if (enable_venc_0) {
		stMppChn.s32ChnId = 0;
		ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stMppChn, &stRgnChnAttr);
		if (RK_SUCCESS != ret) {
			LOG_ERROR("RK_MPI_RGN_AttachToChn (%d) to venc0 failed with %#x\n", RgnHandle, ret);
			return RK_FAILURE;
		}
		LOG_DEBUG("RK_MPI_RGN_AttachToChn to venc0 success\n");
	}
	if (enable_jpeg) {
		stMppChn.s32ChnId = JPEG_VENC_CHN;
		ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stMppChn, &stRgnChnAttr);
		if (RK_SUCCESS != ret) {
			LOG_ERROR("RK_MPI_RGN_AttachToChn (%d) to jpeg failed with %#x\n", RgnHandle, ret);
			return RK_FAILURE;
		}
		LOG_DEBUG("RK_MPI_RGN_AttachToChn to jpeg success\n");
	}
	if (enable_venc_1) {
		stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32X =
		    UPALIGNTO16(osd_data->origin_x * rk_param_get_int("video.1:width", 1) /
		                rk_param_get_int("video.0:width", 1));
		stRgnChnAttr.unChnAttr.stOverlayChn.stPoint.s32Y =
		    UPALIGNTO16(osd_data->origin_y * rk_param_get_int("video.1:height", 1) /
		                rk_param_get_int("video.0:height", 1));
		stMppChn.s32ChnId = 1;
		ret = RK_MPI_RGN_AttachToChn(RgnHandle, &stMppChn, &stRgnChnAttr);
		if (RK_SUCCESS != ret) {
			LOG_ERROR("RK_MPI_RGN_AttachToChn (%d) to venc1 failed with %#x\n", RgnHandle, ret);
			return RK_FAILURE;
		}
		LOG_DEBUG("RK_MPI_RGN_AttachToChn to venc1 success\n");
	}

	// set bitmap
	stBitmap.enPixelFormat = RK_FMT_ARGB8888;
	stBitmap.u32Width = osd_data->width;
	stBitmap.u32Height = osd_data->height;
	stBitmap.pData = (RK_VOID *)osd_data->buffer;
	ret = RK_MPI_RGN_SetBitMap(RgnHandle, &stBitmap);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_RGN_SetBitMap failed with %#x\n", ret);
		return RK_FAILURE;
	}

	return ret;
}

int rkipc_osd_bmp_destroy(int id) {
	LOG_INFO("%s\n", __func__);
	int ret = 0;
	// Detach osd from chn
	MPP_CHN_S stMppChn;
	RGN_HANDLE RgnHandle = id;
	stMppChn.enModId = RK_ID_VENC;
	stMppChn.s32DevId = 0;
	if (enable_venc_0) {
		stMppChn.s32ChnId = 0;
		ret = RK_MPI_RGN_DetachFromChn(RgnHandle, &stMppChn);
		if (RK_SUCCESS != ret)
			LOG_DEBUG("RK_MPI_RGN_DetachFrmChn (%d) to venc0 failed with %#x\n", RgnHandle, ret);
	}
	if (enable_jpeg) {
		stMppChn.s32ChnId = JPEG_VENC_CHN;
		ret = RK_MPI_RGN_DetachFromChn(RgnHandle, &stMppChn);
		if (RK_SUCCESS != ret)
			LOG_DEBUG("RK_MPI_RGN_DetachFrmChn (%d) to jpeg failed with %#x\n", RgnHandle, ret);
	}
	if (enable_venc_1) {
		stMppChn.s32ChnId = 1;
		ret = RK_MPI_RGN_DetachFromChn(RgnHandle, &stMppChn);
		if (RK_SUCCESS != ret)
			LOG_DEBUG("RK_MPI_RGN_DetachFrmChn (%d) to venc1 failed with %#x\n", RgnHandle, ret);
	}

	// destory region
	ret = RK_MPI_RGN_Destroy(RgnHandle);
	if (RK_SUCCESS != ret) {
		LOG_ERROR("RK_MPI_RGN_Destroy [%d] failed with %#x\n", RgnHandle, ret);
	}
	LOG_INFO("Destory handle:%d success\n", RgnHandle);

	return ret;
}

int rkipc_osd_bmp_change(int id, osd_data_s *osd_data) {
	// LOG_INFO("id is %d\n", id);
	int ret = 0;
	RGN_HANDLE RgnHandle = id;
	BITMAP_S stBitmap;

	// set bitmap
	stBitmap.enPixelFormat = RK_FMT_ARGB8888;
	stBitmap.u32Width = osd_data->width;
	stBitmap.u32Height = osd_data->height;
	stBitmap.pData = (RK_VOID *)osd_data->buffer;
	ret = RK_MPI_RGN_SetBitMap(RgnHandle, &stBitmap);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_RGN_SetBitMap failed with %#x\n", ret);
		return RK_FAILURE;
	}

	return ret;
}

int rkipc_osd_init() {
	rk_osd_cover_create_callback_register(rkipc_osd_cover_create);
	rk_osd_cover_destroy_callback_register(rkipc_osd_cover_destroy);
	rk_osd_mosaic_create_callback_register(rkipc_osd_mosaic_create);
	rk_osd_mosaic_destroy_callback_register(rkipc_osd_mosaic_destroy);
	rk_osd_bmp_create_callback_register(rkipc_osd_bmp_create);
	rk_osd_bmp_destroy_callback_register(rkipc_osd_bmp_destroy);
	rk_osd_bmp_change_callback_register(rkipc_osd_bmp_change);
	rk_osd_init();

	return 0;
}

int rkipc_osd_deinit() {
	rk_osd_deinit();
	rk_osd_cover_create_callback_register(NULL);
	rk_osd_cover_destroy_callback_register(NULL);
	rk_osd_bmp_create_callback_register(NULL);
	rk_osd_bmp_destroy_callback_register(NULL);
	rk_osd_bmp_change_callback_register(NULL);

	return 0;
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

	ret = RK_MPI_SYS_Bind(&vi_for_vo_chn, &vo_chn);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("vi and vo bind error! ret=%#x\n", ret);
		return ret;
	}

	return 0;
}

int rkipc_pipe_vi_vo_deinit() {
	int ret;
	RK_S32 u32VoChn = 0;
	ret = RK_MPI_SYS_UnBind(&vi_for_vo_chn, &vo_chn);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("vi and vo unbind error! ret=%#x\n", ret);
		return ret;
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
	enable_jpeg = (RK_BOOL)rk_param_get_int("video.source:enable_jpeg", 1);
	enable_venc_0 = (RK_BOOL)rk_param_get_int("video.source:enable_venc_0", 1);
	enable_venc_1 = (RK_BOOL)rk_param_get_int("video.source:enable_venc_1", 1);
	enable_rtsp = (RK_BOOL)rk_param_get_int("video.source:enable_rtsp", 1);
	enable_rtmp = (RK_BOOL)rk_param_get_int("video.source:enable_rtmp", 1);
	enable_npu = (RK_BOOL)rk_param_get_int("video.source:enable_npu", 0);
	enable_ivs = (RK_BOOL)rk_param_get_int("video.source:enable_ivs", 1);
	enable_wrap = (RK_BOOL)rk_param_get_int("video.source:enable_wrap", 0);
	LOG_INFO("enable_jpeg is %d, enable_venc_0 is %d, enable_venc_1 is %d", enable_jpeg,
	         enable_venc_0, enable_venc_1);

	pipe_id_ = rk_param_get_int("video.source:camera_id", 0);
	g_vi_chn_id = rk_param_get_int("video.source:vi_chn_id", 0);
	g_enable_vo = rk_param_get_int("video.source:enable_vo", 1);
	g_vo_dev_id = rk_param_get_int("video.source:vo_dev_id", 3);
	g_vo_layer_id = rk_param_get_int("video.source:vo_layer_id", 0);
	LOG_INFO("g_vi_chn_id is %d, g_enable_vo is %d, g_vo_dev_id is %d, g_vo_layer_id is %d\n",
	         g_vi_chn_id, g_enable_vo, g_vo_dev_id, g_vo_layer_id);
	g_video_run_ = 1;
	ret |= rkipc_vi_dev_init();

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

	rkipc_osd_init();
	LOG_INFO("over\n");

	return ret;
}

int rk_video_deinit() {
	LOG_INFO("%s\n", __func__);
	g_video_run_ = 0;
	int ret = 0;
	rk_region_clip_set_callback_register(NULL);
	rk_roi_set_callback_register(NULL);

	rkipc_osd_deinit();

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
