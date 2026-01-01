/*
 * Copyright (c) 2025 Rockchip, Inc. All Rights Reserved.
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
#define LOG_TAG "filelist.c"

#include "filelist.h"
#include "common.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static void init_file_list(struct file_list *list) {
	list->path = NULL;
	list->files = NULL;
	list->count = 0;
	list->capacity = 0;
	list->error_code = RK_FILE_OK;
}

void rk_free_file_list(struct file_list *list) {
	if (list) {
		free(list->path);
		for (int i = 0; i < list->count; ++i) {
			free(list->files[i].name);
		}
		free(list->files);
		init_file_list(list);
	}
}

static int expand_capacity(struct file_list *list) {
	const int new_cap = (list->capacity == 0) ? 16 : list->capacity * 2;
	struct file_info *new_files = realloc(list->files, new_cap * sizeof(struct file_info));
	if (!new_files) {
		list->error_code = RK_FILE_NO_MEM;
		return -1;
	}
	list->files = new_files;
	list->capacity = new_cap;
	return 0;
}

int rk_get_file_list(const char *path, struct file_list *list) {
	if (!path || !list)
		return -1;
	DIR *dir = opendir(path);
	if (!dir) {
		list->error_code = RK_FILE_OPEN_FAIL;
		return -1;
	}

	init_file_list(list);
	list->path = strdup(path);
	if (!list->path) {
		list->error_code = RK_FILE_NO_MEM;
		closedir(dir);
		return -1;
	}

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}

		if (list->count >= list->capacity) {
			if (expand_capacity(list) != 0)
				break;
		}

		char full_path[PATH_MAX];
		snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

		struct stat st;
		if (lstat(full_path, &st) != 0) {
			list->error_code = RK_FILE_STAT_FAIL;
			continue;
		}

		struct file_info *info = &list->files[list->count];
		info->name = strdup(entry->d_name);
		info->is_directory = S_ISDIR(st.st_mode);
		info->size = st.st_size;
		info->mtime = st.st_mtime;

		if (!info->name) {
			list->error_code = RK_FILE_NO_MEM;
			break;
		}

		list->count++;
	}

	closedir(dir);
	return (list->error_code == RK_FILE_OK) ? 0 : -1;
}
