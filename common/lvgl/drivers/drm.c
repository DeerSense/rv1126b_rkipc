/**
 * @file drm.c
 *
 */
#ifndef DRAW_UI_BY_VO
/*********************
 *      INCLUDES
 *********************/
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "disp.h"
#include <lvgl.h>

#include <drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <rga/RgaApi.h>
#include <rga/rga.h>

// #define USE_DRM_MODE_ATOMIC
#define DRM_USER_DOUBLE_BUFFER

#define DRM_CARD "/dev/dri/card0"
#define DRM_CONNECTOR_ID -1 /* -1 for the first connected one */

#define DBG_TAG "drm"

#define DIV_ROUND_UP(n, d) (((n) + (d)-1) / (d))

#define print(msg, ...) fprintf(stderr, msg, ##__VA_ARGS__);
#define err(msg, ...) print("error: " msg "\n", ##__VA_ARGS__)
#define info(msg, ...) print(msg "\n", ##__VA_ARGS__)
#define dbg(msg, ...)                                                                              \
	{} // print(msg "\n", ##__VA_ARGS__);

struct drm_buffer {
	uint32_t handle;
	uint32_t pitch;
	uint32_t offset;
	int32_t buf_fd;
	unsigned long int size;
	void *map;
	uint32_t fb_handle;
};

struct drm_dev {
	int fd;
	lv_disp_rot_t rot;
	uint32_t conn_id, enc_id, crtc_id, plane_id, crtc_idx;
	uint32_t width, height;
	uint32_t mmWidth, mmHeight;
	uint32_t fourcc;
	drmModeModeInfo mode;
	uint32_t blob_id;
	drmModeCrtc *saved_crtc;
#ifdef USE_DRM_MODE_ATOMIC
	drmModeAtomicReq *req;
#endif
	drmEventContext drm_event_ctx;
	drmModePlane *plane;
	drmModeCrtc *crtc;
	drmModeConnector *conn;
	uint32_t count_plane_props;
	uint32_t count_crtc_props;
	uint32_t count_conn_props;
	drmModePropertyPtr plane_props[128];
	drmModePropertyPtr crtc_props[128];
	drmModePropertyPtr conn_props[128];
	struct drm_buffer drm_bufs[2];  /* DUMB buffers */
	struct drm_buffer *cur_bufs[2]; /* double buffering handling */
} drm_dev;

static uint32_t get_plane_property_id(const char *name) {
	uint32_t i;

	dbg("Find plane property: %s", name);

	for (i = 0; i < drm_dev.count_plane_props; ++i)
		if (!strcmp(drm_dev.plane_props[i]->name, name))
			return drm_dev.plane_props[i]->prop_id;

	dbg("Unknown plane property: %s", name);

	return 0;
}

static uint32_t get_crtc_property_id(const char *name) {
	uint32_t i;

	dbg("Find crtc property: %s", name);

	for (i = 0; i < drm_dev.count_crtc_props; ++i)
		if (!strcmp(drm_dev.crtc_props[i]->name, name))
			return drm_dev.crtc_props[i]->prop_id;

	dbg("Unknown crtc property: %s", name);

	return 0;
}

static uint32_t get_conn_property_id(const char *name) {
	uint32_t i;

	dbg("Find conn property: %s", name);

	for (i = 0; i < drm_dev.count_conn_props; ++i)
		if (!strcmp(drm_dev.conn_props[i]->name, name))
			return drm_dev.conn_props[i]->prop_id;

	dbg("Unknown conn property: %s", name);

	return 0;
}

static void page_flip_handler(int fd, unsigned int sequence, unsigned int tv_sec,
                              unsigned int tv_usec, void *user_data) {
	dbg("flip");
}

static int drm_get_plane_props(void) {
	uint32_t i;

	drmModeObjectPropertiesPtr props =
	    drmModeObjectGetProperties(drm_dev.fd, drm_dev.plane_id, DRM_MODE_OBJECT_PLANE);
	if (!props) {
		err("drmModeObjectGetProperties failed");
		return -1;
	}
	dbg("Found %u plane props", props->count_props);
	drm_dev.count_plane_props = props->count_props;
	for (i = 0; i < props->count_props; i++) {
		drm_dev.plane_props[i] = drmModeGetProperty(drm_dev.fd, props->props[i]);
		dbg("Added plane prop %u:%s", drm_dev.plane_props[i]->prop_id,
		    drm_dev.plane_props[i]->name);
	}
	drmModeFreeObjectProperties(props);

	return 0;
}

static int drm_get_crtc_props(void) {
	uint32_t i;

	drmModeObjectPropertiesPtr props =
	    drmModeObjectGetProperties(drm_dev.fd, drm_dev.crtc_id, DRM_MODE_OBJECT_CRTC);
	if (!props) {
		err("drmModeObjectGetProperties failed");
		return -1;
	}
	dbg("Found %u crtc props", props->count_props);
	drm_dev.count_crtc_props = props->count_props;
	for (i = 0; i < props->count_props; i++) {
		drm_dev.crtc_props[i] = drmModeGetProperty(drm_dev.fd, props->props[i]);
		dbg("Added crtc prop %u:%s", drm_dev.crtc_props[i]->prop_id, drm_dev.crtc_props[i]->name);
	}
	drmModeFreeObjectProperties(props);

	return 0;
}

static int drm_get_conn_props(void) {
	uint32_t i;

	drmModeObjectPropertiesPtr props =
	    drmModeObjectGetProperties(drm_dev.fd, drm_dev.conn_id, DRM_MODE_OBJECT_CONNECTOR);
	if (!props) {
		err("drmModeObjectGetProperties failed");
		return -1;
	}
	dbg("Found %u connector props", props->count_props);
	drm_dev.count_conn_props = props->count_props;
	for (i = 0; i < props->count_props; i++) {
		drm_dev.conn_props[i] = drmModeGetProperty(drm_dev.fd, props->props[i]);
		dbg("Added connector prop %u:%s", drm_dev.conn_props[i]->prop_id,
		    drm_dev.conn_props[i]->name);
	}
	drmModeFreeObjectProperties(props);

	return 0;
}

static int drm_set_plane_zpos(void) {
	int ret, i;
	const char *zpos_name = "zpos";
	uint64_t min, max, zpos;
	uint32_t prop_id = get_plane_property_id(zpos_name);

	if (!prop_id) {
		// Try uppercase ZPOS if lowercase zpos not found
		zpos_name = "ZPOS";
		prop_id = get_plane_property_id(zpos_name);
	}

	if (!prop_id) {
		err("Couldn't find plane prop ZPOS/zpos");
		return -1;
	}

	for (i = 0; i < drm_dev.count_plane_props; ++i)
		if (!strcmp(drm_dev.plane_props[i]->name, zpos_name))
			break;

	min = drm_dev.plane_props[i]->values[0];
	max = drm_dev.plane_props[i]->values[1];
	zpos = max;

	ret = drmModeObjectSetProperty(drm_dev.fd, drm_dev.plane_id, DRM_MODE_OBJECT_PLANE, prop_id,
	                               zpos);
	if (ret < 0) {
		err("drmModeObjectSetProperty (ZPOS:%" PRIu64 ") failed: %d", zpos, ret);
		return ret;
	}

	info("set plane zpos = %lld (%lld~%lld)", zpos, min, max);

	return 0;
}

#ifdef USE_DRM_MODE_ATOMIC
static int drm_add_plane_property(const char *name, uint64_t value) {
	int ret;
	uint32_t prop_id = get_plane_property_id(name);

	if (!prop_id) {
		err("Couldn't find plane prop %s", name);
		return -1;
	}

	ret =
	    drmModeAtomicAddProperty(drm_dev.req, drm_dev.plane_id, get_plane_property_id(name), value);
	if (ret < 0) {
		err("drmModeAtomicAddProperty (%s:%" PRIu64 ") failed: %d", name, value, ret);
		return ret;
	}

	return 0;
}

static int drm_add_crtc_property(const char *name, uint64_t value) {
	int ret;
	uint32_t prop_id = get_crtc_property_id(name);

	if (!prop_id) {
		err("Couldn't find crtc prop %s", name);
		return -1;
	}

	ret = drmModeAtomicAddProperty(drm_dev.req, drm_dev.crtc_id, get_crtc_property_id(name), value);
	if (ret < 0) {
		err("drmModeAtomicAddProperty (%s:%" PRIu64 ") failed: %d", name, value, ret);
		return ret;
	}

	return 0;
}

static int drm_add_conn_property(const char *name, uint64_t value) {
	int ret;
	uint32_t prop_id = get_conn_property_id(name);

	if (!prop_id) {
		err("Couldn't find conn prop %s", name);
		return -1;
	}

	ret = drmModeAtomicAddProperty(drm_dev.req, drm_dev.conn_id, get_conn_property_id(name), value);
	if (ret < 0) {
		err("drmModeAtomicAddProperty (%s:%" PRIu64 ") failed: %d", name, value, ret);
		return ret;
	}

	return 0;
}
#endif

static int drm_dmabuf_set_plane(struct drm_buffer *buf) {
#ifdef USE_DRM_MODE_ATOMIC
	int ret;
	static int first = 1;
	uint32_t flags = DRM_MODE_PAGE_FLIP_EVENT;

	drm_dev.req = drmModeAtomicAlloc();

	/* On first Atomic commit, do a modeset */
	if (first) {
		drm_add_conn_property("CRTC_ID", drm_dev.crtc_id);

		drm_add_crtc_property("MODE_ID", drm_dev.blob_id);
		drm_add_crtc_property("ACTIVE", 1);

		flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;

		first = 0;
	}

	drm_add_plane_property("FB_ID", buf->fb_handle);
	drm_add_plane_property("CRTC_ID", drm_dev.crtc_id);
	drm_add_plane_property("SRC_X", 0);
	drm_add_plane_property("SRC_Y", 0);
	drm_add_plane_property("SRC_W", drm_dev.width << 16);
	drm_add_plane_property("SRC_H", drm_dev.height << 16);
	drm_add_plane_property("CRTC_X", 0);
	drm_add_plane_property("CRTC_Y", 0);
	drm_add_plane_property("CRTC_W", drm_dev.width);
	drm_add_plane_property("CRTC_H", drm_dev.height);

	ret = drmModeAtomicCommit(drm_dev.fd, drm_dev.req, flags, NULL);
	if (ret) {
		err("drmModeAtomicCommit failed: %s", strerror(errno));
		drmModeAtomicFree(drm_dev.req);
		return ret;
	}
#else
	drmModeSetPlane(drm_dev.fd, drm_dev.plane_id, drm_dev.crtc_id, buf->fb_handle, 0, 0, 0,
	                drm_dev.width, drm_dev.height, 0, 0, drm_dev.width << 16, drm_dev.height << 16);
#endif

	return 0;
}

static int find_plane(unsigned int fourcc, uint32_t *plane_id, uint32_t crtc_id,
                      uint32_t crtc_idx) {
	drmModePlaneResPtr planes;
	drmModePlanePtr plane;
	unsigned int i;
	unsigned int j;
	int ret = 0;
	unsigned int format = fourcc;

	planes = drmModeGetPlaneResources(drm_dev.fd);
	if (!planes) {
		err("drmModeGetPlaneResources failed");
		return -1;
	}

	dbg("drm: found planes %u", planes->count_planes);

	for (i = 0; i < planes->count_planes; ++i) {
		plane = drmModeGetPlane(drm_dev.fd, planes->planes[i]);
		if (!plane) {
			err("drmModeGetPlane failed: %s", strerror(errno));
			break;
		}

		if (plane->crtc_id != 0) {
			drmModeFreePlane(plane);
			continue;
		}

		if (!(plane->possible_crtcs & (1 << crtc_idx))) {
			drmModeFreePlane(plane);
			continue;
		}

		for (j = 0; j < plane->count_formats; ++j) {
			if (plane->formats[j] == format)
				break;
		}

		if (j == plane->count_formats) {
			drmModeFreePlane(plane);
			continue;
		}

		if (plane->plane_id == 84) {
			drmModeFreePlane(plane);
			continue;
		}

		*plane_id = plane->plane_id;
		drmModeFreePlane(plane);

		dbg("found plane %d", *plane_id);

		break;
	}

	if (i == planes->count_planes)
		ret = -1;

	drmModeFreePlaneResources(planes);

	return ret;
}

static int drm_find_connector(void) {
	drmModeConnector *conn = NULL;
	drmModeEncoder *enc = NULL;
	drmModeRes *res;
	int i;

	if ((res = drmModeGetResources(drm_dev.fd)) == NULL) {
		err("drmModeGetResources() failed");
		return -1;
	}

	if (res->count_crtcs <= 0) {
		err("no Crtcs");
		goto free_res;
	}

	/* find all available connectors */
	for (i = 0; i < res->count_connectors; i++) {
		conn = drmModeGetConnector(drm_dev.fd, res->connectors[i]);
		if (!conn)
			continue;

#if DRM_CONNECTOR_ID >= 0
		if (conn->connector_id != DRM_CONNECTOR_ID) {
			drmModeFreeConnector(conn);
			continue;
		}
#endif

		if (conn->connection == DRM_MODE_CONNECTED) {
			dbg("drm: connector %d: connected", conn->connector_id);
		} else if (conn->connection == DRM_MODE_DISCONNECTED) {
			dbg("drm: connector %d: disconnected", conn->connector_id);
		} else if (conn->connection == DRM_MODE_UNKNOWNCONNECTION) {
			dbg("drm: connector %d: unknownconnection", conn->connector_id);
		} else {
			dbg("drm: connector %d: unknown", conn->connector_id);
		}

		if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0)
			break;

		drmModeFreeConnector(conn);
		conn = NULL;
	};

	if (!conn) {
		err("suitable connector not found");
		goto free_res;
	}

	drm_dev.conn_id = conn->connector_id;
	dbg("conn_id: %d", drm_dev.conn_id);
	drm_dev.mmWidth = conn->mmWidth;
	drm_dev.mmHeight = conn->mmHeight;

	memcpy(&drm_dev.mode, &conn->modes[0], sizeof(drmModeModeInfo));

	if (drmModeCreatePropertyBlob(drm_dev.fd, &drm_dev.mode, sizeof(drm_dev.mode),
	                              &drm_dev.blob_id)) {
		err("error creating mode blob");
		goto free_res;
	}

	drm_dev.width = conn->modes[0].hdisplay;
	drm_dev.height = conn->modes[0].vdisplay;

	for (i = 0; i < res->count_encoders; i++) {
		enc = drmModeGetEncoder(drm_dev.fd, res->encoders[i]);
		if (!enc)
			continue;

		dbg("enc%d enc_id %d conn enc_id %d", i, enc->encoder_id, conn->encoder_id);

		if (enc->encoder_id == conn->encoder_id)
			break;

		drmModeFreeEncoder(enc);
		enc = NULL;
	}

	if (enc) {
		drm_dev.enc_id = enc->encoder_id;
		dbg("enc_id: %d", drm_dev.enc_id);
		drm_dev.crtc_id = enc->crtc_id;
		dbg("crtc_id: %d", drm_dev.crtc_id);
		drmModeFreeEncoder(enc);
	} else {
		/* Encoder hasn't been associated yet, look it up */
		for (i = 0; i < conn->count_encoders; i++) {
			int crtc, crtc_id = -1;

			enc = drmModeGetEncoder(drm_dev.fd, conn->encoders[i]);
			if (!enc)
				continue;

			for (crtc = 0; crtc < res->count_crtcs; crtc++) {
				uint32_t crtc_mask = 1 << crtc;

				crtc_id = res->crtcs[crtc];

				dbg("enc_id %d crtc%d id %d mask %x possible %x", enc->encoder_id, crtc, crtc_id,
				    crtc_mask, enc->possible_crtcs);

				if (enc->possible_crtcs & crtc_mask)
					break;
			}

			if (crtc_id > 0) {
				drm_dev.enc_id = enc->encoder_id;
				dbg("enc_id: %d", drm_dev.enc_id);
				drm_dev.crtc_id = crtc_id;
				dbg("crtc_id: %d", drm_dev.crtc_id);
				break;
			}

			drmModeFreeEncoder(enc);
			enc = NULL;
		}

		if (!enc) {
			err("suitable encoder not found");
			goto free_res;
		}

		drmModeFreeEncoder(enc);
	}

	drm_dev.crtc_idx = -1;

	for (i = 0; i < res->count_crtcs; ++i) {
		if (drm_dev.crtc_id == res->crtcs[i]) {
			drm_dev.crtc_idx = i;
			break;
		}
	}

	if (drm_dev.crtc_idx == -1) {
		err("drm: CRTC not found");
		goto free_res;
	}

	dbg("crtc_idx: %d", drm_dev.crtc_idx);

	return 0;

free_res:
	drmModeFreeResources(res);

	return -1;
}

static int drm_open(const char *path) {
	int fd, flags;
	uint64_t has_dumb;
	int ret;

	fd = open(path, O_RDWR);
	if (fd < 0) {
		err("cannot open \"%s\"", path);
		return -1;
	}

	/* set FD_CLOEXEC flag */
	if ((flags = fcntl(fd, F_GETFD)) < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
		err("fcntl FD_CLOEXEC failed");
		goto err;
	}

	/* check capability */
	ret = drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &has_dumb);
	if (ret < 0 || has_dumb == 0) {
		err("drmGetCap DRM_CAP_DUMB_BUFFER failed or \"%s\" doesn't have dumb "
		    "buffer",
		    path);
		goto err;
	}

	return fd;
err:
	close(fd);
	return -1;
}

static int drm_setup(unsigned int fourcc) {
	int ret;
	const char *device_path = NULL;

	device_path = getenv("DRM_CARD");
	if (!device_path)
		device_path = DRM_CARD;

	drm_dev.fd = drm_open(device_path);
	if (drm_dev.fd < 0) {
		err("drm open failed");
		return -1;
	}

	ret = drmSetClientCap(drm_dev.fd, DRM_CLIENT_CAP_ATOMIC, 1);
	if (ret) {
		err("No atomic modesetting support: %s", strerror(errno));
		goto err;
	}

	ret = drm_find_connector();
	if (ret) {
		err("available drm devices not found");
		goto err;
	}

	ret = find_plane(fourcc, &drm_dev.plane_id, drm_dev.crtc_id, drm_dev.crtc_idx);
	if (ret) {
		err("Cannot find plane");
		goto err;
	}

	drm_dev.plane = drmModeGetPlane(drm_dev.fd, drm_dev.plane_id);
	if (!drm_dev.plane) {
		err("Cannot get plane");
		goto err;
	}

	drm_dev.crtc = drmModeGetCrtc(drm_dev.fd, drm_dev.crtc_id);
	if (!drm_dev.crtc) {
		err("Cannot get crtc");
		goto err;
	}

	drm_dev.conn = drmModeGetConnector(drm_dev.fd, drm_dev.conn_id);
	if (!drm_dev.conn) {
		err("Cannot get connector");
		goto err;
	}

	ret = drm_get_plane_props();
	if (ret) {
		err("Cannot get plane props");
		goto err;
	}

	ret = drm_get_crtc_props();
	if (ret) {
		err("Cannot get crtc props");
		goto err;
	}

	ret = drm_get_conn_props();
	if (ret) {
		err("Cannot get connector props");
		goto err;
	}

	ret = drm_set_plane_zpos();
	if (ret) {
		err("Cannot set plane zpos props");
	}

	drm_dev.drm_event_ctx.version = DRM_EVENT_CONTEXT_VERSION;
	drm_dev.drm_event_ctx.page_flip_handler = page_flip_handler;
	drm_dev.fourcc = fourcc;

	info("drm: Found plane_id: %u connector_id: %d crtc_id: %d", drm_dev.plane_id, drm_dev.conn_id,
	     drm_dev.crtc_id);

	info("drm: %dx%d (%dmm X% dmm) pixel format %c%c%c%c", drm_dev.width, drm_dev.height,
	     drm_dev.mmWidth, drm_dev.mmHeight, (fourcc >> 0) & 0xff, (fourcc >> 8) & 0xff,
	     (fourcc >> 16) & 0xff, (fourcc >> 24) & 0xff);

	return 0;

err:
	close(drm_dev.fd);
	return -1;
}

static int drm_allocate_dumb(struct drm_buffer *buf) {
	struct drm_mode_create_dumb creq;
	struct drm_mode_map_dumb mreq;
	struct drm_prime_handle fd_args;
	uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};
	int ret;

	/* create dumb buffer */
	memset(&creq, 0, sizeof(creq));
	creq.width = drm_dev.width;
	creq.height = drm_dev.height;
	creq.bpp = LV_COLOR_DEPTH;
	ret = drmIoctl(drm_dev.fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq);
	if (ret < 0) {
		err("DRM_IOCTL_MODE_CREATE_DUMB fail");
		return -1;
	}

	buf->handle = creq.handle;
	buf->pitch = creq.pitch;
	dbg("pitch %d", buf->pitch);
	buf->size = creq.size;
	dbg("size %d", buf->size);

	/* prepare buffer for memory mapping */
	memset(&mreq, 0, sizeof(mreq));
	mreq.handle = creq.handle;
	ret = drmIoctl(drm_dev.fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);
	if (ret) {
		err("DRM_IOCTL_MODE_MAP_DUMB fail");
		return -1;
	}

	memset(&fd_args, 0, sizeof(fd_args));
	fd_args.fd = -1;
	fd_args.handle = creq.handle;
	fd_args.flags = 0;
	ret = drmIoctl(drm_dev.fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &fd_args);
	if (ret) {
		err("DRM_IOCTL_PRIME_HANDLE_TO_FD fail");
		return -1;
	}
	buf->buf_fd = fd_args.fd;

	buf->offset = mreq.offset;

	/* perform actual memory mapping */
	buf->map = mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_dev.fd, mreq.offset);
	if (buf->map == MAP_FAILED) {
		err("mmap fail");
		return -1;
	}

	/* clear the framebuffer to 0 (= full transparency in ARGB8888) */
	memset(buf->map, 0, creq.size);

	/* create framebuffer object for the dumb-buffer */
	handles[0] = creq.handle;
	pitches[0] = creq.pitch;
	offsets[0] = 0;
	ret = drmModeAddFB2(drm_dev.fd, drm_dev.width, drm_dev.height, drm_dev.fourcc, handles, pitches,
	                    offsets, &buf->fb_handle, 0);
	if (ret) {
		err("drmModeAddFB fail");
		return -1;
	}

	return 0;
}

static int drm_setup_buffers(void) {
	int ret;

	/* Allocate DUMB buffers */
	ret = drm_allocate_dumb(&drm_dev.drm_bufs[0]);
	if (ret)
		return ret;

#ifdef DRM_USER_DOUBLE_BUFFER
	ret = drm_allocate_dumb(&drm_dev.drm_bufs[1]);
	if (ret)
		return ret;
#endif
	/* Set buffering handling */
	drm_dev.cur_bufs[0] = NULL;
	drm_dev.cur_bufs[1] = &drm_dev.drm_bufs[0];

	return 0;
}

#ifdef USE_DRM_MODE_ATOMIC
void drm_wait_vsync(lv_disp_drv_t *disp_drv) {
	int ret;
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(drm_dev.fd, &fds);

	do {
		ret = select(drm_dev.fd + 1, &fds, NULL, NULL, NULL);
	} while (ret == -1 && errno == EINTR);

	if (ret < 0) {
		err("select failed: %s", strerror(errno));
		drmModeAtomicFree(drm_dev.req);
		drm_dev.req = NULL;
		return;
	}

	if (FD_ISSET(drm_dev.fd, &fds))
		drmHandleEvent(drm_dev.fd, &drm_dev.drm_event_ctx);

	drmModeAtomicFree(drm_dev.req);
	drm_dev.req = NULL;
}
#endif

static void draw_buf_rotate_90(lv_color_t *color_p, const lv_area_t *area, lv_color_t *dst_buf,
                               lv_coord_t canvas_w, lv_coord_t canvas_h) {
	lv_coord_t area_w = (area->x2 - area->x1 + 1);
	lv_coord_t area_h = (area->y2 - area->y1 + 1);
	uint32_t initial_i = area->x1 * canvas_w + (canvas_w - area->y1 - 1);
	for (lv_coord_t y = 0; y < area_h; y++) {
		uint32_t i = initial_i - y;
		for (lv_coord_t x = 0; x < area_w; x++) {
			dst_buf[i] = *(color_p++);
			i += canvas_w;
		}
	}
}

#ifdef DRM_USER_DOUBLE_BUFFER
void drm_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
	int32_t format = 0;
	uint32_t wstride;
	bool partial_update = false;
	struct drm_buffer *fbuf = drm_dev.cur_bufs[1];
	lv_coord_t w = (area->x2 - area->x1 + 1);
	lv_coord_t h = (area->y2 - area->y1 + 1);

	dbg("x %d:%d y %d:%d w %d h %d", area->x1, area->x2, area->y1, area->y2, w, h);

	wstride = fbuf->pitch / LV_COLOR_SIZE * 8;

	if (LV_COLOR_DEPTH == 16) {
		format = RK_FORMAT_RGB_565;
	} else if (LV_COLOR_DEPTH == 32) {
		format = RK_FORMAT_ARGB_8888;
	} else {
		format = -1;
		printf("drm_flush rga not supported format\n");
		return;
	}

	rga_info_t src;
	rga_info_t dst;

	/* Partial update */
	if (drm_dev.rot == LV_DISP_ROT_90 || drm_dev.rot == LV_DISP_ROT_270) {
		if ((w != drm_dev.height || h != drm_dev.width) && drm_dev.cur_bufs[0])
			partial_update = true;
	} else {
		if ((w != drm_dev.width || h != drm_dev.height) && drm_dev.cur_bufs[0])
			partial_update = true;
	}

	if (true == partial_update) {
		memset(&src, 0, sizeof(rga_info_t));
		memset(&dst, 0, sizeof(rga_info_t));

		src.virAddr = drm_dev.cur_bufs[0]->map;
		src.mmuFlag = 1;
		dst.fd = fbuf->buf_fd;
		dst.mmuFlag = 1;
		rga_set_rect(&src.rect, 0, 0, drm_dev.width, drm_dev.height, wstride, drm_dev.height,
		             format);
		rga_set_rect(&dst.rect, 0, 0, drm_dev.width, drm_dev.height, wstride, drm_dev.height,
		             format);
		if (c_RkRgaBlit(&src, &dst, NULL))
			printf("c_RkRgaBlit2 error : %s\n", strerror(errno));
	}

	memset(&src, 0, sizeof(rga_info_t));
	memset(&dst, 0, sizeof(rga_info_t));

	if (drm_dev.rot == LV_DISP_ROT_90 || drm_dev.rot == LV_DISP_ROT_270) {
		if (w < 2 || h < 2) {
			draw_buf_rotate_90(color_p, area, fbuf->map, wstride, drm_dev.height);
		} else {
			src.virAddr = color_p;
			src.fd = -1;
			src.mmuFlag = 1;
			src.rotation = HAL_TRANSFORM_ROT_90;
			dst.fd = fbuf->buf_fd;
			dst.mmuFlag = 1;
			rga_set_rect(&src.rect, 0, 0, w, h, w, h, format);
			rga_set_rect(&dst.rect, (drm_dev.width - (area->y1 + h)), area->x1, h, w, wstride,
			             drm_dev.height, format);
			if (c_RkRgaBlit(&src, &dst, NULL))
				printf("c_RkRgaBlit2 error : %s\n", strerror(errno));
		}
	} else {
		if (w < 2 || h < 2) {
			for (uint32_t y = 0, i = area->y1; i <= area->y2; ++i, ++y) {
				memcpy((uint8_t *)fbuf->map + (area->x1 * (LV_COLOR_SIZE / 8)) + (fbuf->pitch * i),
				       (uint8_t *)color_p + (w * (LV_COLOR_SIZE / 8) * y), w * (LV_COLOR_SIZE / 8));
			}
		} else {
			src.virAddr = color_p;
			src.mmuFlag = 1;
			dst.fd = fbuf->buf_fd;
			dst.mmuFlag = 1;
			rga_set_rect(&src.rect, 0, 0, w, h, w, h, format);
			rga_set_rect(&dst.rect, area->x1, area->y1, w, h, wstride, drm_dev.height, format);
			if (c_RkRgaBlit(&src, &dst, NULL))
				printf("c_RkRgaBlit2 error : %s\n", strerror(errno));
		}
	}

#ifdef USE_DRM_MODE_ATOMIC
	if (drm_dev.req)
		drm_wait_vsync(disp_drv);
#endif

	/* show fbuf plane */
	if (drm_dmabuf_set_plane(fbuf)) {
		err("Flush fail");
		return;
	} else
		dbg("Flush done");

	if (!drm_dev.cur_bufs[0])
		drm_dev.cur_bufs[1] = &drm_dev.drm_bufs[1];
	else
		drm_dev.cur_bufs[1] = drm_dev.cur_bufs[0];

	drm_dev.cur_bufs[0] = fbuf;

	lv_disp_flush_ready(disp_drv);
}
#else

void drm_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
	uint32_t wstride;
	int32_t format = 0;
	struct drm_buffer *fbuf = drm_dev.cur_bufs[1];
	lv_coord_t w = (area->x2 - area->x1 + 1);
	lv_coord_t h = (area->y2 - area->y1 + 1);

	dbg("x %d:%d y %d:%d w %d h %d", area->x1, area->x2, area->y1, area->y2, w, h);

	wstride = fbuf->pitch / LV_COLOR_SIZE * 8;

	if (LV_COLOR_DEPTH == 16) {
		format = RK_FORMAT_RGB_565;
	} else if (LV_COLOR_DEPTH == 32) {
		format = RK_FORMAT_ARGB_8888;
	} else {
		format = -1;
		printf("drm_flush rga not supported format\n");
		return;
	}

	rga_info_t src;
	rga_info_t dst;

	memset(&src, 0, sizeof(rga_info_t));
	memset(&dst, 0, sizeof(rga_info_t));

	/* Partial update */
	if (drm_dev.rot == LV_DISP_ROT_90 || drm_dev.rot == LV_DISP_ROT_270) {
		if (w < 2 || h < 2) {
			draw_buf_rotate_90(color_p, area, fbuf->map, wstride, drm_dev.height);
		} else {
			src.virAddr = color_p;
			src.mmuFlag = 1;
			src.rotation = HAL_TRANSFORM_ROT_90;
			dst.fd = fbuf->buf_fd;
			dst.mmuFlag = 1;
			rga_set_rect(&src.rect, 0, 0, w, h, w, h, format);
			rga_set_rect(&dst.rect, (drm_dev.width - (area->y1 + h)), area->x1, h, w, wstride,
			             drm_dev.height, format);
			if (c_RkRgaBlit(&src, &dst, NULL))
				printf("c_RkRgaBlit2 error : %s\n", strerror(errno));
		}
	} else {
		if (w < 2 || h < 2) {
			for (uint32_t y = 0, i = area->y1; i <= area->y2; ++i, ++y) {
				memcpy((uint8_t *)fbuf->map + (area->x1 * (LV_COLOR_SIZE / 8)) + (fbuf->pitch * i),
				       (uint8_t *)color_p + (w * (LV_COLOR_SIZE / 8) * y), w * (LV_COLOR_SIZE / 8));
			}
		} else {
			src.virAddr = color_p;
			src.mmuFlag = 1;
			dst.fd = fbuf->buf_fd;
			dst.mmuFlag = 1;
			rga_set_rect(&src.rect, 0, 0, w, h, w, h, format);
			rga_set_rect(&dst.rect, area->x1, area->y1, w, h, wstride, drm_dev.height, format);
			if (c_RkRgaBlit(&src, &dst, NULL))
				printf("c_RkRgaBlit2 error : %s\n", strerror(errno));
		}
	}

#ifdef USE_DRM_MODE_ATOMIC
	if (drm_dev.req)
		drm_wait_vsync(disp_drv);
#endif

	/* show fbuf plane */
	if (drm_dmabuf_set_plane(fbuf)) {
		err("Flush fail");
		return;
	} else
		dbg("Flush done");

	lv_disp_flush_ready(disp_drv);
}
#endif

#if LV_COLOR_DEPTH == 32
#define DRM_FOURCC DRM_FORMAT_ARGB8888
#elif LV_COLOR_DEPTH == 16
#define DRM_FOURCC DRM_FORMAT_RGB565
#else
#define LV_COLOR_DEPTH not supported
#endif

void drm_get_sizes(lv_coord_t *width, lv_coord_t *height, uint32_t *dpi) {
	if (width)
		*width = drm_dev.width;

	if (height)
		*height = drm_dev.height;

	if (dpi && drm_dev.mmWidth)
		*dpi = DIV_ROUND_UP(drm_dev.width * 25400, drm_dev.mmWidth * 1000);
}

int32_t drm_init(lv_disp_rot_t rotate_disp) {
	int ret;

	ret = drm_setup(DRM_FOURCC);
	if (ret) {
		close(drm_dev.fd);
		drm_dev.fd = -1;
		return -1;
	}

	ret = drm_setup_buffers();
	if (ret) {
		err("DRM buffer allocation failed");
		close(drm_dev.fd);
		drm_dev.fd = -1;
		return -1;
	}

	drm_dev.rot = rotate_disp;

	info("DRM subsystem and buffer mapped successfully");

	return 0;
}

void drm_exit(void) {
	close(drm_dev.fd);
	drm_dev.fd = -1;
}

disp_ops_t drm_ops = {
    drm_init,
    drm_exit,
    drm_get_sizes,
    drm_flush,
};
#endif
