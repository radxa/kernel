/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is part of ILITEK CommonFlow
 *
 * Copyright (c) 2022 ILI Technology Corp.
 * Copyright (c) 2022 Luca Hsu <luca_hsu@ilitek.com>
 * Copyright (c) 2022 Joe Hung <joe_hung@ilitek.com>
 */

#ifndef __ILITEK_UPDATE_H__
#define __ILITEK_UPDATE_H__

#include "ilitek_protocol.h"

#define UPDATE_LEN			1024


#define ILITEK_FW_FILE_SIZE		(512 * 1024)
#define ILITEK_FW_BUF_SIZE		(256 * 1024)

enum fw_file_type {
	fw_hex = 0,
	fw_bin,
	fw_ili,
};

PACKED(struct mapping_info_lego {
	uint8_t tuning_ver[4];
	uint8_t fw_ver[4];
	uint8_t core_test;
	uint8_t core_day;
	uint8_t core_month;
	uint8_t core_year;
	uint32_t core_ver;
	uint8_t vendor_ver[6];
	uint8_t _reserve_1[8];
	uint16_t customer_id;
	uint16_t fwid;
	uint16_t i2c_addr;
	uint8_t _reserve_2[2];
	char model_name[16];
	uint8_t _reserve_3[2];
	uint8_t ic_num;
	uint8_t total_tuning_num;
	uint16_t sizeof_tuning;
	uint16_t sizeof_tp_param;
	uint16_t sizeof_sys_info;
	uint16_t sizeof_sys_algo;
	uint16_t sizeof_key_info;
	uint8_t block_num;
	uint8_t support_tuning_num;
	uint8_t _reserve_4[2];

	PACKED(struct {
		uint8_t addr[3];
	}) blocks[10];

	uint8_t _reserve_5[9];
	uint8_t end_addr[3];
	uint8_t _reserve_6[2];
});

PACKED(union mapping_info {
	PACKED(struct {
		uint8_t mapping_ver[3];
		uint8_t protocol_ver[3];
		uint8_t ic_name[6];

		struct mapping_info_lego _lego;
	});
});

/*
 * for V3, "check" is checksum, block[0] for AP and block[1] for Data Flash.
 * for V6, "check" is CRC.
 */
PACKED(struct ilitek_block {
	bool check_match;
	uint32_t start;
	uint32_t end;
	uint32_t check;
	uint32_t offset;
});

PACKED(struct ilitek_fw_file_info {
	char ic_name[8];
	uint8_t fw_ver[8];

	uint8_t block_num;
	struct ilitek_block blocks[ILTIEK_MAX_BLOCK_NUM];

	uint32_t mm_addr;
	uint32_t mm_size;

	uint32_t buf_size;
	uint8_t *buf;

	/* fw file's sensor-id or fwid or other id */
	uint32_t id;
	uint8_t type;
});

/* return file size in # of bytes, or negative error code */
typedef int (*read_fw_t)(char *, uint8_t *, int, void *);
/* update progress of fw updating */
typedef void (*update_progress_t)(uint8_t, void *);
/* update fw info to callers */
typedef void (*update_fw_file_info_t)(struct ilitek_fw_file_info *, void *);


/* notify caller before/after slave upgrade (for ITS only) */
typedef void (*slave_update_notify_t)(bool, void *);
/* update fw version and crc/checksum before/after update (for ITS only) */
typedef void (*update_fw_ic_info_t)(bool, uint8_t *, uint32_t *, int, void *);

struct ilitek_update_callback {
	read_fw_t read_fw;
	update_progress_t update_progress;
	update_fw_file_info_t update_fw_file_info;

	slave_update_notify_t slave_update_notify;
	update_fw_ic_info_t update_fw_ic_info;
};

struct ilitek_fw_settings {
	int8_t retry;
	bool fw_check_only;
	bool force_update;

	bool fw_ver_check;
	uint8_t fw_ver[8];
};

struct ilitek_fw_handle {
	struct ilitek_ts_device *dev;
	void *_private;

	/* upgrade options */
	struct ilitek_fw_settings setting;

	/* common variable */
	char fw_name[512];

	struct ilitek_fw_file_info file;

	/* M3 + M2V */
	bool m2v;
	bool m2v_need_update;
	uint8_t *m2v_buf;
	uint32_t m2v_checksum;
	uint8_t m2v_fw_ver[8];

	/* upgrade status */
	unsigned int progress_curr;
	unsigned int progress_max;
	uint8_t progress;

	/* callbacks */
	struct ilitek_update_callback cb;
};

#ifdef __cplusplus
extern "C" {
#endif

void __DLL *ilitek_update_init(void *_dev,
			       struct ilitek_update_callback *callback,
			       void *_private);

void __DLL ilitek_update_exit(void *handle);

void __DLL ilitek_update_setting(void *handle,
				 struct ilitek_fw_settings *setting);

int __DLL ilitek_update_bind_dev(void *handle, void *dev);

int __DLL ilitek_update_load_fw(void *handle, char *fw_name);

int __DLL ilitek_update_start(void *handle);

#ifdef __cplusplus
}
#endif


#endif

