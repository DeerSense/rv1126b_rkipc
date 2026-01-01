// Copyright 2025 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "player.c"

#include "audio.h"
#include "common.h"
#include "player.h"
#include <byteswap.h>
#include <mp3dec.h>
#include <rk_debug.h>
#include <rk_mpi_cal.h>
#include <rk_mpi_mb.h>
#include <rk_mpi_mmz.h>
#include <rk_mpi_rgn.h>
#include <rk_mpi_sys.h>
#include <rk_mpi_tde.h>
#include <rk_mpi_vdec.h>
#include <rk_mpi_vo.h>
#include <rk_mpi_vpss.h>
#include <rkdemuxer.h>
#include <sys/stat.h>
#include <unistd.h>

#define VDEC_CHN_ID 0
#define ADEC_CHN_ID 0

#define JPEG_VDEC_CHN_ID 1
#define JPEG_VPSS_GRP_ID 2
#define JPEG_VPSS_CHN_ID 0

typedef enum {
	UNINITIALIZED,
	PAUSE,
	RUNNING,
} PLAYER_STATUS;

typedef struct _rkPlayerCtx {
	char input_file_path[128];
	DemuxerParam demuxer_param;
	bool enable_audio;
	bool enable_video;
	bool media_is_init;
	bool skip_init_vo;
	bool skip_init_vo_layer;
	bool thread_running;
	void *demuxer_handle;
	rk_player_event_cb event_cb;
	pthread_mutex_t mutex; // protect rkdemuxer
	pthread_t video_player_thread_id;
	pthread_t audio_player_thread_id;
	PLAYER_STATUS video_status;
	PLAYER_STATUS audio_status;
	int64_t video_current_pts;
	int64_t audio_current_pts;
	int64_t video_total_duraion;
	int64_t audio_total_duraion;
} PLAYER_CTX_S;

static MB_POOL vdec_mb_pool = MB_INVALID_POOLID;

static int create_vo(PLAYER_CTX_S *ctx) {
	int ret = 0;
	VO_PUB_ATTR_S VoPubAttr;
	VO_VIDEO_LAYER_ATTR_S stLayerAttr;
	VO_CSC_S VideoCSC;
	VO_CHN_ATTR_S VoChnAttr;
	uint32_t u32DispBufLen;
	uint32_t display_width = rk_param_get_int("display:width", 240);
	uint32_t display_height = rk_param_get_int("display:height", 320);
	uint32_t vo_dev = rk_param_get_int("display:dev_id", 1);
	uint32_t vo_layer = rk_param_get_int("display:layer_id", 5);
	uint32_t vo_chn = rk_param_get_int("display:play_chn_id", 1);
	uint32_t fps = rk_param_get_int("display:fps", 30);
	const char *intf_type = rk_param_get_string("display:intf_type", "LCD");

	memset(&VoPubAttr, 0, sizeof(VO_PUB_ATTR_S));
	memset(&stLayerAttr, 0, sizeof(VO_VIDEO_LAYER_ATTR_S));
	memset(&VideoCSC, 0, sizeof(VO_CSC_S));
	memset(&VoChnAttr, 0, sizeof(VoChnAttr));
	if (ctx->skip_init_vo) {
		RK_LOGD("skip init vo\n");
		return ret;
	}
	if (ctx->skip_init_vo_layer) {
		RK_LOGD("skip init vo layer and device\n");
		goto __init_vo_channel;
	}
	if (!strcmp(intf_type, "LCD"))
		VoPubAttr.enIntfType = VO_INTF_LCD;
	else if (!strcmp(intf_type, "MIPI"))
		VoPubAttr.enIntfType = VO_INTF_MIPI;
	else if (!strcmp(intf_type, "DP"))
		VoPubAttr.enIntfType = VO_INTF_DP;
	else {
		LOG_ERROR("bad intf_type!\n");
		return -1;
	}
	VoPubAttr.enIntfSync = VO_OUTPUT_DEFAULT;

	ret = RK_MPI_VO_SetPubAttr(vo_dev, &VoPubAttr);
	if (ret)
		LOG_ERROR("RK_MPI_VO_SetPubAttr failed %#X\n", ret);
	ret = RK_MPI_VO_Enable(vo_dev);
	if (ret)
		LOG_ERROR("RK_MPI_VO_Enable failed %#X\n", ret);
	RK_MPI_VO_GetLayerDispBufLen(vo_layer, &u32DispBufLen);
	LOG_INFO("Get vo_layer %d disp buf len is %d.\n", vo_layer, u32DispBufLen);
	u32DispBufLen = 3;
	ret = RK_MPI_VO_SetLayerDispBufLen(vo_layer, u32DispBufLen);
	LOG_INFO("Agin Get vo_layer %d disp buf len is %d.\n", vo_layer, u32DispBufLen);

	ret = RK_MPI_VO_GetPubAttr(vo_dev, &VoPubAttr);
	if ((VoPubAttr.stSyncInfo.u16Hact == 0) || (VoPubAttr.stSyncInfo.u16Vact == 0)) {
		VoPubAttr.stSyncInfo.u16Hact = display_width;
		VoPubAttr.stSyncInfo.u16Vact = display_height;
	}

	stLayerAttr.stDispRect.s32X = 0;
	stLayerAttr.stDispRect.s32Y = 0;
	stLayerAttr.stDispRect.u32Width = display_width;
	stLayerAttr.stDispRect.u32Height = display_height;
	stLayerAttr.stImageSize.u32Width = display_width;
	stLayerAttr.stImageSize.u32Height = display_height;
	LOG_INFO("stLayerAttr W=%d, H=%d\n", stLayerAttr.stDispRect.u32Width,
	         stLayerAttr.stDispRect.u32Height);

	stLayerAttr.u32DispFrmRt = fps;
	stLayerAttr.enPixFormat = RK_FMT_YUV420SP;
	VideoCSC.enCscMatrix = VO_CSC_MATRIX_IDENTITY;
	VideoCSC.u32Contrast = 50;
	VideoCSC.u32Hue = 50;
	VideoCSC.u32Luma = 50;
	VideoCSC.u32Satuature = 50;

	/*bind layer0 to device hd0*/
	ret = RK_MPI_VO_BindLayer(vo_layer, vo_dev, VO_LAYER_MODE_GRAPHIC);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("failed to bind layer\n");
		return ret;
	}
	ret = RK_MPI_VO_SetLayerAttr(vo_layer, &stLayerAttr);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("failed to set layer attr\n");
		return ret;
	}
	ret = RK_MPI_VO_SetLayerSpliceMode(vo_layer, VO_SPLICE_MODE_RGA);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("failed to set layer splice mode\n");
		return ret;
	}
	ret = RK_MPI_VO_EnableLayer(vo_layer);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("failed to enable layer\n");
		return ret;
	}
	ret = RK_MPI_VO_SetLayerCSC(vo_layer, &VideoCSC);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("failed to set layer csc\n");
		return ret;
	}
__init_vo_channel:
	VoChnAttr.bDeflicker = RK_FALSE;
	VoChnAttr.u32Priority = rk_param_get_int("display:play_chn_priority", 2);
	VoChnAttr.stRect.s32X = 0;
	VoChnAttr.stRect.s32Y = 0;
	VoChnAttr.stRect.u32Width = display_width;
	VoChnAttr.stRect.u32Height = display_height;
	VoChnAttr.enRotation = rk_param_get_int("display:rotation", 1);
	ret = RK_MPI_VO_SetChnAttr(vo_layer, vo_chn, &VoChnAttr);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("set %d layer %d ch vo attr failed!\n", vo_layer, vo_chn);
		return ret;
	}
	ret = RK_MPI_VO_EnableChn(vo_layer, vo_chn);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("create %d layer %d ch vo failed!\n", vo_layer, vo_chn);
		return ret;
	}
	LOG_INFO("create vo success\n");

	return ret;
}

static void destroy_vo(PLAYER_CTX_S *ctx) {
	int ret = 0;
	uint32_t vo_dev = rk_param_get_int("display:dev_id", 1);
	uint32_t vo_layer = rk_param_get_int("display:layer_id", 5);
	uint32_t vo_chn = rk_param_get_int("display:play_chn_id", 1);
	if (ctx->skip_init_vo) {
		RK_LOGD("skip deinit vo\n");
		return;
	}
	ret = RK_MPI_VO_DisableChn(vo_layer, vo_chn);
	if (ret) {
		LOG_ERROR("RK_MPI_VO_DisableChn failed, ret is %#x\n", ret);
		return;
	}
	if (ctx->skip_init_vo_layer) {
		RK_LOGD("skip deinit vo layer and device\n");
		return;
	}
	ret = RK_MPI_VO_DisableLayer(vo_layer);
	if (ret) {
		LOG_ERROR("RK_MPI_VO_DisableLayer failed, ret is %#x\n", ret);
		return;
	}
	ret = RK_MPI_VO_Disable(vo_dev);
	if (ret) {
		LOG_ERROR("RK_MPI_VO_Disable failed, ret is %#x\n", ret);
		return;
	}
	ret = RK_MPI_VO_UnBindLayer(vo_layer, vo_dev);
	if (ret) {
		LOG_ERROR("RK_MPI_VO_UnBindLayer failed, ret is %#x\n", ret);
		return;
	}
}

static int create_vdec(PLAYER_CTX_S *ctx) {
	int ret = RK_SUCCESS;
	VDEC_CHN_ATTR_S stAttr;
	VDEC_CHN_PARAM_S stVdecParam;
	MB_POOL_CONFIG_S stMbPoolCfg;
	VDEC_PIC_BUF_ATTR_S stVdecPicBufAttr;
	MB_PIC_CAL_S stMbPicCalret;
	VDEC_MOD_PARAM_S stModParam;

	memset(&stAttr, 0, sizeof(VDEC_CHN_ATTR_S));
	memset(&stVdecParam, 0, sizeof(VDEC_CHN_PARAM_S));
	memset(&stModParam, 0, sizeof(VDEC_MOD_PARAM_S));

	stAttr.enMode = VIDEO_MODE_STREAM;
	if (ctx->demuxer_param.pVideoCodec == NULL) {
		LOG_ERROR("bad video file format!\n");
		return -1;
	} else if (!strcmp(ctx->demuxer_param.pVideoCodec, "h264")) {
		stAttr.enType = RK_VIDEO_ID_AVC;
	} else if (!strcmp(ctx->demuxer_param.pVideoCodec, "h265")) {
		stAttr.enType = RK_VIDEO_ID_HEVC;
	} else {
		LOG_ERROR("unknown video file input!\n");
		return -1;
	}

	stModParam.enVdecMBSource = MB_SOURCE_USER;
	ret = RK_MPI_VDEC_SetModParam(&stModParam);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("vdec %d SetModParam failed! errcode %x\n", VDEC_CHN_ID, ret);
		return ret;
	}

	memset(&stMbPoolCfg, 0, sizeof(MB_POOL_CONFIG_S));
	stVdecPicBufAttr.enCodecType = stAttr.enType;
	stVdecPicBufAttr.stPicBufAttr.u32Width = ctx->demuxer_param.s32VideoWidth;
	stVdecPicBufAttr.stPicBufAttr.u32Height = ctx->demuxer_param.s32VideoHeigh;
	stVdecPicBufAttr.stPicBufAttr.enPixelFormat = RK_FMT_YUV420SP;
	stVdecPicBufAttr.stPicBufAttr.enCompMode = COMPRESS_MODE_NONE;
	ret = RK_MPI_CAL_VDEC_GetPicBufferSize(&stVdecPicBufAttr, &stMbPicCalret);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("get picture buffer size failed. err 0x%x", ret);
		return ret;
	}

	stMbPoolCfg.u64MBSize = stMbPicCalret.u32MBSize;
	stMbPoolCfg.u32MBCnt = 4;
	stMbPoolCfg.enRemapMode = MB_REMAP_MODE_CACHED;
	stMbPoolCfg.bPreAlloc = RK_TRUE;
	vdec_mb_pool = RK_MPI_MB_CreatePool(&stMbPoolCfg);
	if (vdec_mb_pool == MB_INVALID_POOLID) {
		LOG_ERROR("create pool failed!");
		return ret;
	}
	LOG_INFO("create mb pool %d, width %d, height %d, mb buffer size %d\n", vdec_mb_pool,
	         ctx->demuxer_param.s32VideoWidth, ctx->demuxer_param.s32VideoHeigh,
	         stMbPicCalret.u32MBSize);

	stAttr.u32PicWidth = ctx->demuxer_param.s32VideoWidth;
	stAttr.u32PicHeight = ctx->demuxer_param.s32VideoHeigh;
	stAttr.u32FrameBufCnt = 4;
	stAttr.u32StreamBufCnt = 4;
	stAttr.u32FrameBufSize = stMbPicCalret.u32MBSize;

	ret = RK_MPI_VDEC_CreateChn(VDEC_CHN_ID, &stAttr);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("create %d vdec failed! ", VDEC_CHN_ID);
		return ret;
	}

	stVdecParam.stVdecPictureParam.enPixelFormat = RK_FMT_YUV420SP;
	stVdecParam.stVdecVideoParam.enCompressMode = COMPRESS_MODE_NONE;

	ret = RK_MPI_VDEC_SetChnParam(VDEC_CHN_ID, &stVdecParam);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("set chn %d param failed %x! ", VDEC_CHN_ID, ret);
		return ret;
	}
	ret = RK_MPI_VDEC_AttachMbPool(VDEC_CHN_ID, vdec_mb_pool);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("attatc vdec mb pool %d failed! ", VDEC_CHN_ID);
		return ret;
	}

	ret = RK_MPI_VDEC_StartRecvStream(VDEC_CHN_ID);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("start recv chn %d failed %x! ", VDEC_CHN_ID, ret);
		return ret;
	}
	LOG_INFO("create vdec success\n");
	return ret;
}

static void destroy_vdec(void) {
	RK_MPI_VDEC_StopRecvStream(VDEC_CHN_ID);
	RK_MPI_VDEC_DetachMbPool(VDEC_CHN_ID);
	RK_MPI_VDEC_DestroyChn(VDEC_CHN_ID);
	RK_MPI_MB_DestroyPool(vdec_mb_pool);
	vdec_mb_pool = MB_INVALID_POOLID;
	return;
}

static int bind_vdec_vo(void) {
	int ret = 0;
	MPP_CHN_S stSrcChn, stDestChn;
	uint32_t vo_layer = rk_param_get_int("display:layer_id", 5);
	uint32_t vo_chn = rk_param_get_int("display:play_chn_id", 1);
	stSrcChn.enModId = RK_ID_VDEC;
	stSrcChn.s32DevId = 0;
	stSrcChn.s32ChnId = VDEC_CHN_ID;

	stDestChn.enModId = RK_ID_VO;
	stDestChn.s32DevId = vo_layer;
	stDestChn.s32ChnId = vo_chn;

	ret = RK_MPI_SYS_Bind(&stSrcChn, &stDestChn);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("vdec bind vo fail:%x", ret);
		return -1;
	}
	return 0;
}

static int unbind_vdec_vo(void) {
	int ret = 0;
	MPP_CHN_S stSrcChn, stDestChn;
	uint32_t vo_layer = rk_param_get_int("display:layer_id", 5);
	uint32_t vo_chn = rk_param_get_int("display:play_chn_id", 1);
	stSrcChn.enModId = RK_ID_VDEC;
	stSrcChn.s32DevId = 0;
	stSrcChn.s32ChnId = VDEC_CHN_ID;

	stDestChn.enModId = RK_ID_VO;
	stDestChn.s32DevId = vo_layer;
	stDestChn.s32ChnId = vo_chn;

	ret = RK_MPI_SYS_UnBind(&stSrcChn, &stDestChn);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("vdec unbind vo fail:%x", ret);
		return -1;
	}
	return 0;
}

static int get_jpeg_resolution(uint8_t *buf, uint32_t buf_size, uint32_t *width, uint32_t *height) {
	uint32_t cur = 0;
	if (buf[0] != 0xFF || buf[1] != 0xD8) {
		LOG_ERROR("Invalid jpeg data\n");
		return -1;
	}
	cur += 2;
	while (cur + 4 + 4 < buf_size) {
		if (buf[cur] != 0xFF) {
			LOG_ERROR("Bad Jpg file, 0xFF expected at offset 0x%x\n", cur);
			break;
		}

		if (buf[cur + 1] == 0xC0 || buf[cur + 1] == 0xC2) {
			cur += 5;
			*height = bswap_16(*(uint16_t *)(buf + cur));
			cur += 2;
			*width = bswap_16(*(uint16_t *)(buf + cur));
			LOG_INFO("w * h[%d, %d]\n", *width, *height);
			return 0;
		}
		cur += 2;
		cur += bswap_16(*(uint16_t *)(buf + cur));
	}
	LOG_ERROR("Can't get resolution\n");
	return -1;
}

static bool validate_format(uint32_t format) {
	if (format == RK_FMT_BGRA8888 || format == RK_FMT_RGBA8888 || format == RK_FMT_RGB565 ||
	    format == RK_FMT_YUV420SP)
		return true;
	return false;
};

static int create_jpeg_vdec(const RK_PHOTO_CONFIG *config, RK_PHOTO_DATA *data) {
	int ret = RK_SUCCESS;
	VDEC_CHN_ATTR_S stAttr;
	VDEC_CHN_PARAM_S stVdecParam;
	MB_POOL_CONFIG_S stMbPoolCfg;
	VDEC_PIC_BUF_ATTR_S stVdecPicBufAttr;
	MB_PIC_CAL_S stMbPicCalret;
	VDEC_MOD_PARAM_S stModParam;

	memset(&stAttr, 0, sizeof(VDEC_CHN_ATTR_S));
	memset(&stVdecParam, 0, sizeof(VDEC_CHN_PARAM_S));
	memset(&stModParam, 0, sizeof(VDEC_MOD_PARAM_S));

	stAttr.enMode = VIDEO_MODE_FRAME;
	stAttr.enType = RK_VIDEO_ID_JPEG;
	stAttr.u32PicWidth = config->input_width;   // width of jpeg
	stAttr.u32PicHeight = config->input_height; // height of jpeg
	stAttr.u32FrameBufCnt = 1;
	stAttr.u32FrameBufDepth = 1;
	stAttr.u32StreamBufCnt = 1;

	ret = RK_MPI_VDEC_CreateChn(JPEG_VDEC_CHN_ID, &stAttr);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("create %d vdec failed! ", JPEG_VDEC_CHN_ID);
		return ret;
	}

	stVdecParam.stVdecPictureParam.enPixelFormat = RK_FMT_YUV420SP;

	ret = RK_MPI_VDEC_SetChnParam(JPEG_VDEC_CHN_ID, &stVdecParam);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("set chn %d param failed %x! ", JPEG_VDEC_CHN_ID, ret);
		return ret;
	}

	ret = RK_MPI_VDEC_StartRecvStream(JPEG_VDEC_CHN_ID);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("start recv chn %d failed %x! ", JPEG_VDEC_CHN_ID, ret);
		return ret;
	}
	LOG_INFO("create vdec success\n");
	return ret;
}

static void destroy_jpeg_vdec(void) {
	RK_MPI_VDEC_StopRecvStream(JPEG_VDEC_CHN_ID);
	RK_MPI_VDEC_DestroyChn(JPEG_VDEC_CHN_ID);
	return;
}

static int resize_yuv_by_tde(RK_PHOTO_CONFIG *config, RK_PHOTO_DATA *data,
                             VIDEO_FRAME_INFO_S *tmp_frame) {
	int ret = 0;
	TDE_HANDLE tde_handle;
	TDE_SURFACE_S tde_src = {}, tde_dst = {};
	TDE_RECT_S tde_src_rect = {}, tde_dst_rect = {};
	PIC_BUF_ATTR_S tde_dst_buf_attr = {};
	MB_PIC_CAL_S tde_dst_pic_cal = {};
	MB_BLK tde_dst_mb = RK_NULL;

	tde_dst_buf_attr.u32Width = config->output_width;
	tde_dst_buf_attr.u32Height = config->output_height;
	tde_dst_buf_attr.enPixelFormat = config->output_format;
	tde_dst_buf_attr.enCompMode = COMPRESS_MODE_NONE;

	ret = RK_MPI_CAL_TDE_GetPicBufferSize(&tde_dst_buf_attr, &tde_dst_pic_cal);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("get picture buffer size failed. err 0x%x\n", ret);
		return ret;
	}
	ret = RK_MPI_SYS_MmzAlloc(&tde_dst_mb, RK_NULL, RK_NULL, tde_dst_pic_cal.u32MBSize);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_SYS_MmzAlloc err 0x%x\n", ret);
		return ret;
	}

	ret = RK_TDE_Open();
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_TDE_Open failed %#X\n", ret);
		RK_MPI_SYS_MmzFree(tde_dst_mb);
		return ret;
	}
	tde_handle = RK_TDE_BeginJob();

	tde_src.enColorFmt = RK_FMT_YUV420SP;
	tde_src.enComprocessMode = COMPRESS_MODE_NONE;
	tde_src.u32Width = tmp_frame->stVFrame.u32Width;
	tde_src.u32Height = tmp_frame->stVFrame.u32Height;
	tde_src.pMbBlk = tmp_frame->stVFrame.pMbBlk;
	tde_src_rect.s32Xpos = 0;
	tde_src_rect.s32Ypos = 0;
	tde_src_rect.u32Width = tmp_frame->stVFrame.u32Width;
	tde_src_rect.u32Height = tmp_frame->stVFrame.u32Height;

	tde_dst.enColorFmt = config->output_format;
	tde_dst.enComprocessMode = COMPRESS_MODE_NONE;
	tde_dst.u32Width = config->output_width;
	tde_dst.u32Height = config->output_height;
	tde_dst.pMbBlk = tde_dst_mb;
	tde_dst_rect.s32Xpos = 0;
	tde_dst_rect.s32Ypos = 0;
	tde_dst_rect.u32Width = config->output_width;
	tde_dst_rect.u32Height = config->output_height;

	ret = RK_TDE_QuickResize(tde_handle, &tde_src, &tde_src_rect, &tde_dst, &tde_dst_rect);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_TDE_QuickResize failed. err 0x%x\n", ret);
		RK_TDE_CancelJob(tde_handle);
		RK_MPI_SYS_MmzFree(tde_dst_mb);
		RK_TDE_Close();
		return ret;
	}

	RK_TDE_EndJob(tde_handle, RK_FALSE, RK_TRUE, 4000);
	ret = RK_TDE_WaitForDone(tde_handle);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_TDE_WaitForDone failed %#X\n", ret);
		RK_MPI_SYS_MmzFree(tde_dst_mb);
		RK_TDE_Close();
		return ret;
	} else {
		LOG_DEBUG("tde job done success\n");
	}
	LOG_INFO("get frame width %d, height %d, format %X, size %d\n", tde_dst.u32Width,
	         tde_dst.u32Height, tde_dst.enColorFmt, tde_dst_pic_cal.u32MBSize);
	data->width = tde_dst.u32Width;
	data->height = tde_dst.u32Height;
	data->format = tde_dst.enColorFmt;
	data->size = tde_dst_pic_cal.u32MBSize;
	data->data = malloc(data->size);
#ifdef DUMP_IMG
	{
		char dump_file_path[256] = {'\0'};
		snprintf(dump_file_path, sizeof(dump_file_path), "/tmp/%dx%d.argb8888", tde_dst.u32Width,
		         tde_dst.u32Height);
		FILE *file = fopen(dump_file_path, "w");
		void *buf = RK_MPI_MB_Handle2VirAddr(tde_dst.pMbBlk);
		int size = tde_dst.u32Width * tde_dst.u32Height * 4;
		RK_MPI_SYS_MmzFlushCache(tde_dst.pMbBlk, false);
		fwrite(buf, 1, size, file);
		fclose(file);
	}
#endif
	if (!data) {
		LOG_ERROR("malloc failed\n");
		ret = -1;
	} else {
		RK_MPI_SYS_MmzFlushCache(tde_dst.pMbBlk, false);
		memcpy(data->data, RK_MPI_MB_Handle2VirAddr(tde_dst.pMbBlk), data->size);
	}
	RK_MPI_SYS_MmzFree(tde_dst_mb);
	RK_TDE_Close();
	return ret;
}

static int send_jpeg_to_vdec(RK_PHOTO_CONFIG *config, RK_PHOTO_DATA *data, MB_BLK input_mb,
                             RK_U32 input_size) {
	int ret = 0;
	VDEC_STREAM_S input_frame = {};
	VIDEO_FRAME_INFO_S tmp_frame = {};

	memset(&input_frame, 0, sizeof(input_frame));
	input_frame.u64PTS = 0;
	input_frame.pMbBlk = input_mb;
	input_frame.u32Len = input_size;
	input_frame.bEndOfStream = RK_TRUE;
	input_frame.bEndOfFrame = RK_TRUE;
	input_frame.bBypassMbBlk = RK_FALSE;
	RK_MPI_SYS_MmzFlushCache(input_mb, false);

	ret = RK_MPI_VDEC_SendStream(JPEG_VDEC_CHN_ID, &input_frame, 2000);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VDEC_SendStream failed %#X\n", ret);
		return ret;
	}
	ret = RK_MPI_VDEC_GetFrame(JPEG_VDEC_CHN_ID, &tmp_frame, 1000);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_VDEC_GetFrame failed %#X\n", ret);
		return ret;
	}
#ifdef DUMP_IMG
	{
		char dump_file_path[256] = {'\0'};
		snprintf(dump_file_path, sizeof(dump_file_path), "/tmp/%dx%d.yuv420",
		         tmp_frame.stVFrame.u32VirWidth, tmp_frame.stVFrame.u32VirHeight);
		FILE *file = fopen(dump_file_path, "w");
		void *buf = RK_MPI_MB_Handle2VirAddr(tmp_frame.stVFrame.pMbBlk);
		int size = tmp_frame.stVFrame.u32VirWidth * tmp_frame.stVFrame.u32VirHeight * 3 / 2;
		fwrite(buf, 1, size, file);
		fclose(file);
	}
#endif
	ret = resize_yuv_by_tde(config, data, &tmp_frame);
	if (ret != RK_SUCCESS)
		LOG_ERROR("resize yuv by tde failed %#X\n", ret);
	else
		LOG_INFO("get jpeg success\n");
	RK_MPI_VDEC_ReleaseFrame(JPEG_VDEC_CHN_ID, &tmp_frame);

	return 0;
}

static int decode_one_jpeg(RK_PHOTO_CONFIG *config, RK_PHOTO_DATA *data, MB_BLK input_mb,
                           RK_U32 input_size) {
	int ret = 0;
	ret = create_jpeg_vdec(config, data);
	if (ret != 0)
		return ret;
	ret = send_jpeg_to_vdec(config, data, input_mb, input_size);
	destroy_jpeg_vdec();
	return ret;
}

static int buffer_free_cb(void *opaque) {
	if (opaque) {
		free(opaque);
		opaque = NULL;
	}
	return 0;
}

static void read_video_packet_cb(void *handle) {
	DemuxerPacket *video_packet = (DemuxerPacket *)handle;
	PLAYER_CTX_S *ctx = (PLAYER_CTX_S *)video_packet->ptr;
	int ret = 0;
	VDEC_STREAM_S video_stream;
	MB_BLK buffer = NULL;
	MB_EXT_CONFIG_S mb_ext_config;

	RKIPC_CHECK_POINTER(video_packet, );
	RKIPC_CHECK_POINTER(handle, );
	LOG_DEBUG("packet size %d, data %p, eof %d, pts %" PRId64 " \n", video_packet->s32PacketSize,
	          video_packet->s8PacketData, video_packet->s8EofFlag, video_packet->s64Pts);
	if (!video_packet->s8EofFlag) {
		memset(&mb_ext_config, 0, sizeof(MB_EXT_CONFIG_S));
		mb_ext_config.pFreeCB = buffer_free_cb;
		mb_ext_config.pOpaque = (void *)video_packet->s8PacketData;
		mb_ext_config.pu8VirAddr = (RK_U8 *)video_packet->s8PacketData;
		mb_ext_config.u64Size = video_packet->s32PacketSize;

		RK_MPI_SYS_CreateMB(&buffer, &mb_ext_config);

		video_stream.u64PTS = video_packet->s64Pts;
		video_stream.pMbBlk = buffer;
		video_stream.u32Len = video_packet->s32PacketSize;
		video_stream.bEndOfStream = video_packet->s8EofFlag ? RK_TRUE : RK_FALSE;
		video_stream.bEndOfFrame = video_packet->s8EofFlag ? RK_TRUE : RK_FALSE;
		video_stream.bBypassMbBlk = RK_TRUE;

		ret = RK_MPI_VDEC_SendStream(VDEC_CHN_ID, &video_stream, 1000);
		if (ret)
			LOG_ERROR("RK_MPI_VDEC_SendStream failed[%x]\n", ret);
		RK_MPI_MB_ReleaseMB(video_stream.pMbBlk);
	} else {
		if (video_packet->s8PacketData) {
			free(video_packet->s8PacketData);
			video_packet->s8PacketData = NULL;
		}
	}
	return;
}

static void read_audio_packet_cb(void *handle) {
	int ret = 0;
	DemuxerPacket *audio_packet = (DemuxerPacket *)handle;
	PLAYER_CTX_S *ctx = (PLAYER_CTX_S *)audio_packet->ptr;

	RKIPC_CHECK_POINTER(audio_packet, );
	RKIPC_CHECK_POINTER(handle, );
	LOG_DEBUG("packet size %d, data %p, eof %d, pts %" PRId64 " \n", audio_packet->s32PacketSize,
	          audio_packet->s8PacketData, audio_packet->s8EofFlag, audio_packet->s64Pts);
	if (audio_packet->s8EofFlag && audio_packet->s32PacketSize == 0) {
		ret = rkipc_audio_player_send(NULL, 0, 0);
	} else {
		if (audio_packet->s8PacketData == NULL || audio_packet->s32PacketSize == 0) {
			LOG_ERROR("bad packet!\n");
			return ;
		}
		ret = rkipc_audio_player_send(audio_packet->s8PacketData, audio_packet->s32PacketSize, audio_packet->s64Pts);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("RK_MPI_ADEC_SendStream failed[%x]\n", ret);
		}
	}

	return;
}

static void *video_player_loop(void *p) {
	PLAYER_CTX_S *ctx = (PLAYER_CTX_S *)p;
	int ret = 0;
	uint32_t fps = ctx->demuxer_param.s32VideoAvgFrameRate;
	uint64_t frame_interval_time = 0, cost_time = 0, delay_time = 0;
	DemuxerPacket packet;
	struct timespec start_time, end_time;

	prctl(PR_SET_NAME, "video_player_loop", 0, 0, 0);

	LOG_INFO("enter thread\n");
	if (fps == 0) {
		LOG_ERROR("bad parameter from video file!\n");
		return NULL;
	}
	frame_interval_time = (1000.0 / fps) * 1000;

	while (ctx->thread_running) {
		switch (ctx->video_status) {
		case UNINITIALIZED:
			LOG_ERROR("video player uninitialized, exit thread\n");
			return NULL;
		case PAUSE:
			usleep(300 * 1000);
			break;
		case RUNNING:
			clock_gettime(CLOCK_MONOTONIC, &start_time);
			pthread_mutex_lock(&ctx->mutex);
			// Packet malloc by rkdemuxer, free by user. So this critial zone is safe.
			ret = rkdemuxer_read_one_video_packet(ctx->demuxer_handle, &packet);
			if (ret) {
				LOG_ERROR("rkdemuxer_read_one_video_packet failed\n");
				ctx->video_status = PAUSE;
				if (ctx->event_cb)
					ctx->event_cb(RK_PLAYER_ERROR);
			} else {
				if (packet.s8EofFlag) {
					LOG_INFO("reach EOS, stop play and reset file position\n");
					rkdemuxer_seek_video_pts(ctx->demuxer_handle, 0);
					ctx->video_current_pts = 0;
					ctx->video_status = PAUSE;
				} else {
					ctx->video_current_pts = packet.s64Pts;
				}
			}
			pthread_mutex_unlock(&ctx->mutex);
			if (!ret)
				read_video_packet_cb(&packet);
			clock_gettime(CLOCK_MONOTONIC, &end_time);
			cost_time = end_time.tv_sec * 1000000LL + end_time.tv_nsec / 1000LL -
			            (start_time.tv_sec * 1000000LL + start_time.tv_nsec / 1000LL);
			if (cost_time < frame_interval_time) {
				delay_time = frame_interval_time - cost_time;
				usleep(delay_time);
				LOG_DEBUG("frame interval %" PRIu64 " ms, decode cost %" PRIu64 " ms, delay %" PRIu64 " ms\n",
				          frame_interval_time / 1000, cost_time / 1000, delay_time / 1000);
				if (ctx->event_cb)
					if (ret)
						ctx->event_cb(RK_PLAYER_ERROR);
					else if (packet.s8EofFlag)
						ctx->event_cb(RK_PLAYER_EOF);
			} else {
				LOG_ERROR("decode one frame cost too much time %" PRIu64 " ms\n", cost_time / 1000);
			}
		}
	}
	LOG_INFO("exit thread\n");
	return NULL;
}

static void *audio_player_loop(void *p) {
	PLAYER_CTX_S *ctx = (PLAYER_CTX_S *)p;
	int ret = 0;
	DemuxerPacket packet;
	struct timespec start_time, end_time;

	prctl(PR_SET_NAME, "audio_player_loop", 0, 0, 0);
	LOG_INFO("enter thread\n");

	while (ctx->thread_running) {
		switch (ctx->audio_status) {
		case UNINITIALIZED:
			LOG_ERROR("audio player uninitialized, exit thread\n");
			return NULL;
		case PAUSE:
			usleep(300 * 1000);
			break;
		case RUNNING:
			pthread_mutex_lock(&ctx->mutex);
			// Packet malloc by rkdemuxer, free by user. So this critial zone is safe.
			ret = rkdemuxer_read_one_audio_packet(ctx->demuxer_handle, &packet);
			if (ret) {
				LOG_ERROR("rkdemuxer_read_one_audio_packet failed\n");
				ctx->audio_status = PAUSE;
			} else {
				if (packet.s8EofFlag && packet.s32PacketSize == 0) {
					LOG_INFO("reach EOS, stop play and reset file position\n");
					rkdemuxer_seek_audio_pts(ctx->demuxer_handle, 0);
					ctx->audio_current_pts = 0;
					ctx->audio_status = PAUSE;
				} else {
					ctx->audio_current_pts = packet.s64Pts;
				}
			}
			pthread_mutex_unlock(&ctx->mutex);
			if (!ret)
				read_audio_packet_cb(&packet);
			if (ctx->event_cb)
				if (ret)
					ctx->event_cb(RK_PLAYER_ERROR);
				// INFO: avoid multiple processing
				else if (packet.s8EofFlag && !ctx->enable_video)
					ctx->event_cb(RK_PLAYER_EOF);
			if (ctx->enable_video && (ctx->audio_current_pts > ctx->video_current_pts)) {
				// INFO:
				// Not do sync between audio and video in rkipc, if you need sync video and audio frame
				//, you can refer to RKADK.
				int64_t diff_time = ctx->audio_current_pts - ctx->video_current_pts;
				int64_t frame_time = 33 * 1000; // 30 fps
				int64_t sleep_time = (diff_time > frame_time * 4) ? frame_time * 4 : diff_time;
				if (diff_time > frame_time) {
					LOG_DEBUG("diff time %" PRId64 " us, sleep time %" PRId64 " \n", diff_time, sleep_time);
					usleep(sleep_time);
				}
			}
		}
	}
	LOG_INFO("exit thread\n");
	return NULL;
}

static int create_media_pipe_unlocked(PLAYER_CTX_S *ctx) {
	int ret = 0;

	if (ctx->media_is_init)
		return 0;
	if (ctx->enable_video) {
		ret = create_vo(ctx);
		if (ret)
			return ret;
		ret = create_vdec(ctx);
		if (ret)
			return ret;
		ret = bind_vdec_vo();
		if (ret)
			return ret;
	}

	if (ctx->enable_audio) {
		ret = rkipc_audio_player_init(ctx->demuxer_param.s32AudioSampleRate
				, ctx->demuxer_param.s32AudioChannels
				, ctx->demuxer_param.pAudioCodec
				);
		if (ret)
			return ret;
	}
	ctx->media_is_init = true;
	LOG_INFO("create player success!\n");
	return 0;
}

static int destroy_media_pipe_unlocked(PLAYER_CTX_S *ctx) {
	int ret = 0;

	if (!ctx->media_is_init)
		return 0;

	if (ctx->enable_video) {
		unbind_vdec_vo();
		destroy_vdec();
		destroy_vo(ctx);
	}
	if (ctx->enable_audio) {
		rkipc_audio_player_deinit();
	}
	LOG_INFO("destroy player success!\n");
	ctx->media_is_init = false;
	return 0;
}

// INFO: rkdemuxer_read_xxxx_duration() would travese the record file, so reinit the demuxer after
// read duration is a robust choice.
static int read_duration_unlocked(PLAYER_CTX_S *ctx, const char *input_file_path) {
	int64_t duration;
	int ret = 0;
	DemuxerInput stDemuxerInput;
	DemuxerParam tmp_param;
	void *tmp_handle = NULL;
	if (ctx->audio_status != UNINITIALIZED && ctx->enable_audio) {
		LOG_ERROR("need remove file before set new file\n");
		return -1;
	}
	if (ctx->video_status != UNINITIALIZED && ctx->enable_video) {
		LOG_ERROR("need remove file before set new file\n");
		return -1;
	}
	memset(ctx->input_file_path, 0, sizeof(ctx->input_file_path));
	strncpy(ctx->input_file_path, input_file_path, sizeof(ctx->input_file_path) - 1);
	memset(&stDemuxerInput, 0, sizeof(DemuxerInput));
	stDemuxerInput.ptr = (void *)ctx;
	stDemuxerInput.s8ReadModeFlag = true;
	stDemuxerInput.s8VideoEnableFlag = true;
	stDemuxerInput.s8AudioEnableFlag = true;
	ret = rkdemuxer_init(&tmp_handle, &stDemuxerInput);
	if (ret) {
		LOG_ERROR("rkdemuxer init failed\n");
		return -1;
	}

	memset(&tmp_param, 0, sizeof(tmp_param));
	ret = rkdemuxer_get_param(tmp_handle, ctx->input_file_path, &tmp_param);
	if (ret) {
		LOG_ERROR("rkdemuxer get param failed\n");
		rkdemuxer_deinit(&tmp_handle);
		return -1;
	}

	usleep(100 * 1000);
	if (tmp_param.pVideoCodec) {
		rkdemuxer_read_video_duration(tmp_handle, &duration);
		ctx->video_total_duraion = duration;
		LOG_DEBUG("get video duration %" PRId64 " \n", duration);
	}
	if (tmp_param.pAudioCodec) {
		rkdemuxer_read_audio_duration(tmp_handle, &duration);
		ctx->audio_total_duraion = duration;
		LOG_DEBUG("get audio duration %" PRId64 " \n", duration);
	}
	rkdemuxer_deinit(&tmp_handle);
	return 0;
}

static int set_record_file_unlocked(PLAYER_CTX_S *ctx, const char *input_file_path) {
	int ret = 0;
	DemuxerInput stDemuxerInput;
	if (ctx->audio_status != UNINITIALIZED && ctx->enable_audio) {
		LOG_ERROR("need remove file before set new file\n");
		return -1;
	}
	if (ctx->video_status != UNINITIALIZED && ctx->enable_video) {
		LOG_ERROR("need remove file before set new file\n");
		return -1;
	}
	memset(ctx->input_file_path, 0, sizeof(ctx->input_file_path));
	strncpy(ctx->input_file_path, input_file_path, sizeof(ctx->input_file_path) - 1);
	memset(&stDemuxerInput, 0, sizeof(DemuxerInput));
	stDemuxerInput.ptr = (void *)ctx;
	stDemuxerInput.s8ReadModeFlag = true;
	stDemuxerInput.s8VideoEnableFlag = true;
	stDemuxerInput.s8AudioEnableFlag = true;
	ret = rkdemuxer_init(&ctx->demuxer_handle, &stDemuxerInput);
	if (ret) {
		LOG_ERROR("rkdemuxer init failed\n");
		return -1;
	}

	memset(&ctx->demuxer_param, 0, sizeof(ctx->demuxer_param));
	ret = rkdemuxer_get_param(ctx->demuxer_handle, ctx->input_file_path, &ctx->demuxer_param);
	if (ret) {
		LOG_ERROR("rkdemuxer get param failed\n");
		rkdemuxer_deinit(&ctx->demuxer_handle);
		return -1;
	}

	LOG_INFO("demuxer file %s\n", ctx->input_file_path);
	LOG_INFO("total time %" PRId64 "  ms, width %d, height %d, format %d\n", ctx->video_total_duraion / 1000,
	         ctx->demuxer_param.s32VideoWidth, ctx->demuxer_param.s32VideoHeigh,
	         ctx->demuxer_param.s8VideoFormat);
	LOG_INFO("video codec %s, avg fps %d, first pts %" PRId64 " , timebase %d/%d\n",
	         ctx->demuxer_param.pVideoCodec, ctx->demuxer_param.s32VideoAvgFrameRate,
	         ctx->demuxer_param.s64VideoFirstPTS, ctx->demuxer_param.s32VideoTimeBaseNum,
	         ctx->demuxer_param.s32VideoTimeBaseDen);
	LOG_INFO("audio codec %s, channel %d, sample rate %d, first pts %" PRId64 " , timebase %d/%d\n",
	         ctx->demuxer_param.pAudioCodec, ctx->demuxer_param.s32AudioChannels,
	         ctx->demuxer_param.s32AudioSampleRate, ctx->demuxer_param.s64AudioFirstPTS,
	         ctx->demuxer_param.s32AudioTimeBaseNum, ctx->demuxer_param.s32AudioTimeBaseDen);

	ctx->enable_audio = (ctx->demuxer_param.pAudioCodec != NULL) ? true : false;
	ctx->enable_video = (ctx->demuxer_param.pVideoCodec != NULL) ? true : false;
	return 0;
}

static int remove_record_file_unlocked(PLAYER_CTX_S *ctx) {
	if (ctx->video_status == UNINITIALIZED && ctx->audio_status == UNINITIALIZED)
		return 0;
	LOG_DEBUG("remove record file %s\n", ctx->input_file_path);
	ctx->video_total_duraion = 0;
	ctx->audio_total_duraion = 0;
	rkdemuxer_deinit(&ctx->demuxer_handle);
	ctx->video_status = UNINITIALIZED;
	ctx->audio_status = UNINITIALIZED;
	ctx->enable_audio = false;
	ctx->enable_video = false;
	return 0;
}

int rk_player_create(RK_PLAYER_CONFIG_S *config) {
	LOG_INFO("enter\n");
	RKIPC_CHECK_POINTER(config, -1);
	PLAYER_CTX_S *ctx = malloc(sizeof(PLAYER_CTX_S));
	if (!ctx) {
		LOG_ERROR("malloc failed\n");
		return -1;
	}
	memset(ctx, 0, sizeof(PLAYER_CTX_S));
	ctx->event_cb = config->event_cb;
	ctx->skip_init_vo = config->skip_init_vo;
	ctx->skip_init_vo_layer = config->skip_init_vo_layer;
	config->ctx = ctx;
	pthread_mutex_init(&ctx->mutex, NULL);
	return 0;
}

int rk_player_set_file(RK_PLAYER_CONFIG_S *config, const char *input_file_path) {
	int ret = 0;
	LOG_INFO("set file %s\n", input_file_path);
	RKIPC_CHECK_POINTER(config, -1);
	RKIPC_CHECK_POINTER(input_file_path, -1);
	PLAYER_CTX_S *ctx = (PLAYER_CTX_S *)config->ctx;
	RKIPC_CHECK_POINTER(ctx, -1);
	if (access(input_file_path, F_OK) != 0) {
		LOG_ERROR("can't access file %s\n", input_file_path);
		return -1;
	}
	ctx->thread_running = false;
	if (ctx->enable_video)
		pthread_join(ctx->video_player_thread_id, NULL);
	if (ctx->enable_audio)
		pthread_join(ctx->audio_player_thread_id, NULL);
	pthread_mutex_lock(&ctx->mutex);
	destroy_media_pipe_unlocked(ctx);
	remove_record_file_unlocked(ctx);
	read_duration_unlocked(ctx, input_file_path);
	ret = set_record_file_unlocked(ctx, input_file_path);
	if (ret != 0) {
		LOG_ERROR("set file failed\n");
		goto __unlock;
	}
	ret = create_media_pipe_unlocked(ctx);
	if (ret != 0) {
		LOG_ERROR("initialize failed\n");
		goto __unlock;
	}
	if (ctx->enable_video) {
		ctx->video_status = PAUSE;
		ctx->thread_running = true;
		pthread_create(&ctx->video_player_thread_id, NULL, video_player_loop, (void *)ctx);
	}
	if (ctx->enable_audio) {
		ctx->audio_status = PAUSE;
		ctx->thread_running = true;
		pthread_create(&ctx->audio_player_thread_id, NULL, audio_player_loop, (void *)ctx);
	}
__unlock:
	pthread_mutex_unlock(&ctx->mutex);
	return ret;
}

int rk_player_play(RK_PLAYER_CONFIG_S *config) {
	int ret = 0;
	LOG_INFO("enter\n");
	RKIPC_CHECK_POINTER(config, -1);
	PLAYER_CTX_S *ctx = (PLAYER_CTX_S *)config->ctx;
	RKIPC_CHECK_POINTER(ctx, -1);
	pthread_mutex_lock(&ctx->mutex);
	if (ctx->enable_video) {
		if (ctx->video_status == UNINITIALIZED) {
			ret = -1;
			LOG_ERROR("change video_status in a uninitialized player!\n");
		} else {
			ctx->video_status = RUNNING;
		}
	}
	if (ctx->enable_audio) {
		if (ctx->audio_status == UNINITIALIZED) {
			LOG_ERROR("change audio_status in a uninitialized player!\n");
		} else {
			ctx->audio_status = RUNNING;
		}
	}
	pthread_mutex_unlock(&ctx->mutex);
	if (!ret && ctx->event_cb)
		ctx->event_cb(RK_PLAYER_PLAY);
	return ret;
}

int rk_player_pause(RK_PLAYER_CONFIG_S *config) {
	int ret = 0;
	LOG_INFO("enter\n");
	RKIPC_CHECK_POINTER(config, -1);
	PLAYER_CTX_S *ctx = (PLAYER_CTX_S *)config->ctx;
	RKIPC_CHECK_POINTER(ctx, -1);
	pthread_mutex_lock(&ctx->mutex);
	if (ctx->enable_video) {
		if (ctx->video_status == UNINITIALIZED) {
			ret = -1;
			LOG_ERROR("change video_status in a uninitialized player!\n");
		} else if (ctx->video_status == RUNNING) {
			ctx->video_status = PAUSE;
		}
	}
	if (ctx->enable_audio) {
		if (ctx->audio_status == UNINITIALIZED) {
			LOG_ERROR("change audio_status in a uninitialized player!\n");
		} else if (ctx->audio_status == RUNNING) {
			ctx->audio_status = PAUSE;
		}
	}
	pthread_mutex_unlock(&ctx->mutex);
	if (!ret && ctx->event_cb)
		ctx->event_cb(RK_PLAYER_PAUSE);
	return ret;
}

int rk_player_stop(RK_PLAYER_CONFIG_S *config) {
	int ret = 0;
	LOG_INFO("enter\n");
	RKIPC_CHECK_POINTER(config, -1);
	PLAYER_CTX_S *ctx = (PLAYER_CTX_S *)config->ctx;
	RKIPC_CHECK_POINTER(ctx, -1);
	pthread_mutex_lock(&ctx->mutex);
	if (ctx->enable_video) {
		if (ctx->video_status == UNINITIALIZED) {
			ret = -1;
			LOG_ERROR("change video_status in a uninitialized player!\n");
		} else if (ctx->video_status == RUNNING) {
			ctx->video_status = PAUSE;
		}
		ret = rkdemuxer_seek_video_pts(ctx->demuxer_handle, 0);
		if (ret)
			LOG_ERROR("seek video pts 0 failed\n");
	}
	if (ctx->enable_audio) {
		if (ctx->audio_status == UNINITIALIZED) {
			LOG_ERROR("change audio_status in a uninitialized player!\n");
		} else if (ctx->audio_status == RUNNING) {
			ctx->audio_status = PAUSE;
		}
		ret = rkdemuxer_seek_audio_pts(ctx->demuxer_handle, 0);
		if (ret)
			LOG_ERROR("seek audio pts 0 failed\n");
	}
	pthread_mutex_unlock(&ctx->mutex);
	if (ctx->event_cb)
		ctx->event_cb(RK_PLAYER_STOP);

	return ret;
}

int rk_player_destroy(RK_PLAYER_CONFIG_S *config) {
	LOG_INFO("enter\n");
	RKIPC_CHECK_POINTER(config, -1);
	PLAYER_CTX_S *ctx = (PLAYER_CTX_S *)config->ctx;
	RKIPC_CHECK_POINTER(ctx, -1);
	ctx->thread_running = false;
	if (ctx->enable_video)
		pthread_join(ctx->video_player_thread_id, NULL);
	if (ctx->enable_audio)
		pthread_join(ctx->audio_player_thread_id, NULL);
	pthread_mutex_lock(&ctx->mutex);
	destroy_media_pipe_unlocked(ctx);
	remove_record_file_unlocked(ctx);
	pthread_mutex_unlock(&ctx->mutex);
	pthread_mutex_destroy(&ctx->mutex);
	free(ctx);
	config->ctx = NULL;
	return 0;
}

int rk_player_video_seek(RK_PLAYER_CONFIG_S *config, uint64_t time_ms) {
	int ret = 0;
	RKIPC_CHECK_POINTER(config, -1);
	PLAYER_CTX_S *ctx = (PLAYER_CTX_S *)config->ctx;
	RKIPC_CHECK_POINTER(ctx, -1);
	if (time_ms < 0 || time_ms > ctx->video_total_duraion / 1000) {
		LOG_ERROR("invalid time_ms %" PRIu64 " \n", time_ms);
		return -1;
	}
	pthread_mutex_lock(&ctx->mutex);
	if (ctx->enable_video) {
		if (ctx->video_status == UNINITIALIZED) {
			ret = -1;
			LOG_ERROR("change video_status in a uninitialized player!\n");
		} else {
			ret = rkdemuxer_seek_video_pts(ctx->demuxer_handle, time_ms * 1000);
			ctx->video_status = RUNNING;
		}
	}
	pthread_mutex_unlock(&ctx->mutex);
	LOG_DEBUG("seek video to %" PRIu64 "  ms\n", time_ms);
	return ret;
}

int rk_player_audio_seek(RK_PLAYER_CONFIG_S *config, uint64_t time_ms) {
	int ret = 0;
	RKIPC_CHECK_POINTER(config, -1);
	PLAYER_CTX_S *ctx = (PLAYER_CTX_S *)config->ctx;
	RKIPC_CHECK_POINTER(ctx, -1);
	if (time_ms < 0 || time_ms > ctx->video_total_duraion / 1000) {
		LOG_ERROR("invalid time_ms %" PRIu64 " \n", time_ms);
		return -1;
	}
	pthread_mutex_lock(&ctx->mutex);
	if (ctx->enable_audio) {
		if (ctx->audio_status == UNINITIALIZED) {
			ret = -1;
			LOG_ERROR("change audio_status in a uninitialized player!\n");
		} else {
			ret = rkdemuxer_seek_audio_pts(ctx->demuxer_handle, time_ms * 1000);
			ctx->audio_status = RUNNING;
		}
	}
	pthread_mutex_unlock(&ctx->mutex);
	LOG_DEBUG("seek audio to %" PRIu64 "  ms\n", time_ms);
	return ret;
}

int rk_player_get_video_duration(RK_PLAYER_CONFIG_S *config, uint32_t *duration) {
	int ret = 0;
	LOG_INFO("enter\n");
	RKIPC_CHECK_POINTER(config, -1);
	RKIPC_CHECK_POINTER(duration, -1);
	PLAYER_CTX_S *ctx = (PLAYER_CTX_S *)config->ctx;
	RKIPC_CHECK_POINTER(ctx, -1);
	pthread_mutex_lock(&ctx->mutex);
	*duration = (uint32_t)ctx->video_total_duraion / 1000;
	pthread_mutex_unlock(&ctx->mutex);
	LOG_DEBUG("get video duration %u ms\n", *duration);
	return 0;
}

int rk_player_get_audio_duration(RK_PLAYER_CONFIG_S *config, uint32_t *duration) {
	int ret = 0;
	RKIPC_CHECK_POINTER(config, -1);
	RKIPC_CHECK_POINTER(duration, -1);
	PLAYER_CTX_S *ctx = (PLAYER_CTX_S *)config->ctx;
	RKIPC_CHECK_POINTER(ctx, -1);
	pthread_mutex_lock(&ctx->mutex);
	*duration = (uint32_t)ctx->audio_total_duraion / 1000;
	pthread_mutex_unlock(&ctx->mutex);
	LOG_DEBUG("get audio duration %u ms\n", *duration);
	return 0;
}

int rk_player_get_video_position(RK_PLAYER_CONFIG_S *config, uint32_t *position) {
	int ret = 0;
	RKIPC_CHECK_POINTER(config, -1);
	PLAYER_CTX_S *ctx = (PLAYER_CTX_S *)config->ctx;
	RKIPC_CHECK_POINTER(ctx, -1);
	pthread_mutex_lock(&ctx->mutex);
	if (ctx->enable_video)
		*position = (ctx->video_current_pts / 1000);
	pthread_mutex_unlock(&ctx->mutex);
	LOG_DEBUG("get video position %u ms\n", *position);
	return 0;
}

int rk_player_get_audio_position(RK_PLAYER_CONFIG_S *config, uint32_t *position) {
	int ret = 0;
	RKIPC_CHECK_POINTER(config, -1);
	PLAYER_CTX_S *ctx = (PLAYER_CTX_S *)config->ctx;
	RKIPC_CHECK_POINTER(ctx, -1);
	pthread_mutex_lock(&ctx->mutex);
	if (ctx->enable_audio)
		*position = (ctx->audio_current_pts / 1000);
	pthread_mutex_unlock(&ctx->mutex);
	LOG_DEBUG("get audio position %u ms\n", *position);
	return 0;
}

int rk_player_get_photo(RK_PHOTO_CONFIG *config, RK_PHOTO_DATA *data) {
	RKIPC_CHECK_POINTER(config, -1);
	RKIPC_CHECK_POINTER(data, -1);
	RKIPC_CHECK_POINTER(config->file_path, -1);
	if (config->output_width == 0 || config->output_height == 0) {
		LOG_ERROR("invalid output size");
		return -1;
	}
	if (!validate_format(config->output_format)) {
		LOG_ERROR("invalid output format\n");
		return -1;
	}
	int fd = -1;
	int ret = 0;
	uint32_t width, height;
	uint32_t buf_size = 0;
	uint8_t *buf = NULL;
	MB_BLK mb_buffer = NULL;
	struct stat statbuf;
	MB_EXT_CONFIG_S mb_ext_config;

	// Parse jpeg file.
	fd = open(config->file_path, O_RDONLY);
	if (fd < 0) {
		LOG_ERROR("open file %s error: %s\n", config->file_path, strerror(errno));
		return -1;
	}
	ret = fstat(fd, &statbuf);
	if (ret != 0) {
		LOG_ERROR("get file %s stat error: %s\n", config->file_path, strerror(errno));
		close(fd);
		return -1;
	}
	buf_size = statbuf.st_size;
	buf = malloc(buf_size);
	if (!buf) {
		LOG_ERROR("malloc error\n");
		close(fd);
	}
	ret = read(fd, buf, buf_size);
	if (ret <= 0) {
		LOG_ERROR("read error %s\n", strerror(errno));
		close(fd);
		free(buf);
		return -1;
	}
	if (config->input_width == 0 || config->input_height == 0) {
		ret = get_jpeg_resolution(buf, buf_size, &config->input_width, &config->input_height);
		if (ret != 0) {
			LOG_ERROR("get jpeg resolution failed\n");
			close(fd);
			free(buf);
			return -1;
		}
	}
	LOG_INFO("read jpeg %s: width %u, height %u, jpeg size %u\n", config->file_path,
	         config->input_width, config->input_height, buf_size);

	// Prepare vdec input frame.
	memset(&mb_ext_config, 0, sizeof(MB_EXT_CONFIG_S));
	mb_ext_config.pFreeCB = buffer_free_cb;
	mb_ext_config.pOpaque = (void *)buf;
	mb_ext_config.pu8VirAddr = (RK_U8 *)buf;
	mb_ext_config.u64Size = buf_size;
	ret = RK_MPI_SYS_CreateMB(&mb_buffer, &mb_ext_config);
	if (ret != 0) {
		LOG_ERROR("RK_MPI_SYS_CreateMB failed %#X\n", ret);
		close(fd);
		free(buf);
		return -1;
	}
	// Decode jpeg.
	ret = decode_one_jpeg(config, data, mb_buffer, buf_size);
	if (ret != 0)
		LOG_ERROR("decode jpeg failed\n");
	RKIPC_CHECK_POINTER(data, -1);

	RK_MPI_MB_ReleaseMB(mb_buffer);
	close(fd);
	return ret;
}

int rk_player_release_photo(RK_PHOTO_DATA *data) {
	RKIPC_CHECK_POINTER(data, -1);
	RKIPC_CHECK_POINTER(data->data, -1);
	free(data->data);
	return 0;
}
