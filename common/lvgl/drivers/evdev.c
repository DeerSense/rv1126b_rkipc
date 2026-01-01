/**
 * @file evdev.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "evdev.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/input.h>

static bool translate_all_key_to_click = false;
static int current_key_value = 0;

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static EVDEV_ATTR_S *evdev_set_file(const char *dev_name);

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_disp_rot_t evdev_rot = LV_DISP_ROT_NONE;

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

int evdev_get_current_code(void) {
	return current_key_value;
}

void evdev_force_translate_to_click(bool enable) {
	translate_all_key_to_click = enable;
	current_key_value = 0;
}

/**
 * set coordinate rotation
 *  @param rot Coordinate rotation angle
 */
void evdev_coordinate_rotate(lv_disp_rot_t rot) { evdev_rot = rot; }

/**
 * Initialize the evdev interface
 */
EVDEV_ATTR_S *evdev_init(const char *evdev_name) { return evdev_set_file(evdev_name); }
/**
 * reconfigure the device file for evdev
 * @param dev_name set the evdev device filename
 * @return true: the device file set complete
 *         false: the device file doesn't exist current system
 */
EVDEV_ATTR_S *evdev_set_file(const char *dev_name) {
	EVDEV_ATTR_S *evdev_attr = calloc(1, sizeof *evdev_attr);
	if (NULL == evdev_attr) {
		perror("Failed to apply for memory.");
		return NULL;
	}
	evdev_attr->evdev_fd = -1;

	evdev_attr->evdev_fd = open(dev_name, O_RDWR | O_NOCTTY | O_NDELAY);

	if (evdev_attr->evdev_fd == -1) {
		perror("unable to open evdev interface:");
		free(evdev_attr);
		return NULL;
	}

	fcntl(evdev_attr->evdev_fd, F_SETFL, O_ASYNC | O_NONBLOCK);

	evdev_attr->evdev_root_x = 0;
	evdev_attr->evdev_root_y = 0;
	evdev_attr->evdev_key_val = 0;
	evdev_attr->evdev_button = LV_INDEV_STATE_REL;

	return evdev_attr;
}
/**
 * Get the current position and state of the evdev
 * @param data store the evdev data here
 */
void evdev_read(EVDEV_ATTR_S *evdev_attr, lv_indev_drv_t *drv, lv_indev_data_t *data) {
	struct input_event in;
	// reset key value in each read loop
	current_key_value = 0;
	while (read(evdev_attr->evdev_fd, &in, sizeof(struct input_event)) > 0) {
		if (in.type == EV_REL) {
			if (in.code == REL_X)
				evdev_attr->evdev_root_x += in.value;
			else if (in.code == REL_Y)
				evdev_attr->evdev_root_y += in.value;
		} else if (in.type == EV_ABS) {
			if (in.code == ABS_X) {
				evdev_attr->evdev_root_x = in.value;
			} else if (in.code == ABS_Y) {
				evdev_attr->evdev_root_y = in.value;
			} else if (in.code == ABS_MT_POSITION_X) {
				evdev_attr->evdev_root_x = in.value;
			} else if (in.code == ABS_MT_POSITION_Y) {
				evdev_attr->evdev_root_y = in.value;
			} else if (in.code == ABS_MT_TRACKING_ID) {
				if (in.value == -1)
					evdev_attr->evdev_button = LV_INDEV_STATE_REL;
				else if (in.value == 0)
					evdev_attr->evdev_button = LV_INDEV_STATE_PR;
			}
		} else if (in.type == EV_KEY) {
			if (in.code == BTN_MOUSE || in.code == BTN_TOUCH) {
				if (in.value == 0)
					evdev_attr->evdev_button = LV_INDEV_STATE_REL;
				else if (in.value == 1)
					evdev_attr->evdev_button = LV_INDEV_STATE_PR;
			} else if (drv->type == LV_INDEV_TYPE_KEYPAD) {
				switch (in.code) {
				case KEY_BACKSPACE:
					data->key = LV_KEY_BACKSPACE;
					break;
				case KEY_ENTER:
					data->key = LV_KEY_ENTER;
					break;
				case KEY_PREVIOUS:
					data->key = LV_KEY_PREV;
					break;
				case KEY_NEXT:
					data->key = LV_KEY_NEXT;
					break;
				case KEY_UP:
					data->key = LV_KEY_UP;
					break;
				case KEY_LEFT:
					data->key = LV_KEY_LEFT;
					break;
				case KEY_RIGHT:
					data->key = LV_KEY_RIGHT;
					break;
				case KEY_DOWN:
					data->key = LV_KEY_DOWN;
					break;
				case KEY_TAB:
					data->key = LV_KEY_NEXT;
					break;
				case KEY_ESC:
					data->key = LV_KEY_ESC;
					break;
				case KEY_POWER:
					data->key = LV_KEY_ENTER;
					current_key_value = in.code;
					printf("%s: force to click!\n", __func__);
					break;
				case KEY_MENU:
					data->key = LV_KEY_ENTER;
					current_key_value = in.code;
					if (translate_all_key_to_click) {
						data->key = LV_KEY_ENTER;
						printf("%s: force to click!\n", __func__);
					}
					break;
				case KEY_VOLUMEUP:
					printf("%s: vol up to prev\n", __func__);
					data->key = LV_KEY_PREV;
					current_key_value = in.code;
					if (translate_all_key_to_click) {
						data->key = LV_KEY_ENTER;
						printf("%s: force to click!\n", __func__);
					}
					break;
				case KEY_VOLUMEDOWN:
					printf("%s: vol down to next\n", __func__);
					data->key = LV_KEY_NEXT;
					current_key_value = in.code;
					if (translate_all_key_to_click) {
						data->key = LV_KEY_ENTER;
						printf("%s: force to click!\n", __func__);
					}
					break;
				default:
					data->key = 0;
					break;
				}
				if (data->key != 0) {
					/* Only record button state when actual output is produced to prevent widgets
					 * from refreshing */
					data->state = (in.value) ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
				}
				evdev_attr->evdev_key_val = data->key;
				evdev_attr->evdev_button = data->state;
				return;
			}
		}
	}

	if (drv->type == LV_INDEV_TYPE_KEYPAD) {
		/* No data retrieved */
		data->key = evdev_attr->evdev_key_val;
		data->state = evdev_attr->evdev_button;
		return;
	}
	if (drv->type != LV_INDEV_TYPE_POINTER)
		return;
		/*Store the collected data*/

#ifdef RV1126B
	float true_width = 1080;
	float true_height = 1920;
	float width_rate = (float)evdev_attr->evdev_root_x / 720.0;
	float height_rate = (float)evdev_attr->evdev_root_y / 1280.0;
	data->point.x = true_width * width_rate;
	data->point.y = true_height * height_rate;
#else
	data->point.x = evdev_attr->evdev_root_x;
	data->point.y = evdev_attr->evdev_root_y;
#endif

	data->state = evdev_attr->evdev_button;

	if (data->point.x < 0)
		data->point.x = 0;
	if (data->point.y < 0)
		data->point.y = 0;

	if (evdev_rot == LV_DISP_ROT_90 || evdev_rot == LV_DISP_ROT_270) {
		int tmp;
		if (data->point.x >= drv->disp->driver->ver_res)
			data->point.x = drv->disp->driver->ver_res - 1;
		if (data->point.y >= drv->disp->driver->hor_res)
			data->point.y = drv->disp->driver->hor_res - 1;
		tmp = data->point.x;
		data->point.x = data->point.y;
		data->point.y = drv->disp->driver->ver_res - 1 - tmp;
	} else {
		if (data->point.x >= drv->disp->driver->hor_res)
			data->point.x = drv->disp->driver->hor_res - 1;
		if (data->point.y >= drv->disp->driver->ver_res)
			data->point.y = drv->disp->driver->ver_res - 1;
	}

	return;
}
