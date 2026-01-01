#include <getopt.h>

#include "audio.h"
#include "common.h"
#include "isp.h"
#include "log.h"
#include "network.h"
#include "osd.h"
#include "param.h"
#include "rockiva.h"
#include "server.h"
#include "storage.h"
#include "system.h"
#include "video.h"
#include <linux/input.h>

#include "uvc_control.h"
#include "uvc_video.h"
#include "uvc-gadget.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "rkipc.c"

enum { LOG_ERROR, LOG_WARN, LOG_INFO, LOG_DEBUG };

int enable_minilog = 0;
int rkipc_log_level = LOG_INFO;
int rkipc_camera_id_ = 0;

static int g_main_run_ = 1;
char *rkipc_ini_path_ = NULL;
char *rkipc_iq_file_path_ = NULL;

static void sig_proc(int signo) {
	LOG_INFO("received signo %d \n", signo);
	g_main_run_ = 0;
}

static const char short_options[] = "c:a:l:";
static const struct option long_options[] = {{"config", required_argument, NULL, 'c'},
                                             {"aiq_file", no_argument, NULL, 'a'},
                                             {"log_level", no_argument, NULL, 'l'},
                                             {"help", no_argument, NULL, 'h'},
                                             {0, 0, 0, 0}};

static void usage_tip(FILE *fp, int argc, char **argv) {
	fprintf(fp,
	        "Usage: %s [options]\n"
	        "Version %s\n"
	        "Options:\n"
	        "-c | --config      rkipc ini file, default is "
	        "/userdata/rkipc.ini, need to be writable\n"
	        "-a | --aiq_file    aiq file dir path, default is /etc/iqfiles\n"
	        "-l | --log_level   log_level [0/1/2/3], default is 2\n"
	        "-h | --help        for help \n\n"
	        "\n",
	        argv[0], "V1.0");
}

void rkipc_get_opt(int argc, char *argv[]) {
	for (;;) {
		int idx;
		int c;
		c = getopt_long(argc, argv, short_options, long_options, &idx);
		if (-1 == c)
			break;
		switch (c) {
		case 0: /* getopt_long() flag */
			break;
		case 'c':
			rkipc_ini_path_ = optarg;
			break;
		case 'a':
			rkipc_iq_file_path_ = optarg;
			break;
		case 'l':
			rkipc_log_level = atoi(optarg);
			break;
		case 'h':
			usage_tip(stdout, argc, argv);
			exit(EXIT_SUCCESS);
		default:
			usage_tip(stderr, argc, argv);
			exit(EXIT_FAILURE);
		}
	}
}

int open_uvc(int width, int height, int fcc, int fps) {
	LOG_INFO("open uvc %dx%d %d %d\n", width, height, fcc, fps);
	return 0;
}

void close_uvc(void) { LOG_INFO("close_uvc"); }

#define TARGET_DEVICE "adc-keys"

static int find_adc_keys_device() {
    for (int x = 0; x <= 10; x++) {
        char path[64];
        snprintf(path, sizeof(path), "/sys/class/input/event%d/device/name", x);

        FILE *file = fopen(path, "r");
        if (!file) continue;

        char name[64];
        if (fgets(name, sizeof(name), file)) {
            name[strcspn(name, "\n")] = '\0';

            if (strcmp(name, TARGET_DEVICE) == 0) {
                fclose(file);
                return x;
            }
        }
        fclose(file);
    }
    return -1;
}

#define AO_FREAD_SIZE 1024
static void *wait_key_event(void *arg) {
	int ret;
	int key_fd;
	char path[32];
	int event_num = find_adc_keys_device();
	if (event_num != -1) {
		printf("Found %s at event%d\n", TARGET_DEVICE, event_num);
	} else {
		printf("Device %s not found\n", TARGET_DEVICE);
		return NULL;
	}
	snprintf(path, sizeof(path), "/dev/input/event%d", event_num);
	key_fd = open(path, O_RDONLY);
	if (key_fd < 0) {
		LOG_ERROR("can't open %s\n", path);
		return NULL;
	}
	fd_set rfds;
	int nfds = key_fd + 1;
	struct timeval timeout;
	struct input_event key_event;

	while (g_main_run_) {
		// The rfds collection must be emptied every time,
		// otherwise the descriptor changes cannot be detected
		memset(&timeout, 0, sizeof(timeout));
		timeout.tv_sec = 1;
		FD_ZERO(&rfds);
		FD_SET(key_fd, &rfds);
		ret = select(nfds, &rfds, NULL, NULL, &timeout);
		if (ret < 0) {
			LOG_ERROR("select failed : %s\n", strerror(errno));
			continue;
		}
		if (ret == 0)
			continue;
		// wait for the key event to occur
		if (FD_ISSET(key_fd, &rfds)) {
			read(key_fd, &key_event, sizeof(key_event));
			LOG_INFO("[timeval:sec:%ld,usec:%ld,type:%d,code:%d,value:%d]\n",
			         key_event.input_event_sec, key_event.input_event_usec, key_event.type,
			         key_event.code, key_event.value);
			if ((key_event.code == KEY_LEFT) && key_event.value) {
				LOG_INFO("get KEY_LEFT\n");
				rkipc_ao_init();
				FILE *fp = fopen("/oem/usr/share/speaker_test.wav", "rb");
				int size = AO_FREAD_SIZE;
				char *tmp_data;
				tmp_data = malloc(AO_FREAD_SIZE);
				while (size > 0) {
					memset((void *)tmp_data, 0, AO_FREAD_SIZE);
					size = fread(tmp_data, 1, AO_FREAD_SIZE, fp);
					rkipc_ao_write(tmp_data, size);
				}
				rkipc_ao_write(tmp_data, 0);
				free(tmp_data);
				fclose(fp);
				rkipc_ao_deinit();
			}

			if ((key_event.code == KEY_VOLUMEUP) && key_event.value) {
				LOG_INFO("get KEY_VOLUMEUP\n");
			}
		}
	}

	if (key_fd) {
		close(key_fd);
		key_fd = 0;
	}
	LOG_DEBUG("wait key event out\n");
	return NULL;
}

int main(int argc, char **argv) {
	pthread_t key_chk;
	LOG_INFO("main begin\n");
	rkipc_version_dump();
	signal(SIGINT, sig_proc);
	signal(SIGTERM, sig_proc);

	rkipc_get_opt(argc, argv);
	LOG_INFO("rkipc_ini_path_ is %s, rkipc_iq_file_path_ is %s, rkipc_log_level "
	         "is %d\n",
	         rkipc_ini_path_, rkipc_iq_file_path_, rkipc_log_level);

	// init
	rk_param_init(rkipc_ini_path_);
	rkipc_camera_id_ = rk_param_get_int("video.source:camera_id", 0); // need rk_param_init
	rk_isp_init(rkipc_camera_id_, rkipc_iq_file_path_);
	RK_MPI_SYS_Init();
	if (rk_param_get_int("audio.0:enable", 0))
		rkipc_audio_init();
	rk_video_init();
	rkipc_server_init();
	rk_storage_init();
	pthread_create(&key_chk, NULL, wait_key_event, NULL);

	rk_network_init(NULL);
	rk_system_init();
	if (rk_param_get_int("video.source:enable_npu", 0))
		rkipc_rockiva_init();

	char usb_config_cmd[128];
	const char *output_data_type = rk_param_get_string("video.0:output_data_type", "H.264");
	int width = rk_param_get_int("video.0:width", 1920);
	int height = rk_param_get_int("video.0:height", 1080);
	uint32_t flags = 0; // UVC_CONTROL_LOOP_ONCE;
	int enable_uvc = rk_param_get_int("video.source:enable_uvc", 0);
	int enable_uac = rk_param_get_int("audio.0:enable_uac", 0);
	if (enable_uvc || enable_uac) {
		memset(usb_config_cmd, 0, sizeof(usb_config_cmd));
		system("/etc/init.d/S50usbdevice stop");
		sleep(2); // wait old usb change
		if (enable_uvc && enable_uac) {
			snprintf(usb_config_cmd, sizeof(usb_config_cmd),
			         "rkipc_usb_config.sh -f %s -w %d -h %d -a", output_data_type, width, height);
		} else if (enable_uvc) {
			snprintf(usb_config_cmd, sizeof(usb_config_cmd),
			         "rkipc_usb_config.sh -f %s -w %d -h %d", output_data_type, width, height);
		} else if (enable_uac) {
			snprintf(usb_config_cmd, sizeof(usb_config_cmd), "rkipc_usb_config.sh -a");
		}
		LOG_INFO("usb_config_cmd is %s\n", usb_config_cmd);
		system(usb_config_cmd);
		sleep(1);
		if (enable_uvc) {
			uvc_formats_init(output_data_type, width, height);
			register_uvc_open_camera(open_uvc);
			register_uvc_close_camera(close_uvc);
		}
		uvc_control_run(flags); // also monitor uac
	}
	LOG_INFO("rkipc init over\n");

	while (g_main_run_) {
		usleep(1000 * 1000);
	}

	// deinit
	if (rk_param_get_int("video.source:enable_uvc", 0)) {
		uvc_control_join(flags);
		uvc_formats_deinit();
	}
	pthread_join(key_chk, NULL);
	rk_storage_deinit();
	rkipc_server_deinit();
	rk_system_deinit();
	if (rk_param_get_int("audio.0:enable", 0))
		rkipc_audio_deinit();
	rk_video_deinit();
	RK_MPI_SYS_Exit();
	rk_isp_deinit(rkipc_camera_id_);
	if (rk_param_get_int("video.source:enable_npu", 0))
		rkipc_rockiva_deinit();
	rk_network_deinit();
	rk_param_deinit();
	LOG_INFO("rkipc deinit over\n");

	return 0;
}
