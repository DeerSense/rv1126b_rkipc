/**
 * @file lv_port_indev_templ.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_port_indev.h"
#include "list.h"
#include "lvgl.h"
#include "lvgl/drivers/evdev.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/
#define INPUT_DEV_PATH "/dev/input/"

/**********************
 *  STATIC PROTOTYPES
 **********************/

typedef struct {
	EVDEV_ATTR_S *evdev;
	lv_indev_t *indev;
	lv_indev_drv_t indev_drv;
	struct list_head head;
} indev_context_t;

/**********************
 *  STATIC VARIABLES
 **********************/
static indev_context_t indev_ctx = {
    .evdev = NULL, .indev = NULL, .head = LIST_HEAD_INIT(indev_ctx.head)};

/**********************
 *      MACROS
 **********************/

/**********************
 *   STATIC FUNCTIONS
 **********************/

static void indev_read_data(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
	indev_context_t *obj = indev_drv->user_data;
	if (NULL != obj && NULL != obj->evdev)
		evdev_read(obj->evdev, indev_drv, data);
}

static void indev_add_device(const char *dev_path, lv_indev_type_t type) {
	indev_context_t *obj = NULL;

	obj = calloc(1, sizeof(indev_context_t));
	if (NULL == obj)
		return;

	obj->evdev = evdev_init(dev_path);
	if (NULL == obj->evdev) {
		free(obj), obj = NULL;
		return;
	}

	lv_indev_drv_init(&obj->indev_drv);
	obj->indev_drv.type = type;
	obj->indev_drv.read_cb = indev_read_data;
	obj->indev_drv.user_data = obj;
	obj->indev = lv_indev_drv_register(&obj->indev_drv);

	list_add_tail(&obj->head, &indev_ctx.head);
}

static void indev_match_device(const char *dev_path) {
	int32_t fd = open(dev_path, O_RDONLY);
	if (fd >= 0) {
		char name[256] = "Unknown";
		ioctl(fd, EVIOCGNAME(sizeof(name)), name);
		// Check if the device is a keyboard device
#ifdef LVGL_SUPPORT_KEY
		if (NULL != strstr(name, "key"))
			indev_add_device(dev_path, LV_INDEV_TYPE_KEYPAD);
#endif

#ifdef LVGL_SUPPORT_TOUCHSCREEN
		if (NULL != strstr(name, "goodix-ts"))
			indev_add_device(dev_path, LV_INDEV_TYPE_POINTER);
#endif
		close(fd);
	}
}

static void indev_for_each_entry(void (*cb)(const char *dev_path)) {
	DIR *dir = NULL;
	struct dirent *entry = NULL;
	char devname[PATH_MAX] = {'\0'};

	dir = opendir(INPUT_DEV_PATH);
	if (dir == NULL) {
		perror("opendir");
		return;
	}

	while ((entry = readdir(dir)) != NULL) {
		// Check if the file name matches the pattern "event[0-9]+"
		if (!strncmp(entry->d_name, "event", 5) && 5 < strlen(entry->d_name)) {
			sprintf(devname, "%s%s", INPUT_DEV_PATH, entry->d_name);
			if (NULL != cb)
				cb(devname);
		}
	}

	closedir(dir);

	return;
}

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

typedef struct _GROUP_NODE {
	struct _GROUP_NODE *next;
	lv_group_t *group;
} GROUP_NODE;

static GROUP_NODE *group_list = NULL;

lv_group_t *lv_port_indev_group_create(void) {
	if (list_empty(&indev_ctx.head))
		return NULL;

	indev_context_t *pos = NULL, *n = NULL;
	struct _GROUP_NODE *group_node = NULL;
	lv_group_t *group = lv_group_create();

	list_for_each_entry_safe(pos, n, &indev_ctx.head, head) {
		if (LV_INDEV_TYPE_KEYPAD == pos->indev_drv.type)
			lv_indev_set_group(pos->indev, group);
	}
	lv_group_set_default(group);

	group_node = (struct _GROUP_NODE *)malloc(sizeof(struct _GROUP_NODE));
	group_node->group = group;
	group_node->next = NULL;
	if (group_list) {
		group_node->next = group_list;
		group_list = group_node;
	} else {
		group_list = group_node;
	}

	return group;
}

void lv_port_indev_group_destroy(lv_group_t *group) {
	indev_context_t *pos = NULL, *n = NULL;

	if (group_list) {
		struct _GROUP_NODE *group_node = NULL;
		group_node = group_list;
		if (group_list->group == group) {
			group_list = group_list->next;
			if (group_list) {
				list_for_each_entry_safe(pos, n, &indev_ctx.head, head) {
					if (LV_INDEV_TYPE_KEYPAD == pos->indev_drv.type)
						lv_indev_set_group(pos->indev, group_list->group);
				}
				lv_group_set_default(group_list->group);
			} else {
				list_for_each_entry_safe(pos, n, &indev_ctx.head, head) {
					if (LV_INDEV_TYPE_KEYPAD == pos->indev_drv.type)
						lv_indev_set_group(pos->indev, NULL);
				}
				lv_group_set_default(NULL);
			}
			free(group_node);
		} else {
			while (group_node->next) {
				struct _GROUP_NODE *group_node_next = group_node->next;
				if (group_node_next->group == group) {
					group_node->next = group_node_next->next;
					free(group_node_next);
					break;
				}
				group_node = group_node->next;
			}
		}
		lv_group_del(group);
	}
}

void lv_port_indev_init(lv_disp_rot_t rotate_disp) {
	evdev_coordinate_rotate(rotate_disp);
	indev_for_each_entry(indev_match_device);
}
