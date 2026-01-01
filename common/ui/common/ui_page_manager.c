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
#define LOG_TAG "ui_page_manager.c"

#include "ui_page_manager.h"
#include "common.h"

#include <stdint.h>

/**********************
 *      MACROS
 **********************/
#define UI_PAGE_NUM 16

/**********************
 *  STATIC PROTOTYPES
 **********************/
typedef enum ui_page_state_e {
	UI_PAGESTATE_IDLE = 0,  /* Not in use */
	UI_PAGESTATE_DESTROYED, /* Not active and having been destroyed */
	UI_PAGESTATE_CREATED,   /* Created */
	UI_PAGESTATE_NOTACTIVE, /* Not active */
	UI_PAGESTATE_ACTIVE,    /* Active and at the foreground */
} UI_PAGE_STATE_E;

typedef struct ui_page_stack_t {
	lv_obj_t *obj;
	UI_PAGE_STATE_E state;
	UI_PAGE_HANDLER_T *handler;
} UI_PAGE_STACK_T;

typedef struct ui_page_context_t {
	bool small_mem;
	lv_ll_t stack;
	UI_PAGE_HANDLER_T *page_ll[UI_PAGE_NUM];
	UI_PAGE_STACK_T *main_page;
} UI_PAGE_CONTEXT_T;

/**********************
 *  STATIC VARIABLES
 **********************/
static UI_PAGE_CONTEXT_T page_ctx;

/**********************
 *   STATIC FUNCTIONS
 **********************/
static UI_PAGE_HANDLER_T *page_find_by_name(const char name[]) {
	for (uint32_t i = 0; i < UI_PAGE_NUM && page_ctx.page_ll[i]; i++) {
		if (!strcmp(page_ctx.page_ll[i]->name, name))
			return page_ctx.page_ll[i];
	}

	return NULL;
}

static void ui_page_active(UI_PAGE_STACK_T *stack_item) {
	if (stack_item->state == UI_PAGESTATE_DESTROYED) {
		stack_item->handler->create(stack_item->obj);
		stack_item->handler->enter(stack_item->obj);
		stack_item->state = UI_PAGESTATE_ACTIVE;
	} else if ((stack_item->state == UI_PAGESTATE_CREATED) ||
	           (stack_item->state == UI_PAGESTATE_NOTACTIVE)) {
		stack_item->handler->enter(stack_item->obj);
		stack_item->state = UI_PAGESTATE_ACTIVE;
	}
}

static void ui_page_noactive(UI_PAGE_STACK_T *stack_item) {
	if (stack_item->state > UI_PAGESTATE_NOTACTIVE) {
		stack_item->handler->exit(stack_item->obj);
		stack_item->state = UI_PAGESTATE_NOTACTIVE;
	} else if (stack_item->state > UI_PAGESTATE_DESTROYED) {
		stack_item->state = UI_PAGESTATE_NOTACTIVE;
	}
}

static void ui_page_destroy(UI_PAGE_STACK_T *stack_item) {
	if (stack_item->state > UI_PAGESTATE_NOTACTIVE) {
		stack_item->handler->exit(stack_item->obj);
		stack_item->handler->destroy(stack_item->obj);
		stack_item->state = UI_PAGESTATE_DESTROYED;
	} else if (stack_item->state > UI_PAGESTATE_DESTROYED) {
		stack_item->handler->destroy(stack_item->obj);
		stack_item->state = UI_PAGESTATE_DESTROYED;
	}
}

static void ui_page_remove(UI_PAGE_STACK_T *stack_item) {
	if (stack_item->state > UI_PAGESTATE_NOTACTIVE) {
		stack_item->handler->exit(stack_item->obj);
		stack_item->handler->destroy(stack_item->obj);
		stack_item->state = UI_PAGESTATE_IDLE;
	} else if (stack_item->state > UI_PAGESTATE_DESTROYED) {
		stack_item->handler->destroy(stack_item->obj);
		stack_item->state = UI_PAGESTATE_IDLE;
	}
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
int32_t ui_page_init(bool small_mem) {
	page_ctx.small_mem = small_mem;
	_lv_ll_init(&page_ctx.stack, sizeof(UI_PAGE_STACK_T));
	return 0;
}

void ui_page_deinit(void) {
	void *i = NULL;
	void *i_next = NULL;

	i = _lv_ll_get_head(&page_ctx.stack);
	i_next = NULL;

	while (i != NULL) {
		i_next = _lv_ll_get_next(&page_ctx.stack, i);

		ui_page_remove((UI_PAGE_STACK_T *)i);
		lv_obj_del(((UI_PAGE_STACK_T *)i)->obj);

		_lv_ll_remove(&page_ctx.stack, i);
		lv_mem_free(i);
		i = i_next;
	}
	page_ctx.main_page = NULL;
}

void ui_page_register(UI_PAGE_HANDLER_T *page) {
	for (uint32_t i = 0; i < UI_PAGE_NUM; i++) {
		if (!page_ctx.page_ll[i]) {
			page_ctx.page_ll[i] = page;
			LOG_ERROR("[page mgr] registration page: %s\n", page->name);
			break;
		}
	}
}

int32_t ui_page_push_page(const char *name, void *user_data) {
	if (NULL == name || 0 == strlen(name))
		return -1;

	UI_PAGE_STACK_T *top = NULL, *cur_page = NULL;
	UI_PAGE_HANDLER_T *handler = NULL;

	handler = page_find_by_name(name);
	if (NULL == handler)
		return -1;

	top = (UI_PAGE_STACK_T *)_lv_ll_get_head(&page_ctx.stack);
	if (NULL != top && top->handler == handler)
		return -1;

	cur_page = (UI_PAGE_STACK_T *)_lv_ll_ins_head(&page_ctx.stack);
	if (NULL == cur_page)
		return -1;
	cur_page->handler = handler;
	cur_page->obj = lv_obj_create(NULL);
	lv_obj_set_user_data(cur_page->obj, user_data);
	cur_page->state = UI_PAGESTATE_DESTROYED;
	if (NULL == cur_page->obj) {
		_lv_ll_remove(&page_ctx.stack, cur_page);
		lv_mem_free(cur_page);
		return -1;
	}

	if (NULL == top)
		page_ctx.main_page = cur_page;
	else {
		if (true == page_ctx.small_mem)
			ui_page_destroy(top);
		else
			ui_page_noactive(top);
	}

	ui_page_active(cur_page);
	lv_scr_load(cur_page->obj);

	return 0;
}

int32_t ui_page_pop_page(void) {
	UI_PAGE_STACK_T *cur = NULL, *next = NULL;

	cur = (UI_PAGE_STACK_T *)_lv_ll_get_head(&page_ctx.stack);
	if (NULL == cur || page_ctx.main_page == cur)
		return -1;

	next = (UI_PAGE_STACK_T *)_lv_ll_get_next(&page_ctx.stack, cur);
	if (NULL == next)
		return -1;

	// INFO: switch acitve screen first to avoid lvgl warning.
	lv_scr_load(next->obj);
	ui_page_remove(cur);
	lv_obj_del(cur->obj);
	_lv_ll_remove(&page_ctx.stack, cur);
	lv_mem_free(cur);

	ui_page_active(next);

	return 0;
}

int32_t ui_page_switch_page(const char *name, void *user_data) {
	if (NULL == name || 0 == strlen(name))
		return -1;

	int32_t num = 0;
	void *i = NULL;
	void *i_next = NULL;

	i = _lv_ll_get_head(&page_ctx.stack);
	i_next = NULL;

	while (i != NULL) {
		i_next = _lv_ll_get_next(&page_ctx.stack, i);

		if (!strcmp(name, (((UI_PAGE_STACK_T *)i)->handler->name)))
			break;

		i = i_next;
		num++;
	}

	if (NULL != i) {
		while (num--)
			ui_page_pop_page();
	} else {
		ui_page_push_page(name, user_data);
	}

	return 0;
}
