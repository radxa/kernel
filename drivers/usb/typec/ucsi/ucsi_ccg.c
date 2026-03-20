// SPDX-License-Identifier: GPL-2.0
/*
 * UCSI driver for Cypress CCGx Type-C controller
 *
 * Copyright (C) 2017-2018 NVIDIA Corporation. All rights reserved.
 * Author: Ajay Gupta <ajayg@nvidia.com>
 *
 * Some code borrowed from drivers/usb/typec/ucsi/ucsi_acpi.c
 */
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/usb/typec_dp.h>
#include <linux/of_gpio.h>
#include <linux/sort.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/kernel.h>      /* min_t, simple_strtoul */
#include <linux/bitops.h>      /* BIT */
#include <linux/ctype.h>       /* isspace */

#include <asm/unaligned.h>
#include "ucsi.h"

enum enum_fw_mode {
	BOOT,   /* bootloader */
	FW1,    /* FW partition-1 (contains secondary fw) */
	FW2,    /* FW partition-2 (contains primary fw) */
	FW_INVALID,
};

/* HPIv2 core addresses */
#define HPI_ADDR_ENTER_FLASH      0x000A
#define HPI_ADDR_FLASH_RW_CMD     0x000C
#define HPI_ADDR_RESPONSE         0x007E
#define HPI_ADDR_FLASH_RW_MEM     0x0200
#define CCG_DEV_MODE_FWMODE_MASK 0x03
#define CCG_DEV_MODE_BOOT        0x00
#define CCG_DEV_MODE_FW1         0x01
#define CCG_DEV_MODE_FW2         0x02

/* Response codes (Device) */
#define HPI_RSP_NONE              0x00
#define HPI_RSP_SUCCESS           0x02
#define HPI_RSP_FLASH_DATA_AVAIL  0x03
#define HPI_RSP_INVALID_CMD       0x05
#define HPI_RSP_INVALID_STATE     0x06
#define HPI_RSP_FLASH_UPDATE_FAIL 0x07
#define HPI_RSP_INVALID_FW        0x08
#define HPI_RSP_INVALID_ARGS      0x09
#define HPI_RSP_NOT_SUPPORTED     0x0A
#define HPI_RSP_UNDEFINED_ERR     0x0F

/* INTR_REG bits */
#define INTR_DEV_INTR             BIT(0)

/* HPI register addresses used to refine write filtering */
#define HPI_ADDR_BOOT_LOADER_LAST_ROW   0x0004
#define HPI_ADDR_FIRMWARE_BIN_LOCATION  0x0028
/* HPIv2 device-specific registers (double-byte addressed) */
#define HPI_ADDR_HPI_VERSION        0x003C  /* 4 bytes: bit31 = Hybrid architecture */
#define HPI_ADDR_HPI_VERSION_EXT    0x0034  /* 4 bytes: variant info (optional) */

/* Flash row size and layout for CCG6DF_CFP/CCG6SF_CFP */
#define CCG_ROWS_TOTAL              512
#define CCG_ROW_SIZE                256
/* Metadata row indexes (not addresses) */
#define META_IDX_FW1                0x01FF  /* 511 */
#define META_IDX_FW2                0x01FE//  /* 512 */

/* CFP device constants (aliases) */
#define CCG_MD_FW1_IDX              META_IDX_FW1
#define CCG_MD_FW2_IDX              META_IDX_FW2

/* Device information and control */
#define HPI_ADDR_DEVICE_MODE              0x0000  /* DEVICE_MODE: 1 byte */
#define HPI_ADDR_BOOT_MODE_REASON         0x0001  /* BOOT_MODE_REASON: 1 byte */
#define HPI_ADDR_READ_SILICON_ID          0x0002  /* READ_SILICON_ID: 2 bytes */
#define HPI_ADDR_BOOT_LOADER_LAST_ROW     0x0004  /* BOOT_LOADER_LAST_ROW: 2 bytes */
#define HPI_ADDR_INTR_REG                 0x0006  /* INTR_REG: 1 byte */
#define HPI_ADDR_JUMP_TO_BOOT             0x0007  /* JUMP_TO_BOOT/JUMP_TO_ALT_FW: 1 byte */
#define HPI_ADDR_RESET                    0x0008  /* RESET: 2 bytes */
#define HPI_ADDR_ENTER_FLASHING_MODE      0x000A  /* ENTER_FLASHING_MODE: 1 byte */
#define HPI_ADDR_VALIDATE_FW              0x000B  /* VALIDATE_FW: 1 byte */
#define HPI_ADDR_FLASH_ROW_RW             0x000C  /* FLASH_ROW_READ_WRITE: 4 bytes */

/* Versions and layout */
#define HPI_ADDR_SLEEP_CTRL               0x002D  /* SLEEP_CTRL: 1 byte */
#define HPI_ADDR_POWER_STAT               0x002E  /* POWER_STAT: 1 byte */

/* Flash row read/write buffer (HPIv2 dedicated region) */
#define HPI_ADDR_FLASH_RW_MEM_BASE        0x0200  /* 0x0200–0x02FF used for one flash row */
#define HPI_FLASH_RW_MEM_SIZE             256     /* 256 bytes window (covers row size variants) */

#define HPI_SIG_JUMP_TO_BOOT		'J'	/* Write to HPI_ADDR_JUMP_TO_BOOT */
#define HPI_SIG_JUMP_TO_ALT_FW		'A'	/* HPIv2 only (same register) */
#define HPI_SIG_RESET			'R'	/* Byte 0 at HPI_ADDR_RESET */
#define HPI_SIG_ENTER_FLASHING		'P'	/* HPI_ADDR_ENTER_FLASHING_MODE */
#define HPI_SIG_FLASH_RW		'F'	/* Byte 0 at HPI_ADDR_FLASH_ROW_RW */

/* RESET types (Byte[1] to HPI_ADDR_RESET) */
#define HPI_RESET_TYPE_I2C                0x00
#define HPI_RESET_TYPE_DEVICE             0x01

/* FLASH_ROW_READ_WRITE commands (Byte[1] to HPI_ADDR_FLASH_ROW_RW) */
#define HPI_FLASH_CMD_WRITE               0x01

/* PDPORT_ENABLE bitmask */
#define HPI_PDPORT_EN_PORT0               0x01
#define HPI_PDPORT_EN_PORT1               0x02

#define HPI_RSP_FW_INVALID                0x08
#define HPI_RSP_INVALID_ARGUMENT          0x09

#define HPI_ADDR_PDPORT_ENABLE		0x002C
#define HPI_ADDR_FLASH_ROW_RW		0x000C
#define HPI_SIG_FLASH_RW		'F'
/* dm is HPI_ADDR_DEVICE_MODE byte */
#define HPI_DM_HPI_VERSION(dm)            (((dm) >> 7) & 0x01)  /* 0=HPIv1, 1=HPIv2 */
#define HPI_DM_ROW_SIZE(dm)               (((dm) >> 4) & 0x03)  /* 0=128, 1=256, 3=64 */
#define HPI_DM_NUM_PORTS(dm)              (((dm) >> 2) & 0x03)  /* 0=1 port, 1=2 ports */
#define HPI_DM_FW_MODE(dm)                ((dm) & 0x03)         /* 0=Boot, 1=FW1, 2=FW2 */

/* Optional module parameter to override firmware file for flashing */
static char fw_file_override[128];
module_param_string(ccg_fw_file, fw_file_override, sizeof(fw_file_override), 0644);
MODULE_PARM_DESC(ccg_fw_file, "Override CCG firmware file to flash (supports .cyacd or .cyacd2)");

#define CCGX_RAB_DEVICE_MODE			0x0000
#define CCGX_RAB_INTR_REG			0x0006
#define  DEV_INT				BIT(0)
#define  PORT0_INT				BIT(1)
#define  PORT1_INT				BIT(2)
#define  UCSI_READ_INT				BIT(7)
#define CCGX_RAB_JUMP_TO_BOOT			0x0007
#define  TO_BOOT				'J'
#define  TO_ALT_FW				'A'
#define CCGX_RAB_RESET_REQ			0x0008
#define  RESET_SIG				'R'
#define  CMD_RESET_I2C				0x0
#define  CMD_RESET_DEV				0x1
#define CCGX_RAB_ENTER_FLASHING			0x000A
#define  FLASH_ENTER_SIG			'P'
#define CCGX_RAB_VALIDATE_FW			0x000B
#define CCGX_RAB_FLASH_ROW_RW			0x000C
#define  FLASH_SIG				'F'
#define  FLASH_RD_CMD				0x0
#define  FLASH_WR_CMD				0x1
#define  FLASH_FWCT1_WR_CMD			0x2
#define  FLASH_FWCT2_WR_CMD			0x3
#define  FLASH_FWCT_SIG_WR_CMD			0x4
#define CCGX_RAB_READ_ALL_VER			0x0010
#define CCGX_RAB_READ_FW2_VER			0x0020
#define CCGX_RAB_UCSI_CONTROL			0x0039
#define CCGX_RAB_UCSI_CONTROL_START		BIT(0)
#define CCGX_RAB_UCSI_CONTROL_STOP		BIT(1)
#define CCGX_RAB_UCSI_DATA_BLOCK(offset)	(0xf000 | ((offset) & 0xff))
#define REG_FLASH_RW_MEM        0x0200
#define DEV_REG_IDX				CCGX_RAB_DEVICE_MODE
#define CCGX_RAB_PDPORT_ENABLE			0x002C
#define  PDPORT_1		BIT(0)
#define  PDPORT_2		BIT(1)
#define CCGX_RAB_RESPONSE			0x007E
#define  ASYNC_EVENT				BIT(7)

/* CCGx events & async msg codes */
#define RESET_COMPLETE		0x80
#define EVENT_INDEX		RESET_COMPLETE
#define PORT_CONNECT_DET	0x84
#define PORT_DISCONNECT_DET	0x85
#define ROLE_SWAP_COMPELETE	0x87

/* ccg firmware */
#define CYACD_LINE_SIZE         527
#define CCG4_ROW_SIZE           256
#define FW1_METADATA_ROW        0x1FF
#define FW2_METADATA_ROW        0x1FE
#define FW_CFG_TABLE_SIG_SIZE	256

static int secondary_fw_min_ver = 41;

enum enum_flash_mode {
	SECONDARY_BL,	/* update secondary using bootloader */
	PRIMARY,	/* update primary using secondary */
	SECONDARY,	/* update secondary using primary */
	FLASH_NOT_NEEDED,	/* update not required */
	FLASH_INVALID,
};

static const char * const ccg_fw_names[] = {
	"ccg_boot.cyacd",
	"ccg_primary.cyacd",
	"ccg_secondary.cyacd"
};

struct ccg_dev_info {
#define CCG_DEVINFO_FWMODE_SHIFT (0)
#define CCG_DEVINFO_FWMODE_MASK (0x3 << CCG_DEVINFO_FWMODE_SHIFT)
#define CCG_DEVINFO_PDPORTS_SHIFT (2)
#define CCG_DEVINFO_PDPORTS_MASK (0x3 << CCG_DEVINFO_PDPORTS_SHIFT)
	u8 mode;
	u8 bl_mode;
	__le16 silicon_id;
	__le16 bl_last_row;
} __packed;

struct version_format {
	__le16 build;
	u8 patch;
	u8 ver;
#define CCG_VERSION_PATCH(x) ((x) << 16)
#define CCG_VERSION(x)	((x) << 24)
#define CCG_VERSION_MIN_SHIFT (0)
#define CCG_VERSION_MIN_MASK (0xf << CCG_VERSION_MIN_SHIFT)
#define CCG_VERSION_MAJ_SHIFT (4)
#define CCG_VERSION_MAJ_MASK (0xf << CCG_VERSION_MAJ_SHIFT)
} __packed;

/*
 * Firmware version 3.1.10 or earlier, built for NVIDIA has known issue
 * of missing interrupt when a device is connected for runtime resume
 */
#define CCG_FW_BUILD_NVIDIA	(('n' << 8) | 'v')
#define CCG_OLD_FW_VERSION	(CCG_VERSION(0x31) | CCG_VERSION_PATCH(10))

/* Firmware for Tegra doesn't support UCSI ALT command, built
 * for NVIDIA has known issue of reporting wrong capability info
 */
#define CCG_FW_BUILD_NVIDIA_TEGRA	(('g' << 8) | 'n')

/* Altmode offset for NVIDIA Function Test Board (FTB) */
#define NVIDIA_FTB_DP_OFFSET	(2)
#define NVIDIA_FTB_DBG_OFFSET	(3)

struct version_info {
	struct version_format base;
	struct version_format app;
};

struct fw_config_table {
	u32 identity;
	u16 table_size;
	u8 fwct_version;
	u8 is_key_change;
	u8 guid[16];
	struct version_format base;
	struct version_format app;
	u8 primary_fw_digest[32];
	u32 key_exp_length;
	u8 key_modulus[256];
	u8 key_exp[4];
};

/* CCGx response codes */
enum ccg_resp_code {
	CMD_NO_RESP             = 0x00,
	CMD_SUCCESS             = 0x02,
	FLASH_DATA_AVAILABLE    = 0x03,
	CMD_INVALID             = 0x05,
	FLASH_UPDATE_FAIL       = 0x07,
	INVALID_FW              = 0x08,
	INVALID_ARG             = 0x09,
	CMD_NOT_SUPPORT         = 0x0A,
	TRANSACTION_FAIL        = 0x0C,
	PD_CMD_FAIL             = 0x0D,
	UNDEF_ERROR             = 0x0F,
	INVALID_RESP		= 0x10,
};

/* Simple container for a flash row */
struct ccg_row {
	u16 row;
	u16 len;
	u8  data[CCG_ROW_SIZE];
};

struct ccg_row_list {
	struct ccg_row *rows;
	int count;
	int capacity;
};

#define CCG_EVENT_MAX	(EVENT_INDEX + 43)

struct ccg_cmd {
	u16 reg;
	u32 data;
	int len;
	u32 delay; /* ms delay for cmd timeout  */
};

struct ccg_resp {
	u8 code;
	u8 length;
};

struct ucsi_ccg_altmode {
	u16 svid;
	u32 mid;
	u8 linked_idx;
	u8 active_idx;
#define UCSI_MULTI_DP_INDEX	(0xff)
	bool checked;
} __packed;

struct ucsi_ccg {
	struct device *dev;
	struct ucsi *ucsi;
	struct i2c_client *client;

	struct ccg_dev_info info;
	/* version info for boot, primary and secondary */
	struct version_info version[FW2 + 1];
	u32 fw_version;
	/* CCG HPI communication flags */
	unsigned long flags;
#define RESET_PENDING	0
#define DEV_CMD_PENDING	1
	struct ccg_resp dev_resp;
	u8 cmd_resp;
	int port_num;
	int irq;
	struct work_struct work;
	struct mutex lock; /* to sync between user and driver thread */

	/* fw build with vendor information */
	u16 fw_build;
	struct work_struct pm_work;

	struct completion complete;

	u64 last_cmd_sent;
	bool has_multiple_dp;
	struct ucsi_ccg_altmode orig[UCSI_MAX_ALTMODES];
	struct ucsi_ccg_altmode updated[UCSI_MAX_ALTMODES];
	struct gpio_desc *oc_gpiod;
	bool	force_once;
	bool	updating;
};

/* Minimal I2C helpers for 16-bit HPI addressing */
static int ccg_hpi_write16(struct ucsi_ccg *uc, u16 addr, const u8 *buf, size_t len)
{
	struct i2c_msg msg;
	int ret;
	u8 stack_buf[2 + CCG_ROW_SIZE];
	u8 *wbuf;

	if (len > sizeof(stack_buf) - 2)
		return -EINVAL;

	wbuf = stack_buf;
	wbuf[0] = (u8)(addr & 0xFF);        /* LSB first */
	wbuf[1] = (u8)((addr >> 8) & 0xFF); /* MSB */
	if (buf && len)
		memcpy(&wbuf[2], buf, len);

	msg.addr = uc->client->addr;
	msg.flags = 0;
	msg.len = 2 + len;
	msg.buf = wbuf;

	ret = i2c_transfer(uc->client->adapter, &msg, 1);
	return (ret == 1) ? 0 : (ret < 0 ? ret : -EIO);
}

static int ccg_hpi_read16(struct ucsi_ccg *uc, u16 addr, u8 *buf, size_t len)
{
	struct i2c_msg msgs[2];
	int ret;
	u8 addr_bytes[2] = { (u8)(addr & 0xFF), (u8)((addr >> 8) & 0xFF) };

	msgs[0].addr  = uc->client->addr;
	msgs[0].flags = 0;
	msgs[0].len   = 2;
	msgs[0].buf   = addr_bytes;

	msgs[1].addr  = uc->client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len   = len;
	msgs[1].buf   = buf;

	ret = i2c_transfer(uc->client->adapter, msgs, 2);
	return (ret == 2) ? 0 : (ret < 0 ? ret : -EIO);
}

/* Generic HPIv2 read with 16-bit address (small debug helper) */
static int ccg_i2c_read(struct ucsi_ccg *uc, u16 reg, u8 *buf, size_t len)
{
	struct i2c_client *client = uc->client;

	u8 addr_buf[2] = { reg & 0xFF, (reg >> 8) & 0xFF };
	struct i2c_msg msgs[2] = {
		{ .addr = client->addr, .flags = 0,        .len = 2,   .buf = addr_buf },
		{ .addr = client->addr, .flags = I2C_M_RD, .len = len, .buf = buf      },
	};
	int ret;

	if (!client)
		return -ENODEV;
	ret = i2c_transfer(client->adapter, msgs, 2);

	return (ret == 2) ? 0 : (ret < 0 ? ret : -EIO);
}

/* Convenience little-endian register readers */
static int ccg_read_u16(struct ucsi_ccg *uc, u16 reg, u16 *val)
{
	u8 b[2];
	int r = ccg_i2c_read(uc, reg, b, sizeof(b));

	if (r)
		return r;

	*val = (u16)b[0] | ((u16)b[1] << 8); return 0;
}

static int ccg_read_u32(struct ucsi_ccg *uc, u16 reg, u32 *val)
{
	u8 b[4];
	int r = ccg_i2c_read(uc, reg, b, sizeof(b));

	if (r)
		return r;

	*val = (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24);

	return 0;
}

/* Row size from DEVICE_MODE b5:b4 (Table 15) */
static u16 ccg_row_size_from_device_mode(u8 devmode)
{
	switch ((devmode >> 4) & 0x3) {
	case 0: return 128;
	case 1: return 256;
	case 3: return 64;
	default: return 256;
	}
}

/* Hybrid detection (optional) */
static bool ccg_is_hybrid(struct ucsi_ccg *uc)
{
	u8 ver[4];

	if (!ccg_hpi_read16(uc, HPI_ADDR_HPI_VERSION, ver, sizeof(ver))) {
		u32 v = ver[0] | (ver[1] << 8) | (ver[2] << 16) | (ver[3] << 24);

		return !!(v & BIT(31));
	}
	return false;
}

/* Read device mode (1 byte) */
static int ccg_read_device_mode(struct ucsi_ccg *uc, u8 *mode)
{
	return ccg_hpi_read16(uc, HPI_ADDR_DEVICE_MODE, mode, 1);
}

static int ccg_wait_ready_after_reset(struct ucsi_ccg *uc, unsigned int timeout_ms)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(timeout_ms);
	int ret;
	u8 dm;

	do {
		ret = ccg_read_device_mode(uc, &dm);
		if (!ret)
			return 0;
		msleep(25);
	} while (time_before(jiffies, deadline));
	return -ETIMEDOUT;
}

static int ccg_read(struct ucsi_ccg *uc, u16 rab, u8 *data, u32 len)
{
	struct i2c_client *client = uc->client;
	const struct i2c_adapter_quirks *quirks = client->adapter->quirks;
	unsigned char buf[2];
	struct i2c_msg msgs[] = {
		{
			.addr	= client->addr,
			.flags  = 0x0,
			.len	= sizeof(buf),
			.buf	= buf,
		},
		{
			.addr	= client->addr,
			.flags  = I2C_M_RD,
			.buf	= data,
		},
	};
	u32 rlen, rem_len = len, max_read_len = len;
	int status;

	/* check any max_read_len limitation on i2c adapter */
	if (quirks && quirks->max_read_len)
		max_read_len = quirks->max_read_len;

	pm_runtime_get_sync(uc->dev);
	while (rem_len > 0) {
		msgs[1].buf = &data[len - rem_len];
		rlen = min_t(u16, rem_len, max_read_len);
		msgs[1].len = rlen;
		put_unaligned_le16(rab, buf);
		status = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
		if (status < 0) {
			dev_err(uc->dev, "i2c_transfer failed %d\n", status);
			pm_runtime_put_sync(uc->dev);
			return status;
		}
		rab += rlen;
		rem_len -= rlen;
	}

	pm_runtime_put_sync(uc->dev);
	return 0;
}

static int ccg_write(struct ucsi_ccg *uc, u16 rab, const u8 *data, u32 len)
{
	struct i2c_client *client = uc->client;
	unsigned char *buf;
	struct i2c_msg msgs[] = {
		{
			.addr	= client->addr,
			.flags  = 0x0,
		}
	};
	int status;

	buf = kzalloc(len + sizeof(rab), GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	put_unaligned_le16(rab, buf);
	memcpy(buf + sizeof(rab), data, len);

	msgs[0].len = len + sizeof(rab);
	msgs[0].buf = buf;

	pm_runtime_get_sync(uc->dev);
	status = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (status < 0) {
		dev_err(uc->dev, "i2c_transfer failed %d\n", status);
		pm_runtime_put_sync(uc->dev);
		kfree(buf);
		return status;
	}

	pm_runtime_put_sync(uc->dev);
	kfree(buf);
	return 0;
}

/* Status read helper */
static int ccg_try_read_cmd_status(struct ucsi_ccg *uc, u8 *status)
{
	if (!status)
		return -EINVAL;

	return ccg_hpi_read16(uc, HPI_ADDR_RESPONSE, status, 1);
}

static inline int ccg_active_fw_from_device_mode(u8 devmode)
{
	switch (devmode & CCG_DEV_MODE_FWMODE_MASK) {
	case CCG_DEV_MODE_FW1: return 1;
	case CCG_DEV_MODE_FW2: return 2;
	default:               return -1;
	}
}

static int ucsi_ccg_init(struct ucsi_ccg *uc)
{
	unsigned int count = 10;
	u8 data;
	int status;

	data = CCGX_RAB_UCSI_CONTROL_STOP;
	status = ccg_write(uc, CCGX_RAB_UCSI_CONTROL, &data, sizeof(data));
	if (status < 0)
		return status;

	data = CCGX_RAB_UCSI_CONTROL_START;
	status = ccg_write(uc, CCGX_RAB_UCSI_CONTROL, &data, sizeof(data));
	if (status < 0)
		return status;

	/*
	 * Flush CCGx RESPONSE queue by acking interrupts. Above ucsi control
	 * register write will push response which must be cleared.
	 */
	do {
		status = ccg_read(uc, CCGX_RAB_INTR_REG, &data, sizeof(data));
		if (status < 0)
			return status;

		if (!(data & DEV_INT))
			return 0;

		status = ccg_write(uc, CCGX_RAB_INTR_REG, &data, sizeof(data));
		if (status < 0)
			return status;

		usleep_range(10000, 11000);
	} while (--count);

	return -ETIMEDOUT;
}

static void ucsi_ccg_update_get_current_cam_cmd(struct ucsi_ccg *uc, u8 *data)
{
	u8 cam, new_cam;

	cam = data[0];
	new_cam = uc->orig[cam].linked_idx;
	uc->updated[new_cam].active_idx = cam;
	data[0] = new_cam;
}

/* Must match struct ucsi_operations.update_altmodes signature */
static bool ucsi_ccg_update_altmodes(struct ucsi *ucsi,
				     struct ucsi_altmode *orig,
				     struct ucsi_altmode *updated)
{
	struct ucsi_ccg *uc = ucsi_get_drvdata(ucsi);
	struct ucsi_ccg_altmode *alt, *new_alt;
	int i, j, k = 0;
	bool found = false;

	alt = uc->orig;
	new_alt = uc->updated;
	memset(uc->updated, 0, sizeof(uc->updated));

	/*
	 * Copy original connector altmodes to new structure.
	 * We need this before second loop since second loop
	 * checks for duplicate altmodes.
	 */
	for (i = 0; i < UCSI_MAX_ALTMODES; i++) {
		alt[i].svid = orig[i].svid;
		alt[i].mid = orig[i].mid;
		if (!alt[i].svid)
			break;
	}

	for (i = 0; i < UCSI_MAX_ALTMODES; i++) {
		if (!alt[i].svid)
			break;

		/* already checked and considered */
		if (alt[i].checked)
			continue;

		if (!DP_CONF_GET_PIN_ASSIGN(alt[i].mid)) {
			/* Found Non DP altmode */
			new_alt[k].svid = alt[i].svid;
			new_alt[k].mid |= alt[i].mid;
			new_alt[k].linked_idx = i;
			alt[i].linked_idx = k;
			updated[k].svid = new_alt[k].svid;
			updated[k].mid = new_alt[k].mid;
			k++;
			continue;
		}

		for (j = i + 1; j < UCSI_MAX_ALTMODES; j++) {
			if (alt[i].svid != alt[j].svid ||
			    !DP_CONF_GET_PIN_ASSIGN(alt[j].mid)) {
				continue;
			} else {
				/* Found duplicate DP mode */
				new_alt[k].svid = alt[i].svid;
				new_alt[k].mid |= alt[i].mid | alt[j].mid;
				new_alt[k].linked_idx = UCSI_MULTI_DP_INDEX;
				alt[i].linked_idx = k;
				alt[j].linked_idx = k;
				alt[j].checked = true;
				found = true;
			}
		}
		if (found) {
			uc->has_multiple_dp = true;
		} else {
			/* Didn't find any duplicate DP altmode */
			new_alt[k].svid = alt[i].svid;
			new_alt[k].mid |= alt[i].mid;
			new_alt[k].linked_idx = i;
			alt[i].linked_idx = k;
		}
		updated[k].svid = new_alt[k].svid;
		updated[k].mid = new_alt[k].mid;
		k++;
	}
	return found;
}

static void ucsi_ccg_update_set_new_cam_cmd(struct ucsi_ccg *uc,
					    struct ucsi_connector *con,
					    u64 *cmd)
{
	struct ucsi_ccg_altmode *new_port, *port;
	struct typec_altmode *alt = NULL;
	u8 new_cam, cam, pin;
	bool enter_new_mode;
	int i, j, k = 0xff;

	port = uc->orig;
	new_cam = UCSI_SET_NEW_CAM_GET_AM(*cmd);
	if (new_cam >= ARRAY_SIZE(uc->updated))
		return;

	if (new_cam >= UCSI_MAX_ALTMODES)
		return;

	new_port = &uc->updated[new_cam];
	cam = new_port->linked_idx;
	enter_new_mode = UCSI_SET_NEW_CAM_ENTER(*cmd);

	/*
	 * If CAM is UCSI_MULTI_DP_INDEX then this is DP altmode
	 * with multiple DP mode. Find out CAM for best pin assignment
	 * among all DP mode. Priorite pin E->D->C after making sure
	 * the partner supports that pin.
	 */
	if (cam == UCSI_MULTI_DP_INDEX) {
		if (enter_new_mode) {
			for (i = 0; con->partner_altmode[i]; i++) {
				alt = con->partner_altmode[i];
				if (alt->svid == new_port->svid)
					break;
			}
			/*
			 * alt will always be non NULL since this is
			 * UCSI_SET_NEW_CAM command and so there will be
			 * at least one con->partner_altmode[i] with svid
			 * matching with new_port->svid.
			 */
			for (j = 0; port[j].svid; j++) {
				pin = DP_CONF_GET_PIN_ASSIGN(port[j].mid);
				if (alt && port[j].svid == alt->svid &&
				    (pin & DP_CONF_GET_PIN_ASSIGN(alt->vdo))) {
					/* prioritize pin E->D->C */
					if (k == 0xff || (k != 0xff && pin >
					    DP_CONF_GET_PIN_ASSIGN(port[k].mid))
					    ) {
						k = j;
					}
				}
			}
			cam = k;
			new_port->active_idx = cam;
		} else {
			cam = new_port->active_idx;
		}
	}
	*cmd &= ~UCSI_SET_NEW_CAM_AM_MASK;
	*cmd |= UCSI_SET_NEW_CAM_SET_AM(cam);
}

/*
 * Change the order of vdo values of NVIDIA test device FTB
 * (Function Test Board) which reports altmode list with vdo=0x3
 * first and then vdo=0x. Current logic to assign mode value is
 * based on order in altmode list and it causes a mismatch of CON
 * and SOP altmodes since NVIDIA GPU connector has order of vdo=0x1
 * first and then vdo=0x3
 */
static void ucsi_ccg_nvidia_altmode(struct ucsi_ccg *uc,
				    struct ucsi_altmode *alt)
{
	switch (UCSI_ALTMODE_OFFSET(uc->last_cmd_sent)) {
	case NVIDIA_FTB_DP_OFFSET:
		if (alt[0].mid == USB_TYPEC_NVIDIA_VLINK_DBG_VDO)
			alt[0].mid = USB_TYPEC_NVIDIA_VLINK_DP_VDO |
				     DP_CAP_DP_SIGNALLING(0) | DP_CAP_USB |
				     DP_CONF_SET_PIN_ASSIGN(BIT(DP_PIN_ASSIGN_E));
		break;
	case NVIDIA_FTB_DBG_OFFSET:
		if (alt[0].mid == USB_TYPEC_NVIDIA_VLINK_DP_VDO)
			alt[0].mid = USB_TYPEC_NVIDIA_VLINK_DBG_VDO;
		break;
	default:
		break;
	}
}

struct ucsi_altmode_desc {
	__le16 svid;     // Standard or Vendor ID
	__le16 mid;      // Mode ID (optional)
	__le32 vdo;      // VDO describing the mode
} __packed;

static int ucsi_ccg_get_altmode(struct ucsi *ucsi, u64 command, void *val, size_t len)
{
	struct ucsi_altmode_desc *desc = val;
	u8 offset = (command >> 24) & 0xFF;

	if (offset >= UCSI_MAX_ALTMODES)
		return -EINVAL;

	if (offset == 0) {
		desc->svid = cpu_to_le16(0xFF01);  // Example SVID
		desc->mid  = cpu_to_le16(1);       // Mode ID
		desc->vdo  = cpu_to_le32(0x12345678); // Example VDO
		return sizeof(*desc);
	}

	return 0;
}

static int ucsi_ccg_read(struct ucsi *ucsi, unsigned int offset,
			 void *val, size_t val_len)
{
	struct ucsi_ccg *uc = ucsi_get_drvdata(ucsi);
	u16 reg = CCGX_RAB_UCSI_DATA_BLOCK(offset);
	struct ucsi_capability *cap;
	struct ucsi_altmode *alt;
	int ret;

	if (READ_ONCE(uc->updating))
		return -EBUSY;

	ret = ccg_read(uc, reg, val, val_len);
	if (ret)
		return ret;

	if (offset != UCSI_MESSAGE_IN)
		return ret;

	switch (UCSI_COMMAND(uc->last_cmd_sent)) {
	case UCSI_GET_CURRENT_CAM:
		if (uc->has_multiple_dp)
			ucsi_ccg_update_get_current_cam_cmd(uc, (u8 *)val);
		break;
	case UCSI_GET_ALTERNATE_MODES:
		if (UCSI_ALTMODE_RECIPIENT(uc->last_cmd_sent) ==
		    UCSI_RECIPIENT_SOP) {
			alt = val;
			if (alt[0].svid == USB_TYPEC_NVIDIA_VLINK_SID)
				ucsi_ccg_nvidia_altmode(uc, alt);
			ucsi_ccg_get_altmode(ucsi, uc->last_cmd_sent, val, val_len);
		}
		break;
	case UCSI_GET_CAPABILITY:
		if (uc->fw_build == CCG_FW_BUILD_NVIDIA_TEGRA) {
			cap = val;
			cap->features &= ~UCSI_CAP_ALT_MODE_DETAILS;
		} else {
			cap = val;
			cap->features &= ~UCSI_CAP_PDO_DETAILS;
		}
		break;
	default:
		break;
	}
	uc->last_cmd_sent = 0;

	return ret;
}

static int ucsi_ccg_async_write(struct ucsi *ucsi, unsigned int offset,
				const void *val, size_t val_len)
{
	struct ucsi_ccg *uc = ucsi_get_drvdata(ucsi);
	u16 reg = CCGX_RAB_UCSI_DATA_BLOCK(offset);

	if (READ_ONCE(uc->updating))
		return -EBUSY;
	return ccg_write(ucsi_get_drvdata(ucsi), reg, val, val_len);
}

static int ucsi_ccg_sync_write(struct ucsi *ucsi, unsigned int offset,
			       const void *val, size_t val_len)
{
	struct ucsi_ccg *uc = ucsi_get_drvdata(ucsi);
	struct ucsi_connector *con;
	int con_index;
	int ret;

	if (READ_ONCE(uc->updating))
		return -EBUSY;
	mutex_lock(&uc->lock);
	pm_runtime_get_sync(uc->dev);
	set_bit(DEV_CMD_PENDING, &uc->flags);

	if (offset == UCSI_CONTROL && val_len == sizeof(uc->last_cmd_sent)) {
		uc->last_cmd_sent = *(u64 *)val;

		if (UCSI_COMMAND(uc->last_cmd_sent) == UCSI_SET_NEW_CAM &&
		    uc->has_multiple_dp) {
			con_index = (uc->last_cmd_sent >> 16) &
				    UCSI_CMD_CONNECTOR_MASK;
			if (con_index == 0) {
				ret = -EINVAL;
				goto err_clear_bit;
			}
			con = &uc->ucsi->connector[con_index - 1];
			ucsi_ccg_update_set_new_cam_cmd(uc, con, (u64 *)val);
		}
	}

	ret = ucsi_ccg_async_write(ucsi, offset, val, val_len);
	if (ret)
		goto err_clear_bit;

	if (!wait_for_completion_timeout(&uc->complete, msecs_to_jiffies(5000)))
		ret = -ETIMEDOUT;

err_clear_bit:
	clear_bit(DEV_CMD_PENDING, &uc->flags);
	pm_runtime_put_sync(uc->dev);
	mutex_unlock(&uc->lock);

	return ret;
}

static const struct ucsi_operations ucsi_ccg_ops = {
	.read = ucsi_ccg_read,
	.sync_write = ucsi_ccg_sync_write,
	.async_write = ucsi_ccg_async_write,
	.update_altmodes = ucsi_ccg_update_altmodes
};

static irqreturn_t ccg_irq_handler(int irq, void *data)
{
	u16 reg = CCGX_RAB_UCSI_DATA_BLOCK(UCSI_CCI);
	struct ucsi_ccg *uc = data;
	u8 intr_reg;
	u32 cci;
	int ret;

	ret = ccg_read(uc, CCGX_RAB_INTR_REG, &intr_reg, sizeof(intr_reg));
	if (ret)
		return ret;

	ret = ccg_read(uc, reg, (void *)&cci, sizeof(cci));
	if (ret)
		goto err_clear_irq;

	if (UCSI_CCI_CONNECTOR(cci))
		ucsi_connector_change(uc->ucsi, UCSI_CCI_CONNECTOR(cci));

	if (test_bit(DEV_CMD_PENDING, &uc->flags) &&
	    cci & (UCSI_CCI_ACK_COMPLETE | UCSI_CCI_COMMAND_COMPLETE))
		complete(&uc->complete);

err_clear_irq:
	ccg_write(uc, CCGX_RAB_INTR_REG, &intr_reg, sizeof(intr_reg));

	return IRQ_HANDLED;
}

static int ccg_request_irq(struct ucsi_ccg *uc)
{
	unsigned long flags = IRQF_ONESHOT;
	int ret;

	uc->oc_gpiod = devm_gpiod_get_optional(uc->dev, "pdi2c", GPIOD_IN);
	if (IS_ERR(uc->oc_gpiod)) {
		ret = PTR_ERR(uc->oc_gpiod);
		dev_err(uc->dev, "Error %d extracting OC gpio\n", ret);
	}

	if (uc->oc_gpiod) {
		uc->irq = gpiod_to_irq(uc->oc_gpiod);
		if (uc->irq < 0) {
			ret = uc->irq;
			dev_err(uc->dev, "Error %d extracting I2c IRQ\n", ret);
		}
	}

	if (!dev_fwnode(uc->dev))
		flags |= IRQF_TRIGGER_HIGH;

	return request_threaded_irq(uc->irq, NULL, ccg_irq_handler, flags, dev_name(uc->dev), uc);
}

static void ccg_pm_workaround_work(struct work_struct *pm_work)
{
	ccg_irq_handler(0, container_of(pm_work, struct ucsi_ccg, pm_work));
}

static int get_fw_info(struct ucsi_ccg *uc)
{
	int err;
	struct ccg_dev_info *info = &uc->info;

	err = ccg_read(uc, CCGX_RAB_READ_ALL_VER, (u8 *)(&uc->version),
		       sizeof(uc->version));
	if (err < 0)
		return err;

	uc->fw_version = CCG_VERSION(uc->version[FW2].app.ver) |
			CCG_VERSION_PATCH(uc->version[FW2].app.patch);

	err = ccg_read(uc, CCGX_RAB_DEVICE_MODE, (u8 *)(&uc->info),
		       sizeof(uc->info));
	if (err < 0)
		return err;

	dev_info(uc->dev, "ccg device info: fw_version:%u mode:%u bl_mode:%u silicon_id:0x%04x bl_last_row:0x%04x\n",
		 uc->fw_version, info->mode, info->bl_mode,
		 le16_to_cpu(info->silicon_id), le16_to_cpu(info->bl_last_row));
	return 0;
}

static inline bool invalid_async_evt(int code)
{
	return (code >= CCG_EVENT_MAX) || (code < EVENT_INDEX);
}

static void ccg_process_response(struct ucsi_ccg *uc)
{
	struct device *dev = uc->dev;

	if (uc->dev_resp.code & ASYNC_EVENT) {
		if (uc->dev_resp.code == RESET_COMPLETE) {
			if (test_bit(RESET_PENDING, &uc->flags))
				uc->cmd_resp = uc->dev_resp.code;
			get_fw_info(uc);
		}
		if (invalid_async_evt(uc->dev_resp.code))
			dev_err(dev, "invalid async evt %d\n",
				uc->dev_resp.code);
	} else {
		if (test_bit(DEV_CMD_PENDING, &uc->flags)) {
			uc->cmd_resp = uc->dev_resp.code;
			clear_bit(DEV_CMD_PENDING, &uc->flags);
		} else {
			dev_err(dev, "dev resp 0x%04x but no cmd pending\n",
				uc->dev_resp.code);
		}
	}
}

static int ccg_read_response(struct ucsi_ccg *uc)
{
	unsigned long target = jiffies + msecs_to_jiffies(1000);
	struct device *dev = uc->dev;
	u8 intval;
	int status;

	/* wait for interrupt status to get updated */
	do {
		status = ccg_read(uc, CCGX_RAB_INTR_REG, &intval,
				  sizeof(intval));
		if (status < 0)
			return status;

		if (intval & DEV_INT)
			break;
		usleep_range(500, 600);
	} while (time_is_after_jiffies(target));

	if (time_is_before_jiffies(target)) {
		dev_err(dev, "response timeout error\n");
		return -ETIME;
	}

	status = ccg_read(uc, CCGX_RAB_RESPONSE, (u8 *)&uc->dev_resp,
			  sizeof(uc->dev_resp));
	if (status < 0)
		return status;

	status = ccg_write(uc, CCGX_RAB_INTR_REG, &intval, sizeof(intval));
	if (status < 0)
		return status;

	return 0;
}

/* Caller must hold uc->lock */
static int ccg_send_command(struct ucsi_ccg *uc, struct ccg_cmd *cmd)
{
	struct device *dev = uc->dev;
	int ret;

	switch (cmd->reg & 0xF000) {
	case DEV_REG_IDX:
		set_bit(DEV_CMD_PENDING, &uc->flags);
		break;
	default:
		dev_err(dev, "invalid cmd register\n");
		break;
	}

	ret = ccg_write(uc, cmd->reg, (u8 *)&cmd->data, cmd->len);
	if (ret < 0)
		return ret;

	msleep(cmd->delay);

	ret = ccg_read_response(uc);
	if (ret < 0) {
		dev_err(dev, "response read error\n");
		switch (cmd->reg & 0xF000) {
		case DEV_REG_IDX:
			clear_bit(DEV_CMD_PENDING, &uc->flags);
			break;
		default:
			dev_err(dev, "invalid cmd register\n");
			break;
		}
		return -EIO;
	}
	ccg_process_response(uc);

	return uc->cmd_resp;
}

static int ccg_cmd_enter_flashing(struct ucsi_ccg *uc)
{
	u8 sig = HPI_SIG_ENTER_FLASHING;

	return ccg_hpi_write16(uc, HPI_ADDR_ENTER_FLASHING_MODE, &sig, 1);
}

static int ccg_cmd_jump_boot_mode(struct ucsi_ccg *uc, int bl_mode)
{
	struct ccg_cmd cmd;
	int ret;

	cmd.reg = CCGX_RAB_JUMP_TO_BOOT;

	if (bl_mode)
		cmd.data = TO_BOOT;
	else
		cmd.data = TO_ALT_FW;

	cmd.len = 1;
	cmd.delay = 100;

	mutex_lock(&uc->lock);

	set_bit(RESET_PENDING, &uc->flags);

	ret = ccg_send_command(uc, &cmd);
	if (ret != RESET_COMPLETE)
		goto err_clear_flag;

	ret = 0;

err_clear_flag:
	clear_bit(RESET_PENDING, &uc->flags);

	mutex_unlock(&uc->lock);

	return ret;
}

static inline int ccg_cmd_jump_to_boot(struct ucsi_ccg *uc)
{
	return ccg_cmd_jump_boot_mode(uc, 1); /* writes 'J' */
}

static inline int ccg_cmd_jump_to_alt_fw(struct ucsi_ccg *uc)
{
	return ccg_cmd_jump_boot_mode(uc, 0); /* writes 'A' */
}

#define CCG_FLASH_MEM_BASE   REG_FLASH_RW_MEM     /* 0x0200 */
#define CCG_I2C_CHUNK        32                   /* safe on SMBus-capable masters */

/* Row list helpers (standardized to CCG_ROW_SIZE) */
static int ccg_row_list_add(struct ccg_row_list *lst, u16 row, const u8 *data, u16 len)
{
	if (len != CCG_ROW_SIZE)
		return -EINVAL;
	if (lst->count == lst->capacity) {
		int newcap = lst->capacity ? lst->capacity * 2 : 64;
		struct ccg_row *nr = krealloc(lst->rows, newcap * sizeof(*nr), GFP_KERNEL);

		if (!nr)
			return -ENOMEM;
		lst->rows = nr;
		lst->capacity = newcap;
	}
	lst->rows[lst->count].row = row;
	lst->rows[lst->count].len = len;
	memcpy(lst->rows[lst->count].data, data, len);
	lst->count++;
	return 0;
}

static int ccg_row_cmp(const void *a, const void *b)
{
	const struct ccg_row *ra = a, *rb = b;

	if (ra->row < rb->row)
		return -1;
	if (ra->row > rb->row)
		return 1;

	return 0;
}

/* Read one flash row into buf */
static int ccg_cmd_read_flash_row(struct ucsi_ccg *uc, u16 row_idx, u8 *buf)
{
	int err; u8 rsp = 0;

	u8 rw_cmd[4] = { FLASH_SIG, FLASH_RD_CMD, (u8)(row_idx & 0xFF), (u8)(row_idx >> 8) };

	if (!buf)
		return -EINVAL;
	err = ccg_hpi_write16(uc, HPI_ADDR_FLASH_RW_CMD, rw_cmd, sizeof(rw_cmd));
	if (err)
		return err;
	for (int i = 0; i < 200; i++) {
		usleep_range(1000, 2000);
		if (!ccg_try_read_cmd_status(uc, &rsp) && rsp != 0x00)
			break;
	}
	if (rsp != HPI_RSP_FLASH_DATA_AVAIL && rsp != HPI_RSP_SUCCESS)
		return (rsp == 0x00) ? -ETIMEDOUT : -EIO;
	return ccg_hpi_read16(uc, HPI_ADDR_FLASH_RW_MEM, buf, CCG_ROW_SIZE);
}

/* Port control and reset via HPI (simple, non-ccg_send_command path) */
static int ccg_cmd_port_control(struct ucsi_ccg *uc, bool enable)
{
	u8 val = enable ? (HPI_PDPORT_EN_PORT0 | HPI_PDPORT_EN_PORT1) : 0x00;

	return ccg_hpi_write16(uc, HPI_ADDR_PDPORT_ENABLE, &val, 1);
}

static int ccg_cmd_reset(struct ucsi_ccg *uc)
{
	/* Write: Byte[0] = 'R', Byte[1] = 1 (Device Reset) to HPI_ADDR_RESET */
	u8 buf[2] = { HPI_SIG_RESET, HPI_RESET_TYPE_DEVICE };
	int ret = ccg_hpi_write16(uc, HPI_ADDR_RESET, buf, sizeof(buf));

	/* During reset, bus can NACK. Treat transport errors as acceptable here. */
	if (ret == -ENXIO || ret == -EREMOTEIO || ret == -EIO)
		return 0;
	return ret;
}

/* Enter flashing for Hybrid / Dual-FW devices:
 * - If not already in Boot (b1:b0 != 0), disable PD and jump to Boot.
 * - Then send 'P' (ENTER_FLASHING).
 * - Retry once if we race the reset and see 0x05/0x06.
 */
static int ccg_enter_flashing_hybrid(struct ucsi_ccg *uc)
{
	u8 dm = 0;
	int ret;

	ret = ccg_read_device_mode(uc, &dm);
	if (ret)
		return ret;

	if ((dm & 0x03) != 0x00) {
		/* Disable PD and jump to Boot/Secondary Base */
		ccg_cmd_port_control(uc, false);
		ret = ccg_cmd_jump_boot_mode(uc, 1); /* 'J' */
		if (ret)
			return ret;
		msleep(1000); /* Windows tool waits ~1s */
	}

	/* Try enter flashing */
	ret = ccg_cmd_enter_flashing(uc);
	if (ret == HPI_RSP_INVALID_CMD || ret == HPI_RSP_INVALID_STATE) {
		/* Give device a moment and retry once */
		msleep(100);
		ret = ccg_cmd_enter_flashing(uc);
	}
	if (ret)
		return ret;

	/* Optional: confirm we are in boot by reading DEVICE_MODE */
	if (ccg_read_device_mode(uc, &dm) == 0)
		dev_info(uc->dev, "enter flashing OK, DEVICE_MODE=0x%02x", dm);

	return 0;
}

static bool ccg_check_vendor_version(struct ucsi_ccg *uc,
				     struct version_format *app,
				     struct fw_config_table *fw_cfg)
{
	struct device *dev = uc->dev;

	/* Check if the fw build is for supported vendors */
	if (le16_to_cpu(app->build) != uc->fw_build) {
		dev_info(dev, "current fw is not from supported vendor\n");
		return false;
	}

	/* Check if the new fw build is for supported vendors */
	if (le16_to_cpu(fw_cfg->app.build) != uc->fw_build) {
		dev_info(dev, "new fw is not from supported vendor\n");
		return false;
	}
	return true;
}

static bool ccg_check_fw_version(struct ucsi_ccg *uc, const char *fw_name,
				 struct version_format *app)
{
	const struct firmware *fw = NULL;
	struct device *dev = uc->dev;
	struct fw_config_table fw_cfg;
	u32 cur_version, new_version;
	bool is_later = false;

	if (request_firmware(&fw, fw_name, dev) != 0) {
		dev_err(dev, "error: Failed to open cyacd file %s\n", fw_name);
		return false;
	}

	/*
	 * check if signed fw
	 * last part of fw image is fw cfg table and signature
	 */
	if (fw->size < sizeof(fw_cfg) + FW_CFG_TABLE_SIG_SIZE)
		goto out_release_firmware;

	memcpy((uint8_t *)&fw_cfg, fw->data + fw->size -
	       sizeof(fw_cfg) - FW_CFG_TABLE_SIG_SIZE, sizeof(fw_cfg));

	if (fw_cfg.identity != ('F' | 'W' << 8 | 'C' << 16 | 'T' << 24)) {
		dev_info(dev, "not a signed image\n");
		goto out_release_firmware;
	}

	/* compare input version with FWCT version */
	cur_version = le16_to_cpu(app->build) | CCG_VERSION_PATCH(app->patch) |
			CCG_VERSION(app->ver);

	new_version = le16_to_cpu(fw_cfg.app.build) |
			CCG_VERSION_PATCH(fw_cfg.app.patch) |
			CCG_VERSION(fw_cfg.app.ver);

	if (!ccg_check_vendor_version(uc, app, &fw_cfg))
		goto out_release_firmware;

	if (new_version > cur_version)
		is_later = true;

out_release_firmware:
	release_firmware(fw);
	return is_later;
}

static int ccg_fw_update_needed(struct ucsi_ccg *uc,
				enum enum_flash_mode *mode)
{
	struct device *dev = uc->dev;
	int err;
	struct version_info version[3];
	u8 mode_reg = 0;

	/* Force-path: choose FW2 flash once and update */
	if (uc->force_once) {
		uc->force_once = false; /* consume the force request */

		if (ccg_read_device_mode(uc, &mode_reg))
			dev_err(dev, "force: unable to read device mode\n");
		pr_err("force: DEVICE_MODE=0x%02x\n", mode_reg);

		*mode = SECONDARY;  /* flash FW2 */

		dev_warn(dev, "force: selected inactive bank mode=%d\n", *mode);
		return 0;
	}

	/* Normal path below (unchanged behavior) */
	err = ccg_read(uc, CCGX_RAB_DEVICE_MODE, (u8 *)(&uc->info),
		       sizeof(uc->info));
	if (err) {
		dev_err(dev, "read device mode failed\n");
		return err;
	}

	err = ccg_read(uc, CCGX_RAB_READ_ALL_VER, (u8 *)version,
		       sizeof(version));
	if (err) {
		dev_err(dev, "read device mode failed\n");
		return err;
	}

	if (memcmp(&version[FW1], "\0\0\0\0\0\0\0\0",
		   sizeof(struct version_info)) == 0) {
		dev_info(dev, "secondary fw is not flashed\n");
		*mode = SECONDARY_BL;
	} else if (le16_to_cpu(version[FW1].base.build) <
		secondary_fw_min_ver) {
		dev_info(dev, "secondary fw version is too low (< %d)\n",
			 secondary_fw_min_ver);
		*mode = SECONDARY;
	} else if (memcmp(&version[FW2], "\0\0\0\0\0\0\0\0",
		   sizeof(struct version_info)) == 0) {
		dev_info(dev, "primary fw is not flashed\n");
		*mode = PRIMARY;
	} else if (ccg_check_fw_version(uc, ccg_fw_names[PRIMARY],
		   &version[FW2].app)) {
		dev_info(dev, "found primary fw with later version\n");
		*mode = PRIMARY;
	} else {
		dev_info(dev, "secondary and primary fw are the latest\n");
		*mode = FLASH_NOT_NEEDED;
	}
	return 0;
}

/* Basic hex helpers, used by parsers */
static int hex_nibble(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return 10 + (c - 'a');
	if (c >= 'A' && c <= 'F')
		return 10 + (c - 'A');
	return -1;
}

static int parse_hex16(const char *s, u16 *out)
{
	int i, v, nib;

	v = 0;
	for (i = 0; i < 4; i++) {
		nib = hex_nibble(s[i]);
		if (nib < 0)
			return -EINVAL;
		v = (v << 4) | nib;
	}
	*out = (u16)v;
	return 0;
}

/* Robust APPINFO parse: optional + tolerant */
static void ccg_try_parse_appinfo(struct device *dev, const char *line,
				  u32 *start_addr, u32 *size_bytes)
{
	const char *p = strchr(line, ':');
	unsigned long start = 0, size = 0;
	char *endp;

	if (!p)
		return;
	p++;
	while (*p == ' ' || *p == '\t')
		p++;

	if (!strncasecmp(p, "0x", 2))
		p += 2;
	start = kstrtoul(p, &endp, 16);
	if (!endp || *endp != ',')
		return;

	p = endp + 1;
	while (*p == ' ' || *p == '\t')
		p++;
	if (!strncasecmp(p, "0x", 2))
		p += 2;
	size = kstrtoul(p, &endp, 16);
	if (!endp)
		return;

	*start_addr = (u32)start;
	*size_bytes = (u32)size;
	dev_dbg(dev, "cyacd2 APPINFO: start=0x%08x size=0x%08x\n", *start_addr, *size_bytes);
}

/* Parsed row container for cyacd/cyacd2 text */
struct ccg_row_text {
	u16 row_rel;  /* .cyacd2: relative row; .cyacd: absolute placed here */
	u16 bank;     /* .cyacd2: bank;          .cyacd: 0 */
	u8  data[CCG_ROW_SIZE];
};

struct ccg_row_text_list {
	struct ccg_row_text *rows;
	int count;
	int capacity;
};

static int ccg_row_text_list_add(struct ccg_row_text_list *lst, u16 row_rel,
				 u16 bank, const u8 *data)
{
	if (lst->count == lst->capacity) {
		int newcap = lst->capacity ? lst->capacity * 2 : 128;
		struct ccg_row_text *nr = krealloc(lst->rows, newcap * sizeof(*nr), GFP_KERNEL);

		if (!nr)
			return -ENOMEM;
		lst->rows = nr;
		lst->capacity = newcap;
	}
	lst->rows[lst->count].row_rel = row_rel;
	lst->rows[lst->count].bank    = bank;
	memcpy(lst->rows[lst->count].data, data, CCG_ROW_SIZE);
	lst->count++;
	return 0;
}

static inline u16 ccg_row_idx_from_rel(u16 row_rel, u16 bank, u16 rows_per_bank)
{
	return (u16)(row_rel + bank * rows_per_bank);
}

/* Detect .cyacd2 by filename */
static bool ccg_is_cyacd2_name(const char *name)
{
	const char *dot = strrchr(name, '.');

	return dot && !strcmp(dot, ".cyacd2");
}

/* Quick content probe: true if we see @APPINFO or lines starting with ':rrrrbbbb' */
static bool ccg_content_is_cyacd2_text(const u8 *buf, size_t sz)
{
	const u8 *p = buf, *end = buf + min_t(size_t, sz, 4096);

	while (p < end) {
		const u8 *nl = memchr(p, '\n', end - p);
		size_t len = nl ? (nl - p) : (end - p);

		if (len >= 10 && p[0] == ':') {
			int ok = 1;

			for (int i = 1; i < 9; i++) {
				if (hex_nibble(p[i]) < 0) {
					ok = 0;
					break;
				}
			}
			if (ok)
				return true;
		}
		if (len >= 8 && p[0] == '@') {
			if (!strncmp((const char *)p, "@APPINFO", 8))
				return true;
		}
		if (!nl)
			break;
		p = nl + 1;
	}
	return false;
}

/* Parse .cyacd2 ASCII text: @APPINFO optional; :rrrrbbbb<512 hex> rows */
static int ccg_parse_cyacd2_text(struct device *dev, const u8 *buf, size_t sz,
				 struct ccg_row_text_list *out,
				 u32 *app_start, u32 *app_size)
{
	const u8 *p = buf, *end = buf + sz;
	char line[1152];

	memset(out, 0, sizeof(*out));
	*app_start = 0;
	*app_size  = 0;

	while (p < end) {
		const u8 *nl = memchr(p, '\n', end - p);
		size_t len = nl ? (nl - p) : (end - p);
		size_t l = min_t(size_t, len, sizeof(line) - 1);

		if (l == 0) {
			p = nl ? nl + 1 : end;
			continue;
		}

		memcpy(line, p, l);
		line[l] = '\0';
		p = nl ? nl + 1 : end;
		if (l && (line[l - 1] == '\r'))
			line[--l] = '\0';

		/* Trim leading spaces */
		size_t s = 0;

		while (s < l && isspace(line[s]))
			s++;
		if (s >= l)
			continue;

		if (line[s] == '@') {
			if (!strncmp(&line[s], "@APPINFO", 8))
				ccg_try_parse_appinfo(dev, &line[s], app_start, app_size);
			continue;
		}

		if (line[s] == ':') {
			const size_t hdr_off     = s + 1;
			const size_t payload_off = s + 1 + 8;
			u16 addr16_lo = 0, addr16_hi = 0;
			u16 row_idx;
			int rc;

			if (hdr_off + 8 > l) {
				dev_err(dev, "cyacd2: short header line\n");
				return -EINVAL;
			}

			/* first 4 hex chars: low 16 bits (rrrr) */
			rc = parse_hex16(&line[hdr_off], &addr16_lo);
			if (rc)
				return rc;

			/* next 4 hex chars: high 16 bits (bbbb) */
			rc = parse_hex16(&line[hdr_off + 4], &addr16_hi);
			if (rc)
				return rc;

			/*
			 * MSB+LSB row mapping:
			 *   row index = addr16_lo + addr16_hi
			 *
			 * This yields:
			 *   :00670000 -> 0x0067
			 *   :006C0000 -> 0x006C
			 *   :00FF0000 -> 0x00FF
			 *   :00000100 -> 0x0100
			 *   :00010100 -> 0x0101
			 *   :00FE0100 -> 0x01FE
			 */
			row_idx = (u16)((u32)addr16_lo + (u32)addr16_hi);

			if ((l - payload_off) != (CCG_ROW_SIZE * 2)) {
				dev_err(dev, "cyacd2: payload not %dB (hex chars=%zu)\n",
					CCG_ROW_SIZE, l - payload_off);
				return -EINVAL;
			}

			u8 data[CCG_ROW_SIZE];

			for (int i = 0; i < CCG_ROW_SIZE; i++) {
				int hi = hex_nibble(line[payload_off + 2 * i]);
				int lo = hex_nibble(line[payload_off + 2 * i + 1]);

				if (hi < 0 || lo < 0)
					return -EINVAL;
				data[i] = (hi << 4) | lo;
			}

			/* Store computed row index, ignore bank (we use direct row indices) */
			rc = ccg_row_text_list_add(out, row_idx, 0, data);
			if (rc)
				return rc;
		}
	}

	return 0;
}

/* Parse legacy .cyacd ASCII text: ":rrrr<512 hex>" */
static int ccg_parse_cyacd_text(struct device *dev, const u8 *buf, size_t sz,
				struct ccg_row_text_list *out)
{
	const u8 *p = buf, *end = buf + sz;
	char line[1152];

	memset(out, 0, sizeof(*out));

	while (p < end) {
		const u8 *nl = memchr(p, '\n', end - p);
		size_t len = nl ? (nl - p) : (end - p);
		size_t l = min_t(size_t, len, sizeof(line) - 1);

		if (l == 0) {
			p = nl ? nl + 1 : end;
			continue;
		}
		memcpy(line, p, l);
		line[l] = '\0';
		p = nl ? nl + 1 : end;

		if (l && (line[l - 1] == '\r'))
			line[--l] = '\0';

		size_t s = 0;

		while (s < l && isspace(line[s]))
			s++;
		if (s >= l)
			continue;

		if (line[s] != ':')
			continue;

		if (s + 1 + 4 > l)
			continue;

		u16 row_abs = 0;

		if (parse_hex16(&line[s + 1], &row_abs))
			continue;

		const size_t payload_off = s + 1 + 4;

		if ((l - payload_off) != (CCG_ROW_SIZE * 2)) {
			dev_err(dev, "cyacd: payload not 256B at row=0x%04x (hex=%zu)\n",
				row_abs, l - payload_off);
			return -EINVAL;
		}

		u8 data[CCG_ROW_SIZE];

		for (int i = 0; i < CCG_ROW_SIZE; i++) {
			int hi = hex_nibble(line[payload_off + 2 * i]);
			int lo = hex_nibble(line[payload_off + 2 * i + 1]);

			if (hi < 0 || lo < 0)
				return -EINVAL;
			data[i] = (hi << 4) | lo;
		}

		if (ccg_row_text_list_add(out, row_abs, 0 /* bank=0 */, data))
			return -ENOMEM;
	}

	return 0;
}

/* Build absolute row list from parsed relative rows */
static int ccg_rows_to_absolute(const struct ccg_row_text_list *txt,
				struct ccg_row_list *abs_out)
{
	memset(abs_out, 0, sizeof(*abs_out));

	for (int i = 0; i < txt->count; i++) {
		if (ccg_row_list_add(abs_out,
				     txt->rows[i].row_rel,  /* direct row index */
				     txt->rows[i].data,
				     CCG_ROW_SIZE))
			return -ENOMEM;
	}

	return 0;
}

/* ===== New: unified parse entry to build absolute rows from firmware buffer ===== */
static int ccg_parse_and_build_rows(struct device *dev,
				    const u8 *buf, size_t sz,
				    u16 fw1_start, u16 fw2_start,
				    u16 rows_per_bank,
				    u8 target_bank,
				    struct ccg_row_list *abs)
{
	int rc;
	struct ccg_row_text_list txt = {0};

	if (!buf || !sz || !abs)
		return -EINVAL;

	/* Detect format by content: cyacd2 has @APPINFO or :rrrrbbbb header lines */
	if (ccg_content_is_cyacd2_text(buf, sz)) {
		u32 dummy_s = 0, dummy_l = 0;

		rc = ccg_parse_cyacd2_text(dev, buf, sz, &txt, &dummy_s, &dummy_l);
		if (rc) {
			dev_err(dev, "parse cyacd2 text failed (%d)\n", rc);
			return rc;
		}
		rc = ccg_rows_to_absolute(&txt, abs);
		kfree(txt.rows);
		return rc;
	}

	/* Legacy .cyacd: row numbers are absolute indices across whole flash */
	rc = ccg_parse_cyacd_text(dev, buf, sz, &txt);
	if (rc) {
		dev_err(dev, "parse cyacd text failed (%d)\n", rc);
		return rc;
	}
	memset(abs, 0, sizeof(*abs));
	for (int i = 0; i < txt.count; i++) {
		if (ccg_row_list_add(abs, txt.rows[i].row_rel, txt.rows[i].data, CCG_ROW_SIZE)) {
			kfree(txt.rows);
			return -ENOMEM;
		}
	}
	kfree(txt.rows);
	return 0;
}

static void ccg_log_device_mode(struct ucsi_ccg *uc, const char *tag)
{
	u8 dm = 0;

	if (ccg_read_device_mode(uc, &dm) == 0)
		dev_info(uc->dev, "%s: DEVICE_MODE=0x%02x", tag, dm);
}

static int ccg_boot_fallback(struct ucsi_ccg *uc)
{
	int rc;

	dev_warn(uc->dev, "row write refused; switching to Secondary Base");

	ccg_cmd_port_control(uc, false);
	msleep(10);

	/* Hybrid CCG6: jump to Secondary Base (JUMP_TO_BOOT) */
	rc = ccg_cmd_jump_boot_mode(uc, 1);
	if (rc)	{
		dev_err(uc->dev, "jump to Secondary Base failed (%d)", rc);
		return rc;
	}
	msleep(100);
	ccg_log_device_mode(uc, "after jump");

	rc = ccg_cmd_enter_flashing(uc);
	if (rc)	{
		dev_err(uc->dev, "enter flashing (boot) failed (%d)", rc);
		return rc;
	}

	return 0;
}

/* Pick firmware file based on mode (property override still wins) */
static const char *ccg_pick_fw_name(struct device *dev,
				    enum enum_flash_mode mode,
				    char *buf, size_t bufsz)
{
	const char *prop;

	if (!device_property_read_string(dev, "firmware-name", &prop) && prop && *prop) {
		strscpy(buf, prop, bufsz);
		return buf;
	}
	switch (mode) {
	case SECONDARY_BL:
	case SECONDARY: return "ccg_secondary.cyacd2";
	case PRIMARY:   return "ccg_primary.cyacd2";
	default:
		return "ccg_secondary.cyacd2";
	}
}

#include <linux/crc32.h>

/* Compute CRC32 over contiguous rows in span */
static u32 ccg_crc32_rows(const struct ccg_row_list *abs,
			  u16 base, u16 limit)
{
	u32 crc = ~0U;
	u16 expected = base;

	for (int i = 0; i < abs->count; i++) {
		u16 idx = abs->rows[i].row;

		if (idx < base || idx >= limit)
			continue;
		/* Require contiguous rows for CRC correctness */
		if (idx != expected)
			break;
		crc = crc32_le(crc, abs->rows[i].data, CCG_ROW_SIZE);
		expected++;
	}
	return crc ^ ~0U;
}

/* Optional metadata version macro; 0x0002 is typical for .cyacd2 */
#ifndef CCG_CYACD2_META_VERSION
#define CCG_CYACD2_META_VERSION 0x0002
#endif

static int ccg_cmd_validate_fw(struct ucsi_ccg *uc, unsigned int fwid)
{
	struct ccg_cmd cmd;
	int ret;

	cmd.reg = CCGX_RAB_VALIDATE_FW;
	cmd.data = fwid;
	cmd.len = 1;
	cmd.delay = 500;

	mutex_lock(&uc->lock);

	ret = ccg_send_command(uc, &cmd);

	mutex_unlock(&uc->lock);

	if (ret != CMD_SUCCESS)
		return ret;

	return 0;
}

/* ========================== Utility: APPINFO parser ========================== */

/* Parse @APPINFO line out of a cyacd2 text buffer: "@APPINFO:0x<start>,0x<size>"
 * Returns 0 on success, -ENOENT if not found or malformed.
 */
static int ccg_parse_appinfo_text(const u8 *data, size_t size, u32 *app_start, u32 *app_size)
{
	const char *p = (const char *)data;
	const char *end = p + size;
	const char *tag = "@APPINFO:";
	size_t taglen = strlen(tag);

	if (!data || !app_start || !app_size)
		return -EINVAL;

	while (p < end) {
		const char *nl = memchr(p, '\n', end - p);
		size_t linelen = nl ? (size_t)(nl - p) : (size_t)(end - p);

		if (linelen >= taglen && !memcmp(p, tag, taglen)) {
			/* Expect hex values like 0x700,0x43cc */
			u32 start = 0, sizeb = 0;
			/* Simple sscanf over a temporary zero-terminated buffer */
			char tmp[64];
			size_t copy = min(linelen, sizeof(tmp) - 1);

			memcpy(tmp, p, copy);
			tmp[copy] = '\0';
			if (sscanf(tmp, "@APPINFO:0x%x,0x%x", &start, &sizeb) == 2) {
				*app_start = start;
				*app_size  = sizeb;
				return 0;
			}
			break;
		}
		p = nl ? (nl + 1) : end;
	}
	return -ENOENT;
}

static int ccg_wait_success(struct ucsi_ccg *uc, unsigned int timeout_ms)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(timeout_ms);
	u8 status = 0xFF;

	do {
		(void)ccg_try_read_cmd_status(uc, &status);
		if (status == HPI_RSP_SUCCESS)
			return 0;
		usleep_range(2000, 4000);
	} while (time_before(jiffies, timeout));

	return -ETIMEDOUT;
}

/* Drain pending device responses and DEV_INTR once, to avoid stale rsp values. */
static void ccg_drain_responses(struct ucsi_ccg *uc)
{
	u8 intr = 0, resp = 0xFF;
	int lim = 32;

	while (lim--) {
		if (ccg_read(uc, HPI_ADDR_INTR_REG, &intr, sizeof(intr)))
			break;
		if (!(intr & BIT(0)))
			break;

		(void)ccg_read(uc, HPI_ADDR_RESPONSE, &resp, sizeof(resp));
		/* Ack DEV_INTR by writing 1 to bit0 if your device requires it */
		intr = BIT(0);
		(void)ccg_write(uc, HPI_ADDR_INTR_REG, &intr, sizeof(intr));

		if (resp == 0x00)
			break;
		usleep_range(1000, 2000);
	}
}

/* Disable PD ports and wait for Success (0x02). Adds diagnostics requested. */
static int ccg_disable_pd_ports(struct ucsi_ccg *uc)
{
	int err;
	u8 rsp = 0xFF, resp_before = 0xFF;

	/* DIAG: dump RESPONSE before disabling PD */
	(void)ccg_read(uc, HPI_ADDR_RESPONSE, &resp_before, sizeof(resp_before));
	dev_info(uc->dev, "diag: RESPONSE before PD disable: 0x%02x", resp_before);

	/* Clear stale responses first */
	ccg_drain_responses(uc);

	ccg_read(uc, HPI_ADDR_RESPONSE, &resp_before, sizeof(resp_before));
	dev_info(uc->dev, "diag: RESPONSE before PD disable: 0x%02x", resp_before);
	/* PDPORT_ENABLE = 0 */
	{
		u8 zero = 0x00;

		err = ccg_hpi_write16(uc, HPI_ADDR_PDPORT_ENABLE, &zero, 1);
		if (err) {
			dev_err(uc->dev, "PDPORT_ENABLE(0) write failed (%d)", err);
			return err;
		}
	}
	/* Poll for SUCCESS up to 1500 ms – DIAG print when we see it */
	{
		unsigned long end = jiffies + msecs_to_jiffies(1500);

		do {
			(void)ccg_try_read_cmd_status(uc, &rsp);
			if (rsp == HPI_RSP_SUCCESS) {
				dev_info(uc->dev, "diag: PD disable rsp=0x%02x (SUCCESS)", rsp);
				return 0;
			}
			usleep_range(3000, 5000);
		} while (time_before(jiffies, end));
	}

	if (rsp == HPI_RSP_SUCCESS) {
		dev_info(uc->dev, "diag: RESPONSE after PD disable rsp=0x%02x (SUCCESS)", rsp);
		return 0;
	}
	dev_err(uc->dev, "PDPORT_ENABLE(0) timeout rsp=0x%02x", rsp);
	return -ETIMEDOUT;
}

static int ccg_wait_async_event(struct ucsi_ccg *uc,
				u8 *resp0, u8 *resp1,
				unsigned int timeout_ms)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(timeout_ms);
	u8 intr;
	u8 rsp2[2];
	int ret;

	if (!resp0 || !resp1)
		return -EINVAL;

	*resp0 = 0;
	*resp1 = 0;

	do {
		/* Check interrupt register */
		ret = ccg_read(uc, HPI_ADDR_INTR_REG, &intr, sizeof(intr));
		if (ret) {
			dev_err(uc->dev, "read INTR_REG failed %d\n", ret);
			return ret;
		}

		if (intr & INTR_DEV_INTR) {
			/* Read 2-byte response */
			ret = ccg_read(uc, HPI_ADDR_RESPONSE, rsp2, sizeof(rsp2));
			if (ret) {
				dev_err(uc->dev, "read RESPONSE failed %d\n", ret);
				return ret;
			}

			*resp0 = rsp2[0];
			*resp1 = rsp2[1];

			/* Ack interrupt */
			intr = INTR_DEV_INTR;
			ret = ccg_write(uc, HPI_ADDR_INTR_REG, &intr, sizeof(intr));
			if (ret) {
				dev_err(uc->dev,
					"write INTR_REG (ack) failed %d\n", ret);
				return ret;
			}

			/* Interpret status per HPI spec */
			if (*resp0 == 0x02 || *resp0 == 0x03) {
				/* 0x02 = Success, 0x03 = Flash Data Available */
				return 0;
			}

			/* Anything else is an error code, propagate it */
			dev_err(uc->dev, "HPI error: resp0=0x%02x resp1=0x%02x\n",
				*resp0, *resp1);

			/* Optional: map some codes specially */
			switch (*resp0) {
			case 0x05: return -EINVAL;  /* Invalid Command */
			case 0x06: return -EIO;     /* Invalid State */
			case 0x07: return -EIO;     /* Flash Update Failed */
			case 0x08: return -EFAULT;  /* Invalid FW */
			case 0x09: return -EINVAL;  /* Invalid Arguments */
			case 0x0A: return -EOPNOTSUPP; /* Not Supported */
			default:
				return -EIO;
			}
		}

		usleep_range(2000, 4000);
	} while (time_before(jiffies, deadline));

	dev_err(uc->dev, "timeout waiting for async event\n");
	return -ETIMEDOUT;
}

static inline int ccg_wait_success_or_error(struct ucsi_ccg *uc,
					    const char *tag,
					    unsigned int timeout_ms)
{
	u8 ev0 = 0, ev1 = 0;
	int ret = ccg_wait_async_event(uc, &ev0, &ev1, timeout_ms);

	if (!ret) {
		dev_info(uc->dev,
			 "diag: %s: success (resp0=0x%02x resp1=0x%02x)\n",
			 tag ? tag : "async", ev0, ev1);
		return 0;
	}

	if (ret > 0) {
		/* HPI_RSP_* code returned */
		dev_err(uc->dev,
			"diag: %s: HPI error ret=0x%02x (resp0=0x%02x resp1=0x%02x)\n",
			tag ? tag : "async", ret, ev0, ev1);
	} else {
		dev_err(uc->dev,
			"diag: %s: transport/timeout err=%d (resp0=0x%02x resp1=0x%02x)\n",
			tag ? tag : "async", ret, ev0, ev1);
	}

	return ret;
}

/* Helper: wait for async event with context message, but allow non-fatal failure */
static void ccg_wait_and_log_async(struct ucsi_ccg *uc,
				   const char *tag,
				   unsigned int timeout_ms)
{
	u8 ev0 = 0, ev1 = 0;
	int ret;

	ret = ccg_wait_async_event(uc, &ev0, &ev1, timeout_ms);
	if (ret) {
		dev_warn(uc->dev,
			 "diag: %s: no async event (ret=%d)\n",
			 tag ? tag : "async", ret);
	} else {
		dev_info(uc->dev,
			 "diag: %s: async resp0=0x%02x resp1=0x%02x\n",
			 tag ? tag : "async", ev0, ev1);
	}
}

static int ccg_cmd_write_flash_row(struct ucsi_ccg *uc, u16 row_idx,
				   const void *data, u8 flash_cmd)
{
	int ret;
	u8 cmd[4] = {
		HPI_SIG_FLASH_RW,               /* 'F' */
		flash_cmd,                      /* 0x01 = write row */
		(u8)(row_idx & 0xFF),
		(u8)((row_idx >> 8) & 0xFF),
	};
	u8 rsp0 = 0;

	if (!data)
		return -EINVAL;

	/* 13. Copy data into FLASH_RW_MEM window */
	ret = ccg_hpi_write16(uc, HPI_ADDR_FLASH_RW_MEM_BASE,
			      data, HPI_FLASH_RW_MEM_SIZE);
	if (ret) {
		dev_err(uc->dev,
			"flash row 0x%04x: write data window failed (%d)",
			row_idx, ret);
		return ret;
	}

	/* 13. Trigger write */
	ret = ccg_hpi_write16(uc, HPI_ADDR_FLASH_ROW_RW,
			      cmd, sizeof(cmd));
	if (ret) {
		dev_err(uc->dev,
			"flash row 0x%04x: trigger write failed (%d)",
			row_idx, ret);
		return ret;
	}

	return 0;
}

static int ccg_enter_flashing_robust(struct ucsi_ccg *uc, u8 jump_sig)
{
	int err;
	u8 devmode = 0;
	u8 sig;

	/* 1. Check device mode */
	ccg_read_device_mode(uc, &devmode);
	dev_info(uc->dev,
		 "diag: ENTER ccg_enter_flashing_robust, DEVICE_MODE=0x%02x",
		 devmode);

	/* 3. Disable PD ports */
	err = ccg_disable_pd_ports(uc);
	if (err) {
		dev_err(uc->dev, "disable PD failed: %d", err);
		return err;
	}

	/* Optional: clear/consume any pending async after PD disable */
	ccg_wait_success_or_error(uc, "after PD disable", 200);

	/* 5. Jump to boot/alt based on target bank.
	 * For now we use 'A' (alternate) as existing code.
	 * Later you can pass jump char from do_flash: 'J' for primary, 'A' for secondary.
	 */
	ccg_drain_responses(uc);
	dev_info(uc->dev,
		 "diag: JUMP_TO_BOOT for bank switch needed:(%d)",
		 jump_sig);

	err = ccg_cmd_jump_boot_mode(uc, 1);
	if (err)
		dev_err(uc->dev, "JUMP_TO_BOOT write failed (%d)", err);
	/* 6. Wait for async event for JUMP_TO_BOOT (RESET_COMPLETE etc.) */
	ccg_wait_success_or_error(uc, "after JUMP_TO_BOOT", 1000);

	/* 8. Re-read DEVICE_MODE */
	ccg_read_device_mode(uc, &devmode);
	dev_info(uc->dev,
		 "diag: after JUMP_TO_BOOT, DEVICE_MODE=0x%02x",
		 devmode);

	/* Even if devmode still looks like app, we proceed; you can add
	 * a strict check and fail here if needed.
	 */

	/* 9. Initiate flashing: write 'P' to ENTER_FLASHING_MODE (0x000A) */
	ccg_drain_responses(uc);
	sig = HPI_SIG_ENTER_FLASHING; /* 'P' */
	dev_info(uc->dev, "diag: issuing ENTER_FLASHING (post-jump)");

	err = ccg_hpi_write16(uc, HPI_ADDR_ENTER_FLASHING_MODE, &sig, 1);
	if (err) {
		dev_err(uc->dev, "ENTER_FLASHING write failed (%d)", err);
		return err;
	}

	/* 10. Wait async for ENTER_FLASHING completion */
	ccg_wait_success_or_error(uc, "after ENTER_FLASHING", 1000);

	return 0;
}

static void ccg_dump_first_rows(struct device *dev,
				const struct ccg_row_text_list *lst,
				int rows_per_bank, bool is_cyacd2)
{
	int n = min(lst->count, 3);

	for (int i = 0; i < n; i++) {
		u16 row_abs = is_cyacd2
			? (lst->rows[i].row_rel + lst->rows[i].bank * rows_per_bank)
			: lst->rows[i].row_rel;
		dev_err(dev,
			"parsed[%d]: row_rel=0x%04x bank=%u abs=0x%04x data=%02x %02x %02x %02x\n",
			i, lst->rows[i].row_rel, lst->rows[i].bank, row_abs,
			lst->rows[i].data[0], lst->rows[i].data[1],
			lst->rows[i].data[2], lst->rows[i].data[3]);
	}
}

static bool rows_look_absolute(const struct ccg_row_list *list, u16 fw2_start)
{
	int i;

	for (i = 0; i < list->count; i++) {
		u16 idx = list->rows[i].row;

		if (idx >= fw2_start)
			return true;
	}
	return false;
}

static void remap_rows_to_bank(struct ccg_row_list *list, u16 bank_base)
{
	int i;

	for (i = 0; i < list->count; i++)
		list->rows[i].row = bank_base + list->rows[i].row;
}

static int do_flash(struct ucsi_ccg *uc, enum enum_flash_mode mode)
{
	struct device *dev = uc->dev;
	const struct firmware *fw = NULL;
	const char *fwname = NULL;
	u8 devmode = 0;
	u16 bl_last = 0;
	u32 bin_loc = 0;
	u16 fw1_start = 0, fw2_start = 0;
	u16 rows_per_bank = 0;
	int err = 0;

	struct ccg_row_list abs = {0};
	u8 target_bank;
	u16 base, limit, meta_idx;
	int i;

	err = ccg_read_device_mode(uc, &devmode);
	if (err) {
		dev_err(dev, "read DEVICE_MODE failed (%d)", err);
		return err;
	}
	err = ccg_read_u16(uc, HPI_ADDR_BOOT_LOADER_LAST_ROW, &bl_last);
	if (err) {
		dev_err(dev, "read BL_LAST_ROW failed (%d)", err);
		return err;
	}
	err = ccg_read_u32(uc, HPI_ADDR_FIRMWARE_BIN_LOCATION, &bin_loc);
	if (err) {
		dev_err(dev, "read FIRMWARE_BIN_LOCATION failed (%d)", err);
		return err;
	}

	{
		const bool hybrid = ccg_is_hybrid(uc);
		u32 loc = 0;
		u16 fw1 = 0, fw2 = 0;

		if (!ccg_read_u32(uc, HPI_ADDR_FIRMWARE_BIN_LOCATION, &loc)) {
			fw1 = (u16)(loc & 0xFFFF);
			fw2 = (u16)((loc >> 16) & 0xFFFF);
		}

		pr_err("Ak: hybrid:%d\n", hybrid);
		if (hybrid) {
			fw1_start     = max_t(u16, (u16)(bl_last + 1), 2);
			rows_per_bank = (u16)(CCG_ROWS_TOTAL / 2);
			fw2_start     = (u16)(fw1_start + rows_per_bank);
		} else if (fw2 > fw1 && fw2 < CCG_ROWS_TOTAL) {
			fw1_start     = fw1;
			fw2_start     = fw2;
			rows_per_bank = (u16)(fw2 - fw1);
		} else {
			fw1_start     = max_t(u16, (u16)(bl_last + 1), 2);
			rows_per_bank = (u16)(CCG_ROWS_TOTAL / 2);
			fw2_start     = (u16)(fw1_start + rows_per_bank);
		}
	}

	dev_info(dev, "DEVICE_MODE=0x%02x row_size=%u BL_LAST=0x%04x FW1_START=%u FW2_START=%u rows_per_bank=%u",
		 devmode, CCG_ROW_SIZE, bl_last, fw1_start, fw2_start, rows_per_bank);

	{
		if (fw_file_override[0])
			fwname = fw_file_override;
		else
			fwname = "ccg_secondary.cyacd2";

		dev_info(dev, "requesting firmware: %s", fwname);
		err = request_firmware(&fw, fwname, dev);
		if (err) {
			dev_err(dev, "request_firmware(%s) failed (%d)", fwname, err);
			return err;
		}

		err = ccg_parse_and_build_rows(dev, fw->data, fw->size,
					       fw1_start, fw2_start,
					       rows_per_bank, target_bank,
					       &abs);
		if (err) {
			dev_err(dev, "parse rows failed (%d)", err);
			release_firmware(fw);
			return err;
		}

		/* --- 3. Parse Firmware File --- */
		dev_info(dev, "Successfully parsed %d rows from binary firmware.", abs.count);

		for (i = 0; i < min(abs.count, 256); i++) {
			dev_info(dev,
				 "abs[%d]: row=0x%04x first4=%02x %02x %02x %02x\n",
				 i, abs.rows[i].row,
				 abs.rows[i].data[0], abs.rows[i].data[1],
				 abs.rows[i].data[2], abs.rows[i].data[3]);
		}

		/* Optional APPINFO logging only */
		{
			u32 app_start_bytes = 0, app_size_bytes = 0;

			if (!ccg_parse_appinfo_text(fw->data, fw->size,
						    &app_start_bytes, &app_size_bytes))
				dev_info(dev, "@APPINFO: start=0x%x size=0x%x",
					 app_start_bytes, app_size_bytes);
		}

		err = ccg_enter_flashing_robust(uc, 1);
		if (err) {
			dev_err(dev, "enter flashing failed (%d)", err);
			kfree(abs.rows);
			release_firmware(fw);
			WRITE_ONCE(uc->updating, false);
			return err;
		}

		base  = (target_bank == 0) ? fw1_start : fw2_start;
		limit = base + rows_per_bank;
		meta_idx = (target_bank == 1) ? META_IDX_FW1 : META_IDX_FW2;

		dev_info(dev, "target span: base=%u limit=%u BL_LAST=0x%04x", base, limit, bl_last);
		dev_info(dev, "target metadata idx=0x%04x", meta_idx);
		dev_info(dev, "target metadata abs.count:%d", abs.count);

		/* Clear metadata row early */
		{
			u8 zero[CCG_ROW_SIZE] = {0};

			int err = ccg_cmd_write_flash_row(uc, meta_idx, zero, FLASH_WR_CMD);

			if (err)
				dev_err(dev, "Write to row 0x%04x failed (%d)", meta_idx, err);

			ccg_wait_success_or_error(uc, "after metadata clear", 200);
		}

		/* Find metadata row in parsed image */
		{
			int rows_written = 0;

			for (i = 0; i < abs.count; i++) {
				u16 row_idx = abs.rows[i].row;

				/* Optional: keep some safety filters */
				if (row_idx <= bl_last)          /* don’t touch bootloader rows */
					continue;
				if (row_idx >= CCG_ROWS_TOTAL)   /* outside flash range */
					continue;

				err = ccg_cmd_write_flash_row(uc, row_idx,
							      abs.rows[i].data,
							      HPI_FLASH_CMD_WRITE);
				if (err) {
					dev_err(dev, "Write to row 0x%04x failed (%d)",
						row_idx, err);
					/* Continue on error to allow for protected row rejection */
					continue;
				}
				rows_written++;
			}

			ccg_wait_success_or_error(uc, "after data write", 200);
			dev_info(dev, "total %d rows flashed (including metadata) target bank:%d",
				 rows_written, target_bank);
		}

		/* Optional: sanity readback using direct row indices from abs[] */
		{
			int max_read = min(abs.count, 256); /* limit log spam */

			for (i = 0; i < max_read; i++) {
				u16 row_idx = abs.rows[i].row;
				u8 rb[4] = {0};

				/* Same safety filters as write, if you want them */
				if (row_idx <= bl_last)
					continue;
				if (row_idx >= CCG_ROWS_TOTAL)
					continue;

				err = ccg_cmd_read_flash_row(uc, row_idx, rb);
				if (!err) {
					dev_info(dev, "readback row 0x%04x first4=%02x %02x %02x %02x",
						 row_idx, rb[0], rb[1], rb[2], rb[3]);
				} else {
					dev_err(dev, "readback row 0x%04x failed (%d)",
						row_idx, err);
				}
			}
		}

		/* --- ADD THIS SNIPPET to verify specific rows --- */
		dev_info(dev, "--- Verifying specific rows ---");
		{
			u8 temp_row_buf[CCG_ROW_SIZE] = {0};
			u16 rows_to_check[] = {
				0x0067, /* row from :00670000 */
				0x006C,  /* row from :006C0000 */
				0x00FF,  /* row from :00FF0000 */
				0x0100,  /* row from :00000100 */
				0x0101,  /* row from :00010100 */
				0x01FE,  /* row from :00FE0100 */
			};

			for (i = 0; i < ARRAY_SIZE(rows_to_check); i++) {
				u16 row_to_read = rows_to_check[i];

				err = ccg_cmd_read_flash_row(uc, row_to_read, temp_row_buf);
				if (!err) {
					dev_info(dev, "readback row 0x%04x first16: %*phN",
						 row_to_read, 16, temp_row_buf);
				} else {
					dev_err(dev, "readback row 0x%04x failed (%d)",
						row_to_read, err);
				}
			}
		}

		// Now, call VALIDATE_FW. The device will find the metadata row you just wrote.
		{
			u8 validate_id = 0x01; // Should be 0x01 for primary

			err = ccg_cmd_validate_fw(uc, validate_id);
			ccg_wait_success_or_error(uc, "after VALIDATE_FW", 1000);
		}

		{
			u8 md2[CCG_ROW_SIZE] = {0};

			ccg_cmd_read_flash_row(uc, META_IDX_FW2, md2);
			dev_info(dev, "FW2 meta[14..17] seq=0x%02x%02x%02x%02x, valid=0x%02x%02x, crc=0x%02x%02x%02x%02x",
				 md2[0x14], md2[0x15], md2[0x16], md2[0x17],
				 md2[0x56], md2[0x57],
				 md2[0x58], md2[0x59], md2[0x5A], md2[0x5B]);

			// Read FW1 metadata
			u8 md1[CCG_ROW_SIZE] = {0};

			ccg_cmd_read_flash_row(uc, META_IDX_FW1, md1);
			dev_info(dev, "FW1 meta[14..17] seq=0x%02x%02x%02x%02x, valid=0x%02x%02x, crc=0x%02x%02x%02x%02x",
				 md1[0x14], md1[0x15], md1[0x16], md1[0x17],
				 md1[0x56], md1[0x57],
				 md1[0x58], md1[0x59], md1[0x5A], md1[0x5B]);
		}

		dev_info(dev, "diag: issuing RESET after VALIDATE_FW");
		err = ccg_cmd_reset(uc);
		dev_info(dev, "diag: ccg_cmd_reset() returned %d", err);
		ccg_wait_success_or_error(uc, "after RESET", 200);

		{
			int j;
			u8 dm2 = 0;
			int dm_err;

			msleep(300);

			for (j = 0; j < 5; j++) {
				dm_err = ccg_read(uc, CCGX_RAB_DEVICE_MODE, &dm2, sizeof(dm2));
				if (!dm_err) {
					dev_info(dev,
						 "diag: post-reset DEVICE_MODE=0x%02x (read ok)",
						 dm2);
					break;
				}
				dev_info(dev,
					 "diag: post-reset DEVICE_MODE read failed (%d), retry %d",
					 dm_err, j + 1);
				msleep(100);
			}
		}

		kfree(abs.rows);
		release_firmware(fw);
	}
	return 0;
}

/*******************************************************************************
 * CCG4 has two copies of the firmware in addition to the bootloader.
 * If the device is running FW1, FW2 can be updated with the new version.
 * Dual firmware mode allows the CCG device to stay in a PD contract and support
 * USB PD and Type-C functionality while a firmware update is in progress.
 ******************************************************************************/
static int ccg_fw_update(struct ucsi_ccg *uc, enum enum_flash_mode flash_mode)
{
	int err = 0;
	bool forced = false;

	/* detect the one-shot forced modes */
	if (uc->force_once &&
	    (flash_mode == PRIMARY || flash_mode == SECONDARY))
		forced = true;

	while (flash_mode != FLASH_NOT_NEEDED) {
		WRITE_ONCE(uc->updating, true);
		err = do_flash(uc, flash_mode);
		WRITE_ONCE(uc->updating, false);
		if (err < 0)
			return err;
	}
	dev_info(uc->dev, "CCG FW update successful\n");

	return err;
}

static int ccg_restart(struct ucsi_ccg *uc)
{
	struct device *dev = uc->dev;
	int status;

	status = ucsi_ccg_init(uc);
	if (status < 0) {
		dev_err(dev, "ucsi_ccg_start fail, err=%d\n", status);
		return status;
	}

	status = ccg_request_irq(uc);
	if (status < 0) {
		dev_err(dev, "request_threaded_irq failed - %d\n", status);
		return status;
	}

	status = ucsi_register(uc->ucsi);
	if (status) {
		dev_err(uc->dev, "failed to register the interface\n");
		return status;
	}

	pm_runtime_enable(uc->dev);
	return 0;
}

static void ccg_update_firmware(struct work_struct *work)
{
	struct ucsi_ccg *uc = container_of(work, struct ucsi_ccg, work);
	enum enum_flash_mode flash_mode;
	int status;

	status = ccg_fw_update_needed(uc, &flash_mode);
	if (status < 0)
		return;

	if (flash_mode != FLASH_NOT_NEEDED) {
		ucsi_unregister(uc->ucsi);
		pm_runtime_disable(uc->dev);
		free_irq(uc->irq, uc);

		ccg_fw_update(uc, flash_mode);
		/* After detecting we just reset, or on first init after flash: */
		status = ccg_wait_ready_after_reset(uc, 400); /* tolerate re-validate + BootWait */
		dev_err(uc->dev, "ccg_wait_ready_after_reset status:%d\n", status);

		ccg_restart(uc);
	}
}

static ssize_t do_flash_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t n)
{
	struct ucsi_ccg *uc = i2c_get_clientdata(to_i2c_client(dev));
	bool flash;

	/* Accept "1"/"true" for normal update; "force" for forced inactive update */
	if (sysfs_streq(buf, "force")) {
		uc->force_once = true;
	} else {
		if (kstrtobool(buf, &flash))
			return -EINVAL;
		if (!flash)
			return n;
	}

	if (uc->fw_build == 0x0)
		dev_warn(dev, "proceeding with FW flash without vendor fw_build tag\n");

	schedule_work(&uc->work);
	return n;
}

static DEVICE_ATTR_WO(do_flash);

static struct attribute *ucsi_ccg_attrs[] = {
	&dev_attr_do_flash.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ucsi_ccg);

static int ucsi_ccg_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ucsi_ccg *uc;
	const char *fw_name;
	int status;

	uc = devm_kzalloc(dev, sizeof(*uc), GFP_KERNEL);
	if (!uc)
		return -ENOMEM;

	uc->dev = dev;
	uc->client = client;
	uc->irq = client->irq;
	mutex_init(&uc->lock);
	init_completion(&uc->complete);
	INIT_WORK(&uc->work, ccg_update_firmware);
	INIT_WORK(&uc->pm_work, ccg_pm_workaround_work);

	/* Only fail FW flashing when FW build information is not provided */
	status = device_property_read_string(dev, "firmware-name", &fw_name);
	if (!status) {
		if (!strcmp(fw_name, "nvidia,jetson-agx-xavier"))
			uc->fw_build = CCG_FW_BUILD_NVIDIA_TEGRA;
		else if (!strcmp(fw_name, "nvidia,gpu"))
			uc->fw_build = CCG_FW_BUILD_NVIDIA;
		if (!uc->fw_build)
			dev_err(uc->dev, "failed to get FW build information\n");
	}

	/* reset ccg device and initialize ucsi */
	status = ucsi_ccg_init(uc);
	if (status < 0) {
		dev_err(uc->dev, "ucsi_ccg_init failed - %d\n", status);
		return status;
	}

	status = get_fw_info(uc);
	if (status < 0) {
		dev_err(uc->dev, "get_fw_info failed - %d\n", status);
		return status;
	}

	uc->port_num = 1;

	if (uc->info.mode & CCG_DEVINFO_PDPORTS_MASK)
		uc->port_num++;

	uc->ucsi = ucsi_create(dev, &ucsi_ccg_ops);
	if (IS_ERR(uc->ucsi))
		return PTR_ERR(uc->ucsi);

	ucsi_set_drvdata(uc->ucsi, uc);

	status = ccg_request_irq(uc);
	if (status < 0) {
		dev_err(uc->dev, "request_threaded_irq failed - %d\n", status);
		goto out_ucsi_destroy;
	}

	dev_info(uc->dev, "uc->fw_version:%d\n", uc->fw_version);
	if (uc->fw_version) {
		status = ucsi_register(uc->ucsi);
		if (status)
			goto out_free_irq;
	}

	i2c_set_clientdata(client, uc);

	if (uc->fw_version) {
		pm_runtime_set_active(uc->dev);
		pm_runtime_enable(uc->dev);
		pm_runtime_use_autosuspend(uc->dev);
		pm_runtime_set_autosuspend_delay(uc->dev, 5000);
		pm_runtime_idle(uc->dev);
	}

	return 0;

out_free_irq:
	free_irq(uc->irq, uc);
out_ucsi_destroy:
	ucsi_destroy(uc->ucsi);

	return status;
}

static void ucsi_ccg_remove(struct i2c_client *client)
{
	struct ucsi_ccg *uc = i2c_get_clientdata(client);

	cancel_work_sync(&uc->pm_work);
	cancel_work_sync(&uc->work);
	pm_runtime_disable(uc->dev);
	ucsi_unregister(uc->ucsi);
	ucsi_destroy(uc->ucsi);
	free_irq(uc->irq, uc);
}

static const struct of_device_id ucsi_ccg_of_match_table[] = {
		{ .compatible = "cypress,cypd4226", },
		{ .compatible = "cypress,cypd6129", },
		{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ucsi_ccg_of_match_table);

static const struct i2c_device_id ucsi_ccg_device_id[] = {
	{"ccgx-ucsi", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, ucsi_ccg_device_id);

static const struct acpi_device_id amd_i2c_ucsi_match[] = {
	{"AMDI0042"},
	{}
};
MODULE_DEVICE_TABLE(acpi, amd_i2c_ucsi_match);

static int ucsi_ccg_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ucsi_ccg *uc = i2c_get_clientdata(client);

	return ucsi_resume(uc->ucsi);
}

static int ucsi_ccg_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(client->irq);
	else
		disable_irq(client->irq);

	return 0;
}

static int ucsi_ccg_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ucsi_ccg *uc = i2c_get_clientdata(client);

	if (device_may_wakeup(dev))
		disable_irq_wake(client->irq);
	else
		enable_irq(client->irq);

	/*
	 * Firmware version 3.1.10 or earlier, built for NVIDIA has known issue
	 * of missing interrupt when a device is connected for runtime resume.
	 * Schedule a work to call ISR as a workaround.
	 */
	schedule_work(&uc->pm_work);

	return 0;
}

static const struct dev_pm_ops ucsi_ccg_pm = {
	.resume = ucsi_ccg_resume,
	.runtime_suspend = ucsi_ccg_runtime_suspend,
	.runtime_resume = ucsi_ccg_runtime_resume,
};

static struct i2c_driver ucsi_ccg_driver = {
	.driver = {
		.name = "ucsi_ccg",
		.pm = &ucsi_ccg_pm,
		.dev_groups = ucsi_ccg_groups,
		.acpi_match_table = amd_i2c_ucsi_match,
		.of_match_table = ucsi_ccg_of_match_table,
	},
	.probe = ucsi_ccg_probe,
	.remove = ucsi_ccg_remove,
	.id_table = ucsi_ccg_device_id,
};

module_i2c_driver(ucsi_ccg_driver);

MODULE_AUTHOR("Ajay Gupta <ajayg@nvidia.com>");
MODULE_DESCRIPTION("UCSI driver for Cypress CCGx Type-C controller");
MODULE_LICENSE("GPL v2");
