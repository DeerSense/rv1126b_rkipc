/*
 * Copyright (c) 2023 Rockchip, Inc. All Rights Reserved.
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

#include "ui_gettext.h"
#include "common.h"

#include <ctype.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <unistd.h>

/**********************
 *  STATIC PROTOTYPES
 **********************/
typedef struct {
	size_t size;
	char **vector;
} LANG_RES_S;

typedef enum {
	LANG_UNPROCESSED,
	LANG_ERROR,
	LANG_EMPTY,
	LANG_COMMENT,
	LANG_SECTION,
	LANG_VALUE
} LANG_PARSE_STATE_E;

typedef struct {
	LANG_TYPE_E type;
	const char *file_path;
	LANG_RES_S *res;
} LANG_RES_LIST_S;

/**********************s
 *  STATIC VARIABLES
 **********************/
static char *xstrdup(const char *s);
static uint32_t strstrip(char *s);

static LANG_RES_S *lang_res_new(size_t size);
static void lang_res_delete(LANG_RES_S *res);
static int32_t lang_res_grow(LANG_RES_S *res);
static LANG_PARSE_STATE_E lang_parse_line(const char *input_line, char *section, char *key,
                                          char *value);
static int32_t lang_res_set(LANG_RES_S *res, const char *key, const char *val);
static LANG_RES_S *lang_resource_file_load(const char *res_name);

static LANG_TYPE_E g_select_language = LANG_TYPE_CHINESE;
static pthread_rwlock_t g_res_lock = PTHREAD_RWLOCK_INITIALIZER;
static LANG_RES_LIST_S g_lang_array[LANG_TYPE_MAX] = {
    {.type = LANG_TYPE_CHINESE, .file_path = "/usr/share/res/string/Chinese.txt", .res = NULL},
    {.type = LANG_TYPE_ENGLISH, .file_path = "/usr/share/res/string/English.txt", .res = NULL}};
/**********************
 *      MACROS
 **********************/
#define LANG_LINE_MAX_SIZE 1024
#define LANG_RES_MIN_SIZE 128

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
const char *__string_by_index(int32_t index) {
	pthread_rwlock_rdlock(&g_res_lock);

	LANG_RES_S *language_res = g_lang_array[g_select_language].res;
	if (NULL == language_res) {
		pthread_rwlock_unlock(&g_res_lock);
		return NULL;
	}

	/* Indices must start at 1 */
	if (index <= 0 || index > language_res->size) {
		pthread_rwlock_unlock(&g_res_lock);
		return NULL;
	}
	pthread_rwlock_unlock(&g_res_lock);

	return language_res->vector[index - 1];
}

int32_t ui_language_load(void) {
	pthread_rwlock_wrlock(&g_res_lock);
	for (LANG_TYPE_E index = LANG_TYPE_CHINESE; index < LANG_TYPE_MAX; index++) {
		if (NULL != g_lang_array[index].res)
			lang_res_delete(g_lang_array[index].res);

		g_lang_array[index].res = lang_resource_file_load(g_lang_array[index].file_path);
	}
	pthread_rwlock_unlock(&g_res_lock);
	return 0;
}

void ui_language_release(void) {
	pthread_rwlock_wrlock(&g_res_lock);
	for (LANG_TYPE_E index = LANG_TYPE_CHINESE; index < LANG_TYPE_MAX; index++) {
		if (NULL != g_lang_array[index].res) {
			lang_res_delete(g_lang_array[index].res);
			g_lang_array[index].res = NULL;
		}
	}
	pthread_rwlock_unlock(&g_res_lock);
}

int32_t ui_language_select(LANG_TYPE_E lang_type) {
	pthread_rwlock_wrlock(&g_res_lock);
	if (lang_type >= LANG_TYPE_MAX || NULL == g_lang_array[lang_type].res) {
		pthread_rwlock_unlock(&g_res_lock);
		return -1;
	}

	g_select_language = lang_type;
	pthread_rwlock_unlock(&g_res_lock);
	return 0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static void parse_escape_char(const char *src, char *dest, uint32_t dest_size) {
	if (NULL == src || NULL == dest || 0 == dest_size) {
		return;
	}

	int32_t i = 0, j = 0;
	int32_t src_len = strlen(src);

	while ((i < src_len) && (j < dest_size - 1)) {
		if (src[i] == '\\' && (i + 1 < src_len)) {
			if (src[i + 1] == 'n') {
				dest[j++] = '\n';
				i += 2;
			} else if ((src[i + 1] == '\"') || (src[i + 1] == '\\')) {
				dest[j++] = src[i + 1];
				i += 2;
			} else {
				dest[j++] = src[i++];
			}
		} else {
			dest[j++] = src[i++];
		}
	}

	dest[j] = '\0';
}

static char *xstrdup(const char *s) {
	char *t;
	size_t len;
	if (!s)
		return NULL;

	len = strlen(s) + 1;
	t = (char *)malloc(len);
	if (t) {
		memcpy(t, s, len);
	}
	return t;
}

static uint32_t strstrip(char *s) {
	char *last = NULL;
	char *dest = s;

	if (s == NULL)
		return 0;

	last = s + strlen(s);
	while (isspace((int)*s) && *s)
		s++;
	while (last > s) {
		if (!isspace((int)*(last - 1)))
			break;
		last--;
	}
	*last = (char)0;

	memmove(dest, s, last - s + 1);
	return last - s;
}

static LANG_RES_S *lang_res_new(size_t size) {
	LANG_RES_S *res = NULL;

	if (size < LANG_RES_MIN_SIZE)
		size = LANG_RES_MIN_SIZE;

	res = (LANG_RES_S *)calloc(1, sizeof *res);
	if (NULL != res) {
		res->size = size;
		res->vector = (char **)calloc(size, sizeof *(res->vector));
		if (NULL == res->vector) {
			free(res);
			return NULL;
		}
	}

	return res;
}

static void lang_res_delete(LANG_RES_S *res) {
	if (NULL == res) {
		LOG_ERROR("Invalid input parameter.\n");
		return;
	}

	for (size_t i = 0; i < res->size; i++) {
		if (NULL != res->vector[i]) {
			free(res->vector[i]);
			res->vector[i] = NULL;
		}
	}

	free(res);
}

static int32_t lang_res_grow(LANG_RES_S *res) {
	if (NULL == res) {
		LOG_ERROR("Invalid input parameter.\n");
		return -1;
	}

	char **vector = (char **)realloc(res->vector, sizeof(char *) * (res->size * 2));
	if (NULL == vector) {
		LOG_ERROR("Memory expansion failed.\n");
		return -1;
	}

	res->size = res->size * 2;
	res->vector = vector;

	return 0;
}

static LANG_PARSE_STATE_E lang_parse_line(const char *input_line, char *section, char *key,
                                          char *value) {
	LANG_PARSE_STATE_E sta;
	char *line = NULL;
	size_t len;

	line = xstrdup(input_line);
	len = strstrip(line);

	sta = LANG_UNPROCESSED;
	if (len < 1) {
		/* Empty line */
		sta = LANG_EMPTY;
	} else if (line[0] == '#' || line[0] == ';') {
		/* Comment line */
		sta = LANG_COMMENT;
	} else if (line[0] == '[' && line[len - 1] == ']') {
		/* Section name */
		sscanf(line, "[%[^]]", section);
		strstrip(section);
		sta = LANG_SECTION;
	} else if (sscanf(line, "%[^=] = \"%[^\"]\"", key, value) == 2 ||
	           sscanf(line, "%[^=] = '%[^\']'", key, value) == 2) {
		strstrip(key);
		sta = LANG_VALUE;
	} else {
		/* Generate syntax error */
		sta = LANG_ERROR;
	}

	free(line);
	return sta;
}

static int32_t lang_res_set(LANG_RES_S *res, const char *key, const char *val) {
	int32_t index = -1;
	char tmp[LANG_LINE_MAX_SIZE + 1] = {0};

	if (res == NULL || key == NULL)
		return -1;

	if (1 != sscanf(key, "%*[^[] [ %[^]]", tmp)) {
		return -1;
	}

	index = (int32_t)strtol(tmp, NULL, 0);
	if (index <= 0 || index > (res->size * 2)) {
		return -1;
	}

	if (index > res->size) {
		if (0 != lang_res_grow(res)) {
			return -1;
		}
	}

	if (NULL != res->vector[index - 1]) {
		free(res->vector[index - 1]);
		res->vector[index - 1] = NULL;
	}

	memset(tmp, 0, sizeof(tmp));
	parse_escape_char(val, tmp, sizeof(tmp));
	res->vector[index - 1] = xstrdup(tmp);

	return 0;
}

static LANG_RES_S *lang_resource_file_load(const char *res_name) {
	if (NULL == res_name) {
		LOG_ERROR("Invalid input parameter.\n");
		return NULL;
	}

	LANG_RES_S *lang_res = NULL;

	FILE *in = NULL;

	int32_t last = 0;
	int32_t len;
	int32_t lineno = 0;
	int32_t errs = 0;

	char line[LANG_LINE_MAX_SIZE + 1] = {0};
	char section[LANG_LINE_MAX_SIZE + 1] = {0};
	char key[LANG_LINE_MAX_SIZE + 1] = {0};
	char val[LANG_LINE_MAX_SIZE + 1] = {0};

	if (NULL == (in = fopen(res_name, "rb"))) {
		LOG_ERROR("Cannot open %s\n", res_name);
		return NULL;
	}

	lang_res = lang_res_new(0);
	if (NULL == lang_res) {
		fclose(in);
		return NULL;
	}

	while (NULL != fgets(line + last, LANG_LINE_MAX_SIZE - last, in)) {
		lineno++;

		/* Skip the case where the number of characters in a single line is 1 */
		len = (int32_t)strlen(line) - 1;
		if (len <= 0)
			continue;

		/* Safety check against buffer overflows */
		if (line[len] != '\n' && !feof(in)) {
			LOG_ERROR("Input line too long in %s (%d)\n", res_name, lineno);
			lang_res_delete(lang_res);
			fclose(in);
			return NULL;
		}

		/* Get rid of \n and spaces at end of line */
		while ((len >= 0) && ((line[len] == '\n') || (isspace(line[len])))) {
			line[len] = 0;
			len--;
		}

		/* Line was entirely \n and/or spaces */
		if (len < 0) {
			len = 0;
		}

		/* Detect multi-line */
		if (line[len] == '\\') {
			/* Multi-line value */
			last = len;
			continue;
		} else {
			last = 0;
		}

		switch (lang_parse_line(line, section, key, val)) {
		case LANG_EMPTY:
		case LANG_COMMENT:
			break;

		case LANG_SECTION:
			/* TODO */
			break;

		case LANG_VALUE:
			lang_res_set(lang_res, key, val);
			break;

		case LANG_ERROR:
			LOG_ERROR("syntax error in %s (%d):\n-> %s\n", res_name, lineno, line);
			errs = 1;
			break;

		default:
			break;
		}

		memset(line, 0, LANG_LINE_MAX_SIZE);
		last = 0;
	}

	if (errs) {
		lang_res_delete(lang_res);
		lang_res = NULL;
	}
	fclose(in);
	return lang_res;
}
