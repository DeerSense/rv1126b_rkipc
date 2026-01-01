// Copyright 2024 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "common.h"
#include "log.h"

#include <rk_debug.h>
#include <rk_mpi_mb.h>
#include <rk_mpi_sys.h>
#include <rk_mpi_venc.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "venc.c"

int rkipc_set_advanced_venc_params(int venc_chn_id) {
	char entry[128] = {'\0'};
	const char *strings = NULL;
	int ret = 0;
	snprintf(entry, 127, "video.%d:output_data_type", venc_chn_id);
	const char *tmp_output_data_type = rk_param_get_string(entry, "H.265");
	LOG_INFO("tmp_output_data_type is %s\n", tmp_output_data_type);

	VENC_RC_PARAM_S venc_rc_param;
	RK_MPI_VENC_GetRcParam(venc_chn_id, &venc_rc_param);
	if (!strcmp(tmp_output_data_type, "H.264")) {
		snprintf(entry, 127, "video.%d:frame_min_i_qp", venc_chn_id);
		venc_rc_param.stParamH264.u32FrmMinIQp = rk_param_get_int(entry, 25);
		snprintf(entry, 127, "video.%d:frame_min_qp", venc_chn_id);
		venc_rc_param.stParamH264.u32FrmMinQp = rk_param_get_int(entry, 26);
		snprintf(entry, 127, "video.%d:frame_max_i_qp", venc_chn_id);
		venc_rc_param.stParamH264.u32FrmMaxIQp = rk_param_get_int(entry, 45);
		snprintf(entry, 127, "video.%d:frame_max_qp", venc_chn_id);
		venc_rc_param.stParamH264.u32FrmMaxQp = rk_param_get_int(entry, 48);
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		snprintf(entry, 127, "video.%d:frame_min_i_qp", venc_chn_id);
		venc_rc_param.stParamH265.u32FrmMinIQp = rk_param_get_int(entry, 25);
		snprintf(entry, 127, "video.%d:frame_min_qp", venc_chn_id);
		venc_rc_param.stParamH265.u32FrmMinQp = rk_param_get_int(entry, 26);
		snprintf(entry, 127, "video.%d:frame_max_i_qp", venc_chn_id);
		venc_rc_param.stParamH265.u32FrmMaxIQp = rk_param_get_int(entry, 45);
		snprintf(entry, 127, "video.%d:frame_max_qp", venc_chn_id);
		venc_rc_param.stParamH265.u32FrmMaxQp = rk_param_get_int(entry, 48);
	} else {
		LOG_ERROR("tmp_output_data_type is %s, not support\n", tmp_output_data_type);
		return -1;
	}
	RK_MPI_VENC_SetRcParam(venc_chn_id, &venc_rc_param);

	snprintf(entry, 127, "video.%d:enable_motion_deblur", venc_chn_id);
	ret = RK_MPI_VENC_EnableMotionDeblur(venc_chn_id, rk_param_get_int(entry, 1));
	if (ret)
		LOG_ERROR("RK_MPI_VENC_EnableMotionDeblur error! ret=%#x\n", ret);

	snprintf(entry, 127, "video.%d:motion_deblur_strength", venc_chn_id);
#ifdef RKIPC_RV1126B
	ret = RK_MPI_VENC_SetMotionDeblurStrength(venc_chn_id, rk_param_get_int(entry, 5));
#else
	ret = RK_MPI_VENC_SetMotionDeblurStrength(venc_chn_id, rk_param_get_int(entry, 0));
#endif
	if (ret)
		LOG_ERROR("RK_MPI_VENC_SetMotionDeblurStrength error! ret=%#x\n", ret);

	snprintf(entry, 127, "video.%d:enable_motion_static_switch", venc_chn_id);
	ret = RK_MPI_VENC_EnableMotionStaticSwitch(venc_chn_id, rk_param_get_int(entry, 0));
	if (ret)
		LOG_ERROR("RK_MPI_VENC_EnableMotionStaticSwitch error! ret=%#x\n", ret);

	VENC_DEBREATHEFFECT_S debfrath_effect;
	memset(&debfrath_effect, 0, sizeof(VENC_DEBREATHEFFECT_S));
	snprintf(entry, 127, "video.%d:enable_debreath_effect", venc_chn_id);
	debfrath_effect.bEnable = rk_param_get_int(entry, 0);
	snprintf(entry, 127, "video.%d:debreath_effect_strength", venc_chn_id);
	debfrath_effect.s32Strength1 = rk_param_get_int(entry, 16);
	ret = RK_MPI_VENC_SetDeBreathEffect(venc_chn_id, &debfrath_effect);
	if (ret)
		LOG_ERROR("RK_MPI_VENC_SetDeBreathEffect error! ret=%#x\n", ret);

	if (!strcmp(tmp_output_data_type, "H.264")) {
		VENC_H264_TRANS_S pstH264Trans;
		RK_MPI_VENC_GetH264Trans(venc_chn_id, &pstH264Trans);
		snprintf(entry, 127, "video.%d:scalinglist", venc_chn_id);
		pstH264Trans.bScalingListValid = rk_param_get_int(entry, 1);
		RK_MPI_VENC_SetH264Trans(venc_chn_id, &pstH264Trans);
	} else if (!strcmp(tmp_output_data_type, "H.265")) {
		VENC_H265_TRANS_S pstH265Trans;
		RK_MPI_VENC_GetH265Trans(venc_chn_id, &pstH265Trans);
		snprintf(entry, 127, "video.%d:scalinglist", venc_chn_id);
		pstH265Trans.bScalingListEnabled = rk_param_get_int(entry, 1);
		RK_MPI_VENC_SetH265Trans(venc_chn_id, &pstH265Trans);
	}

	VENC_RC_PARAM2_S rc_param2;
	ret = RK_MPI_VENC_GetRcParam2(venc_chn_id, &rc_param2);
	if (ret)
		LOG_ERROR("RK_MPI_VENC_GetRcParam2 error! ret=%#x\n", ret);
	snprintf(entry, 127, "video.%d:thrd_i", venc_chn_id);
	strings = rk_param_get_string(entry, "0,0,0,0,3,3,5,5,8,8,8,15,15,20,25,25");
	if (strings) {
		char *str = strdup(strings);
		if (str) {
			char *tmp = str;
			char *token = strsep(&tmp, ",");
			int i = 0;
			while (token != NULL) {
				rc_param2.u32ThrdI[i++] = atoi(token);
				token = strsep(&tmp, ",");
			}
			free(str);
		}
	}
	snprintf(entry, 127, "video.%d:thrd_p", venc_chn_id);
	strings = rk_param_get_string(entry, "0,0,0,0,3,3,5,5,8,8,8,15,15,20,25,25");
	if (strings) {
		char *str = strdup(strings);
		if (str) {
			char *tmp = str;
			char *token = strsep(&tmp, ",");
			int i = 0;
			while (token != NULL) {
				rc_param2.u32ThrdP[i++] = atoi(token);
				token = strsep(&tmp, ",");
			}
			free(str);
		}
	}
	snprintf(entry, 127, "video.%d:aq_step_i", venc_chn_id);
	strings = rk_param_get_string(entry, "-8,-7,-6,-5,-4,-3,-2,-1,0,1,2,3,4,7,8,9");
	if (strings) {
		char *str = strdup(strings);
		if (str) {
			char *tmp = str;
			char *token = strsep(&tmp, ",");
			int i = 0;
			while (token != NULL) {
				rc_param2.s32AqStepI[i++] = atoi(token);
				token = strsep(&tmp, ",");
			}
			free(str);
		}
	}
	snprintf(entry, 127, "video.%d:aq_step_p", venc_chn_id);
	strings = rk_param_get_string(entry, "-8,-7,-6,-5,-4,-3,-2,-1,0,1,2,3,4,7,8,9");
	if (strings) {
		char *str = strdup(strings);
		if (str) {
			char *tmp = str;
			char *token = strsep(&tmp, ",");
			int i = 0;
			while (token != NULL) {
				rc_param2.s32AqStepP[i++] = atoi(token);
				token = strsep(&tmp, ",");
			}
			free(str);
		}
	}
	ret = RK_MPI_VENC_SetRcParam2(venc_chn_id, &rc_param2);
	if (ret)
		LOG_ERROR("RK_MPI_VENC_SetRcParam2 error! ret=%#x\n", ret);

	if (!strcmp(tmp_output_data_type, "H.264")) {
		VENC_H264_QBIAS_S qbias;
		snprintf(entry, 127, "video.%d:qbias_enable", venc_chn_id);
		qbias.bEnable = rk_param_get_int(entry, 1);
		snprintf(entry, 127, "video.%d:qbias_i", venc_chn_id);
		qbias.u32QbiasI = rk_param_get_int(entry, 171);
		snprintf(entry, 127, "video.%d:qbias_p", venc_chn_id);
		qbias.u32QbiasP = rk_param_get_int(entry, 85);
		ret = RK_MPI_VENC_SetH264Qbias(venc_chn_id, &qbias);
		if (ret)
			LOG_ERROR("RK_MPI_VENC_SetH264Qbias error! ret=%#x\n", ret);
	} else {
		VENC_H265_QBIAS_S qbias;
		snprintf(entry, 127, "video.%d:qbias_enable", venc_chn_id);
		qbias.bEnable = rk_param_get_int(entry, 1);
		snprintf(entry, 127, "video.%d:qbias_i", venc_chn_id);
		qbias.u32QbiasI = rk_param_get_int(entry, 171);
		snprintf(entry, 127, "video.%d:qbias_p", venc_chn_id);
		qbias.u32QbiasP = rk_param_get_int(entry, 85);
		ret = RK_MPI_VENC_SetH265Qbias(venc_chn_id, &qbias);
		if (ret)
			LOG_ERROR("RK_MPI_VENC_SetH265Qbias error! ret=%#x\n", ret);
	}

	VENC_FILTER_S filter_s;
	RK_MPI_VENC_GetFilter(venc_chn_id, &filter_s);
	snprintf(entry, 127, "video.%d:flt_str_i", venc_chn_id);
	filter_s.u32StrengthI = rk_param_get_int(entry, 0);
	snprintf(entry, 127, "video.%d:flt_str_p", venc_chn_id);
	filter_s.u32StrengthP = rk_param_get_int(entry, 0);
	RK_MPI_VENC_SetFilter(venc_chn_id, &filter_s);

	if (!strcmp(tmp_output_data_type, "H.265")) {
		VENC_H265_CU_DQP_S cu_dqp_s;
		RK_MPI_VENC_GetH265CuDqp(venc_chn_id, &cu_dqp_s);
		snprintf(entry, 127, "video.%d:cu_dqp", venc_chn_id);
		cu_dqp_s.u32CuDqp = rk_param_get_int(entry, 1);
		RK_MPI_VENC_SetH265CuDqp(venc_chn_id, &cu_dqp_s);
	}

	VENC_ANTI_RING_S anti_ring_s;
	RK_MPI_VENC_GetAntiRing(venc_chn_id, &anti_ring_s);
	snprintf(entry, 127, "video.%d:anti_ring", venc_chn_id);
	anti_ring_s.u32AntiRing = rk_param_get_int(entry, 3);
	RK_MPI_VENC_SetAntiRing(venc_chn_id, &anti_ring_s);

	VENC_ANTI_LINE_S anti_line_s;
	RK_MPI_VENC_GetAntiLine(venc_chn_id, &anti_line_s);
	snprintf(entry, 127, "video.%d:anti_line", venc_chn_id);
	anti_line_s.u32AntiLine = rk_param_get_int(entry, 3);
	RK_MPI_VENC_SetAntiLine(venc_chn_id, &anti_line_s);

#if defined(RKIPC_RV1103B) || defined(RKIPC_RV1126B)
	VENC_LAMBDA_S lambda_s;
	RK_MPI_VENC_GetLambda(venc_chn_id, &lambda_s);
	snprintf(entry, 127, "video.%d:lambda", venc_chn_id);
	if (!strcmp(tmp_output_data_type, "H.264"))
		lambda_s.u32Lambda = rk_param_get_int(entry, 6);
	else
		lambda_s.u32Lambda = rk_param_get_int(entry, 5);
	snprintf(entry, 127, "video.%d:lambda_i", venc_chn_id);
	if (!strcmp(tmp_output_data_type, "H.264"))
		lambda_s.u32LambdaI = rk_param_get_int(entry, 6);
	else
		lambda_s.u32LambdaI = rk_param_get_int(entry, 5);
	RK_MPI_VENC_SetLambda(venc_chn_id, &lambda_s);

	VENC_ANTI_FLICK_S anti_flick_s;
	RK_MPI_VENC_GetAntiFlick(venc_chn_id, &anti_flick_s);
	snprintf(entry, 127, "video.%d:atf_str", venc_chn_id);
	anti_flick_s.u32AntiFlick = rk_param_get_int(entry, 2);
	RK_MPI_VENC_SetAntiFlick(venc_chn_id, &anti_flick_s);

	VENC_STATIC_WEIGHT_S static_weight_s;
	RK_MPI_VENC_GetStaticWeight(venc_chn_id, &static_weight_s);
	snprintf(entry, 127, "video.%d:static_frm_num", venc_chn_id);
#ifdef RKIPC_RV1126B
	static_weight_s.u32FrmNum = rk_param_get_int(entry, 0);
#else
	static_weight_s.u32FrmNum = rk_param_get_int(entry, 5);
#endif
	snprintf(entry, 127, "video.%d:madp16_th", venc_chn_id);
	static_weight_s.u32Madp16Th = rk_param_get_int(entry, 15);
	snprintf(entry, 127, "video.%d:skip16_wgt", venc_chn_id);
	static_weight_s.u32Skip16Weight = rk_param_get_int(entry, 15);
	snprintf(entry, 127, "video.%d:skip32_wgt", venc_chn_id);
	static_weight_s.u32Skip32Weight = rk_param_get_int(entry, 15);
	RK_MPI_VENC_SetStaticWeight(venc_chn_id, &static_weight_s);

	if (!strcmp(tmp_output_data_type, "H.264")) {
		VENC_H264_QBIAS2_S qbias2_s;
		RK_MPI_VENC_GetH264Qbias2(venc_chn_id, &qbias2_s);
		snprintf(entry, 127, "video.%d:qbias_enable", venc_chn_id);
		qbias2_s.bEnable = rk_param_get_int(entry, 1);
		snprintf(entry, 127, "video.%d:qbias_arr", venc_chn_id);
		strings =
		    rk_param_get_string(entry, "3,6,13,144,144,144,144,3,6,13,144,144,171,144,85,85,85,85");
		if (strings) {
			char *str = strdup(strings);
			if (str) {
				char *tmp = str;
				char *token = strsep(&tmp, ",");
				int i = 0;
				while (token != NULL) {
					qbias2_s.u32Qbias2[i++] = atoi(token);
					token = strsep(&tmp, ",");
				}
				free(str);
			}
		}
		RK_MPI_VENC_SetH264Qbias2(venc_chn_id, &qbias2_s);
	} else {
		VENC_H265_QBIAS2_S qbias2_s;
		RK_MPI_VENC_GetH265Qbias2(venc_chn_id, &qbias2_s);
		snprintf(entry, 127, "video.%d:qbias_enable", venc_chn_id);
		qbias2_s.bEnable = rk_param_get_int(entry, 1);
		snprintf(entry, 127, "video.%d:qbias_arr", venc_chn_id);
		strings =
		    rk_param_get_string(entry, "3,6,13,144,144,144,144,3,6,13,144,144,171,144,85,85,85,85");
		LOG_INFO("strings is %s\n", strings);
		if (strings) {
			char *str = strdup(strings);
			if (str) {
				char *tmp = str;
				char *token = strsep(&tmp, ",");
				int i = 0;
				while (token != NULL) {
					qbias2_s.u32Qbias2[i++] = atoi(token);
					token = strsep(&tmp, ",");
				}
				free(str);
			}
		}
		RK_MPI_VENC_SetH265Qbias2(venc_chn_id, &qbias2_s);
	}

	VENC_RC_PARAM3_S rc_param3;
	RK_MPI_VENC_GetRcParam3(venc_chn_id, &rc_param3);
	snprintf(entry, 127, "video.%d:aq_rnge_arr", venc_chn_id);
	strings = rk_param_get_string(entry, "8,8,12,12,12,8,8,12,12,12");
	if (strings) {
		char *str = strdup(strings);
		if (str) {
			char *tmp = str;
			char *token = strsep(&tmp, ",");
			int i = 0;
			while (token != NULL) {
				rc_param3.u32AqRange[i++] = atoi(token);
				token = strsep(&tmp, ",");
			}
			free(str);
		}
	}
	RK_MPI_VENC_SetRcParam3(venc_chn_id, &rc_param3);

	VENC_LIGHT_CHANGE_S light_change;
	RK_MPI_VENC_GetLightChange(venc_chn_id, &light_change);
	snprintf(entry, 127, "video.%d:lgt_chg_lvl", venc_chn_id);
	light_change.u32Level = rk_param_get_int(entry, 0);
	RK_MPI_VENC_SetLightChange(venc_chn_id, &light_change);

	if (!strcmp(tmp_output_data_type, "H.265")) {
		snprintf(entry, 127, "video.%d:tmvp_en", venc_chn_id);
		RK_MPI_VENC_EnableTmvp(venc_chn_id, rk_param_get_int(entry, 0));
	}
#endif

#ifdef RKIPC_RV1126B
	if (!strcmp(tmp_output_data_type, "H.265")) {
		VENC_H265_SAO_S sao_s;
		RK_MPI_VENC_GetH265Sao(venc_chn_id, &sao_s);
		snprintf(entry, 127, "video.%d:sao_str_i", venc_chn_id);
		sao_s.u32SaoStrengthI = rk_param_get_int(entry, 0);
		snprintf(entry, 127, "video.%d:sao_str_p", venc_chn_id);
		sao_s.u32SaoStrengthP = rk_param_get_int(entry, 0);
		RK_MPI_VENC_SetH265Sao(venc_chn_id, &sao_s);
	}
	snprintf(entry, 127, "video.%d:speed", venc_chn_id);
	RK_MPI_VENC_SetSpeed(venc_chn_id, rk_param_get_int(entry, 1));
#endif

	return ret;
}
