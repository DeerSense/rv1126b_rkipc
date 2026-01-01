// Copyright 2025 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once
#include "common.h"
#include <mp3dec.h>
#include "mp3_enc_table1.h"
#include "mp3_enc_table2.h"
#include "mp3_enc_types.h"
int register_aenc_mp3(void);
int unregister_aenc_mp3(void);
int register_adec_mp3(void);
int unregister_adec_mp3(void);
