/* SPDX-License-Identifier: GPL-2.0 */
/*
* This file is part of ILITEK CommonFlow
*
* Copyright (c) 2022 ILI Technology Corp.
* Copyright (c) 2022 Luca Hsu <luca_hsu@ilitek.com>
* Copyright (c) 2022 Joe Hung <joe_hung@ilitek.com>
*/

#ifndef __ILITEK_DEF_H__
#define __ILITEK_DEF_H__

#define COMMONFLOW_CODE_VERSION		0x00000205

#ifdef _WIN32
#include <windows.h>
#include <time.h>

#ifdef _WINDLL
#define __DLL __declspec(dllexport)
#else
#define __DLL __declspec(dllimport)
#endif

#define PACKED(_declare_)	\
	__pragma(pack(push, 1)) _declare_ __pragma(pack(pop))
#define _sprintf(buf, idx, fmt, ...)	\
	sprintf_s((buf) + (idx), sizeof((buf)) - (idx), (fmt), ##__VA_ARGS__)
#define _strncpy(dst, src, n, dst_size)	strncpy_s((dst), (dst_size), (src), (n))
#define _strcasecmp(l, r)		_stricmp((l), (r))
#define _strcat(dst, src, dst_size)	strcat_s((dst),(dst_size), (src))
#define _sscanf(str, fmt, ...)		sscanf_s(str, fmt, ##__VA_ARGS__)

#define _fopen(pfp, filename, mode)	(fopen_s((pfp), (filename), (const char *)(mode)))
#define _fclose(pfp)			fclose((pfp))

#define MUTEX_T				HANDLE
#define MUTEX_INIT(x)			((x) = CreateMutex(NULL, false, NULL))
#define MUTEX_LOCK(x)			(WaitForSingleObject((x), INFINITE))
#define MUTEX_UNLOCK(x)			(ReleaseMutex((x)))
#define MUTEX_EXIT(x)			(CloseHandle((x)))

#else /* _WIN32 */
#define __DLL
#define PACKED(_declare_)	\
	_declare_ __attribute__((__packed__))
#define _sprintf(buf, idx, fmt, ...)	sprintf((buf) + (idx), (fmt), ##__VA_ARGS__)
#define _strncpy(dst, src, n, dst_size) strncpy((dst), (src), (n))
#define _strcasecmp(l, r)		strcasecmp((l), (r))
#define _strcat(dst, src, dst_size)	strcat((dst), (src))
#define _sscanf(str, fmt, ...)		sscanf(str, fmt, ##__VA_ARGS__)

#ifdef __KERNEL__
#define FILE				void
#define _fopen(pfp, filename, mode)	(-EINVAL)
#define _fclose(pfp)

#include <linux/spinlock.h>
#define MUTEX_T				spinlock_t
#define MUTEX_INIT(x)			spin_lock_init(&(x))
#define MUTEX_LOCK(x)			spin_lock(&(x))
#define MUTEX_UNLOCK(x)			spin_unlock(&(x))
#define MUTEX_EXIT(x)

#else
#include <sys/time.h>
#define _fopen(pfp, filename, mode)	\
	((!((*(pfp)) = fopen((filename), (mode)))) ? -EFAULT : 0)
#define _fclose(pfp)			fclose((pfp))

#include <pthread.h>
#define MUTEX_T				pthread_mutex_t
#define MUTEX_INIT(x)			(pthread_mutex_init(&(x), NULL))
#define MUTEX_LOCK(x)			(pthread_mutex_lock(&(x)))
#define MUTEX_UNLOCK(x)			(pthread_mutex_unlock(&(x)))
#define MUTEX_EXIT(x)			(pthread_mutex_destroy(&(x)))

#endif
#endif /* _WIN32 */



#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>

#define MALLOC(size)		kmalloc(size, GFP_KERNEL)
#define CALLOC(num, size)	vmalloc(num * size)
#define FREE(ptr)		\
	do {			\
		kfree((ptr));	\
		(ptr) = NULL;	\
	} while (0)
#define CFREE(ptr)		\
	do {			\
		vfree((ptr));	\
		(ptr) = NULL;	\
	} while (0)
#define TP_DIV_ROUND_UP(a, b)	DIV_ROUND_UP(a, b)
#define TP_PRINT(fmt, ...)	printk(fmt, ##__VA_ARGS__)

#define TP_LOG(fp, str)

#define tp_log(_id, level, need_tag, tag, fmt, ...)			\
	do {								\
		char *__id__ = (_id);					\
									\
		if (level > tp_log_level)				\
			break;						\
									\
		g_str[0] = '\0';					\
									\
		if (need_tag)						\
			_sprintf(g_str, strlen(g_str), "%s", tag);	\
									\
		if (__id__) {						\
			_sprintf(g_str, strlen(g_str), "[%s] " fmt,	\
				 __id__, ##__VA_ARGS__);		\
		} else {						\
			_sprintf(g_str, strlen(g_str), " " fmt,		\
				 ##__VA_ARGS__);			\
		}							\
		TP_PRINT("%s", g_str);					\
	} while (0)

#else /* __KERNEL__ */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define MALLOC(size)		malloc(size)
#define CALLOC(num, size)	calloc(num, size)
#define FREE(ptr)		\
	do {			\
		free((ptr));	\
		(ptr) = NULL;	\
	} while (0)
#define CFREE(ptr)		FREE(ptr)
#define TP_DIV_ROUND_UP(a, b)	(((a) + (b) - 1) / (b))
#ifdef USE_ANDROID
#include <jni.h>
#include <android/log.h>
#define TP_PRINT(fmt, ...)	\
	__android_log_print(ANDROID_LOG_INFO, "ILITEK COMMON", "[%s][%d]" fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__);	
#else /* USE_ANDROID */
#define TP_PRINT(fmt, ...)				\
	do {						\
		printf(fmt, ##__VA_ARGS__);		\
		fflush(stdout);				\
	} while (0)
#endif /* USE_ANDROID */
#define TP_LOG(fp, str)				\
	do {					\
		if (!tp_save_log || !fp)	\
			break;			\
		fprintf((fp), "%s", (str));	\
	} while (0)

#define tp_log(_id, level, need_tag, tag, fmt, ...)			\
	do {								\
		char *__id__ = (_id);					\
		uint32_t __time_ms__ = get_time_ms();;			\
									\
		if (level > tp_log_level)				\
			break;						\
									\
		g_str[0] = '\0';					\
									\
		if (need_tag)						\
			_sprintf(g_str, strlen(g_str), "[%7u.%03u]%s",	\
				 __time_ms__ / 1000,			\
				 __time_ms__ % 1000, tag);		\
									\
		if (__id__) {						\
			_sprintf(g_str, strlen(g_str), "[%s] " fmt,	\
				 __id__, ##__VA_ARGS__);		\
		} else {						\
			_sprintf(g_str, strlen(g_str), " " fmt, 	\
		 ##__VA_ARGS__);			\
		}							\
		TP_PRINT("%s", g_str);					\
									\
		if (g_msg)						\
			g_msg(level, g_str);				\
		TP_LOG(tp_fp, g_str);					\
	} while (0)
#endif /* __KERNEL__ */

#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d)	(((n) + (d) - 1) / (d))
#endif

#ifndef UNUSED
#define UNUSED(x)		(void)(x)
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a)		((sizeof(a) / sizeof(*(a))))
#endif

#ifndef MIN
#define MIN(l, r)		(((l) > (r)) ? (r) : (l))
#endif

#ifndef MAX
#define MAX(l, r)		(((l) > (r)) ? (l) : (r))
#endif

enum ilitek_log_level {
	log_level_none = -1,	/* no log displayed */
	log_level_err = 0,	/* critical errors */
	log_level_warn,		/* warnings */
	log_level_tag,		/* special-required tags */
	log_level_info,		/* important/UI messages */
	log_level_msg,		/* non-important messages */
	log_level_dbg,		/* debugging messages */
	log_level_pkt,		/* tx/rx packets */

	log_level_max,		/* sentinel */
};

#define _TP_ERR(fmt, ...)	\
	tp_log(NULL, log_level_err, false, "", fmt, ##__VA_ARGS__)
#define _TP_WARN(fmt, ...)	\
	tp_log(NULL, log_level_warn, false, "", fmt, ##__VA_ARGS__)
#define _TP_TAG(fmt, ...)	\
	tp_log(NULL, log_level_tag, false, "", fmt, ##__VA_ARGS__)
#define _TP_INFO(fmt, ...)	\
	tp_log(NULL, log_level_info, false, "", fmt, ##__VA_ARGS__)
#define _TP_MSG(fmt, ...)	\
	tp_log(NULL, log_level_msg, false, "", fmt, ##__VA_ARGS__)
#define _TP_DBG(fmt, ...)	\
	tp_log(NULL, log_level_dbg, false, "", fmt, ##__VA_ARGS__)
#define _TP_PKT(fmt, ...)	\
	tp_log(NULL, log_level_pkt, false, "", fmt, ##__VA_ARGS__)


#define TP_ERR(id, fmt, ...)	\
	tp_log(id, log_level_err, true, "[ILITEK][ERR]", fmt, ##__VA_ARGS__)
#define TP_WARN(id, fmt, ...)	\
	tp_log(id, log_level_warn, true, "[ILITEK][WARN]", fmt, ##__VA_ARGS__)
#define TP_TAG(id, fmt, ...)	\
	tp_log(id, log_level_tag, true, "[ILITEK][TAG]", fmt, ##__VA_ARGS__)
#define TP_INFO(id, fmt, ...)	\
	tp_log(id, log_level_info, true, "[ILITEK][INFO]", fmt, ##__VA_ARGS__)
#define TP_MSG(id, fmt, ...)	\
	tp_log(id, log_level_msg, true, "[ILITEK][MSG]", fmt, ##__VA_ARGS__)
#define TP_DBG(id, fmt, ...)	\
	tp_log(id, log_level_dbg, true, "[ILITEK][DBG]", fmt, ##__VA_ARGS__)
#define TP_PKT(id, fmt, ...)	\
	tp_log(id, log_level_pkt, true, "[ILITEK][PKT]", fmt, ##__VA_ARGS__)

enum ilitek_array_type {
	TYPE_U8 = 0,
	TYPE_INT,
};

#define TP_ERR_ARR(id, tag, type, len, buf) \
	tp_log_arr(id, log_level_err, "[ILITEK][ERR]", tag, type, len, buf)
#define TP_WARN_ARR(id, tag, type, len, buf) \
	tp_log_arr(id, log_level_warn, "[ILITEK][WARN]", tag, type, len, buf)
#define TP_TAG_ARR(id, tag, type, len, buf) \
	tp_log_arr(id, log_level_tag, "[ILITEK][TAG]", tag, type, len, buf)
#define TP_INFO_ARR(id, tag, type, len, buf) \
	tp_log_arr(id, log_level_info, "[ILITEK][INFO]", tag, type, len, buf)
#define TP_MSG_ARR(id, tag, type, len, buf) \
	tp_log_arr(id, log_level_msg, "[ILITEK][MSG]", tag, type, len, buf)
#define TP_DBG_ARR(id, tag, type, len, buf) \
	tp_log_arr(id, log_level_dbg, "[ILITEK][DBG]", tag, type, len, buf)
#define TP_PKT_ARR(id, tag, type, len, buf) \
	tp_log_arr(id, log_level_pkt, "[ILITEK][PKT]", tag, type, len, buf)

extern int tp_log_level;
extern char g_str[4096];
extern bool tp_save_log;
extern FILE *tp_fp;

typedef void(*msg_t)(int, char *);
extern msg_t g_msg;

struct queue {
	uint32_t curr_size;
	uint32_t max_size;
	uint8_t *buf;
	uint8_t *push_ptr;
	uint8_t *pop_ptr;
	uint8_t *end_ptr;
	uint32_t item_size;

	MUTEX_T mutex;
};


void _strcpy(char *dst, const char *src, int dst_size);
char *_strtok(char* str, const char* del, char** next_token);

#ifdef __cplusplus
extern "C" {
#endif

void __DLL tp_log_arr(char *id, int level, const char *header,
		      const char *tag, int type, int len, void *buf);

uint32_t __DLL get_time_ms(void);
void __DLL set_log_level(int level);
int __DLL set_log_fopen(char *filename);
void __DLL set_log_fclose(void);
void __DLL set_log_fwrite(char *str);

int queue_init(struct queue *q, uint32_t item_size, uint32_t max_items);
void queue_exit(struct queue *q);
void queue_push(struct queue *q);
void queue_pop(struct queue *q);

#ifdef __cplusplus
}
#endif

#endif
