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
#pragma once
#include <stdio.h>
#include <time.h>

struct file_info {
	char *name;
	int is_directory;
	off_t size;
	time_t mtime;
};

struct file_list {
	char *path;
	struct file_info *files;
	int count;
	int capacity;
	int error_code;
};

#define RK_FILE_OK 0
#define RK_FILE_OPEN_FAIL (-1)
#define RK_FILE_READ_FAIL (-2)
#define RK_FILE_NO_MEM (-3)
#define RK_FILE_STAT_FAIL (-4)

void rk_free_file_list(struct file_list *list);
int rk_get_file_list(const char *path, struct file_list *list);