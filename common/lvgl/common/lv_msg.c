/*
 * Copyright (c) 2022 Rockchip, Inc. All Rights Reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "video.c"

#include "lvgl/common/lv_msg.h"
#include "common.h"
#include "list.h"

#include <stddef.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define LOCK_MSGQ(msg_queue) pthread_mutex_lock(&(msg_queue)->mutex)
#define UNLOCK_MSGQ(msg_queue) pthread_mutex_unlock(&(msg_queue)->mutex)

#define LOCK_HDL(handler_list) pthread_mutex_lock(&(handler_list).mutex)
#define UNLOCK_HDL(handler_list) pthread_mutex_unlock(&(handler_list).mutex)

typedef struct lv_msg {
	void *obj;
	int32_t msg;
	void *wparam;
	void *lparam;
	struct list_head head;
} LV_MSG_S;

typedef struct {
	int8_t quit;
	pthread_t tid;
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	LV_MSG_S list;
} LV_MSG_QUEUE_S;

typedef struct {
	void *obj;
	struct list_head head;
	msg_handler_t handler;
	LV_MSG_QUEUE_S *queue;
	pthread_mutex_t mutex;
} LV_MSG_HANDLER_S;

static LV_MSG_QUEUE_S g_msg_queue;
static LV_MSG_HANDLER_S g_handler_list;

static int32_t lv_msg_clear_handler(LV_MSG_HANDLER_S *handler_list) {
	LV_MSG_HANDLER_S *pos = NULL, *n = NULL;

	list_for_each_entry_safe(pos, n, &handler_list->head, head) {
		list_del(&pos->head);
		free(pos);
	}

	if (!list_empty(&handler_list->head)) {
		LOG_ERROR("Clear handler_list fail!\n");
		return -1;
	}

	return 0;
}

static int32_t lv_msg_clear_queue(LV_MSG_QUEUE_S *msg_queue) {
	LV_MSG_S *pos = NULL, *n = NULL;

	list_for_each_entry_safe(pos, n, &msg_queue->list.head, head) {
		list_del(&pos->head);
		free(pos);
	}

	if (!list_empty(&msg_queue->list.head)) {
		LOG_ERROR("Clear msg_list fail!\n");
		return -1;
	}

	return 0;
}

static LV_MSG_QUEUE_S *lv_msg_get_queue(void *obj) {
	if (NULL == obj) {
		return NULL;
	}

	LV_MSG_HANDLER_S *msg_handler = (LV_MSG_HANDLER_S *)obj;
	return msg_handler->queue;
}

static int32_t lv_dispatch_message(void *obj, int32_t msg, void *wparam, void *lparam) {
	if (NULL != obj) {
		if (((LV_MSG_HANDLER_S *)obj)->handler) {
			if (true == ((LV_MSG_HANDLER_S *)obj)->handler(msg, wparam, lparam)) {
				return 0;
			}
		}
	} else {
		LV_MSG_HANDLER_S *pos = NULL, *n = NULL;

		list_for_each_entry_safe(pos, n, &g_handler_list.head, head) {
			if (NULL != pos->handler) {
				if (true == pos->handler(msg, wparam, lparam)) {
					return 0;
				}
			}
		}
	}

	LOG_ERROR("Check the obj is registered or msg is exit\n");
	return -1;
}

static LV_MSG_S *lv_get_message(LV_MSG_QUEUE_S *msg_queue, int32_t s32TimeoutMs) {
	LV_MSG_S *elm = NULL;
	struct timeval timeNow;
	struct timespec timeout;

	LOCK_MSGQ(msg_queue);

	if (list_empty(&msg_queue->list.head)) {
		gettimeofday(&timeNow, NULL);
		timeout.tv_sec = timeNow.tv_sec + s32TimeoutMs / 1000;
		timeout.tv_nsec = (timeNow.tv_usec + (s32TimeoutMs % 1000) * 1000) * 1000;
		if (timeout.tv_nsec >= 1000000000) {
			timeout.tv_sec += 1;
			timeout.tv_nsec -= 1000000000;
		}
		pthread_cond_timedwait(&msg_queue->cond, &msg_queue->mutex, &timeout);
	}

	if (!list_empty(&msg_queue->list.head)) {
		elm = list_first_entry(&msg_queue->list.head, LV_MSG_S, head);
		list_del(&elm->head);
	}

	UNLOCK_MSGQ(msg_queue);

	return elm;
}

int32_t lv_send_msg(void *obj, int32_t msg, void *wparam, void *lparam) {
	int32_t ret = lv_dispatch_message(obj, msg, wparam, lparam);
	if (ret) {
		LOG_ERROR("Send msg handle fail!\n");
		return -1;
	}
	return 0;
}

int32_t lv_post_msg(void *obj, int32_t msg, void *wparam, void *lparam) {
	LV_MSG_S *elm = NULL;
	LV_MSG_QUEUE_S *msg_queue = NULL;

	if (!(msg_queue = lv_msg_get_queue(obj)))
		return -1;

	elm = calloc(1, sizeof(LV_MSG_S));
	if (!elm) {
		LOG_ERROR("elm malloc failed.");
		return -1;
	}
	elm->obj = obj;
	elm->msg = msg;
	elm->wparam = wparam;
	elm->lparam = lparam;

	LOCK_MSGQ(msg_queue);
	list_add_tail(&elm->head, &msg_queue->list.head);
	pthread_cond_signal(&msg_queue->cond);
	UNLOCK_MSGQ(msg_queue);

	return 0;
}

void *lv_msg_register_handler(msg_handler_t handler) {
	if (handler == NULL) {
		return NULL;
	}

	LV_MSG_HANDLER_S *pos = NULL, *n = NULL, *elm = NULL;

	elm = (LV_MSG_HANDLER_S *)calloc(1, sizeof(LV_MSG_HANDLER_S));
	if (elm == NULL) {
		LOG_ERROR("calloc fail!\n");
		return NULL;
	}
	elm->handler = handler;
	elm->obj = elm;
	elm->queue = &g_msg_queue;

	LOCK_HDL(g_handler_list);
	list_for_each_entry_safe(pos, n, &g_handler_list.head, head) {
		if (handler == pos->handler) {
			LOG_ERROR("Register handler fail!\n");
			free(elm);
			elm = NULL;
			UNLOCK_HDL(g_handler_list);
			return NULL;
		}
	}

	list_add_tail(&elm->head, &g_handler_list.head);

	UNLOCK_HDL(g_handler_list);

	return elm;
}

void lv_msg_unregister_handler(void *obj) {
	LV_MSG_HANDLER_S *pos = NULL, *n = NULL;
	LOCK_HDL(g_handler_list);
	list_for_each_entry_safe(pos, n, &g_handler_list.head, head) {
		if (obj == pos) {
			list_del(&pos->head);
			free(pos);
			break;
		}
	}
	UNLOCK_HDL(g_handler_list);
}

static void *lv_msg_recv_thread(void *arg) {
	LV_MSG_QUEUE_S *msg_queue = (LV_MSG_QUEUE_S *)arg;

	while (msg_queue->quit == 0) {
		LV_MSG_S *elm = lv_get_message(msg_queue, 50);
		if (elm) {
			if (lv_dispatch_message(elm->obj, elm->msg, elm->wparam, elm->lparam)) {
				LOG_ERROR("lv_dispatch_message fail.\n");
			}
			free(elm);
			elm = NULL;
		}
	}
	return NULL;
}

int32_t lv_msg_init(void) {
	g_msg_queue.quit = 0;
	INIT_LIST_HEAD(&g_msg_queue.list.head);
	pthread_mutex_init(&g_msg_queue.mutex, NULL);
	pthread_cond_init(&g_msg_queue.cond, NULL);
	if (pthread_create(&g_msg_queue.tid, NULL, lv_msg_recv_thread, &g_msg_queue)) {
		LOG_ERROR("RecMsgThread create failed!\n");
		return -1;
	}

	INIT_LIST_HEAD(&g_handler_list.head);
	pthread_mutex_init(&g_handler_list.mutex, NULL);

	return 0;
}

int32_t lv_msg_deinit(void) {
	g_msg_queue.quit = 1;
	if (g_msg_queue.tid) {
		pthread_cancel(g_msg_queue.tid);
		if (pthread_join(g_msg_queue.tid, NULL)) {
			LOG_ERROR("Recmsgthread join failed!\n");
			return -1;
		}
		g_msg_queue.tid = 0;
	}

	if (lv_msg_clear_queue(&g_msg_queue))
		LOG_WARN("lv_mag_clear_list fail\n");

	pthread_mutex_destroy(&g_msg_queue.mutex);
	pthread_cond_destroy(&g_msg_queue.cond);

	if (lv_msg_clear_handler(&g_handler_list))
		LOG_WARN("lv_mag_clear_list fail\n");

	pthread_mutex_destroy(&g_handler_list.mutex);

	return 0;
}
