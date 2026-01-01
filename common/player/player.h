// Copyright 2025 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
	RK_PLAYER_EOF,
	RK_PLAYER_SOF,
	RK_PLAYER_PLAY,
	RK_PLAYER_PAUSE,
	RK_PLAYER_STOP,
	RK_PLAYER_ERROR,
	RK_PLAYER_BUTT,
} RK_PLAYER_EVENT;

typedef void (*rk_player_event_cb)(RK_PLAYER_EVENT);

typedef struct _rkPlayerConfig {
	bool enable_audio;
	bool enable_video;
	bool skip_init_vo; // If caller already create VO layer and channel, skip init vo
	bool skip_init_vo_layer; // If caller already create VO layer, skip init VO layer and device
	rk_player_event_cb event_cb;
	void *ctx;
} RK_PLAYER_CONFIG_S;

typedef struct _rkPhotoConfig {
	const char *file_path;
	uint32_t input_width;
	uint32_t input_height;
	uint32_t output_width;
	uint32_t output_height;
	uint32_t output_format;
} RK_PHOTO_CONFIG;

typedef struct _rkPhotoData {
	void *data;
	uint32_t width;
	uint32_t height;
	uint32_t size;
	uint32_t format;
} RK_PHOTO_DATA;

int rk_player_create(RK_PLAYER_CONFIG_S *config);
int rk_player_destroy(RK_PLAYER_CONFIG_S *config);
int rk_player_play(RK_PLAYER_CONFIG_S *config);
int rk_player_pause(RK_PLAYER_CONFIG_S *config);
int rk_player_stop(RK_PLAYER_CONFIG_S *config);
int rk_player_set_file(RK_PLAYER_CONFIG_S *config, const char *input_file_path);

int rk_player_get_photo(RK_PHOTO_CONFIG *config, RK_PHOTO_DATA *data);
int rk_player_release_photo(RK_PHOTO_DATA *data);

int rk_player_video_seek(RK_PLAYER_CONFIG_S *config, uint64_t time_ms);
int rk_player_audio_seek(RK_PLAYER_CONFIG_S *config, uint64_t time_ms);
int rk_player_get_video_duration(RK_PLAYER_CONFIG_S *config, uint32_t *duration);
int rk_player_get_audio_duration(RK_PLAYER_CONFIG_S *config, uint32_t *duration);
int rk_player_get_video_position(RK_PLAYER_CONFIG_S *config, uint32_t *position);
int rk_player_get_audio_position(RK_PLAYER_CONFIG_S *config, uint32_t *position);
