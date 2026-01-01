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

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <stdlib.h>

#include <linux/netlink.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <math.h>

#include "audio.h"
#include "uevent.h"
#include "uvc_control.h"

#define UVC_SUBSYSTEM "video4linux"
#define UAC_SUBSYSTEM "u_audio"

/*
 * case 1:
 * the UAC1 uevent when pc/remote close(play sound of usb close)
 *
 * strs[0] = ACTION=change
 * strs[1] = DEVPATH=/devices/virtual/u_audio/UAC1_Gadget 0   // UAC2_Gadget
 * strs[2] = SUBSYSTEM=u_audio
 * strs[3] = USB_STATE=SET_INTERFACE
 * strs[4] = STREAM_DIRECTION=OUT
 * strs[5] = STREAM_STATE=OFF
 *
 *
 * case 2:
 * the UAC1 uevent when pc/remote play start(play sound of usb open)
 *
 * strs[0] = ACTION=change
 * strs[1] = DEVPATH=/devices/virtual/u_audio/UAC1_Gadget 0
 * strs[2] = SUBSYSTEM=u_audio
 * strs[3] = USB_STATE=SET_INTERFACE
 * strs[4] = STREAM_DIRECTION=OUT
 * strs[5] = STREAM_STATE=ON
 *
 *
 * case 3:
 * the UAC1 uevent when pc/remote capture start(record sound of usb open)
 *
 * strs[0] = ACTION=change
 * strs[1] = DEVPATH=/devices/virtual/u_audio/UAC1_Gadget 0
 * strs[2] = SUBSYSTEM=u_audio
 * strs[3] = USB_STATE=SET_INTERFACE
 * strs[4] = STREAM_DIRECTION=IN
 * strs[5] = STREAM_STATE=ON
 *
 *
 * case 4:
 * the UAC1 uevent when pc/remote capture stop(record sound of usb open)
 *
 * strs[0] = ACTION=change
 * strs[1] = DEVPATH=/devices/virtual/u_audio/UAC1_Gadget 0
 * strs[2] = SUBSYSTEM=u_audio
 * strs[3] = USB_STATE=SET_INTERFACE
 * strs[4] = STREAM_DIRECTION=IN
 * strs[5] = STREAM_STATE=OFF
 *
 *
 * case 5:
 * the UAC1 uevent
 *
 * strs[0] = ACTION=change
 * strs[1] = DEVPATH=/devices/virtual/u_audio/UAC1_Gadget 0
 * strs[2] = SUBSYSTEM=u_audio
 * strs[3] = USB_STATE=SET_SAMPLE_RATE
 * strs[4] = STREAM_DIRECTION=IN
 * strs[5] = SAMPLE_RATE=48000
 */
#define UAC_UEVENT_AUDIO "SUBSYSTEM=u_audio"
#define UAC_UEVENT_SET_INTERFACE "USB_STATE=SET_INTERFACE"
#define UAC_UEVENT_SET_SAMPLE_RATE "USB_STATE=SET_SAMPLE_RATE"
#define UAC_UEVENT_SET_VOLUME "USB_STATE=SET_VOLUME"
#define UAC_UEVENT_SET_MUTE "USB_STATE=SET_MUTE"
#define UAC_UEVENT_SET_AUDIO_CLK "USB_STATE=SET_AUDIO_CLK"

#define UAC_STREAM_DIRECT "STREAM_DIRECTION="
#define UAC_STREAM_STATE "STREAM_STATE="
#define UAC_SAMPLE_RATE "SAMPLE_RATE="
#define UAC_SET_VOLUME "VOLUME="
#define UAC_SET_MUTE "MUTE="
#define UAC_PPM "PPM="

// remote device/pc->our device
#define UAC_REMOTE_PLAY "OUT"

// our device->remote device/pc
#define UAC_REMOTE_CAPTURE "IN"

// sound card is opened
#define UAC_STREAM_START "ON"

// sound card is closed
#define UAC_STREAM_STOP "OFF"

enum UAC_UEVENT_KEY {
	UAC_KEY_AUDIO = 2,
	UAC_KEY_USB_STATE = 3,
	UAC_KEY_DIRECTION = 4,
	UAC_KEY_PPM = 4,
	UAC_KEY_STREAM_STATE = 5,
	UAC_KEY_SAMPLE_RATE = UAC_KEY_STREAM_STATE,
	UAC_KEY_VOLUME = UAC_KEY_STREAM_STATE,
	UAC_KEY_MUTE = UAC_KEY_STREAM_STATE,
};

static bool find_video;

bool compare(const char *dst, const char *srt) {
	if ((dst == NULL) || (srt == NULL))
		return false;

	if (!strncmp(dst, srt, strlen(srt))) {
		return true;
	}

	return false;
}

static void video_uevent(const struct _uevent *event) {
	const char dev_name[] = "DEVNAME=";
	char *tmp = NULL;
	char *act = event->strs[0] + 7;
	int i, id;

	for (i = 3; i < event->size; i++) {
		tmp = event->strs[i];
		/* search "DEVNAME=" */
		if (!strncmp(dev_name, tmp, strlen(dev_name)))
			break;
	}

	if (i < event->size) {
		tmp = strchr(tmp, '=') + 1;

		if (sscanf((char *)&tmp[strlen("video")], "%d", &id) < 1) {
			printf("failed to parse video id\n");
			return;
		}

		if (!strcmp(act, "add")) {
			printf("add video...\n");
			uvc_control_signal();
			find_video = true;
			// video_record_addvideo(id, 1920, 1080, 30);
		} else {
			printf("delete video...\n");
			find_video = false;
			// video_record_deletevideo(id);
		}
	}
}

static bool g_record_started = false;
static bool g_playback_started = false;
static pthread_mutex_t g_audio_mutex = PTHREAD_MUTEX_INITIALIZER;
void audio_play(const struct _uevent *uevent) {
	char *direct = uevent->strs[UAC_KEY_DIRECTION];
	char *status = uevent->strs[UAC_KEY_STREAM_STATE];

	if (compare(direct, UAC_STREAM_DIRECT) && compare(status, UAC_STREAM_STATE)) {
		char *device = &direct[strlen(UAC_STREAM_DIRECT)];
		char *state = &status[strlen(UAC_STREAM_STATE)];
		// remote device/pc open/close usb sound card to write data
		pthread_mutex_lock(&g_audio_mutex);
		if (compare(device, UAC_REMOTE_PLAY)) {
			if (compare(UAC_STREAM_START, state)) {
				// stream start, we need to open usb card to record datas
				if (!g_record_started) {
					printf("remote device/pc start to play data to us, we need to open usb to "
					       "capture datas\n");
					uac_record_start();
					g_record_started = true;
				}
			} else if (compare(UAC_STREAM_STOP, state)) {
				if (g_record_started) {
					printf("remote device/pc stop to play data to us, we need to stop capture "
					       "datas\n");
					uac_record_stop();
					g_record_started = false;
				}
			}
		} else if (compare(device, UAC_REMOTE_CAPTURE)) {
			// our device->remote device/pc
			if (compare(UAC_STREAM_START, state)) {
				// stream start, we need to open usb card to record datas
				if (!g_playback_started) {
					printf("remote device/pc start to record from us, we need to open usb to send "
					       "datas\n");
					uac_playback_start();
					g_playback_started = true;
				}
			} else if (compare(UAC_STREAM_STOP, state)) {
				if (g_playback_started) {
					printf("remote device/pc stop to record from us, we need to stop write datas "
					       "to usb\n");
					uac_playback_stop();
					g_playback_started = false;
				}
			}
		}
		pthread_mutex_unlock(&g_audio_mutex);
	}
}

void audio_set_samplerate(const struct _uevent *uevent) {
	char *direct = uevent->strs[UAC_KEY_DIRECTION];
	char *samplerate = uevent->strs[UAC_KEY_SAMPLE_RATE];
	printf("%s: %s\n", __FUNCTION__, direct);
	printf("%s: %s\n", __FUNCTION__, samplerate);
	if (compare(direct, UAC_STREAM_DIRECT)) {
		char *device = &direct[strlen(UAC_STREAM_DIRECT)];
		char *rate = &samplerate[strlen(UAC_SAMPLE_RATE)];
		int sampleRate = atoi(rate);
		if (compare(device, UAC_REMOTE_PLAY)) {
			printf("set samplerate %d to usb record\n", sampleRate);
			uac_set_sample_rate(0, sampleRate);
		} else if (compare(device, UAC_REMOTE_CAPTURE)) {
			printf("set samplerate %d to usb playback\n", sampleRate);
			uac_set_sample_rate(1, sampleRate);
		}
	}
}

/*
 * strs[0] = ACTION=change
 * strs[1] = DEVPATH=/devicges/virtual/u_audio/UAC1_Gadgeta 0
 * strs[2] = SUBSYSTEM=u_audio
 * strs[3] = USB_STATE=SET_VOLUME
 * strs[4] = STREAM_DIRECTION=OUT
 * strs[5] = VOLUME=0x7FFF
 *    index       db
 *   0x7FFF:   127.9961
 *   ......
 *   0x0100:   1.0000
 *   ......
 *   0x0002:   0.0078
 *   0x0001:   0.0039
 *   0x0000:   0.0000
 *   0xFFFF:  -0.0039
 *   0xFFFE:  -0.0078
 *   ......
 *   0xFE00:  -1.0000
 *   ......
 *   0x8002:  -127.9922
 *   0x8001:  -127.9961
 *
 */
void audio_set_volume(const struct _uevent *uevent) {
	char *direct = uevent->strs[UAC_KEY_DIRECTION];
	char *volumeStr = uevent->strs[UAC_KEY_VOLUME];
	int unit = 0x100;
	printf("direct = %s volume = %s\n", direct, volumeStr);
	if (compare(direct, UAC_STREAM_DIRECT)) {
		char *device = &direct[strlen(UAC_STREAM_DIRECT)];
		short volume = 0;
		int volume_t = 0;
		float db = 0;
		sscanf(volumeStr, "VOLUME=0x%x", &volume_t);
		volume = (short)volume_t;
		db = volume / (float)unit;
		double precent = pow(10, db / 10);
		int precentInt = (int)(precent * 100);
		printf("set db = %f, precent = %lf, precentInt = %d\n", db, precent, precentInt);
		if (compare(device, UAC_REMOTE_PLAY)) {
			printf("set volume %d  to usb record\n", precentInt);
			uac_set_volume(0, precentInt);
		} else if (compare(device, UAC_REMOTE_CAPTURE)) {
			printf("set volume %d  to usb playback\n", precentInt);
			uac_set_volume(1, precentInt);
		}
	}
}

/*
 * strs[0] = ACTION=change
 * strs[1] = DEVPATH=/devices/virtual/u_audio/UAC1_Gadget 0
 * strs[2] = SUBSYSTEM=u_audio
 * strs[3] = USB_STATE=SET_MUTE
 * strs[4] = STREAM_DIRECTION=OUT
 * strs[5] = MUTE=1
 */
void audio_set_mute(const struct _uevent *uevent) {
	char *direct = uevent->strs[UAC_KEY_DIRECTION];
	char *muteStr = uevent->strs[UAC_KEY_MUTE];
	printf("direct = %s mute = %s\n", direct, muteStr);

	if (compare(direct, UAC_STREAM_DIRECT)) {
		char *device = &direct[strlen(UAC_STREAM_DIRECT)];
		int mute = 0;
		sscanf(muteStr, "MUTE=%d", &mute);
		if (compare(device, UAC_REMOTE_PLAY)) {
			printf("set mute = %d to usb record\n", mute);
			uac_set_mute(0, mute);
		} else if (compare(device, UAC_REMOTE_CAPTURE)) {
			printf("set mute = %d to usb playback\n", mute);
			uac_set_mute(1, mute);
		}
	}
}

/*
 * strs[0] = ACTION=change
 * strs[1] = DEVPATH=/devices/virtual/u_audio/UAC1_Gadget 0
 * strs[2] = SUBSYSTEM=u_audio
 * strs[3] = USB_STATE=SET_AUDIO_CLK
 * strs[4] = PPM=-21
 * strs[5] = SEQNUM=1573
 */
void audio_set_ppm(const struct _uevent *uevent) {
	char *ppmStr = uevent->strs[UAC_KEY_PPM];

	if (compare(ppmStr, UAC_PPM)) {
		int ppm = 0;
		sscanf(ppmStr, "PPM=%d", &ppm);
		uac_set_ppm(0, ppm);
		uac_set_ppm(1, ppm);
	}
}

void audio_event(const struct _uevent *uevent) {
	char *event = uevent->strs[UAC_KEY_USB_STATE];
	char *direct = uevent->strs[UAC_KEY_DIRECTION];
	char *status = uevent->strs[UAC_KEY_STREAM_STATE];
	printf("audio_event = %s, direct = %s, status = %s---", event, direct, status);
	if ((event == NULL) || (direct == NULL) || (status == NULL)) {
		return;
	}

	bool setInterface = compare(event, UAC_UEVENT_SET_INTERFACE);
	bool setSampleRate = compare(event, UAC_UEVENT_SET_SAMPLE_RATE);
	bool setVolume = compare(event, UAC_UEVENT_SET_VOLUME);
	bool setMute = compare(event, UAC_UEVENT_SET_MUTE);
	bool setClk = compare(event, UAC_UEVENT_SET_AUDIO_CLK);
	if (!setInterface && !setSampleRate && !setVolume && !setMute && !setClk) {
		return;
	}

	if (setInterface) {
		printf("---audio_play\n");
		audio_play(uevent);
	} else if (setSampleRate) {
		// printf("---audio_set_samplerate\n"); // tmp only 8000
		// audio_set_samplerate(uevent);
	} else if (setVolume) {
		printf("---setVolume\n");
		audio_set_volume(uevent);
	} else if (setMute) {
		printf("---setMute\n");
		audio_set_mute(uevent);
	} else if (setClk) {
		// printf("---setClk\n");
		// audio_set_ppm(uevent);
	}
}

/*
 * e.g uevent info
 * ACTION=change
 * DEVPATH=/devices/11050000.i2c/i2c-0/0-0012/cvr_uevent/gsensor
 * SUBSYSTEM=cvr_uevent
 * CVR_DEV_NAME=gsensor
 * CVR_DEV_TYPE=2
 */
static void parse_event(const struct _uevent *event) {
	char *sysfs = NULL;

	if (event->size <= 0)
		return;

#if 0
	for (int i = 0 ; i < 10; i++) {
		if (event->strs[i] != NULL) {
			printf("strs[%d] = %s\n", i, event->strs[i]);
		}
	}
#endif

	sysfs = event->strs[2] + 10;
	if (!strcmp(sysfs, UVC_SUBSYSTEM)) {
		video_uevent(event);
	} else if (!strcmp(sysfs, UAC_SUBSYSTEM)) {
		audio_event(event);
	}
}

static void *event_monitor_thread(void *arg) {
	int sockfd;
	int i, j, len;
	char buf[512];
	struct iovec iov;
	struct msghdr msg;
	struct sockaddr_nl sa;
	struct _uevent event;
	uint32_t flags = *(uint32_t *)arg;

	prctl(PR_SET_NAME, "event_monitor", 0, 0, 0);

	memset(&sa, 0, sizeof(sa));
	sa.nl_family = AF_NETLINK;
	sa.nl_groups = NETLINK_KOBJECT_UEVENT;
	sa.nl_pid = 0;
	memset(&msg, 0, sizeof(msg));
	iov.iov_base = (void *)buf;
	iov.iov_len = sizeof(buf);
	msg.msg_name = (void *)&sa;
	msg.msg_namelen = sizeof(sa);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	sockfd = socket(AF_NETLINK, SOCK_RAW, NETLINK_KOBJECT_UEVENT);
	if (sockfd == -1) {
		printf("socket creating failed:%s\n", strerror(errno));
		goto err_event_monitor;
	}

	if (bind(sockfd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
		printf("bind error:%s\n", strerror(errno));
		goto err_event_monitor;
	}

	find_video = false;
	while (1) {
		event.size = 0;
		len = recvmsg(sockfd, &msg, 0);
		if (len < 0) {
			printf("receive error\n");
		} else if (len < 32 || len > sizeof(buf)) {
			printf("invalid message");
		} else {
			for (i = 0, j = 0; i < len; i++) {
				if (*(buf + i) == '\0' && (i + 1) != len) {
					event.strs[j++] = buf + i + 1;
					event.size = j;
				}
			}
		}
		parse_event(&event);
		if ((flags & UVC_CONTROL_LOOP_ONCE) && find_video)
			break;
	}

err_event_monitor:
	pthread_detach(pthread_self());
	pthread_exit(NULL);
}

int uevent_monitor_run(uint32_t flags) {
	pthread_t tid;

	return pthread_create(&tid, NULL, event_monitor_thread, &flags);
}
