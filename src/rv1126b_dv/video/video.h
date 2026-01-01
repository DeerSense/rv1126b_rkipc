// Copyright 2021 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "common.h"
#include "isp.h"
#include "osd.h"
#include "rtsp.h"
#include "storage.h"

#include <inttypes.h> // PRId64
#include <rk_debug.h>
#include <rk_mpi_gdc.h>
#include <rk_mpi_mb.h>
#include <rk_mpi_mmz.h>
#include <rk_mpi_rgn.h>
#include <rk_mpi_sys.h>
#include <rk_mpi_tde.h>
#include <rk_mpi_venc.h>
#include <rk_mpi_vi.h>
#include <rk_mpi_vo.h>
#include <rk_mpi_vpss.h>

typedef enum {
	RK_VIDEO_MODE = 0,
	RK_PHOTO_MODE,
	RK_SLOW_MOTION_MODE,
	RK_TIME_LAPSE_MODE,
	RK_MODE_NUM,
} RK_MODE_E;

typedef enum {
	RK_EIS_OFF = 0,
	RK_NORMAL_STEADY,
	RK_HORIZON_STEADY,
	RK_DISTORTION_CORRECTION,
	RK_EIS_MODE_NUM,
} RK_EIS_MODE_E;

typedef enum {
	RK_LINEAR_MODE = 0,
	RK_HDR_DAG_MODE,
	RK_HDR_STAGGERED_MODE,
	RK_HDR_MODE_NUM,
} RK_HDR_MODE_E;

int rk_init_mode(void);
int rk_set_mode(RK_MODE_E mode);
RK_MODE_E rk_get_mode(void);

RK_EIS_MODE_E rk_get_eis_mode(void);
int rk_set_eis_mode(RK_EIS_MODE_E eis_mode);

int rk_set_hdr(RK_HDR_MODE_E hdr_mode);
RK_HDR_MODE_E rk_get_hdr(void);

int rk_set_memc_mode(bool enable);
int rk_get_memc_mode(void);

int rk_set_eis_debug(bool enable);
int rk_get_eis_debug(void);

int rk_set_smart_ae(bool enable);
int rk_get_smart_ae(void);

int rk_set_rtsp(bool enable);
int rk_get_rtsp(void);

int rk_video_start_record(void);
int rk_video_stop_record(void);

int rk_photo_set_max_num(int num);
int rk_photo_get_max_num(void);
int rk_photo_get_done_num(void);
int rk_photo_start(void); 
int rk_photo_stop(void); 
int rk_enter_sleep(void);

int rk_video_init(void);
int rk_video_deinit(void);
int rk_video_restart(void);

static int rk_video_reset_frame_rate(int stream_id) {
	return 0;
}

// Init display device like LCD, MCU
int rk_display_init(void);
int rk_display_deinit(void);

int rk_pause_preview(void);
int rk_resume_preview(void);