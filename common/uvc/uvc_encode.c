/*
 * Copyright (C) 2019 Rockchip Electronics Co., Ltd.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL), available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "uvc_encode.h"
#include "uvc-gadget.h"
#include "uvc_video.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int uvc_encode_init(struct uvc_encode *e, int width, int height, int fcc) {
	printf("%s: width = %d, height = %d, fcc = %d\n", __func__, width, height, fcc);
	memset(e, 0, sizeof(*e));
	e->video_id = -1;
	e->width = -1;
	e->height = -1;
	e->width = width;
	e->height = height;
	e->fcc = fcc;
	if ((fcc == V4L2_PIX_FMT_YUYV) || (fcc == V4L2_PIX_FMT_NV12))
		return 0;

	return 0;
}

void uvc_encode_exit(struct uvc_encode *e) {
	e->video_id = -1;
	e->width = -1;
	e->height = -1;
}

bool uvc_encode_process(struct uvc_encode *e, void *virt, int fd, size_t size) {
	int ret = 0;
	unsigned int fcc;
	int width, height;
	int jpeg_quant;
	void *hnd = NULL;

	if (!uvc_get_user_run_state(e->video_id) || !uvc_buffer_write_enable(e->video_id))
		return false;

	uvc_get_user_resolution(&width, &height, e->video_id);
	fcc = uvc_get_user_fcc(e->video_id);
	switch (fcc) {
	case V4L2_PIX_FMT_YUYV:
		if (virt)
			uvc_buffer_write(0, NULL, 0, virt, width * height * 2, fcc, e->video_id);
		break;
	case V4L2_PIX_FMT_NV12:
		if (virt)
			uvc_buffer_write(0, NULL, 0, virt, width * height * 3 / 2, fcc, e->video_id);
		break;
	case V4L2_PIX_FMT_MJPEG:
		if (virt)
			uvc_buffer_write(0, NULL, 0, virt, size, fcc, e->video_id);
		break;
	case V4L2_PIX_FMT_H264:
		if (virt)
			uvc_buffer_write(0, NULL, 0, virt, size, fcc, e->video_id);
		break;
	case V4L2_PIX_FMT_HEVC:
		if (virt)
			uvc_buffer_write(0, NULL, 0, virt, size, fcc, e->video_id);
		break;
	default:
		printf("%s: not support fcc: %u\n", __func__, fcc);
		break;
	}

	return true;
}
