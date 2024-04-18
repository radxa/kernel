// SPDX-License-Identifier: GPL-2.0
/*
 * This file is part of ILITEK CommonFlow
 *
 * Copyright (c) 2022 ILI Technology Corp.
 * Copyright (c) 2022 Luca Hsu <luca_hsu@ilitek.com>
 * Copyright (c) 2022 Joe Hung <joe_hung@ilitek.com>
 */

#include "ilitek_def.h"

int tp_log_level = log_level_msg;
bool tp_save_log = false;
FILE *tp_fp = NULL;

char g_str[4096];
msg_t g_msg = NULL;

#ifdef __KERNEL__
uint32_t get_time_ms(void)
{
	return 0;
}

#else
#ifdef _WIN32
static int gettimeofday(struct timeval *tp, void *tzp)
{
	time_t clock;
	struct tm tm;
	SYSTEMTIME wtm;

	GetLocalTime(&wtm);
	tm.tm_year = wtm.wYear - 1900;
	tm.tm_mon = wtm.wMonth - 1;
	tm.tm_mday = wtm.wDay;
	tm.tm_hour = wtm.wHour;
	tm.tm_min = wtm.wMinute;
	tm.tm_sec = wtm.wSecond;
	tm.tm_isdst = -1;
	clock = mktime(&tm);
	tp->tv_sec = (long)clock;
	tp->tv_usec = wtm.wMilliseconds * 1000;
	return (0);
}
#endif

uint32_t get_time_ms(void)
{
	static uint32_t time_ms_init = 0;
	struct timeval t;
	uint32_t time_ms;

	gettimeofday(&t, NULL);
	time_ms = t.tv_sec * 1000 + t.tv_usec / 1000;
	time_ms_init = (!time_ms_init) ? time_ms : time_ms_init;

	return time_ms - time_ms_init;
}

#endif

void tp_log_arr(char *id, int level, const char *header, const char *tag,
		int type, int len, void *buf)
{
	const int num = 64;
	int i, idx = 0;
	uint32_t time_ms;

	if (level > tp_log_level || !buf)
		return;

	time_ms = get_time_ms();

	do {
		memset(g_str, 0, sizeof(g_str));

#ifdef __KERNEL__
		/* no need to add time tag at kernel log */
		if (id)
			_sprintf(g_str, 0, "%s[%s] %s ", header, id, tag);
		else
			_sprintf(g_str, 0, "%s %s ", header, tag);
#else
		if (id) {
			_sprintf(g_str, 0, "[%7u.%03u]%s[%s] %s ",
				time_ms / 1000, time_ms % 1000,
				header, id, tag);
		} else {
			_sprintf(g_str, 0, "[%7u.%03u]%s %s ",
				time_ms / 1000, time_ms % 1000,
				header, tag);
		}
#endif

		for (i = 0; i < num && idx < len; i++, idx++) {
			switch (type) {
			default:
			case TYPE_U8:
				_sprintf(g_str, strlen(g_str), "%02x-",
					((uint8_t *)buf)[idx]);
				break;
			case TYPE_INT:
				_sprintf(g_str, strlen(g_str), "%d-",
					((int *)buf)[idx]);
				break;
			}
		}
		_sprintf(g_str, strlen(g_str) - 1, ", len: [%d/%d]\n",
			idx, len);

		TP_PRINT("%s", g_str);
		if (g_msg)
			g_msg(level, g_str);
		TP_LOG(tp_fp, g_str);
	} while (idx < len);
}

char *_strtok(char* str, const char* del, char** next_token)
{
#ifdef _WIN32
	return strtok_s(str, del, next_token);
#elif defined(__KERNEL__)
	UNUSED(str);
	UNUSED(del);
	UNUSED(next_token);
	return NULL;
#else
	UNUSED(next_token);
	return strtok(str, del);
#endif
}

void _strcpy(char *dst, const char *src, int dst_size)
{
#ifdef _WIN32
	strcpy_s(dst, dst_size, src);
#else
	UNUSED(dst_size);
	strcpy(dst, src);
#endif
}

int queue_init(struct queue *q, uint32_t item_size, uint32_t max_items)
{
	int error = 0;

	MUTEX_INIT(q->mutex);

	MUTEX_LOCK(q->mutex);

	do {
		q->item_size = item_size;
		q->curr_size = 0;
		q->max_size = max_items;

		q->buf = (uint8_t *)CALLOC(max_items, item_size);
		if (!q->buf) {
			error = -ENOMEM;
			break;
		}

		q->push_ptr = q->buf;
		q->pop_ptr = q->buf;
		q->end_ptr = q->buf + (max_items - 1) * item_size;
	} while (false);

	MUTEX_UNLOCK(q->mutex);

	return error;
}

void queue_exit(struct queue *q)
{
	if (q->buf)
		CFREE(q->buf);
	MUTEX_EXIT(q->mutex);
}

void queue_push(struct queue *q)
{
	MUTEX_LOCK(q->mutex);

	/* Stop push data when queue is full */
	if (q->curr_size >= q->max_size)
		goto release_push_lock;

	q->curr_size++;
	if (q->push_ptr == q->end_ptr)
		q->push_ptr = q->buf;
	else
		q->push_ptr += q->item_size;

	if (q->push_ptr == q->pop_ptr)
		TP_ERR(NULL, "[Warn]Queue overload, queue size: %u\n", q->curr_size);

release_push_lock:
	MUTEX_UNLOCK(q->mutex);
}

void queue_pop(struct queue *q)
{
	MUTEX_LOCK(q->mutex);

	if (!q->curr_size)
		goto release_pop_lock;

	q->curr_size--;
	if (q->pop_ptr == q->end_ptr)
		q->pop_ptr = q->buf;
	else
		q->pop_ptr += q->item_size;

release_pop_lock:
	MUTEX_UNLOCK(q->mutex);
}


void set_log_level(int level)
{
	tp_log_level = level;
}

int set_log_fopen(char *filename)
{
	int error;

	if (tp_save_log)
		return -EINVAL;

	if ((error = _fopen(&tp_fp, filename, "w+")) < 0) {
		tp_fp = NULL;
		return error;
	}
	tp_save_log = true;

	return 0;
}

void set_log_fclose(void)
{
	if (!tp_save_log)
		return;

	_fclose(tp_fp);
	tp_fp = NULL;
	tp_save_log = false;
}

void set_log_fwrite(char *str)
{
	TP_LOG(tp_fp, str);
}
