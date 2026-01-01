/**
 * @file evdev.h
 *
 */

#ifndef EVDEV_H
#define EVDEV_H

#ifdef __cplusplus
extern "C" {
#endif

#define USE_EVDEV 1

/*********************
 *      INCLUDES
 *********************/
#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/
typedef struct {
	int evdev_fd;
	int evdev_root_x;
	int evdev_root_y;
	int evdev_button;
	int evdev_key_val;
} EVDEV_ATTR_S;

/**********************
 * GLOBAL PROTOTYPES
 **********************/

/**
 * set coordinate rotation
 *  @param rot Coordinate rotation angle
 */
void evdev_coordinate_rotate(lv_disp_rot_t rot);

/**
 * Initialize the evdev
 */
EVDEV_ATTR_S *evdev_init(const char *evdev_name);

/**
 * Uninitialize the evdev
 */
void evdev_deinit(EVDEV_ATTR_S *evdev_attr);

int evdev_get_current_code(void);

void evdev_force_translate_to_click(bool enable);

/**
 * Get the current position and state of the evdev
 * @param data store the evdev data here
 */
void evdev_read(EVDEV_ATTR_S *evdev_attr, lv_indev_drv_t *drv, lv_indev_data_t *data);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* EVDEV_H */
