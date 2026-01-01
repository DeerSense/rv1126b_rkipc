// Copyright 2021 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <stdint.h>
int rkipc_audio_init();
int rkipc_audio_deinit();
int rkipc_ao_init();
int rkipc_ao_deinit();
int rkipc_ao_write(unsigned char *data, int data_len);
int rkipc_audio_player_init(int sample_rate, int channels, const char *encode_type);
int rkipc_audio_player_deinit();
int rkipc_audio_player_send(uint8_t *data, uint32_t len, uint64_t pts);
int uac_record_start();
int uac_record_stop();
int uac_playback_start();
int uac_playback_stop();
void uac_set_sample_rate(int type, int sampleRate);
void uac_set_volume(int type, int volume);
void uac_set_mute(int type, int mute);
void uac_set_ppm(int type, int ppm);


// export api
int rk_audio_restart();
int rk_audio_get_bit_rate(int stream_id, int *value);
int rk_audio_set_bit_rate(int stream_id, int value);
int rk_audio_get_sample_rate(int stream_id, int *value);
int rk_audio_set_sample_rate(int stream_id, int value);
int rk_audio_get_volume(int stream_id, int *value);
int rk_audio_set_volume(int stream_id, int value);
int rk_audio_get_enable_vqe(int stream_id, int *value);
int rk_audio_set_enable_vqe(int stream_id, int value);
int rk_audio_get_encode_type(int stream_id, const char **value);
int rk_audio_set_encode_type(int stream_id, const char *value);
