/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is part of ILITEK CommonFlow
 *
 * Copyright (c) 2022 ILI Technology Corp.
 * Copyright (c) 2022 Luca Hsu <luca_hsu@ilitek.com>
 * Copyright (c) 2022 Joe Hung <joe_hung@ilitek.com>
 */

#ifndef __ILITEK_PROTOCOL_H__
#define __ILITEK_PROTOCOL_H__

#include "ilitek_def.h"

#define START_ADDR_LEGO			0x3000
#define START_ADDR_29XX			0x4000
#define END_ADDR_LEGO			0x40000

#define MM_ADDR_LEGO			0x3020
#define MM_ADDR_29XX			0x4020

#define DF_START_ADDR_LEGO		0x3C000
#define DF_START_ADDR_29XX		0x2C000

#define SLAVE_SET_AP			0xC1
#define SLAVE_SET_BL			0xC2
#define SLAVE_WRITE_AP			0xC3
#define M2V_GET_MOD			0xC0
#define M2V_WRITE_ENABLE		0xC4
#define M2V_GET_CHECKSUM		0xC7
#define M2V_GET_FW_VER			0x40

#define ILITEK_TP_SYSTEM_READY		0x50

#define CRC_CALCULATE			0
#define CRC_GET				1

#define ILTIEK_MAX_BLOCK_NUM		20

#define PTL_ANY				0x00
#define PTL_V3				0x01
#define PTL_V6				0x02

#define BL_PROTOCOL_V1_8		0x10800
#define BL_PROTOCOL_V1_7		0x10700
#define BL_PROTOCOL_V1_6		0x10600

#define STYLUS_MODES			\
	X(STYLUS_WGP,	0x1,	"WGP")	\
	X(STYLUS_USI,	0x2,	"USI")	\
	X(STYLUS_MPP,	0x4,	"MPP")

#define ILITEK_CMD_MAP							\
	X(0x20, PTL_ANY, GET_TP_INFO, api_protocol_get_tp_info)		\
	X(0x21, PTL_ANY, GET_SCRN_RES, api_protocol_get_scrn_res)	\
	X(0x22, PTL_ANY, GET_KEY_INFO, api_protocol_get_key_info)	\
	X(0x30, PTL_ANY, SET_IC_SLEEP, api_protocol_set_sleep)		\
	X(0x31, PTL_ANY, SET_IC_WAKE, api_protocol_set_wakeup)		\
	X(0x34, PTL_ANY, SET_MCU_IDLE, api_protocol_set_idle)		\
	X(0x40, PTL_ANY, GET_FW_VER, api_protocol_get_fw_ver)		\
	X(0x42, PTL_ANY, GET_PTL_VER, api_protocol_get_ptl_ver)		\
	X(0x43, PTL_ANY, GET_CORE_VER, api_protocol_get_core_ver)	\
	X(0x60, PTL_ANY, SET_SW_RST, api_protocol_set_sw_reset)		\
	X(0x61, PTL_ANY, GET_MCU_VER, api_protocol_get_mcu_ver)		\
	X(0x68, PTL_ANY, SET_FUNC_MOD, api_protocol_set_func_mode)	\
	X(0x80, PTL_ANY, GET_SYS_BUSY, api_protocol_get_sys_busy)	\
	X(0xC0, PTL_ANY, GET_MCU_MOD, api_protocol_get_mcu_mode)	\
	X(0xC1, PTL_ANY, SET_AP_MODE, api_protocol_set_ap_mode)		\
	X(0xC2, PTL_ANY, SET_BL_MODE, api_protocol_set_bl_mode)		\
	X(0xC5, PTL_ANY, READ_FLASH, api_protocol_read_flash)		\
	X(0xC7, PTL_ANY, GET_AP_CRC, api_protocol_get_ap_crc)		\
	X(0xC8, PTL_ANY, SET_ADDR, api_protocol_set_flash_addr)		\
									\
	/* v3 only cmds */						\
	X(0x25, PTL_V3, GET_CDC_INFO_V3, api_protocol_get_cdc_info_v3)	\
	X(0x63, PTL_V3, TUNING_PARA_V3, api_protocol_tuning_para_v3)	\
	X(0xC3, PTL_V3, WRITE_DATA_V3, api_protocol_write_data_v3)	\
	X(0xC4, PTL_V3, WRITE_ENABLE, api_protocol_write_enable)	\
	X(0xCA, PTL_V3, GET_DF_CRC, api_protocol_get_df_crc)		\
	X(0xF2, PTL_V3, SET_TEST_MOD, api_protocol_set_mode_v3)		\
	X(0xF3, PTL_V3, INIT_CDC_V3, api_protocol_set_cdc_init_v3)	\
									\
	/* v6 only cmds */						\
	X(0x27, PTL_V6, GET_SENSOR_ID, api_protocol_get_sensor_id)	\
	X(0x45, PTL_V6, GET_PRODUCT_INFO, api_protocol_get_product_info)\
	X(0x46, PTL_V6, GET_FWID, api_protocol_get_fwid)		\
	X(0x47, PTL_V6, GET_CRYPTO_INFO, api_protocol_get_crypto_info)	\
	X(0x48, PTL_V6, GET_HID_INFO, api_protocol_get_hid_info)	\
	X(0x62, PTL_V6, GET_MCU_INFO, api_protocol_get_mcu_info)	\
	X(0x65, PTL_V6, TUNING_PARA_V6, api_protocol_tuning_para_v6)	\
	X(0x69, PTL_V6, SET_FS_INFO, api_protocol_set_fs_info)		\
	X(0x6A, PTL_V6, SET_SHORT_INFO, api_protocol_set_short_info)	\
	X(0x6B, PTL_V6, C_MODEL_INFO, api_protocol_c_model_info)	\
	X(0x6C, PTL_V6, SET_P2P_INFO, api_protocol_set_p2p_info)	\
	X(0x6D, PTL_V6, SET_OPEN_INFO, api_protocol_set_open_info)	\
	X(0x6F, PTL_V6, SET_PEN_FS_INFO, api_protocol_set_pen_fs_info)	\
	X(0xB0, PTL_V6, WRITE_DATA_M2V, api_protocol_write_data_m2v)	\
	X(0xC3, PTL_V6, WRITE_DATA_V6, api_protocol_write_data_v6)	\
	X(0xC9, PTL_V6, SET_DATA_LEN, api_protocol_set_data_len)	\
	X(0xCB, PTL_V6, ACCESS_SLAVE, api_protocol_access_slave)	\
	X(0xCC, PTL_V6, SET_FLASH_EN, api_protocol_set_flash_enable)	\
	X(0xCD, PTL_V6, GET_BLK_CRC_ADDR, api_protocol_get_crc_by_addr)	\
	X(0xCF, PTL_V6, GET_BLK_CRC_NUM, api_protocol_get_crc_by_num)	\
	X(0xF0, PTL_V6, SET_MOD_CTRL, api_protocol_set_mode_v6)		\
	X(0xF1, PTL_V6, INIT_CDC_V6, api_protocol_set_cdc_init_v6)	\
	X(0xF2, PTL_V6, GET_CDC_V6, api_protocol_get_cdc_v6)


#define X(_cmd, _protocol, _cmd_id, _api)	_cmd_id,
enum ilitek_cmd_ids {
	ILITEK_CMD_MAP
	/* ALWAYS keep at the end */
	MAX_CMD_CNT
};
#undef X

#define X(_cmd, _protocol, _cmd_id, _api)	CMD_##_cmd_id = _cmd,
enum ilitek_cmds { ILITEK_CMD_MAP };
#undef X

enum ilitek_hw_interfaces {
	interface_i2c = 0,
	interface_hid_over_i2c,
	interface_usb,
};

enum ilitek_fw_modes {
	mode_normal = 0,
	mode_test,
	mode_debug,
	mode_suspend,
};

enum ilitek_key_modes {
	key_disable = 0,
	key_hw = 1,
	key_hsw = 2,
	key_vitual = 3,
	key_fw_disable = 0xff,
};

#define ILITEK_TOUCH_REPORT_FORMAT 	\
	X(touch_fmt_0, 0, 5, 10)	\
	X(touch_fmt_1, 1, 6, 10)	\
	X(touch_fmt_2, 2, 10, 5)	\
	X(touch_fmt_3, 3, 10, 5)

#define X(_enum, _id, _size, _cnt)	_enum = _id,
enum ilitek_touch_fmts { ILITEK_TOUCH_REPORT_FORMAT };
#undef X

PACKED(struct touch_fmt {
	uint8_t id:6;
	uint8_t status:1;
	uint8_t reserve:1;
	uint16_t x;
	uint16_t y;
	uint8_t p;
	uint16_t w;
	uint16_t h;
});

PACKED(struct pen_fmt {
	PACKED(union {
		uint8_t modes;
		PACKED(struct {
			uint8_t tip_sw : 1;
			uint8_t barrel_sw : 1;
			uint8_t eraser : 1;
			uint8_t invert : 1;
			uint8_t in_range : 1;
			uint8_t reserve : 3;
		});
	});
	uint16_t x;
	uint16_t y;
	uint16_t pressure;
	int16_t x_tilt;
	int16_t y_tilt;
});

struct ilitek_report_info {
	unsigned int size;
	unsigned int max_cnt;
};

PACKED(struct ilitek_screen_info {
	uint16_t x_min;
	uint16_t y_min;
	uint16_t x_max;
	uint16_t y_max;
	uint16_t pressure_min;
	uint16_t pressure_max;
	int16_t x_tilt_min;
	int16_t x_tilt_max;
	int16_t y_tilt_min;
	int16_t y_tilt_max;
	uint16_t pen_x_min;
	uint16_t pen_y_min;
	uint16_t pen_x_max;
	uint16_t pen_y_max;
});

PACKED(struct ilitek_tp_info_v6 {
	uint16_t x_resolution;
	uint16_t y_resolution;
	uint16_t x_ch;
	uint16_t y_ch;
	uint8_t max_fingers;
	uint8_t key_num;
	uint8_t ic_num;
	uint8_t support_modes;
	uint8_t format;
	uint8_t die_num;
	uint8_t block_num;
	uint8_t pen_modes;
	uint8_t pen_format;
	uint16_t pen_x_resolution;
	uint16_t pen_y_resolution;
});

PACKED(struct ilitek_tp_info_v3 {
	uint16_t x_resolution;
	uint16_t y_resolution;
	uint8_t x_ch;
	uint8_t y_ch;
	uint8_t max_fingers;
	uint8_t reserve;
	uint8_t key_num;
	uint8_t reserve_1[5];
	uint8_t support_modes;
});

PACKED(struct ilitek_key_info_v6 {
	uint8_t mode;
	uint16_t x_len;
	uint16_t y_len;

	PACKED(struct{
		uint8_t id;
		uint16_t x;
		uint16_t y;
	}) keys[50];
});

PACKED(struct ilitek_key_info_v3 {
	uint8_t x_len[2];
	uint8_t y_len[2];

	PACKED(struct{
		uint8_t id;
		uint8_t x[2];
		uint8_t y[2];
	}) keys[20];
});

PACKED(struct ilitek_ts_kernel_ver {
	uint16_t ic_name;
	uint8_t df_start_addr[3];
	uint8_t df_size;

	char module_name[26];
});

struct ilitek_ts_kernel_info {
	PACKED(struct {
		char ic_name[5];
		char mask_ver[2];
		uint8_t mm_addr[3];
		char module_name[18];
		uint8_t reserve[4];
	}) parser;

	char ic_name[6];
	char mask_ver[2];
	uint32_t mm_addr;
	uint32_t min_addr;
	uint32_t max_addr;
	char module_name[32];
};

struct ilitek_key_info {
	struct ilitek_key_info_v6 info;
	bool clicked[50];
};

struct ilitek_sensor_id {
	uint16_t header;
	uint8_t id;
};

struct ilitek_ts_protocol {
	uint32_t ver;
	uint8_t flag;
};

struct ilitek_ts_ic {
	uint8_t mode;
	uint32_t crc[ILTIEK_MAX_BLOCK_NUM];

	char mode_str[32];
};

struct ilitek_slave_access {
	uint8_t slave_id;
	uint8_t func;
	void *data;
};

PACKED(struct ilitek_hid_info {
	uint16_t pid;
	uint16_t vid;
	uint16_t rev;
});

struct tuning_para_settings {
	uint8_t func;
	uint8_t ctrl;
	uint8_t type;

	uint8_t *buf;
	uint32_t read_len;
};

struct reports {
	bool touch_need_update;
	bool pen_need_update;

	uint8_t touch[64];
	uint8_t pen[64];
};

struct grid_data {
	bool need_update;
	unsigned int X, Y;

	int32_t *data;
};

struct grids {
	struct grid_data mc;
	struct grid_data sc_x;
	struct grid_data sc_y;
	struct grid_data pen_x;
	struct grid_data pen_y;

	struct grid_data key_mc;
	struct grid_data key_x;
	struct grid_data key_y;

	struct grid_data self;

	/* touch/pen debug message along with frame update */
	struct reports dmsg;
};

PACKED(struct freq_category {
	unsigned int start;
	unsigned int end;
	unsigned int step;

	unsigned int size;
	char limit[1024];

	uint8_t mode;

	int *data;
});

PACKED(struct freq_settings {
	bool prepared;
	
	unsigned int frame_cnt;
	unsigned int scan_type;

	struct freq_category sine;
	struct freq_category mc_swcap;
	struct freq_category sc_swcap;
	struct freq_category pen;

	struct freq_category dump1;
	struct freq_category dump2;
	uint8_t dump1_val;
	uint8_t dump2_val;
});

PACKED(struct short_settings {
	bool prepared;

	uint8_t dump_1;
	uint8_t dump_2;
	uint8_t v_ref_L;
	uint16_t post_idle;
});

PACKED(struct open_settings {
	bool prepared;

	uint16_t freq;
	uint8_t gain;
	uint8_t gain_rfb;
	uint8_t afe_res_sel;
	uint8_t mc_fsel;
});

PACKED(struct p2p_settings {
	bool prepared;

	uint16_t frame_cnt;
});

PACKED(struct cdc_settings {
	uint8_t cmd;
	uint16_t config;

	bool no_reset_grid_after_update;

	/* freq. */
	struct freq_settings freq;
	/* short */
	struct short_settings _short;
	/* open */
	struct open_settings open;
	/* p2p */
	struct p2p_settings p2p;

	/* status only writable by CDC commonflow */
	bool is_p2p;
	bool is_freq;
	bool is_16bit;
	bool is_sign;
	bool is_fast_mode;
	unsigned int total_bytes;
});

enum ilitek_enum_type {
	enum_ap_bl = 0,
	enum_sw_reset,
};

typedef void (*update_grid_t)(uint32_t, uint32_t, struct grids *, void *);
typedef void (*update_report_rate_t)(unsigned int);

typedef int (*write_then_read_t)(uint8_t *, int, uint8_t *, int, void *);
typedef int (*read_interrupt_in_t)(uint8_t *, int, unsigned int, void *);
typedef void (*init_ack_t)(void *);
typedef int (*wait_ack_t)(uint8_t, unsigned int, void *);
typedef int (*hw_reset_t)(unsigned int, void *);
typedef int (*re_enum_t)(uint8_t, void *);
typedef void (*delay_ms_t)(unsigned int);

typedef int (*write_direct_t)(uint8_t *, int, void *);
typedef void (*mode_switch_notify_t)(bool , void *);

struct ilitek_ts_callback {
	/* Please don't use "repeated start" for I2C interface */
	write_then_read_t write_then_read;
	read_interrupt_in_t read_interrupt_in;
	init_ack_t init_ack;
	wait_ack_t wait_ack;
	hw_reset_t hw_reset;
	re_enum_t re_enum;
	delay_ms_t delay_ms;
	msg_t msg;

	/* write cmd without adding any hid header */
	write_direct_t write_direct;
	/* notify caller after AP/BL mode switch command */
	mode_switch_notify_t mode_switch_notify;
};

struct ilitek_ts_settings {
	volatile bool no_retry;
	volatile bool no_INT_ack;

	uint8_t sensor_id_mask;
};

struct ilitek_sys_info {
	bool libusb;
	uint16_t pid;
};

/* quirks definition */
#define DEV_QUIRK_SKIP_INT_AFTER_C2			1
#define DEV_QUIRK_SET_BRIDGE_ENG_MODE_AFTER_F2		2
#define DEV_QUIRK_DAEMON_V3_I2C_REPORT_HANDLING		4

struct ilitek_ts_device {
	void *_private;
	char id[64];
	uint32_t quirks;

	struct ilitek_ts_settings setting;
	struct ilitek_sys_info sys;

	uint8_t _interface;
	struct ilitek_ts_protocol protocol;
	unsigned int reset_time;

	uint8_t fw_ver[8];
	uint8_t core_ver[8];

	uint8_t product_info[8];
	uint16_t customer_id;
	uint16_t fwid;
	struct ilitek_sensor_id sensor;

	struct ilitek_ts_ic ic[32];
	struct ilitek_screen_info screen_info;
	struct ilitek_tp_info_v6 tp_info;
	struct ilitek_key_info key;
	struct ilitek_ts_kernel_info mcu_info;
	struct ilitek_hid_info hid_info;

	uint8_t func_mode;

	char pen_mode[64];

	struct ilitek_report_info finger;

	uint8_t wbuf[4096];
	uint8_t rbuf[4096];
	struct ilitek_ts_callback cb;
};

#ifdef __cplusplus
extern "C" {
#endif

uint16_t __DLL le16(const uint8_t *p);
uint16_t __DLL be16(const uint8_t *p);
uint32_t __DLL le32(const uint8_t *p, int bytes);
uint32_t __DLL be32(const uint8_t *p, int bytes);

bool __DLL is_29xx(void *handle);
bool __DLL is_231x(void *handle);

int __DLL grid_alloc(void *handle, struct grids *grid);
void __DLL grid_free(struct grids *grid);
void __DLL grid_reset(struct grids *grid);

uint16_t __DLL get_crc(uint32_t start, uint32_t end, uint8_t *buf,
	uint32_t buf_size);

uint32_t __DLL  get_checksum(uint32_t start, uint32_t end, uint8_t *buf,
	uint32_t buf_size);

bool __DLL support_sensor_id(void *handle);

bool __DLL support_production_info(void *handle);

bool __DLL support_fwid(void *handle);

int __DLL reset_helper(void *handle);

int __DLL write_then_read(void *handle, uint8_t *cmd, int wlen,
			  uint8_t *buf, int rlen);
int __DLL read_interrupt_in(void *handle, uint8_t *buf, int rlen,
			    unsigned int timeout_ms);

void __DLL __ilitek_get_ts_info(void *handle,
				struct ilitek_tp_info_v6 *tp_info);

void __DLL ilitek_dev_set_quirks(void *handle, uint32_t quirks);
void __DLL ilitek_dev_set_sys_info(void *handle, struct ilitek_sys_info *sys);
void __DLL ilitek_dev_setting(void *handle, struct ilitek_ts_settings *setting);

void __DLL *ilitek_dev_init(uint8_t _interface, char *id,
			    struct ilitek_ts_callback *callback,
			    void *_private);
void __DLL ilitek_dev_exit(void *handle);

int __DLL api_update_protocol_ver(void *handle);

int __DLL api_update_ts_info(void *handle);

int __DLL api_protocol_set_cmd(void *handle, uint8_t idx, void *data);
int __DLL api_set_ctrl_mode(void *handle, uint8_t mode, bool eng);

uint16_t __DLL api_get_block_crc_by_addr(void *handle,
					 uint8_t type, uint32_t start,
					 uint32_t end);
uint16_t __DLL api_get_block_crc_by_num(void *handle, uint8_t type,
					uint8_t block_num);

int __DLL api_set_data_len(void *handle, uint16_t data_len);
int __DLL api_write_enable_v6(void *handle, bool in_ap,
			      bool is_slave, uint32_t start, uint32_t end);
int __DLL api_write_data_v6(void *handle, int wlen);
int __DLL api_access_slave(void *handle, uint8_t id, uint8_t func, void *data);
int __DLL api_check_busy(void *handle, int timeout_ms, int delay_ms);
int __DLL api_write_enable_v3(void *handle, bool in_ap, bool write_ap,
			      uint32_t end, uint32_t checksum);
int __DLL api_write_data_v3(void *handle);

int __DLL api_to_bl_mode(void *handle, bool bl, uint32_t start, uint32_t end);

int __DLL api_write_data_m2v(void *handle, int wlen);
int __DLL api_to_bl_mode_m2v(void *handle, bool to_bl);

int __DLL api_set_idle(void *handle, bool enable);
int __DLL api_set_func_mode(void *handle, uint8_t mode);
int __DLL api_get_func_mode(void *handle);

int __DLL api_erase_data_v3(void *handle);

int __DLL api_read_flash(void *handle, uint8_t *buf,
			 uint32_t start_addr, uint32_t len);

int __DLL api_read_mp_result(void *handle, uint8_t *buf, int buf_size);


#ifdef __cplusplus
}
#endif

#endif
