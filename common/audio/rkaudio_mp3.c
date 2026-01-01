// Copyright 2025 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "rkaudio_mp3.c"

#include "rkaudio_mp3.h"
#include <rk_debug.h>
#include <rk_mpi_adec.h>
#include <rk_mpi_aenc.h>
#include <rk_mpi_ai.h>
#include <rk_mpi_ao.h>
#include <rk_mpi_mb.h>
#include <rk_mpi_sys.h>

#define MP3MAXFRAMESIZE 4608

typedef struct _MP3DecInfo {
	/* pointers to platform-specific data structures */
	void *FrameHeaderPS;
	void *SideInfoPS;
	void *ScaleFactorInfoPS;
	void *HuffmanInfoPS;
	void *DequantInfoPS;
	void *IMDCTInfoPS;
	void *SubbandInfoPS;

	/* buffer which must be large enough to hold largest possible main_data section */
	unsigned char mainBuf[MAINBUF_SIZE];

	/* special info for "free" bitrate files */
	int freeBitrateFlag;
	int freeBitrateSlots;

	/* user-accessible info */
	int bitrate;
	int nChans;
	int samprate;
	int nGrans;     /* granules per frame */
	int nGranSamps; /* samples per granule */
	int nSlots;
	int layer;
	int vbr;         /* is vbr flag */
	uint32_t fSize;  /* file length, no tags */
	uint32_t fCount; /* frame count */
	char TOC[100];   /* TOC */

	MPEGVersion version;

	int mainDataBegin;
	int mainDataBytes;

	int part23Length[MAX_NGRAN][MAX_NCHAN];
} MP3DecInfo;

typedef struct {
	HMP3Decoder pMp3Dec;
	MP3FrameInfo frameInfo;
	RK_U8 decMp3buf[MP3MAXFRAMESIZE];
	short int decPCMbuf[MP3MAXFRAMESIZE];
	RK_U64 audioPts;
} RK_ADEC_MP3_CTX_S;

typedef struct _RK_AENC_MP3_CTX_S {
	mp3_enc *pMp3Enc;
	int frameLength;
} RK_AENC_MP3_CTX_S;


static int extDecoderHandle = -1;
static unsigned mp3DecInitCnt = 0;
static int extCodecHandle = -1;

static int rkaudio_mp3_encoder_close(RK_VOID *pEncoder) {
	RK_AENC_MP3_CTX_S *ctx = (RK_AENC_MP3_CTX_S *)pEncoder;
	if (ctx == NULL)
		return 0;

	Mp3EncodeDeinit(ctx->pMp3Enc);
	free(ctx);
	ctx = NULL;
	return 0;
}

static int rkaudio_mp3_encoder_open(RK_VOID *pEncoderAttr, RK_VOID **ppEncoder) {
	int bitrate = 0;
	if (pEncoderAttr == NULL) {
		LOG_ERROR("pEncoderAttr is NULL\n");
		return RK_FAILURE;
	}

	AENC_ATTR_CODEC_S *attr = (AENC_ATTR_CODEC_S *)pEncoderAttr;
	if (attr->enType != RK_AUDIO_ID_MP3) {
		LOG_ERROR("Invalid enType[%d]\n", attr->enType);
		return RK_FAILURE;
	}

	RK_AENC_MP3_CTX_S *ctx = (RK_AENC_MP3_CTX_S *)malloc(sizeof(RK_AENC_MP3_CTX_S));
	if (!ctx) {
		LOG_ERROR("malloc aenc mp3 ctx failed\n");
		return RK_FAILURE;
	}

	memset(ctx, 0, sizeof(RK_AENC_MP3_CTX_S));
	if (attr->u32Resv[0] > 1152) {
		LOG_ERROR("error: MP3 FrameLength is too large, FrameLength = %d\n", attr->u32Resv[0]);
		goto __FAILED;
	}

	ctx->frameLength = attr->u32Resv[0];
	bitrate = attr->u32Resv[1] / 1000;
	LOG_DEBUG("MP3Encode: sample_rate = %d, channel = %d, bitrate = %d.\n", attr->u32SampleRate,
	          attr->u32Channels, bitrate);
	ctx->pMp3Enc = Mp3EncodeVariableInit(attr->u32SampleRate, attr->u32Channels, bitrate);
	if (ctx->pMp3Enc->frame_size <= 0) {
		LOG_ERROR("MP3Encode init failed! r:%d c:%d\n", attr->u32SampleRate, attr->u32Channels);
		goto __FAILED;
	}

	LOG_DEBUG("MP3Encode FrameSize = %d\n", ctx->pMp3Enc->frame_size);
	*ppEncoder = (RK_VOID *)ctx;

	return 0;

__FAILED:
	rkaudio_mp3_encoder_close((RK_VOID *)ctx);
	*ppEncoder = RK_NULL;
	return RK_FAILURE;
}

static int rkaudio_mp3_encoder_encode(RK_VOID *pEncoder, RK_VOID *pEncParam) {
	RK_AENC_MP3_CTX_S *ctx = (RK_AENC_MP3_CTX_S *)pEncoder;
	AUDIO_ADENC_PARAM_S *pParam = (AUDIO_ADENC_PARAM_S *)pEncParam;

	if (ctx == NULL || pParam == NULL) {
		LOG_ERROR("Invalid ctx or pParam\n");
		return AENC_ENCODER_ERROR;
	}

	RK_U32 u32EncSize = 0;
	RK_U8 *inData = pParam->pu8InBuf;
	RK_U64 inPts = pParam->u64InTimeStamp;
	RK_U32 inbufSize = 0;
	RK_U32 copySize = 0;

	// if input buffer is NULL, this means eos(end of stream)
	if (inData == NULL) {
		pParam->u64OutTimeStamp = inPts;
	}

	inbufSize = 2 * ctx->pMp3Enc->frame_size;
	copySize = (pParam->u32InLen > inbufSize) ? inbufSize : pParam->u32InLen;
	memcpy(ctx->pMp3Enc->config.in_buf, inData, copySize);
	pParam->u32InLen = pParam->u32InLen - copySize;

	u32EncSize = L3_compress(ctx->pMp3Enc, 0, (unsigned char **)(&ctx->pMp3Enc->config.out_buf));

	u32EncSize = (u32EncSize > pParam->u32OutLen) ? pParam->u32OutLen : u32EncSize;
	memcpy(pParam->pu8OutBuf, ctx->pMp3Enc->config.out_buf, u32EncSize);
	pParam->u64OutTimeStamp = inPts;
	pParam->u32OutLen = u32EncSize;

	return AENC_ENCODER_OK;
}

int register_aenc_mp3(void) {
	int ret;
	AENC_ENCODER_S aencCtx;
	memset(&aencCtx, 0, sizeof(AENC_ENCODER_S));

	extCodecHandle = -1;
	aencCtx.enType = RK_AUDIO_ID_MP3;
	snprintf((RK_CHAR *)(aencCtx.aszName), sizeof(aencCtx.aszName), "rkaudio");
	aencCtx.u32MaxFrmLen = 2048;
	aencCtx.pfnOpenEncoder = rkaudio_mp3_encoder_open;
	aencCtx.pfnEncodeFrm = rkaudio_mp3_encoder_encode;
	aencCtx.pfnCloseEncoder = rkaudio_mp3_encoder_close;

	LOG_DEBUG("register external aenc(%s)\n", aencCtx.aszName);
	ret = RK_MPI_AENC_RegisterEncoder(&extCodecHandle, &aencCtx);
	if (ret != 0) {
		LOG_ERROR("aenc %s register decoder fail %x\n", aencCtx.aszName, ret);
		return RK_FAILURE;
	}

	return 0;
}

int unregister_aenc_mp3(void) {
	if (extCodecHandle == -1) {
		return 0;
	}

	LOG_DEBUG("unregister external aenc\n");
	int ret = RK_MPI_AENC_UnRegisterEncoder(extCodecHandle);
	if (ret != 0) {
		LOG_ERROR("aenc unregister decoder fail %x\n", ret);
		return RK_FAILURE;
	}
	extCodecHandle = -1;

	return 0;
}

static int mp3_decoder_open(void *pDecoderAttr, void **ppDecoder) {
	if (pDecoderAttr == NULL) {
		LOG_ERROR("pDecoderAttr is NULL\n");
		return RK_FAILURE;
	}

	RK_ADEC_MP3_CTX_S *ctx = (RK_ADEC_MP3_CTX_S *)malloc(sizeof(RK_ADEC_MP3_CTX_S));
	if (!ctx) {
		LOG_ERROR("malloc adec mp3 ctx failed\n");
		return RK_FAILURE;
	}

	memset(ctx, 0, sizeof(RK_ADEC_MP3_CTX_S));
	ctx->pMp3Dec = MP3InitDecoder();
	if (!ctx->pMp3Dec) {
		LOG_ERROR("malloc adec pMp3Dec failed\n");
		free(ctx);
		ctx = NULL;
		return RK_FAILURE;
	}

	memset(ctx->decMp3buf, 0, sizeof(ctx->decMp3buf));
	memset(ctx->decPCMbuf, 0, sizeof(ctx->decPCMbuf));

	*ppDecoder = (void *)ctx;
	return RK_SUCCESS;
}

static int mp3_decoder_close(void *pDecoder) {
	RK_ADEC_MP3_CTX_S *ctx = (RK_ADEC_MP3_CTX_S *)pDecoder;
	if (ctx == NULL)
		return RK_FAILURE;

	if (ctx->pMp3Dec) {
		MP3FreeDecoder(ctx->pMp3Dec);
		ctx->pMp3Dec = NULL;
	}

	if (ctx)
		free(ctx);

	return RK_SUCCESS;
}

static int mp3_decoder_decode(void *pDecoder, void *pDecParam) {
	RK_S32 ret = 0;
	RK_ADEC_MP3_CTX_S *ctx = (RK_ADEC_MP3_CTX_S *)pDecoder;
	RK_S32 skip = 0;

	if (ctx == NULL || ctx->pMp3Dec == NULL || pDecParam == NULL)
		return ADEC_DECODER_ERROR;

	AUDIO_ADENC_PARAM_S *pParam = (AUDIO_ADENC_PARAM_S *)pDecParam;
	RK_BOOL eos = RK_FALSE;
	RK_U8 *pInput = NULL;
	RK_S32 inLength = pParam->u32InLen;
	RK_S32 copySize = 0;
	RK_U64 calcPts = 0;

	if ((pParam->pu8InBuf == NULL) || (inLength == 0))
		eos = RK_TRUE;

	copySize = (inLength <= MP3MAXFRAMESIZE) ? inLength : MP3MAXFRAMESIZE;
	inLength -= copySize;
	memcpy(ctx->decMp3buf, pParam->pu8InBuf, copySize);
	pInput = (RK_U8 *)ctx->decMp3buf;
	if (copySize && (skip = MP3FindSyncWord(pInput, copySize)) < 0) {
		LOG_ERROR("mp3 decode don't find sync word\n");
		pParam->u32InLen = 0;
		return ADEC_DECODER_ERROR;
	}

	copySize -= skip;
	pInput += skip;

	ret = MP3Decode(ctx->pMp3Dec, &pInput, &copySize, ctx->decPCMbuf, 0);
	pParam->u32InLen = inLength + copySize;

	if (ret) {
		if (eos)
			return ADEC_DECODER_EOS;

		pParam->u32InLen = inLength;
		if (ret == ERR_MP3_INDATA_UNDERFLOW) {
			LOG_WARN("mp3 decode input data underflow\n");
			return ADEC_DECODER_ERROR;
		}

		if (ret == -2) {
			LOG_WARN("mp3 encoded data does not start from the first frame\n");
		} else {
			LOG_ERROR("mp3 decode error, ret = %d\n", ret);
			return ADEC_DECODER_ERROR;
		}
	}

	MP3GetLastFrameInfo(ctx->pMp3Dec, &ctx->frameInfo);

	MP3DecInfo *mp3DecInfo = (MP3DecInfo *)ctx->pMp3Dec;
	pParam->u32OutLen = mp3DecInfo->nGrans * mp3DecInfo->nGranSamps * ctx->frameInfo.nChans * 2;
	memcpy(pParam->pu8OutBuf, (RK_U8 *)ctx->decPCMbuf, pParam->u32OutLen);

	calcPts = (((RK_U64)pParam->u32OutLen / 2) * 1000000) / ctx->frameInfo.samprate;
	if (pParam->u64InTimeStamp)
		pParam->u64OutTimeStamp = pParam->u64InTimeStamp;
	else if (ctx->audioPts)
		pParam->u64OutTimeStamp = ctx->audioPts + calcPts;

	if (pParam->u64OutTimeStamp)
		ctx->audioPts = pParam->u64OutTimeStamp;
	else {
		ctx->audioPts += calcPts;
	}

	return ADEC_DECODER_OK;
}

static int mp3_decoder_get_frame_info(void *pDecoder, void *pInfo) {
	ADEC_FRAME_INFO_S stFrameInfo;
	RK_ADEC_MP3_CTX_S *ctx = (RK_ADEC_MP3_CTX_S *)pDecoder;

	if (ctx == NULL || pInfo == NULL)
		return RK_FAILURE;

	MP3GetLastFrameInfo(ctx->pMp3Dec, &ctx->frameInfo);

	if (!ctx->frameInfo.bitsPerSample) {
		LOG_ERROR("mp3 decode get info failed\n");
		return RK_FAILURE;
	}

	memset(&stFrameInfo, 0, sizeof(ADEC_FRAME_INFO_S));
	stFrameInfo.u32Channels = ctx->frameInfo.nChans;
	stFrameInfo.u32SampleRate = ctx->frameInfo.samprate;
	stFrameInfo.enBitWidth = AUDIO_BIT_WIDTH_16;
	memcpy(pInfo, &stFrameInfo, sizeof(ADEC_FRAME_INFO_S));
	return RK_SUCCESS;
}

int register_adec_mp3() {
	if (!mp3DecInitCnt) {
		ADEC_DECODER_S adecCtx;
		memset(&adecCtx, 0, sizeof(ADEC_DECODER_S));

		extDecoderHandle = -1;
		adecCtx.enType = RK_AUDIO_ID_MP3;
		snprintf((RK_CHAR *)(adecCtx.aszName), sizeof(adecCtx.aszName), "rkaudio1");
		adecCtx.pfnOpenDecoder = mp3_decoder_open;
		adecCtx.pfnDecodeFrm = mp3_decoder_decode;
		adecCtx.pfnGetFrmInfo = mp3_decoder_get_frame_info;
		adecCtx.pfnCloseDecoder = mp3_decoder_close;
		adecCtx.pfnResetDecoder = RK_NULL;

		LOG_DEBUG("register ext audio decoder\n");
		RK_S32 ret = RK_MPI_ADEC_RegisterDecoder(&extDecoderHandle, &adecCtx);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("adec register decoder fail, ret = 0x%x\n", ret);
			return RK_FAILURE;
		}
	}

	mp3DecInitCnt++;
	return RK_SUCCESS;
}

int unregister_adec_mp3() {
	if (extDecoderHandle == -1)
		return RK_SUCCESS;

	if (0 == mp3DecInitCnt) {
		return 0;
	} else if (1 == mp3DecInitCnt) {
		LOG_DEBUG("unregister ext audio decoder\n");
		int ret = RK_MPI_ADEC_UnRegisterDecoder(extDecoderHandle);
		if (ret != RK_SUCCESS) {
			LOG_ERROR("adec unregister decoder fail, ret = 0x%x\n", ret);
			return RK_FAILURE;
		}

		extDecoderHandle = -1;
	}

	mp3DecInitCnt--;
	return RK_SUCCESS;
}
