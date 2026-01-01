// Copyright 2021 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "common.h"
#include "log.h"
#include "rkaudio_mp3.h"
#include "rtsp.h"
#include "storage.h"

#include <rk_debug.h>
#include <rk_mpi_adec.h>
#include <rk_mpi_aenc.h>
#include <rk_mpi_ai.h>
#include <rk_mpi_amix.h>
#include <rk_mpi_ao.h>
#include <rk_mpi_mb.h>
#include <rk_mpi_sys.h>
#include <stdint.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "audio.c"

#define ADEC_CHN_ID 0
#define AO_CHN_ID 0
#define AO_DEV_ID 0

pthread_t save_ai_tid, save_aenc_tid, ai_get_detect_result_tid;
static int ai_dev_id = 0;
static int ao_dev_id = 0;
static int usb_ai_dev_id = 1;
static int usb_ao_dev_id = 1;
static int ai_chn_id = 0;
static int aenc_dev_id = 0;
static int aenc_chn_id = 0;
static int g_audio_run_ = 1;
static int enable_aed, enable_bcd, enable_vqe;
MPP_CHN_S ai_chn, aenc_chn;

static void *ai_get_detect_result(void *arg);

void *save_ai_thread(void *ptr) {
	int ret = 0;
	int s32MilliSec = -1;
	AUDIO_FRAME_S frame;

	AUDIO_SAVE_FILE_INFO_S save;
	save.bCfg = RK_TRUE;
	save.u32FileSize = 1024;
	snprintf(save.aFilePath, sizeof(save.aFilePath), "%s", "/tmp/");
	snprintf(save.aFileName, sizeof(save.aFileName), "%s", "cap_out.pcm");
	RK_MPI_AI_SaveFile(ai_dev_id, ai_chn_id, &save);

	while (g_audio_run_) {
		ret = RK_MPI_AI_GetFrame(ai_dev_id, ai_chn_id, &frame, RK_NULL, s32MilliSec);
		if (ret == 0) {
			void *data = RK_MPI_MB_Handle2VirAddr(frame.pMbBlk);
			LOG_DEBUG("data = %p, len = %d\n", data, frame.u32Len);
			RK_MPI_AI_ReleaseFrame(ai_dev_id, ai_chn_id, &frame, RK_NULL);
		}
	}

	return RK_NULL;
}
static RK_S64 fake_time = 0;
void *save_aenc_thread(void *ptr) {
	prctl(PR_SET_NAME, "save_aenc_thread", 0, 0, 0);
	int s32ret = 0;
	FILE *file = RK_NULL;
	AUDIO_STREAM_S pstStream;
	RK_S32 eos = 0;
	RK_S32 count = 0;
	char encode_type[16] = {'\0'};

	LOG_INFO("enter\n");
	const char *tmp = rk_param_get_string("audio.0:encode_type", NULL);
	if (tmp == NULL) {
		LOG_ERROR("not found audio.0:encode_type\n");
		return NULL;
	}
	strncpy(encode_type, tmp, strlen(tmp));
	// file = fopen("/tmp/aenc.mp3", "wb+");
	// if (file == RK_NULL) {
	// 	LOG_ERROR("failed to open /tmp/aenc.mp3, error: %s\n", strerror(errno));
	// 	return RK_NULL;
	// }

	while (g_audio_run_) {
		s32ret = RK_MPI_AENC_GetStream(aenc_chn_id, &pstStream, 1000);
		if (s32ret == 0) {
			MB_BLK bBlk = pstStream.pMbBlk;
			void *buffer = RK_MPI_MB_Handle2VirAddr(bBlk);
			eos = (pstStream.u32Len <= 0) ? 1 : 0;
			if (buffer) {
				// LOG_INFO("get frame data = %p, size = %d, pts is %lld, seq is %d\n", buffer,
				//          pstStream.u32Len, pstStream.u64TimeStamp, pstStream.u32Seq);
				if (!strcmp(encode_type, "MP2") || !strcmp(encode_type, "MP3")) {
					rk_storage_write_audio_frame(0, buffer, pstStream.u32Len,
					                             pstStream.u64TimeStamp);
					rk_storage_write_audio_frame(1, buffer, pstStream.u32Len,
					                             pstStream.u64TimeStamp);
					rk_storage_write_audio_frame(2, buffer, pstStream.u32Len,
					                             pstStream.u64TimeStamp);
				} else if (!strcmp(encode_type, "G711A")) {
					rkipc_rtsp_write_audio_frame(0, buffer, pstStream.u32Len,
					                             pstStream.u64TimeStamp);
				}
				// if (file) {
				// 	fwrite(buffer, pstStream.u32Len, 1, file);
				// 	fflush(file);
				// }
				RK_MPI_AENC_ReleaseStream(aenc_chn_id, &pstStream);
				count++;
			}
		} else {
			LOG_ERROR("fail to get aenc frame\n");
		}
		if (eos) {
			LOG_INFO("get eos stream\n");
			break;
		}
	}
	// if (file) {
	// 	fclose(file);
	// 	file = RK_NULL;
	// }
	LOG_INFO("exit\n");

	return RK_NULL;
}

int rkipc_audio_aed_init() {
	int result;
	AI_AED_CONFIG_S ai_aed_config;

	ai_aed_config.fSnrDB = 10.0f;
	ai_aed_config.fLsdDB = -25.0f;
	ai_aed_config.s32Policy = 1;
	result = RK_MPI_AI_SetAedAttr(ai_dev_id, ai_chn_id, &ai_aed_config);
	if (result != 0) {
		LOG_ERROR("RK_MPI_AI_SetAedAttr(%d,%d) failed with %#x\n", ai_dev_id, ai_chn_id, result);
		return result;
	}
	LOG_DEBUG("RK_MPI_AI_SetAedAttr(%d,%d) success\n", ai_dev_id, ai_chn_id);
	result = RK_MPI_AI_EnableAed(ai_dev_id, ai_chn_id);
	if (result != 0) {
		LOG_ERROR("RK_MPI_AI_EnableAed(%d,%d) failed with %#x\n", ai_dev_id, ai_chn_id, result);
		return result;
	}
	LOG_DEBUG("RK_MPI_AI_EnableAed(%d,%d) success\n", ai_dev_id, ai_chn_id);

	return result;
}

int rkipc_audio_bcd_init() {
	int result;
	AI_BCD_CONFIG_S ai_bcd_config;

	ai_bcd_config.mFrameLen = 50;
	ai_bcd_config.mConfirmProb = 0.83f;
	const char *bcd_model_path =
	    rk_param_get_string("audio.0:bcd_model_path", "/oem/usr/lib/rkaudio_model_sed_bcd.rknn");
	memcpy(ai_bcd_config.aModelPath, bcd_model_path, strlen(bcd_model_path) + 1);

	result = RK_MPI_AI_SetBcdAttr(ai_dev_id, ai_chn_id, &ai_bcd_config);
	if (result != 0) {
		LOG_ERROR("RK_MPI_AI_SetBcdAttr(%d,%d) failed with %#x\n", ai_dev_id, ai_chn_id, result);
		return result;
	}
	LOG_DEBUG("RK_MPI_AI_SetBcdAttr(%d,%d) success\n", ai_dev_id, ai_chn_id);
	result = RK_MPI_AI_EnableBcd(ai_dev_id, ai_chn_id);
	if (result != 0) {
		LOG_ERROR("RK_MPI_AI_EnableBcd(%d,%d) failed with %#x\n", ai_dev_id, ai_chn_id, result);
		return result;
	}
	LOG_DEBUG("RK_MPI_AI_EnableBcd(%d,%d) success\n", ai_dev_id, ai_chn_id);

	return result;
}

int rkipc_audio_vqe_init() {
	int result;
	AI_VQE_CONFIG_S stAiVqeConfig;
	int vqe_gap_ms = 16;
	if (vqe_gap_ms != 16 && vqe_gap_ms != 10) {
		LOG_ERROR("Invalid gap: %d, just supports 16ms or 10ms for AI VQE", vqe_gap_ms);
		return RK_FAILURE;
	}
	memset(&stAiVqeConfig, 0, sizeof(AI_VQE_CONFIG_S));
	stAiVqeConfig.enCfgMode = AIO_VQE_CONFIG_LOAD_FILE;
	memcpy(stAiVqeConfig.aCfgFile, "/oem/usr/share/vqefiles/config_aivqe.json",
	       strlen("/oem/usr/share/vqefiles/config_aivqe.json"));

	const char *vqe_cfg =
	    rk_param_get_string("audio.0:vqe_cfg", "/oem/usr/share/vqefiles/config_aivqe.json");
	memcpy(stAiVqeConfig.aCfgFile, vqe_cfg, strlen(vqe_cfg) + 1);
	memset(stAiVqeConfig.aCfgFile + strlen(vqe_cfg) + 1, '\0', sizeof(char));
	LOG_INFO("stAiVqeConfig.aCfgFile = %s\n", stAiVqeConfig.aCfgFile);

	stAiVqeConfig.s32WorkSampleRate = rk_param_get_int("audio.0:sample_rate", 16000);
	stAiVqeConfig.s32FrameSample =
	    rk_param_get_int("audio.0:sample_rate", 16000) * vqe_gap_ms / 1000;

	if (rk_param_get_int("audio.0:channels", 2) == 4) {
		stAiVqeConfig.s64RefChannelType = 0x04;
		stAiVqeConfig.s64RecChannelType = 0x03;
	} else {
		stAiVqeConfig.s64RefChannelType = 0x02;
		stAiVqeConfig.s64RecChannelType = 0x01;
	}
	result = RK_MPI_AI_SetVqeAttr(ai_dev_id, ai_chn_id, 0, 0, &stAiVqeConfig);
	if (result != 0) {
		LOG_ERROR("RK_MPI_AI_SetVqeAttr(%d,%d) failed with %#x", ai_dev_id, ai_chn_id, result);
		return result;
	}
	LOG_DEBUG("RK_MPI_AI_SetVqeAttr(%d,%d) success\n", ai_dev_id, ai_chn_id);
	result = RK_MPI_AI_EnableVqe(ai_dev_id, ai_chn_id);
	if (result != 0) {
		LOG_ERROR("RK_MPI_AI_EnableVqe(%d,%d) failed with %#x", ai_dev_id, ai_chn_id, result);
		return result;
	}
	LOG_DEBUG("RK_MPI_AI_EnableVqe(%d,%d) success\n", ai_dev_id, ai_chn_id);

	return result;
}

static void *ai_get_detect_result(void *arg) {
	printf("#Start %s thread, arg:%p\n", __func__, arg);
	prctl(PR_SET_NAME, "ai_get_detect_result", 0, 0, 0);
	int result;

	while (g_audio_run_) {
		usleep(1000 * 1000);
		AI_AED_RESULT_S aed_result;
		AI_BCD_RESULT_S bcd_result;
		memset(&aed_result, 0, sizeof(aed_result));
		memset(&bcd_result, 0, sizeof(bcd_result));
		if (enable_aed) {
			result = RK_MPI_AI_GetAedResult(ai_dev_id, ai_chn_id, &aed_result);
			if (result == 0) {
				LOG_DEBUG("aed_result: %d, %d", aed_result.bAcousticEventDetected,
				          aed_result.bLoudSoundDetected);
			}
		}
		if (enable_bcd) {
			result = RK_MPI_AI_GetBcdResult(ai_dev_id, ai_chn_id, &bcd_result);
			if (result == 0) {
				LOG_DEBUG("bcd_result: %d", bcd_result.bBabyCry);
			}
		}
	}

	return 0;
}

int rkipc_ai_init() {
	int ret;
	AUDIO_DEV aiDevId = ai_dev_id;
	AIO_ATTR_S aiAttr;

	memset(&aiAttr, 0, sizeof(AIO_ATTR_S));
	const char *card_name = rk_param_get_string("audio.0:card_name", "default");
	snprintf(aiAttr.u8CardName, sizeof(aiAttr.u8CardName), "%s", card_name);
	LOG_INFO("aiAttr.u8CardName is %s\n", aiAttr.u8CardName);
	aiAttr.soundCard.channels = 2;
	aiAttr.soundCard.sampleRate = rk_param_get_int("audio.0:sample_rate", 16000);
	const char *format = rk_param_get_string("audio.0:format", NULL);
	if (!strcmp(format, "S16")) {
		aiAttr.soundCard.bitWidth = AUDIO_BIT_WIDTH_16;
		aiAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
	} else if (!strcmp(format, "U8")) {
		aiAttr.soundCard.bitWidth = AUDIO_BIT_WIDTH_8;
		aiAttr.enBitwidth = AUDIO_BIT_WIDTH_8;
	} else {
		LOG_ERROR("not support %s\n", format);
	}
	aiAttr.enSamplerate = rk_param_get_int("audio.0:sample_rate", 16000);
	aiAttr.u32FrmNum = 4;
	aiAttr.u32PtNumPerFrm = rk_param_get_int("audio.0:frame_size", 1024);
	aiAttr.u32EXFlag = 0;
	aiAttr.u32ChnCnt = 2;
	if (rk_param_get_int("audio.0:channels", 2) == 2)
		aiAttr.enSoundmode = AUDIO_SOUND_MODE_STEREO;
	else
		aiAttr.enSoundmode = AUDIO_SOUND_MODE_MONO;

	ret = RK_MPI_AI_SetPubAttr(ai_dev_id, &aiAttr);
	if (ret != 0) {
		LOG_ERROR("ai set attr fail, reason = %d\n", ret);
		return RK_FAILURE;
	}

	ret = RK_MPI_AI_Enable(ai_dev_id);
	if (ret != 0) {
		LOG_ERROR("ai enable fail, reason = %d\n", ret);
		return RK_FAILURE;
	}

	// aed bcd vqe
	enable_aed = rk_param_get_int("audio.0:enable_aed", 0);
	enable_bcd = rk_param_get_int("audio.0:enable_bcd", 0);
	enable_vqe = rk_param_get_int("audio.0:enable_vqe", 0);
	if (enable_aed)
		rkipc_audio_aed_init();
	if (enable_bcd)
		rkipc_audio_bcd_init();
	if (enable_vqe)
		rkipc_audio_vqe_init();
	if (enable_aed || enable_bcd)
		pthread_create(&ai_get_detect_result_tid, RK_NULL, ai_get_detect_result, NULL);

	ret = RK_MPI_AI_EnableChn(ai_dev_id, ai_chn_id);
	if (ret != 0) {
		LOG_ERROR("ai enable channel fail, aoChn = %d, reason = %x\n", ai_chn_id, ret);
		return RK_FAILURE;
	}

	// ret = RK_MPI_AI_EnableReSmp(ai_dev_id, ai_chn_id,
	//                               (AUDIO_SAMPLE_RATE_E)params->s32SampleRate);
	// if (ret != 0) {
	//     LOG_ERROR("ai enable resample fail, reason = %x, aoChn = %d\n", ret, ai_chn_id);
	//     return RK_FAILURE;
	// }

	RK_MPI_AI_SetVolume(ai_dev_id, rk_param_get_int("audio.0:volume", 100));
	if (rk_param_get_int("audio.0:channels", 2) == 1) {
		RK_MPI_AI_SetTrackMode(ai_dev_id, AUDIO_TRACK_FRONT_LEFT);
	}

	// pthread_create(&save_ai_tid, RK_NULL, save_ai_thread, NULL);

	return 0;
}

int rkipc_ai_deinit() {
	// pthread_join(save_ai_tid, RK_NULL);
	// RK_MPI_AI_DisableReSmp(ai_dev_id, ai_chn_id);
	int ret = RK_MPI_AI_DisableChn(ai_dev_id, ai_chn_id);
	if (ret != 0) {
		LOG_ERROR("ai disable channel fail, reason = %d\n", ret);
		return RK_FAILURE;
	}
	LOG_DEBUG("RK_MPI_AI_DisableChn success\n");

	ret = RK_MPI_AI_Disable(ai_dev_id);
	if (ret != 0) {
		LOG_ERROR("ai disable fail, reason = %d\n", ret);
		return RK_FAILURE;
	}
	LOG_DEBUG("RK_MPI_AI_Disable success\n");

	return 0;
}

int rkipc_aenc_init() {
	AENC_CHN_ATTR_S stAencAttr;
	const char *encode_type = rk_param_get_string("audio.0:encode_type", NULL);

	memset(&stAencAttr, 0, sizeof(AENC_CHN_ATTR_S));
	if (!strcmp(encode_type, "MP2")) {
		stAencAttr.enType = RK_AUDIO_ID_MP2;
		stAencAttr.stCodecAttr.enType = RK_AUDIO_ID_MP2;
	} else if (!strcmp(encode_type, "G711A")) {
		stAencAttr.enType = RK_AUDIO_ID_PCM_ALAW;
		stAencAttr.stCodecAttr.enType = RK_AUDIO_ID_PCM_ALAW;
	} else if (!strcmp(encode_type, "MP3")) {
		stAencAttr.enType = RK_AUDIO_ID_MP3;
		stAencAttr.stCodecAttr.enType = RK_AUDIO_ID_MP3;
		stAencAttr.stCodecAttr.u32Resv[0] = 1152;
		stAencAttr.stCodecAttr.u32Resv[1] = 160000;
		register_aenc_mp3();
	} else {
		LOG_ERROR("not support %s\n", encode_type);
	}
	stAencAttr.stCodecAttr.u32Channels = rk_param_get_int("audio.0:channels", 2);
	stAencAttr.stCodecAttr.u32SampleRate = rk_param_get_int("audio.0:sample_rate", 16000);
	const char *format = rk_param_get_string("audio.0:format", NULL);
	if (!strcmp(format, "S16")) {
		stAencAttr.stCodecAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
	} else if (!strcmp(format, "U8")) {
		stAencAttr.stCodecAttr.enBitwidth = AUDIO_BIT_WIDTH_8;
	} else {
		LOG_ERROR("not support %s\n", format);
	}
	stAencAttr.u32BufCount = 4;

	int ret = RK_MPI_AENC_CreateChn(aenc_chn_id, &stAencAttr);
	if (ret) {
		LOG_ERROR("create aenc chn %d err:0x%x\n", aenc_chn_id, ret);
		return RK_FAILURE;
	}
	LOG_DEBUG("create aenc chn %d success\n", aenc_chn_id);

	pthread_create(&save_aenc_tid, RK_NULL, save_aenc_thread, NULL);

	return 0;
}

int rkipc_aenc_deinit() {
	int ret = RK_MPI_AENC_DestroyChn(aenc_chn_id);
	if (ret)
		LOG_ERROR("RK_MPI_AENC_DestroyChn fail\n");
	LOG_DEBUG("RK_MPI_AENC_DestroyChn success\n");

	return 0;
}

static bool check_rk730_sound_card(void) {
	FILE *fp;
	char line[256];
	bool found = false;

	fp = fopen("/proc/asound/cards", "r");
	if (fp == NULL) {
		printf("Failed to open /proc/asound/cards\n");
		return false;
	}

	while (fgets(line, sizeof(line), fp)) {
		if (strstr(line, "rk730") != NULL) {
			found = true;
			break;
		}
	}

	fclose(fp);
	return found;
}

int rkipc_ao_init_ex(int sample_rate, int channels, int frame_size) {
	int ret = 0;
	const char *card_name = rk_param_get_string("audio.0:card_name", "default");
	AUDIO_DEV aoDevId = 0;
	AO_CHN aoChn = 0;
	AIO_ATTR_S aoAttr;
	AO_CHN_PARAM_S pstParams;

	memset(&pstParams, 0, sizeof(AO_CHN_PARAM_S));
	memset(&aoAttr, 0, sizeof(AIO_ATTR_S));
	sprintf((char *)aoAttr.u8CardName, "%s", card_name);

	aoAttr.soundCard.channels = 2;
	aoAttr.soundCard.sampleRate = sample_rate;
	const char *format = rk_param_get_string("audio.0:format", NULL);
	if (!strcmp(format, "S16")) {
		aoAttr.soundCard.bitWidth = AUDIO_BIT_WIDTH_16;
		aoAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
	} else if (!strcmp(format, "U8")) {
		aoAttr.soundCard.bitWidth = AUDIO_BIT_WIDTH_8;
		aoAttr.enBitwidth = AUDIO_BIT_WIDTH_8;
	} else {
		LOG_ERROR("not support %s\n", format);
		return RK_FAILURE;
	}
	aoAttr.enSamplerate = (AUDIO_SAMPLE_RATE_E)sample_rate;

	if (channels == 1)
		aoAttr.enSoundmode = AUDIO_SOUND_MODE_MONO;
	else if (channels == 2)
		aoAttr.enSoundmode = AUDIO_SOUND_MODE_STEREO;
	else {
		LOG_ERROR("unsupport = %d\n", channels);
		return RK_FAILURE;
	}
	aoAttr.u32PtNumPerFrm = frame_size;
	aoAttr.u32FrmNum = 4;
	aoAttr.u32EXFlag = 0;
	aoAttr.u32ChnCnt = 2;

	if (check_rk730_sound_card()) {
		ret = RK_MPI_AMIX_SetControl(aoDevId, "spk switch", (char *)"on");
		ret |= RK_MPI_AMIX_SetControl(aoDevId, "hp switch", (char *)"on");
		ret |= RK_MPI_AMIX_SetControl(aoDevId, "OUT1 Switch", (char *)"on");
		ret |= RK_MPI_AMIX_SetControl(aoDevId, "OUT2 Switch", (char *)"on");
		if (ret != RK_SUCCESS) {
			LOG_ERROR("RK_MPI_AMIX_SetControl failed %#X\n", ret);
			return RK_FAILURE;
		}
	}
	RK_MPI_AO_SetPubAttr(aoDevId, &aoAttr);
	ret = RK_MPI_AO_Enable(aoDevId);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_AO_Enable failed %#X\n", ret);
		return RK_FAILURE;
	}
	/*==============================================================================*/
	pstParams.enLoopbackMode = AUDIO_LOOPBACK_NONE;
	ret = RK_MPI_AO_SetChnParams(aoDevId, aoChn, &pstParams);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("ao set channel params, aoChn = %d", aoChn);
		return RK_FAILURE;
	}

	if (channels == 1)
		RK_MPI_AO_SetTrackMode(aoDevId, AUDIO_TRACK_OUT_STEREO);
	else
		RK_MPI_AO_SetTrackMode(aoDevId, AUDIO_TRACK_NORMAL);
	if (rk_param_get_int("audio.0:enable_ao_vqe", 0)) {
		const char *vqe_file_path = rk_param_get_string("audio.0:ao_vqe_file", NULL);
		if (vqe_file_path) {
			AO_VQE_CONFIG_S vqe_config = {};
			strncpy(vqe_config.aCfgFile, vqe_file_path, strlen(vqe_file_path));
			vqe_config.enCfgMode = AIO_VQE_CONFIG_LOAD_FILE;
			ret = RK_MPI_AO_SetVqeAttr(aoDevId, aoChn, &vqe_config);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("RK_MPI_AO_SetVqeAttr failed %#X\n", ret);
				return RK_FAILURE;
			}
			ret = RK_MPI_AO_EnableVqe(aoDevId, aoChn);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("RK_MPI_AO_EnableVqe failed %#X\n", ret);
				return RK_FAILURE;
			}
		}
	}
	/*==============================================================================*/
	ret = RK_MPI_AO_EnableChn(aoDevId, aoChn);
	if (ret != 0) {
		LOG_ERROR("ao enable channel fail, aoChn = %d, reason = %x", aoChn, ret);
		return RK_FAILURE;
	}
	/*==============================================================================*/
	// set sample rate of input data
	ret = RK_MPI_AO_EnableReSmp(aoDevId, aoChn, (AUDIO_SAMPLE_RATE_E)sample_rate);
	if (ret != 0) {
		LOG_ERROR("ao enable channel fail, reason = %x, aoChn = %d", ret, aoChn);
		return RK_FAILURE;
	}

	ret = RK_MPI_AO_SetVolume(aoDevId, rk_param_get_int("audio.0:ao_volume", 100));
	if (ret != 0) {
		LOG_ERROR("RK_MPI_AO_SetVolume failed %#X\n", ret);
		return RK_FAILURE;
	}
	LOG_INFO("create ao success\n");

	return RK_SUCCESS;
}

int rkipc_ao_init(void) {
	int ret = 0;
	int sample_rate = rk_param_get_int("audio.0:sample_rate", 16000);
	int channels = rk_param_get_int("audio.0:channels", 2);
	int frame_size = rk_param_get_int("audio.0:ao_frame_size", 1024);
	return rkipc_ao_init_ex(sample_rate, channels, frame_size);
}

int rkipc_ao_deinit(void) {
	if (rk_param_get_int("audio.0:enable_ao_vqe", 0))
		RK_MPI_AO_DisableVqe(0, 0);
	RK_MPI_AO_DisableReSmp(0, 0);
	RK_MPI_AO_DisableChn(0, 0);
	RK_MPI_AO_Disable(0);
	if (check_rk730_sound_card()) {
		RK_MPI_AMIX_SetControl(0, "spk switch", (char *)"off");
		RK_MPI_AMIX_SetControl(0, "hp switch", (char *)"off");
		RK_MPI_AMIX_SetControl(0, "OUT1 Switch", (char *)"off");
		RK_MPI_AMIX_SetControl(0, "OUT2 Switch", (char *)"off");
	}
}

int rkipc_adec_init(int sample_rate, int channels, const char *encode_type) {
	int ret = 0;
	ADEC_CHN_ATTR_S pstChnAttr;
	memset(&pstChnAttr, 0, sizeof(ADEC_CHN_ATTR_S));

	if (encode_type == NULL) {
		LOG_ERROR("bad audio file format!\n");
		return -1;
	}
	ret = register_adec_mp3();
	if (ret != 0) {
		LOG_ERROR("register_adec_mp3 failed %#X\n", ret);
		return -1;
	}
	if (!strcmp(encode_type, "mp2")) {
		pstChnAttr.enType = RK_AUDIO_ID_MP2;
		pstChnAttr.stCodecAttr.enType = RK_AUDIO_ID_MP2;
	} else if (!strcmp(encode_type, "mp3")) {
		pstChnAttr.enType = RK_AUDIO_ID_MP3;
		pstChnAttr.stCodecAttr.enType = RK_AUDIO_ID_MP3;
	} else {
		LOG_ERROR("not support %s\n", encode_type);
		return -1;
	}
	pstChnAttr.stCodecAttr.u32Channels = channels; // default 1
	pstChnAttr.stCodecAttr.u32SampleRate = sample_rate;

	pstChnAttr.u32BufCount = 4;
	pstChnAttr.u32BufSize = 50 * 1024;
	ret = RK_MPI_ADEC_CreateChn(ADEC_CHN_ID, &pstChnAttr);
	if (ret) {
		LOG_ERROR("create adec chn %d err:0x%x\n", ADEC_CHN_ID, ret);
		ret = RK_FAILURE;
	}
	LOG_INFO("create adec success\n");
	return ret;
}

int rkipc_adec_deinit(void) {
	RK_MPI_ADEC_DestroyChn(ADEC_CHN_ID);
	unregister_adec_mp3();
	return 0;
}

int rkipc_audio_player_init(int sample_rate, int channels, const char *encode_type) {
	int ret = 0, frame_size = 0;
	MPP_CHN_S src_chn = {}, dst_chn = {};
	if (sample_rate == 32000 || sample_rate == 44100 || sample_rate == 48000)
		frame_size = 1152 * channels * 2;
	else
		frame_size = 576 * channels * 2;
	ret = rkipc_ao_init_ex(sample_rate, channels, frame_size);
	if (ret != RK_SUCCESS)
		return ret;
	ret = rkipc_adec_init(sample_rate, channels, encode_type);
	if (ret != RK_SUCCESS)
		return ret;
	src_chn.enModId = RK_ID_ADEC;
	src_chn.s32DevId = 0;
	src_chn.s32ChnId = 0;
	dst_chn.enModId = RK_ID_AO;
	dst_chn.s32DevId = 0;
	dst_chn.s32ChnId = 0;
	ret = RK_MPI_SYS_Bind(&src_chn, &dst_chn);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_SYS_Bind failed %#X\n", ret);
		return ret;
	}
	LOG_INFO("success\n");
	return RK_SUCCESS;
}

int rkipc_audio_player_deinit(void) {
	MPP_CHN_S src_chn = {}, dst_chn = {};
	src_chn.enModId = RK_ID_ADEC;
	src_chn.s32DevId = 0;
	src_chn.s32ChnId = 0;
	dst_chn.enModId = RK_ID_AO;
	dst_chn.s32DevId = 0;
	dst_chn.s32ChnId = 0;
	RK_MPI_SYS_UnBind(&src_chn, &dst_chn);
	rkipc_adec_deinit();
	rkipc_ao_deinit();
	LOG_INFO("success\n");
}

static int buffer_free_cb(void *opaque) {
	if (opaque) {
		free(opaque);
		opaque = NULL;
	}
	return 0;
}

int rkipc_audio_player_send(uint8_t *data, uint32_t len, uint64_t pts) {
	int ret = 0;
	AUDIO_STREAM_S audio_stream = {};
	MB_EXT_CONFIG_S mb_ext_config = {};
	if (len == 0) {
		RK_MPI_ADEC_SendEndOfStream(ADEC_CHN_ID, RK_FALSE);
	} else {
		audio_stream.u32Len = len;
		audio_stream.u64TimeStamp = pts;
		audio_stream.bBypassMbBlk = RK_TRUE;

		memset(&mb_ext_config, 0, sizeof(MB_EXT_CONFIG_S));
		mb_ext_config.pFreeCB = buffer_free_cb;
		mb_ext_config.pOpaque = (void *)data;
		mb_ext_config.pu8VirAddr = (RK_U8 *)data;
		mb_ext_config.u64Size = len;
		ret = RK_MPI_SYS_CreateMB(&(audio_stream.pMbBlk), &mb_ext_config);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("create mb failed!\n");
			return ret;
		}

		ret = RK_MPI_ADEC_SendStream(ADEC_CHN_ID, &audio_stream, RK_TRUE);
		if (ret != RK_SUCCESS)
			LOG_ERROR("RK_MPI_ADEC_SendStream failed[%x]\n", ret);
		RK_MPI_MB_ReleaseMB(audio_stream.pMbBlk);
	}
	LOG_DEBUG("send stream pts %" PRId64 ", len %d, data %p %s\n", pts, len, data,
	          ret ? "failed" : "success");
	return ret;
}

int rkipc_ao_write(unsigned char *data, int data_len) {
	int ret;
	AUDIO_FRAME_S frame;
	MB_EXT_CONFIG_S extConfig;
	AIO_ATTR_S aoAttr;

	RK_MPI_AO_GetPubAttr(0, &aoAttr);
	memset(&frame, 0, sizeof(frame));
	frame.u32Len = data_len;
	frame.u64TimeStamp = 0;
	frame.enBitWidth = aoAttr.enBitwidth;
	frame.enSoundMode = aoAttr.enSoundmode;
	frame.bBypassMbBlk = RK_FALSE;

	memset(&extConfig, 0, sizeof(extConfig));
	extConfig.pOpaque = data;
	extConfig.pu8VirAddr = data;
	extConfig.u64Size = data_len;
	RK_MPI_SYS_CreateMB(&(frame.pMbBlk), &extConfig);

	ret = RK_MPI_AO_SendFrame(0, 0, &frame, 1000);
	if (ret)
		LOG_ERROR("send frame fail, result = %#x\n", ret);
	RK_MPI_MB_ReleaseMB(frame.pMbBlk);

	if (data_len <= 0) {
		LOG_INFO("eof\n");
		RK_MPI_AO_WaitEos(0, 0, -1);
	}

	return 0;
}

int rkipc_audio_init() {
	LOG_INFO("%s\n", __func__);
	g_audio_run_ = 1;
	int ret = rkipc_ai_init();
	ret |= rkipc_aenc_init();

	// bind ai to aenc
	ai_chn.enModId = RK_ID_AI;
	ai_chn.s32DevId = ai_dev_id;
	ai_chn.s32ChnId = ai_chn_id;

	aenc_chn.enModId = RK_ID_AENC;
	aenc_chn.s32DevId = aenc_dev_id;
	aenc_chn.s32ChnId = aenc_chn_id;

	ret |= RK_MPI_SYS_Bind(&ai_chn, &aenc_chn);
	if (ret != 0) {
		LOG_ERROR("RK_MPI_SYS_Bind fail %x\n", ret);
	}
	LOG_DEBUG("RK_MPI_SYS_Bind success\n");

	return ret;
}

int rkipc_audio_deinit() {
	LOG_DEBUG("%s\n", __func__);
	int ret;
	g_audio_run_ = 0;
	if (enable_aed || enable_bcd)
		pthread_join(ai_get_detect_result_tid, NULL);
	// if (enable_aed)
	// 	rkipc_audio_aed_deinit();
	// if (enable_bcd)
	// 	rkipc_audio_bcd_deinit();
	// if (enable_vqe)
	// 	rkipc_audio_vqe_deinit();
	pthread_join(save_aenc_tid, RK_NULL);
	ret = RK_MPI_SYS_UnBind(&ai_chn, &aenc_chn);
	if (ret != 0) {
		LOG_ERROR("RK_MPI_SYS_UnBind fail %x\n", ret);
	}
	LOG_DEBUG("RK_MPI_SYS_UnBind success\n");
	ret |= rkipc_aenc_deinit();
	ret |= rkipc_ai_deinit();
	const char *encode_type = rk_param_get_string("audio.0:encode_type", NULL);
	if (!strcmp(encode_type, "MP3"))
		unregister_aenc_mp3();

	return ret;
}

// export api

int rk_audio_restart() {
	int ret;
	ret |= rkipc_audio_deinit();
	ret |= rkipc_audio_init();

	return ret;
}

int rk_audio_get_bit_rate(int stream_id, int *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "audio.%d:bit_rate", stream_id);
	*value = rk_param_get_int(entry, 16000);

	return 0;
}

int rk_audio_set_bit_rate(int stream_id, int value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "audio.%d:bit_rate", stream_id);
	rk_param_set_int(entry, value);

	return 0;
}

int rk_audio_get_sample_rate(int stream_id, int *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "audio.%d:sample_rate", stream_id);
	*value = rk_param_get_int(entry, 8000);

	return 0;
}

int rk_audio_set_sample_rate(int stream_id, int value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "audio.%d:sample_rate", stream_id);
	rk_param_set_int(entry, value);

	return 0;
}

int rk_audio_get_volume(int stream_id, int *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "audio.%d:volume", stream_id);
	*value = rk_param_get_int(entry, 50);

	return 0;
}

int rk_audio_set_volume(int stream_id, int value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "audio.%d:volume", stream_id);
	rk_param_set_int(entry, value);

	return 0;
}

int rk_audio_get_enable_vqe(int stream_id, int *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "audio.%d:enable_vqe", stream_id);
	*value = rk_param_get_int(entry, 1);

	return 0;
}

int rk_audio_set_enable_vqe(int stream_id, int value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "audio.%d:enable_vqe", stream_id);
	rk_param_set_int(entry, value);

	return 0;
}

int rk_audio_get_encode_type(int stream_id, const char **value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "audio.%d:encode_type", stream_id);
	*value = rk_param_get_string(entry, "G711A");

	return 0;
}

int rk_audio_set_encode_type(int stream_id, const char *value) {
	char entry[128] = {'\0'};
	snprintf(entry, 127, "audio.%d:encode_type", stream_id);
	rk_param_set_string(entry, value);

	return 0;
}

int rkipc_usb_ai_init() {
	int ret;
	AIO_ATTR_S aiAttr;

	memset(&aiAttr, 0, sizeof(AIO_ATTR_S));
	const char *card_name = "hw:1,0";
	snprintf(aiAttr.u8CardName, sizeof(aiAttr.u8CardName), "%s", card_name);
	LOG_INFO("aiAttr.u8CardName is %s\n", aiAttr.u8CardName);
	aiAttr.soundCard.channels = 2;
	aiAttr.soundCard.sampleRate = rk_param_get_int("audio.0:sample_rate", 16000);
	const char *format = rk_param_get_string("audio.0:format", NULL);
	if (!strcmp(format, "S16")) {
		aiAttr.soundCard.bitWidth = AUDIO_BIT_WIDTH_16;
		aiAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
	} else if (!strcmp(format, "U8")) {
		aiAttr.soundCard.bitWidth = AUDIO_BIT_WIDTH_8;
		aiAttr.enBitwidth = AUDIO_BIT_WIDTH_8;
	} else {
		LOG_ERROR("not support %s\n", format);
	}
	aiAttr.enSamplerate = rk_param_get_int("audio.0:sample_rate", 16000);
	aiAttr.u32FrmNum = 4;
	aiAttr.u32PtNumPerFrm = rk_param_get_int("audio.0:frame_size", 1024);
	aiAttr.u32EXFlag = 0;
	aiAttr.u32ChnCnt = 2;
	if (rk_param_get_int("audio.0:channels", 2) == 2)
		aiAttr.enSoundmode = AUDIO_SOUND_MODE_STEREO;
	else
		aiAttr.enSoundmode = AUDIO_SOUND_MODE_MONO;

	ret = RK_MPI_AI_SetPubAttr(usb_ai_dev_id, &aiAttr);
	if (ret != 0) {
		LOG_ERROR("ai set attr fail, reason = %d\n", ret);
		return RK_FAILURE;
	}

	ret = RK_MPI_AI_Enable(usb_ai_dev_id);
	if (ret != 0) {
		LOG_ERROR("ai enable fail, reason = %d\n", ret);
		return RK_FAILURE;
	}

	ret = RK_MPI_AI_EnableChn(usb_ai_dev_id, ai_chn_id);
	if (ret != 0) {
		LOG_ERROR("ai enable channel fail, aoChn = %d, reason = %x\n", ai_chn_id, ret);
		return RK_FAILURE;
	}

	// ret = RK_MPI_AI_EnableReSmp(ai_dev_id, ai_chn_id,
	//                               (AUDIO_SAMPLE_RATE_E)params->s32SampleRate);
	// if (ret != 0) {
	//     LOG_ERROR("ai enable resample fail, reason = %x, aoChn = %d\n", ret, ai_chn_id);
	//     return RK_FAILURE;
	// }

	RK_MPI_AI_SetVolume(usb_ai_dev_id, rk_param_get_int("audio.0:volume", 100));
	if (rk_param_get_int("audio.0:channels", 2) == 1) {
		RK_MPI_AI_SetTrackMode(usb_ai_dev_id, AUDIO_TRACK_FRONT_LEFT);
	}

	// pthread_create(&save_ai_tid, RK_NULL, save_ai_thread, NULL);

	return 0;
}

int rkipc_usb_ai_deinit() {
	int ret = RK_MPI_AI_DisableChn(usb_ai_dev_id, ai_chn_id);
	if (ret != 0) {
		LOG_ERROR("ai disable channel fail, reason = %d\n", ret);
		return RK_FAILURE;
	}
	ret = RK_MPI_AI_Disable(usb_ai_dev_id);
	if (ret != 0) {
		LOG_ERROR("ai disable fail, reason = %d\n", ret);
		return RK_FAILURE;
	}
	return 0;
}

int rkipc_usb_ao_init(void) {
	int ret = 0;
	int sample_rate = rk_param_get_int("audio.0:sample_rate", 16000);
	int channels = rk_param_get_int("audio.0:channels", 2);
	const char *card_name = "hw:1,0";
	AO_CHN aoChn = 0;
	AIO_ATTR_S aoAttr;
	AO_CHN_PARAM_S pstParams;

	memset(&pstParams, 0, sizeof(AO_CHN_PARAM_S));
	memset(&aoAttr, 0, sizeof(AIO_ATTR_S));
	sprintf((char *)aoAttr.u8CardName, "%s", card_name);

	aoAttr.soundCard.channels = 2;
	aoAttr.soundCard.sampleRate = sample_rate;
	const char *format = rk_param_get_string("audio.0:format", NULL);
	if (!strcmp(format, "S16")) {
		aoAttr.soundCard.bitWidth = AUDIO_BIT_WIDTH_16;
		aoAttr.enBitwidth = AUDIO_BIT_WIDTH_16;
	} else if (!strcmp(format, "U8")) {
		aoAttr.soundCard.bitWidth = AUDIO_BIT_WIDTH_8;
		aoAttr.enBitwidth = AUDIO_BIT_WIDTH_8;
	} else {
		LOG_ERROR("not support %s\n", format);
		return RK_FAILURE;
	}
	aoAttr.enSamplerate = (AUDIO_SAMPLE_RATE_E)sample_rate;

	if (channels == 1)
		aoAttr.enSoundmode = AUDIO_SOUND_MODE_MONO;
	else if (channels == 2)
		aoAttr.enSoundmode = AUDIO_SOUND_MODE_STEREO;
	else {
		LOG_ERROR("unsupport = %d\n", channels);
		return RK_FAILURE;
	}

	aoAttr.u32PtNumPerFrm = rk_param_get_int("audio.0:ao_frame_size", 1024);
	aoAttr.u32FrmNum = 4;
	aoAttr.u32EXFlag = 0;
	aoAttr.u32ChnCnt = 2;

	RK_MPI_AO_SetPubAttr(usb_ao_dev_id, &aoAttr);
	ret = RK_MPI_AO_Enable(usb_ao_dev_id);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("RK_MPI_AO_Enable failed %#X\n", ret);
		return RK_FAILURE;
	}
	/*==============================================================================*/
	pstParams.enLoopbackMode = AUDIO_LOOPBACK_NONE;
	ret = RK_MPI_AO_SetChnParams(usb_ao_dev_id, aoChn, &pstParams);
	if (ret != RK_SUCCESS) {
		LOG_ERROR("ao set channel params, aoChn = %d", aoChn);
		return RK_FAILURE;
	}

	if (channels == 1)
		RK_MPI_AO_SetTrackMode(usb_ao_dev_id, AUDIO_TRACK_OUT_STEREO);
	else
		RK_MPI_AO_SetTrackMode(usb_ao_dev_id, AUDIO_TRACK_NORMAL);
	if (rk_param_get_int("audio.0:enable_ao_vqe", 0)) {
		const char *vqe_file_path = rk_param_get_string("audio.0:ao_vqe_file", NULL);
		if (vqe_file_path) {
			AO_VQE_CONFIG_S vqe_config = {};
			strncpy(vqe_config.aCfgFile, vqe_file_path, strlen(vqe_file_path));
			vqe_config.enCfgMode = AIO_VQE_CONFIG_LOAD_FILE;
			ret = RK_MPI_AO_SetVqeAttr(usb_ao_dev_id, aoChn, &vqe_config);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("RK_MPI_AO_SetVqeAttr failed %#X\n", ret);
				return RK_FAILURE;
			}
			ret = RK_MPI_AO_EnableVqe(usb_ao_dev_id, aoChn);
			if (ret != RK_SUCCESS) {
				LOG_ERROR("RK_MPI_AO_EnableVqe failed %#X\n", ret);
				return RK_FAILURE;
			}
		}
	}
	/*==============================================================================*/
	ret = RK_MPI_AO_EnableChn(usb_ao_dev_id, aoChn);
	if (ret != 0) {
		LOG_ERROR("ao enable channel fail, aoChn = %d, reason = %x", aoChn, ret);
		return RK_FAILURE;
	}
	/*==============================================================================*/
	// set sample rate of input data
	ret = RK_MPI_AO_EnableReSmp(usb_ao_dev_id, aoChn, (AUDIO_SAMPLE_RATE_E)sample_rate);
	if (ret != 0) {
		LOG_ERROR("ao enable channel fail, reason = %x, aoChn = %d", ret, aoChn);
		return RK_FAILURE;
	}

	ret = RK_MPI_AO_SetVolume(usb_ao_dev_id, rk_param_get_int("audio.0:volume", 100));
	if (ret != 0) {
		LOG_ERROR("RK_MPI_AO_SetVolume failed %#X\n", ret);
		return RK_FAILURE;
	}
	LOG_INFO("create usb ao success\n");

	return RK_SUCCESS;
}

int rkipc_usb_ao_deinit(void) {
	RK_MPI_AO_DisableReSmp(usb_ao_dev_id, 0);
	RK_MPI_AO_DisableChn(usb_ao_dev_id, 0);
	RK_MPI_AO_Disable(usb_ao_dev_id);
}

int uac_record_start() {
	int ret = 0;
	LOG_INFO("uac_record_start\n");
	rkipc_usb_ai_init();
	rkipc_ao_init();
	MPP_CHN_S src_chn = {}, dst_chn = {};
	src_chn.enModId = RK_ID_AI;
	src_chn.s32DevId = usb_ai_dev_id;
	src_chn.s32ChnId = 0;
	dst_chn.enModId = RK_ID_AO;
	dst_chn.s32DevId = ao_dev_id;
	dst_chn.s32ChnId = 0;
	RK_MPI_SYS_Bind(&src_chn, &dst_chn);
	return ret;
}

int uac_record_stop() {
	LOG_INFO("uac_record_stop\n");
	MPP_CHN_S src_chn = {}, dst_chn = {};
	src_chn.enModId = RK_ID_AI;
	src_chn.s32DevId = usb_ai_dev_id;
	src_chn.s32ChnId = 0;
	dst_chn.enModId = RK_ID_AO;
	dst_chn.s32DevId = ao_dev_id;
	dst_chn.s32ChnId = 0;
	RK_MPI_SYS_UnBind(&src_chn, &dst_chn);
	rkipc_usb_ai_deinit();
	rkipc_ao_deinit();

	return 0;
}

int uac_playback_start() {
	LOG_INFO("uac_playback_start\n");
	// the default ai is created by default, so there is no need to init
	rkipc_usb_ao_init();
	MPP_CHN_S src_chn = {}, dst_chn = {};
	src_chn.enModId = RK_ID_AI;
	src_chn.s32DevId = ai_dev_id;
	src_chn.s32ChnId = 0;
	dst_chn.enModId = RK_ID_AO;
	dst_chn.s32DevId = usb_ao_dev_id;
	dst_chn.s32ChnId = 0;
	RK_MPI_SYS_Bind(&src_chn, &dst_chn);

	return 0;
}

int uac_playback_stop() {
	LOG_INFO("uac_playback_stop\n");
	MPP_CHN_S src_chn = {}, dst_chn = {};
	src_chn.enModId = RK_ID_AI;
	src_chn.s32DevId = ai_dev_id;
	src_chn.s32ChnId = 0;
	dst_chn.enModId = RK_ID_AO;
	dst_chn.s32DevId = usb_ao_dev_id;
	dst_chn.s32ChnId = 0;
	RK_MPI_SYS_UnBind(&src_chn, &dst_chn);
	rkipc_usb_ao_deinit();
	// the default ai is created by default, so there is no need to deinit

	return 0;
}

void uac_set_sample_rate(int type, int sampleRate) {
	if (sampleRate == 0)
		return;
	LOG_INFO("type = %d, sampleRate = %d\n", type, sampleRate);
	/*
	 * 1. for usb capture, we update audio config to capture
	 * 2. for usb playback, if there is resample before usb playback,
	 *    we set audio config to this resample, the new config will
	 *    pass to usb playback from resample to usb playback when
	 *    the datas move from resample to usb.
	 * 3. we alway use samperate=48K to open mic and speaker,
	 *    because usually, they use the same group i2s, and
	 *    not allowned to use diffrent samplerate.
	 */
	if (type == 0) {
		// the usb record always the first node
		AI_CHN_ATTR_S params;
		memset(&params, 0, sizeof(AI_CHN_ATTR_S));
		params.u32SampleRate = sampleRate;
		params.enChnAttr = AUDIO_CHN_ATTR_RATE;
		RK_MPI_AI_SetChnAttr(usb_ai_dev_id, 0, &params);
	} else {
		// find the resample before usb playback
		AO_CHN_ATTR_S params;
		memset(&params, 0, sizeof(AO_CHN_ATTR_S));
		params.u32SampleRate = sampleRate;
		params.enChnAttr = AUDIO_CHN_ATTR_RATE;
		RK_MPI_AO_SetChnAttr(usb_ao_dev_id, 0, &params);
	}
}

void uac_set_volume(int type, int volume) {
	LOG_INFO("type = %d, volume = %d\n", type, volume);
	AUDIO_FADE_S aFade;
	memset(&aFade, 0, sizeof(AUDIO_FADE_S));
	RK_BOOL bMute = RK_FALSE;
	if (type == 0) {
		RK_MPI_AO_SetMute(ao_dev_id, bMute, &aFade);
		RK_MPI_AO_SetVolume(ao_dev_id, volume);
	} else {
		RK_MPI_AO_SetMute(usb_ao_dev_id, bMute, &aFade);
		RK_MPI_AO_SetVolume(usb_ao_dev_id, volume);
	}
}

void uac_set_mute(int type, int mute) {
	LOG_INFO("type = %d, mute = %d\n", type, mute);
	AUDIO_FADE_S aFade;
	memset(&aFade, 0, sizeof(AUDIO_FADE_S));
	if (type == 0) {
		RK_MPI_AO_SetMute(ao_dev_id, mute, &aFade);
	} else {
		RK_MPI_AO_SetMute(usb_ao_dev_id, mute, &aFade);
	}
}

void uac_set_ppm(int type, int ppm) {
	LOG_INFO("type = %d, ppm = %d\n", type, ppm);
	// need amixer set, and sound card support, example 'PCM Clk Compensation In PPM'
}
