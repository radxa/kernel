// SPDX-License-Identifier: GPL-2.0
/*
 * Aeonsemi AS21XXxX PHY Driver
 *
 * Author: Christian Marangi <ansuelsmth@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/firmware.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy.h>
#include <linux/property.h>

#define VEND1_CHIP_CTRL			0x2
#define   VEND1_CHIP_CTRL_XFI_ACCESS	BIT(15)
#define   VEND1_CHIP_CTRL_SXGMII_MODE	BIT(13)

#define VEND1_GLB_REG_CPU_RESET_ADDR_LO_BASEADDR 0x3
#define VEND1_GLB_REG_CPU_RESET_ADDR_HI_BASEADDR 0x4

#define VEND1_GLB_REG_CPU_CTRL		0xe
#define   VEND1_GLB_CPU_CTRL_MASK	GENMASK(4, 0)
#define   VEND1_GLB_CPU_CTRL_LED_POLARITY_MASK GENMASK(12, 8)
#define   VEND1_GLB_CPU_CTRL_LED_POLARITY(_n) FIELD_PREP(VEND1_GLB_CPU_CTRL_LED_POLARITY_MASK, \
							 BIT(_n))

#define VEND1_FW_START_ADDR		0x100
#define AS22XXX_AN_STATES1		0x8005
#define AS22XXX_AN_STATES1_ARB_MASK	GENMASK(15, 12)
#define AS22XXX_LINK_GOOD		9

#define VEND1_GLB_REG_MDIO_INDIRECT_ADDRCMD 0x101
#define VEND1_GLB_REG_MDIO_INDIRECT_LOAD 0x102

#define VEND1_GLB_REG_MDIO_INDIRECT_STATUS 0x103

#define VEND1_PTP_CLK			0x142
#define   VEND1_PTP_CLK_EN		BIT(6)

/* 5 LED at step of 0x20
 * FE: Fast-Ethernet (10/100)
 * GE: Gigabit-Ethernet (1000)
 * NG: New-Generation (2500/5000/10000)
 */
#define VEND1_LED_REG(_n)		(0x1800 + ((_n) * 0x10))
#define   VEND1_LED_REG_A_EVENT		GENMASK(15, 11)
#define VEND1_LED_CONF			0x1881
#define   VEND1_LED_CONFG_BLINK		GENMASK(7, 0)

#define VEND1_SPEED_STATUS		0x4002
#define   VEND1_SPEED_MASK		GENMASK(7, 0)
#define   VEND1_SPEED_10000		FIELD_PREP_CONST(VEND1_SPEED_MASK, 0x3)
#define   VEND1_SPEED_5000		FIELD_PREP_CONST(VEND1_SPEED_MASK, 0x5)
#define   VEND1_SPEED_2500		FIELD_PREP_CONST(VEND1_SPEED_MASK, 0x9)
#define   VEND1_SPEED_1000		FIELD_PREP_CONST(VEND1_SPEED_MASK, 0x10)
#define   VEND1_SPEED_100		FIELD_PREP_CONST(VEND1_SPEED_MASK, 0x20)
#define   VEND1_SPEED_10		FIELD_PREP_CONST(VEND1_SPEED_MASK, 0x0)

#define VEND1_IPC_CMD			0x5801
#define   AEON_IPC_CMD_PARITY		BIT(15)
#define   AEON_IPC_CMD_SIZE		GENMASK(10, 6)
#define   AEON_IPC_CMD_OPCODE		GENMASK(5, 0)

#define IPC_CMD_NOOP			0x0  /* Do nothing */
#define IPC_CMD_INFO			0x1  /* Get Firmware Version */
#define IPC_CMD_SYS_CPU			0x2  /* SYS_CPU */
#define IPC_CMD_BULK_DATA		0xa  /* Pass bulk data in ipc registers. */
#define IPC_CMD_BULK_WRITE		0xc  /* Write bulk data to memory */
#define IPC_CMD_CFG_PARAM		0x1a /* Write config parameters to memory */
#define IPC_CMD_NG_TESTMODE		0x1b /* Set NG test mode and tone */
#define IPC_CMD_TEMP_MON		0x15 /* Temperature monitoring function */
#define IPC_CMD_SET_LED			0x23 /* Set led */

#define IPC_OPCODE_DBGCMD		0x16
#define IPC_OPCODE_POLL			0x17
#define IPC_OPCODE_WBUF			0x18
#define IPC_OPCODE_RBUF			0x19

#define IPC_DBGCMD_DPC			0x8b

#define IPC_DBGCMD_SDS			0x96

#define IPC_SDS_RST			26

#define IPC_DPC_SDS_SET_CFG		0x2
#define IPC_DPC_SDS_GET_CFG		0x3
#define IPC_DPC_ETH_STS_HW_UPD_CFG	0x8
#define IPC_DPC_FC_SET			0xd

#define AEON_SDS_PCS_SEL_NORMAL		0
#define AEON_SDS_PCS_SEL_64B66B		1
#define AEON_SDS_SPD_1G			0
#define AEON_SDS_SPD_10G		3

#define AEON_CU_AN_TOP_SPD_10M	0x02
#define AEON_CU_AN_TOP_SPD_100M	0x04
#define AEON_CU_AN_TOP_SPD_1G	0x08
#define AEON_CU_AN_TOP_SPD_2500M	0x10
#define AEON_CU_AN_TOP_SPD_5G	0x20
#define AEON_CU_AN_TOP_SPD_10G	0x40

#define AEON_DPC_MAX_POLL		100
#define AEON_DPC_RBUF_MAX		144

#define VEND1_IPC_STS			0x5802
#define   AEON_IPC_STS_PARITY		BIT(15)
#define   AEON_IPC_STS_SIZE		GENMASK(14, 10)
#define   AEON_IPC_STS_OPCODE		GENMASK(9, 4)
#define   AEON_IPC_STS_STATUS		GENMASK(3, 0)
#define   AEON_IPC_STS_STATUS_RCVD	FIELD_PREP_CONST(AEON_IPC_STS_STATUS, 0x1)
#define   AEON_IPC_STS_STATUS_PROCESS	FIELD_PREP_CONST(AEON_IPC_STS_STATUS, 0x2)
#define   AEON_IPC_STS_STATUS_SUCCESS	FIELD_PREP_CONST(AEON_IPC_STS_STATUS, 0x4)
#define   AEON_IPC_STS_STATUS_ERROR	FIELD_PREP_CONST(AEON_IPC_STS_STATUS, 0x8)
#define   AEON_IPC_STS_STATUS_BUSY	FIELD_PREP_CONST(AEON_IPC_STS_STATUS, 0xe)
#define   AEON_IPC_STS_STATUS_READY	FIELD_PREP_CONST(AEON_IPC_STS_STATUS, 0xf)

#define VEND1_IPC_DATA0			0x5808
#define VEND1_IPC_DATA1			0x5809
#define VEND1_IPC_DATA2			0x580a
#define VEND1_IPC_DATA3			0x580b
#define VEND1_IPC_DATA4			0x580c
#define VEND1_IPC_DATA5			0x580d
#define VEND1_IPC_DATA6			0x580e
#define VEND1_IPC_DATA7			0x580f
#define VEND1_IPC_DATA(_n)		(VEND1_IPC_DATA0 + (_n))

/* Sub command of CMD_INFO */
#define IPC_INFO_VERSION		0x1

/* Sub command of CMD_SYS_CPU */
#define IPC_SYS_CPU_REBOOT		0x3
#define IPC_SYS_CPU_IMAGE_OFST		0x4
#define IPC_SYS_CPU_IMAGE_CHECK		0x5
#define IPC_SYS_CPU_PHY_ENABLE		0x6

/* Sub command of CMD_CFG_PARAM */
#define IPC_CFG_PARAM_DIRECT		0x4

/* CFG DIRECT sub command */
#define IPC_CFG_PARAM_DIRECT_NG_PHYCTRL	0x1
#define IPC_CFG_PARAM_DIRECT_CU_AN	0x2
#define IPC_CMD_CFG_CU_AN_RESTART	0xa
#define IPC_CMD_CFG_CU_AN_TOP_SPD	0xc
#define IPC_CFG_PARAM_DIRECT_SDS_PCS	0x3
#define IPC_CFG_PARAM_DIRECT_AUTO_EEE	0x4
#define IPC_CFG_PARAM_DIRECT_SDS_PMA	0x5
#define IPC_CFG_PARAM_DIRECT_DPC_RA	0x6
#define IPC_CFG_PARAM_DIRECT_DPC_PKT_CHK 0x7
#define IPC_CFG_PARAM_DIRECT_DPC_SDS_WAIT_ETH 0x8
#define IPC_CFG_PARAM_DIRECT_WDT	0x9
#define IPC_CFG_PARAM_DIRECT_SDS_RESTART_AN 0x10
#define IPC_CFG_PARAM_DIRECT_TEMP_MON	0x11
#define IPC_CFG_PARAM_DIRECT_WOL	0x12

/* Sub command of CMD_TEMP_MON */
#define IPC_CMD_TEMP_MON_GET		0x4

#define AS21XXX_MDIO_AN_C22		0xffe0

#define AS22XXX_SDS_MII_ADV_LINK		BIT(15)
#define AS22XXX_SDS_MII_ADV_ACK		BIT(14)
#define AS22XXX_SDS_MII_ADV_REMOTE_FAULT	BIT(13)
#define AS22XXX_SDS_MII_ADV_FULL		BIT(12)
#define AS22XXX_SDS_MII_ADV_SPEED		GENMASK(11, 10)
#define AS22XXX_SDS_MII_ADV_EEE		BIT(9)
#define AS22XXX_SDS_MII_ADV_EEE_CLK_STOP	BIT(8)
#define AS22XXX_SDS_MII_ADV_RESERVED_7		BIT(7)
#define AS22XXX_SDS_MII_ADV_SELECTOR		BIT(0)
#define AS22XXX_SDS_MII_ADV_CONTROL_MASK	(AS22XXX_SDS_MII_ADV_LINK | \
						 AS22XXX_SDS_MII_ADV_ACK | \
						 AS22XXX_SDS_MII_ADV_REMOTE_FAULT | \
						 AS22XXX_SDS_MII_ADV_FULL | \
						 AS22XXX_SDS_MII_ADV_SPEED | \
						 AS22XXX_SDS_MII_ADV_RESERVED_7 | \
						 AS22XXX_SDS_MII_ADV_SELECTOR)

#define AS22XXX_SDS_PCS_STS20		0x0020
#define   AS22XXX_SDS_PCS_LINK_UP	BIT(12)
#define   AS22XXX_SDS_PCS_HIGH_BER	BIT(1)
#define   AS22XXX_SDS_PCS_BLOCK_LOCK	BIT(0)
#define AS22XXX_SDS_READY_POLL_US	100000
#define AS22XXX_SDS_READY_TIMEOUT_US	10000000

#define PHY_ID_AS21XXX			0x75009410
/* AS21xxx ID Legend
 * AS21x1xxB1
 *     ^ ^^
 *     | |J: Supports SyncE/PTP
 *     | |P: No SyncE/PTP support
 *     | 1: Supports 2nd Serdes
 *     | 2: Not 2nd Serdes support
 *     0: 10G, 5G, 2.5G
 *     5: 5G, 2.5G
 *     2: 2.5G
 */
#define PHY_ID_AS21011JB1		0x75009402
#define PHY_ID_AS21011PB1		0x75009412
#define PHY_ID_AS21010JB1		0x75009422
#define PHY_ID_AS21010PB1		0x75009432
#define PHY_ID_AS21511JB1		0x75009442
#define PHY_ID_AS21511PB1		0x75009452
#define PHY_ID_AS21510JB1		0x75009462
#define PHY_ID_AS21510PB1		0x75009472
#define PHY_ID_AS21210JB1		0x75009482
#define PHY_ID_AS21210PB1		0x75009492
#define PHY_ID_AS22XXX			0x750094a1
#define PHY_VENDOR_AEONSEMI		0x75009400

#define AEON_MAX_LEDS			5
#define AEON_IPC_DELAY			10000
#define AEON_IPC_TIMEOUT		(AEON_IPC_DELAY * 100)
#define AEON_IPC_DATA_NUM_REGISTERS	8
#define AEON_IPC_DATA_MAX		(AEON_IPC_DATA_NUM_REGISTERS * sizeof(u16))

#define AEON_BOOT_ADDR			0x1000
#define AEON_CPU_BOOT_ADDR		0x2000
#define AEON_CPU_CTRL_FW_LOAD		(BIT(4) | BIT(2) | BIT(1) | BIT(0))
#define AEON_CPU_CTRL_FW_START		BIT(0)

enum as21xxx_led_event {
	VEND1_LED_REG_A_EVENT_ON_10 = 0x0,
	VEND1_LED_REG_A_EVENT_ON_100,
	VEND1_LED_REG_A_EVENT_ON_1000,
	VEND1_LED_REG_A_EVENT_ON_2500,
	VEND1_LED_REG_A_EVENT_ON_5000,
	VEND1_LED_REG_A_EVENT_ON_10000,
	VEND1_LED_REG_A_EVENT_ON_FE_GE,
	VEND1_LED_REG_A_EVENT_ON_NG,
	VEND1_LED_REG_A_EVENT_ON_FULL_DUPLEX,
	VEND1_LED_REG_A_EVENT_ON_COLLISION,
	VEND1_LED_REG_A_EVENT_BLINK_TX,
	VEND1_LED_REG_A_EVENT_BLINK_RX,
	VEND1_LED_REG_A_EVENT_BLINK_ACT,
	VEND1_LED_REG_A_EVENT_ON_LINK,
	VEND1_LED_REG_A_EVENT_ON_LINK_BLINK_ACT,
	VEND1_LED_REG_A_EVENT_ON_LINK_BLINK_RX,
	VEND1_LED_REG_A_EVENT_ON_FE_GE_BLINK_ACT,
	VEND1_LED_REG_A_EVENT_ON_NG_BLINK_ACT,
	VEND1_LED_REG_A_EVENT_ON_NG_BLINK_FE_GE,
	VEND1_LED_REG_A_EVENT_ON_FD_BLINK_COLLISION,
	VEND1_LED_REG_A_EVENT_ON,
	VEND1_LED_REG_A_EVENT_OFF,
};

enum as22xxx_system_sync_state {
	AS22XXX_SYSTEM_SYNC_IDLE,
	AS22XXX_SYSTEM_SYNC_NEEDS_PHY_ENABLE,
	AS22XXX_SYSTEM_SYNC_WAIT_PHY_ENABLE,
};

struct as21xxx_led_pattern_info {
	unsigned int pattern;
	u16 val;
};

struct as21xxx_priv {
	bool parity_status;
	bool mode_switch;
	/* Protect concurrent IPC access */
	struct mutex ipc_lock;

	struct mutex sds_lock;

	phy_interface_t sds_interface;
	int sds_speed;

	enum as22xxx_system_sync_state system_sync_state;
};

static struct as21xxx_led_pattern_info as21xxx_led_supported_pattern[] = {
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_10),
		.val = VEND1_LED_REG_A_EVENT_ON_10
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_100),
		.val = VEND1_LED_REG_A_EVENT_ON_100
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_1000),
		.val = VEND1_LED_REG_A_EVENT_ON_1000
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_2500),
		.val = VEND1_LED_REG_A_EVENT_ON_2500
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_5000),
		.val = VEND1_LED_REG_A_EVENT_ON_5000
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_10000),
		.val = VEND1_LED_REG_A_EVENT_ON_10000
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK),
		.val = VEND1_LED_REG_A_EVENT_ON_LINK
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_10) |
			   BIT(TRIGGER_NETDEV_LINK_100) |
			   BIT(TRIGGER_NETDEV_LINK_1000),
		.val = VEND1_LED_REG_A_EVENT_ON_FE_GE
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_2500) |
			   BIT(TRIGGER_NETDEV_LINK_5000) |
			   BIT(TRIGGER_NETDEV_LINK_10000),
		.val = VEND1_LED_REG_A_EVENT_ON_NG
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_FULL_DUPLEX),
		.val = VEND1_LED_REG_A_EVENT_ON_FULL_DUPLEX
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_TX),
		.val = VEND1_LED_REG_A_EVENT_BLINK_TX
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_RX),
		.val = VEND1_LED_REG_A_EVENT_BLINK_RX
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_TX) |
			   BIT(TRIGGER_NETDEV_RX),
		.val = VEND1_LED_REG_A_EVENT_BLINK_ACT
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_10) |
			   BIT(TRIGGER_NETDEV_LINK_100) |
			   BIT(TRIGGER_NETDEV_LINK_1000) |
			   BIT(TRIGGER_NETDEV_LINK_2500) |
			   BIT(TRIGGER_NETDEV_LINK_5000) |
			   BIT(TRIGGER_NETDEV_LINK_10000),
		.val = VEND1_LED_REG_A_EVENT_ON_LINK
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_10) |
			   BIT(TRIGGER_NETDEV_LINK_100) |
			   BIT(TRIGGER_NETDEV_LINK_1000) |
			   BIT(TRIGGER_NETDEV_LINK_2500) |
			   BIT(TRIGGER_NETDEV_LINK_5000) |
			   BIT(TRIGGER_NETDEV_LINK_10000) |
			   BIT(TRIGGER_NETDEV_TX) |
			   BIT(TRIGGER_NETDEV_RX),
		.val = VEND1_LED_REG_A_EVENT_ON_LINK_BLINK_ACT
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_10) |
			   BIT(TRIGGER_NETDEV_LINK_100) |
			   BIT(TRIGGER_NETDEV_LINK_1000) |
			   BIT(TRIGGER_NETDEV_LINK_2500) |
			   BIT(TRIGGER_NETDEV_LINK_5000) |
			   BIT(TRIGGER_NETDEV_LINK_10000) |
			   BIT(TRIGGER_NETDEV_RX),
		.val = VEND1_LED_REG_A_EVENT_ON_LINK_BLINK_RX
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_10) |
			   BIT(TRIGGER_NETDEV_LINK_100) |
			   BIT(TRIGGER_NETDEV_LINK_1000) |
			   BIT(TRIGGER_NETDEV_TX) |
			   BIT(TRIGGER_NETDEV_RX),
		.val = VEND1_LED_REG_A_EVENT_ON_FE_GE_BLINK_ACT
	},
	{
		.pattern = BIT(TRIGGER_NETDEV_LINK_2500) |
			   BIT(TRIGGER_NETDEV_LINK_5000) |
			   BIT(TRIGGER_NETDEV_LINK_10000) |
			   BIT(TRIGGER_NETDEV_TX) |
			   BIT(TRIGGER_NETDEV_RX),
		.val = VEND1_LED_REG_A_EVENT_ON_NG_BLINK_ACT
	}
};

static int aeon_firmware_boot(struct phy_device *phydev, const u8 *data,
			      size_t size)
{
	int i, ret;
	u16 val;

	ret = phy_modify_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_CPU_CTRL,
			     VEND1_GLB_CPU_CTRL_MASK, AEON_CPU_CTRL_FW_LOAD);
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_FW_START_ADDR,
			    AEON_BOOT_ADDR);
	if (ret)
		return ret;

	ret = phy_modify_mmd(phydev, MDIO_MMD_VEND1,
			     VEND1_GLB_REG_MDIO_INDIRECT_ADDRCMD,
			     0x3ffc, 0xc000);
	if (ret)
		return ret;

	val = phy_read_mmd(phydev, MDIO_MMD_VEND1,
			   VEND1_GLB_REG_MDIO_INDIRECT_STATUS);
	if (val > 1) {
		phydev_err(phydev, "wrong origin mdio_indirect_status: %x\n", val);
		return -EINVAL;
	}

	/* Firmware is always aligned to u16 */
	for (i = 0; i < size; i += 2) {
		val = data[i + 1] << 8 | data[i];

		ret = phy_write_mmd(phydev, MDIO_MMD_VEND1,
				    VEND1_GLB_REG_MDIO_INDIRECT_LOAD, val);
		if (ret)
			return ret;
	}

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1,
			    VEND1_GLB_REG_CPU_RESET_ADDR_LO_BASEADDR,
			    lower_16_bits(AEON_CPU_BOOT_ADDR));
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1,
			    VEND1_GLB_REG_CPU_RESET_ADDR_HI_BASEADDR,
			    upper_16_bits(AEON_CPU_BOOT_ADDR));
	if (ret)
		return ret;

	return phy_modify_mmd(phydev, MDIO_MMD_VEND1, VEND1_GLB_REG_CPU_CTRL,
			      VEND1_GLB_CPU_CTRL_MASK, AEON_CPU_CTRL_FW_START);
}

static int aeon_firmware_load(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	const struct firmware *fw;
	const char *fw_name;
	int ret;

	ret = of_property_read_string(dev->of_node, "firmware-name",
				      &fw_name);
	if (ret)
		return ret;

	ret = request_firmware(&fw, fw_name, dev);
	if (ret) {
		phydev_err(phydev, "failed to find FW file %s (%d)\n",
			   fw_name, ret);
		return ret;
	}

	ret = aeon_firmware_boot(phydev, fw->data, fw->size);

	release_firmware(fw);

	return ret;
}

static bool aeon_ipc_ready(u16 val, bool parity_status)
{
	u16 status;

	if (FIELD_GET(AEON_IPC_STS_PARITY, val) != parity_status)
		return false;

	status = val & AEON_IPC_STS_STATUS;

	return status != AEON_IPC_STS_STATUS_RCVD &&
	       status != AEON_IPC_STS_STATUS_PROCESS &&
	       status != AEON_IPC_STS_STATUS_BUSY;
}

static int aeon_ipc_wait_cmd(struct phy_device *phydev, bool parity_status)
{
	u16 val;

	/* Exit condition logic:
	 * - Wait for parity bit equal
	 * - Wait for status success, error OR ready
	 */
	return phy_read_mmd_poll_timeout(phydev, MDIO_MMD_VEND1, VEND1_IPC_STS, val,
					 aeon_ipc_ready(val, parity_status),
					 AEON_IPC_DELAY, AEON_IPC_TIMEOUT, false);
}

static int aeon_ipc_send_cmd(struct phy_device *phydev,
			     struct as21xxx_priv *priv,
			     u16 cmd, u16 *ret_sts)
{
	bool curr_parity;
	int ret;

	/* The IPC sync by using a single parity bit.
	 * Each CMD have alternately this bit set or clear
	 * to understand correct flow and packet order.
	 */
	curr_parity = priv->parity_status;
	if (priv->parity_status)
		cmd |= AEON_IPC_CMD_PARITY;

	/* Always update parity for next packet */
	priv->parity_status = !priv->parity_status;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_IPC_CMD, cmd);
	if (ret)
		return ret;

	/* Wait for packet to be processed */
	usleep_range(AEON_IPC_DELAY, AEON_IPC_DELAY + 5000);

	/* With no ret_sts, ignore waiting for packet completion
	 * (ipc parity bit sync)
	 */
	if (!ret_sts)
		return 0;

	ret = aeon_ipc_wait_cmd(phydev, curr_parity);
	if (ret)
		return ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_IPC_STS);
	if (ret < 0)
		return ret;

	*ret_sts = ret;
	if ((*ret_sts & AEON_IPC_STS_STATUS) != AEON_IPC_STS_STATUS_SUCCESS)
		return -EINVAL;

	return 0;
}

/* If data is NULL, return 0 or negative error.
 * If data not NULL, return number of Bytes received from IPC or
 * a negative error.
 */
static int aeon_ipc_send_msg_locked(struct phy_device *phydev, u16 opcode,
				    u16 *data, unsigned int data_len,
				    u16 *ret_data)
{
	struct as21xxx_priv *priv = phydev->priv;
	unsigned int ret_size;
	u16 cmd, ret_sts;
	int ret;
	int i;

	/* IPC have a max of 8 register to transfer data,
	 * make sure we never exceed this.
	 */
	if (data_len > AEON_IPC_DATA_MAX || (data_len && !data))
		return -EINVAL;

	cmd = FIELD_PREP(AEON_IPC_CMD_SIZE, data_len) |
	      FIELD_PREP(AEON_IPC_CMD_OPCODE, opcode);

	for (i = 0; i < DIV_ROUND_UP(data_len, sizeof(u16)); i++) {
		ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_IPC_DATA(i),
				    data[i]);
		if (ret)
			goto out;
	}

	ret = aeon_ipc_send_cmd(phydev, priv, cmd, &ret_sts);
	if (ret) {
		phydev_err(phydev, "failed to send ipc msg for %x: %d\n",
			   opcode, ret);
		goto out;
	}

	if (!ret_data)
		goto out;

	if ((ret_sts & AEON_IPC_STS_STATUS) == AEON_IPC_STS_STATUS_ERROR) {
		ret = -EINVAL;
		goto out;
	}

	/* Prevent IPC from stack smashing the kernel.
	 * We can't trust IPC to return a good value and we always
	 * preallocate space for 16 Bytes.
	 */
	ret_size = FIELD_GET(AEON_IPC_STS_SIZE, ret_sts);
	if (ret_size > AEON_IPC_DATA_MAX) {
		ret = -EINVAL;
		goto out;
	}

	/* Read data from IPC data register for ret_size value from IPC */
	for (i = 0; i < DIV_ROUND_UP(ret_size, sizeof(u16)); i++) {
		ret = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_IPC_DATA(i));
		if (ret < 0)
			goto out;

		ret_data[i] = ret;
	}

	ret = ret_size;

out:
	return ret;
}

static int aeon_ipc_send_msg(struct phy_device *phydev, u16 opcode, u16 *data,
			     unsigned int data_len, u16 *ret_data)
{
	struct as21xxx_priv *priv = phydev->priv;
	int ret;

	mutex_lock(&priv->ipc_lock);
	ret = aeon_ipc_send_msg_locked(phydev, opcode, data, data_len,
				       ret_data);
	mutex_unlock(&priv->ipc_lock);

	return ret;
}

static int aeon_ipc_sync_parity(struct phy_device *phydev,
				struct as21xxx_priv *priv);
static int aeon_ipc_sync_parity_locked(struct phy_device *phydev,
				       struct as21xxx_priv *priv);

static int aeon_cfg_xfer(struct phy_device *phydev, u16 section, u16 sub,
			 const void *payload, unsigned int payload_len,
			 void *rbuf, unsigned int rbytes)
{
	u16 wbuf[AEON_IPC_DATA_NUM_REGISTERS] = {};
	u16 data[AEON_IPC_DATA_NUM_REGISTERS] = {};
	struct as21xxx_priv *priv = phydev->priv;
	unsigned int i, done, chunk;
	u16 cmd, hdr[3], ret_sts = 0;
	u8 *out = rbuf;
	int ret, iter;

	if (payload_len > sizeof(wbuf) || rbytes > AEON_DPC_RBUF_MAX)
		return -EINVAL;

	hdr[0] = (section << 8) | sub;
	hdr[1] = payload_len;
	hdr[2] = 0;

	mutex_lock(&priv->ipc_lock);

	ret = aeon_ipc_send_msg_locked(phydev, IPC_OPCODE_DBGCMD, hdr,
				       sizeof(hdr), data);
	if (ret < 0) {
		phydev_err(phydev,
			   "firmware command %02x/%02x start failed: %d\n",
			   section, sub, ret);
		goto out;
	}

	ret = aeon_ipc_sync_parity_locked(phydev, priv);
	if (ret)
		goto out;

	if (payload && payload_len) {
		memcpy(wbuf, payload, payload_len);
		ret = aeon_ipc_send_msg_locked(phydev, IPC_OPCODE_WBUF, wbuf,
					       payload_len, data);
		if (ret < 0) {
			phydev_err(phydev,
				   "firmware command %02x/%02x payload failed: %d\n",
				   section, sub, ret);
			goto out;
		}

		ret = aeon_ipc_sync_parity_locked(phydev, priv);
		if (ret)
			goto out;
	}

	for (iter = 0; iter < AEON_DPC_MAX_POLL; iter++) {
		cmd = FIELD_PREP(AEON_IPC_CMD_OPCODE, IPC_OPCODE_POLL);
		ret = aeon_ipc_send_cmd(phydev, priv, cmd, &ret_sts);
		if (ret) {
			phydev_err(phydev, "firmware command %02x/%02x poll failed: %d sts %04x\n",
				   section, sub, ret, ret_sts);
			goto out;
		}

		ret = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_IPC_DATA(0));
		if (ret < 0)
			goto out;
		if (ret)
			break;
	}

	if (iter == AEON_DPC_MAX_POLL) {
		phydev_err(phydev, "firmware command %02x/%02x timed out\n",
			   section, sub);
		ret = -ETIMEDOUT;
		goto out;
	}

	ret = 0;
	if (!rbuf || !rbytes)
		goto out_sync;

	for (done = 0; done < rbytes; done += chunk) {
		chunk = min_t(unsigned int, rbytes - done, AEON_IPC_DATA_MAX);

		cmd = FIELD_PREP(AEON_IPC_CMD_OPCODE, IPC_OPCODE_RBUF);
		ret = aeon_ipc_send_cmd(phydev, priv, cmd, &ret_sts);
		if (ret) {
			phydev_err(phydev, "firmware command %02x/%02x result failed: %d sts %04x\n",
				   section, sub, ret, ret_sts);
			goto out;
		}

		for (i = 0; i < AEON_IPC_DATA_NUM_REGISTERS; i++) {
			ret = phy_read_mmd(phydev, MDIO_MMD_VEND1,
					   VEND1_IPC_DATA(i));
			if (ret < 0)
				goto out;

			data[i] = ret;
		}

		memcpy(out + done, data, chunk);
	}

	ret = 0;

out_sync:
	ret = aeon_ipc_sync_parity_locked(phydev, priv);

out:
	mutex_unlock(&priv->ipc_lock);
	return ret;
}

static int aeon_dpc_cmd(struct phy_device *phydev, u16 sub,
			const void *payload, unsigned int payload_len)
{
	return aeon_cfg_xfer(phydev, IPC_DBGCMD_DPC, sub, payload, payload_len,
			     NULL, 0);
}

static int aeon_dpc_query(struct phy_device *phydev, u16 sub, void *rbuf,
			  unsigned int rbytes)
{
	return aeon_cfg_xfer(phydev, IPC_DBGCMD_DPC, sub, NULL, 0, rbuf,
			     rbytes);
}

static int aeon_dpc_fc_apply_mode(struct phy_device *phydev, u8 pcs_sel)
{
	u8 enable = pcs_sel == AEON_SDS_PCS_SEL_NORMAL;

	return aeon_dpc_cmd(phydev, IPC_DPC_FC_SET, &enable, sizeof(enable));
}

static int aeon_dpc_sds_get(struct phy_device *phydev, u8 *pcs_sel,
			    u8 *sds_spd)
{
	u8 cfg[AEON_IPC_DATA_MAX] = {};
	int ret;

	ret = aeon_dpc_query(phydev, IPC_DPC_SDS_GET_CFG, cfg, sizeof(cfg));
	if (ret)
		return ret;

	*pcs_sel = cfg[0];
	*sds_spd = cfg[1];

	return 0;
}

static int aeon_dpc_sds_set(struct phy_device *phydev, u8 pcs_sel, u8 sds_spd)
{
	u16 payload = pcs_sel | (sds_spd << 8);

	return aeon_dpc_cmd(phydev, IPC_DPC_SDS_SET_CFG, &payload,
			    sizeof(payload));
}

static int aeon_ipc_noop(struct phy_device *phydev,
			 struct as21xxx_priv *priv, u16 *ret_sts)
{
	u16 cmd;

	cmd = FIELD_PREP(AEON_IPC_CMD_SIZE, 0) |
	      FIELD_PREP(AEON_IPC_CMD_OPCODE, IPC_CMD_NOOP);

	return aeon_ipc_send_cmd(phydev, priv, cmd, ret_sts);
}

/* Logic to sync parity bit with IPC.
 * We send 2 NOP cmd with same partity and we wait for IPC
 * to handle the packet only for the second one. This way
 * we make sure we are sync for every next cmd.
 */
static int aeon_ipc_sync_parity_locked(struct phy_device *phydev,
				       struct as21xxx_priv *priv)
{
	u16 ret_sts = 0;
	int ret;

	/* Send NOP with no parity */
	ret = aeon_ipc_noop(phydev, priv, NULL);
	if (ret)
		return ret;

	/* Reset packet parity */
	priv->parity_status = false;

	/* Send second NOP with no parity */
	ret = aeon_ipc_noop(phydev, priv, &ret_sts);

	/* We expect to return -EINVAL */
	if (ret != -EINVAL)
		return ret;

	if ((ret_sts & AEON_IPC_STS_STATUS) != AEON_IPC_STS_STATUS_READY) {
		phydev_err(phydev, "Invalid IPC status on sync parity: %x\n",
			   ret_sts);
		return -EINVAL;
	}

	return 0;
}

static int aeon_ipc_sync_parity(struct phy_device *phydev,
				struct as21xxx_priv *priv)
{
	int ret;

	mutex_lock(&priv->ipc_lock);
	ret = aeon_ipc_sync_parity_locked(phydev, priv);
	mutex_unlock(&priv->ipc_lock);

	return ret;
}

static int aeon_ipc_get_fw_version(struct phy_device *phydev)
{
	u16 ret_data[AEON_IPC_DATA_NUM_REGISTERS], data[] = { IPC_INFO_VERSION };
	int ret;

	ret = aeon_ipc_send_msg(phydev, IPC_CMD_INFO, data,
				sizeof(data), ret_data);

	return ret < 0 ? ret : 0;
}

static int aeon_ipc_phy_enable_mode(struct phy_device *phydev, bool enable)
{
	struct as21xxx_priv *priv = phydev->priv;
	u16 data[] = { IPC_SYS_CPU_PHY_ENABLE, enable };
	int ret;

	mutex_lock(&priv->ipc_lock);
	ret = aeon_ipc_sync_parity_locked(phydev, priv);
	if (!ret)
		ret = aeon_ipc_send_msg_locked(phydev, IPC_CMD_SYS_CPU, data,
					       sizeof(data), NULL);
	mutex_unlock(&priv->ipc_lock);

	return ret;
}

static int aeon_dpc_ra_enable(struct phy_device *phydev)
{
	u16 data[2];

	data[0] = IPC_CFG_PARAM_DIRECT;
	data[1] = IPC_CFG_PARAM_DIRECT_DPC_RA;

	return aeon_ipc_send_msg(phydev, IPC_CMD_CFG_PARAM, data,
				 sizeof(data), NULL);
}

static int aeon_dpc_eth_sts_hw_update(struct phy_device *phydev, bool enable)
{
	u8 value = enable ? 1 : 0;

	return aeon_dpc_cmd(phydev, IPC_DPC_ETH_STS_HW_UPD_CFG, &value,
			    sizeof(value));
}

static int as22xxx_restart_an(struct phy_device *phydev)
{
	u16 data[3];

	data[0] = IPC_CFG_PARAM_DIRECT;
	data[1] = IPC_CFG_PARAM_DIRECT_CU_AN;
	data[2] = IPC_CMD_CFG_CU_AN_RESTART;

	return aeon_ipc_send_msg(phydev, IPC_CMD_CFG_PARAM, data,
				 sizeof(data), NULL);
}

static int as22xxx_set_top_speed(struct phy_device *phydev)
{
	u16 data[4] = {
		IPC_CFG_PARAM_DIRECT,
		IPC_CFG_PARAM_DIRECT_CU_AN,
		IPC_CMD_CFG_CU_AN_TOP_SPD,
		AEON_CU_AN_TOP_SPD_10M,
	};
	unsigned long *advertising = phydev->advertising;

	if (linkmode_test_bit(ETHTOOL_LINK_MODE_10000baseT_Full_BIT,
			      advertising))
		data[3] = AEON_CU_AN_TOP_SPD_10G;
	else if (linkmode_test_bit(ETHTOOL_LINK_MODE_5000baseT_Full_BIT,
				   advertising))
		data[3] = AEON_CU_AN_TOP_SPD_5G;
	else if (linkmode_test_bit(ETHTOOL_LINK_MODE_2500baseT_Full_BIT,
				   advertising))
		data[3] = AEON_CU_AN_TOP_SPD_2500M;
	else if (linkmode_test_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
				   advertising))
		data[3] = AEON_CU_AN_TOP_SPD_1G;
	else if (linkmode_test_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT,
				   advertising))
		data[3] = AEON_CU_AN_TOP_SPD_100M;

	return aeon_ipc_send_msg(phydev, IPC_CMD_CFG_PARAM, data,
				 sizeof(data), NULL);
}

static int as22xxx_config_aneg(struct phy_device *phydev)
{
	int ret;

	if (phydev->autoneg == AUTONEG_DISABLE)
		return genphy_c45_pma_setup_forced(phydev);

	ret = genphy_c45_an_config_aneg(phydev);
	if (ret < 0)
		return ret;

	ret = as22xxx_set_top_speed(phydev);
	if (ret)
		return ret;

	return as22xxx_restart_an(phydev);
}

static int as21xxx_probe(struct phy_device *phydev)
{
	struct as21xxx_priv *priv;
	int ret;

	priv = devm_kzalloc(&phydev->mdio.dev,
			    sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	phydev->priv = priv;

	ret = devm_mutex_init(&phydev->mdio.dev,
			      &priv->ipc_lock);
	if (ret)
		return ret;
	ret = devm_mutex_init(&phydev->mdio.dev, &priv->sds_lock);
	if (ret)
		return ret;

	ret = aeon_ipc_sync_parity(phydev, priv);
	if (ret)
		return ret;

	ret = aeon_ipc_get_fw_version(phydev);
	if (ret)
		return ret;

	/* Enable PTP clk if not already Enabled */
	ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_PTP_CLK,
			       VEND1_PTP_CLK_EN);
	if (ret)
		return ret;

	return aeon_dpc_ra_enable(phydev);
}

static int as21xxx_read_link(struct phy_device *phydev, int *bmcr)
{
	int status;

	/* Normal C22 BMCR report inconsistent data, use
	 * the mapped C22 in C45 to have more consistent link info.
	 */
	*bmcr = phy_read_mmd(phydev, MDIO_MMD_AN,
			     AS21XXX_MDIO_AN_C22 + MII_BMCR);
	if (*bmcr < 0)
		return *bmcr;

	/* Autoneg is being started, therefore disregard current
	 * link status and report link as down.
	 */
	if (*bmcr & BMCR_ANRESTART) {
		phydev->link = 0;
		return 0;
	}

	status = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_STAT1);
	if (status < 0)
		return status;

	phydev->link = !!(status & MDIO_STAT1_LSTATUS);

	return 0;
}

static int as21xxx_read_c22_lpa(struct phy_device *phydev)
{
	int lpagb;

	/* MII_STAT1000 are only filled in the mapped C22
	 * in C45, use that to fill lpagb values and check.
	 */
	lpagb = phy_read_mmd(phydev, MDIO_MMD_AN,
			     AS21XXX_MDIO_AN_C22 + MII_STAT1000);
	if (lpagb < 0)
		return lpagb;

	if (lpagb & LPA_1000MSFAIL) {
		int adv = phy_read_mmd(phydev, MDIO_MMD_AN,
				       AS21XXX_MDIO_AN_C22 + MII_CTRL1000);

		if (adv < 0)
			return adv;

		if (adv & CTL1000_ENABLE_MASTER)
			phydev_err(phydev, "Master/Slave resolution failed, maybe conflicting manual settings?\n");
		else
			phydev_err(phydev, "Master/Slave resolution failed\n");
		return -ENOLINK;
	}

	mii_stat1000_mod_linkmode_lpa_t(phydev->lp_advertising,
					lpagb);

	return 0;
}

static int as21xxx_read_status(struct phy_device *phydev)
{
	int bmcr, old_link = phydev->link;
	int ret;

	ret = as21xxx_read_link(phydev, &bmcr);
	if (ret)
		return ret;

	/* why bother the PHY if nothing can have changed */
	if (phydev->autoneg == AUTONEG_ENABLE && old_link && phydev->link)
		return 0;

	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;
	phydev->pause = 0;
	phydev->asym_pause = 0;

	if (phydev->autoneg == AUTONEG_ENABLE) {
		ret = genphy_c45_read_lpa(phydev);
		if (ret)
			return ret;

		ret = as21xxx_read_c22_lpa(phydev);
		if (ret)
			return ret;

		phy_resolve_aneg_linkmode(phydev);
	} else {
		int speed;

		linkmode_zero(phydev->lp_advertising);

		speed = phy_read_mmd(phydev, MDIO_MMD_VEND1,
				     VEND1_SPEED_STATUS);
		if (speed < 0)
			return speed;

		switch (speed & VEND1_SPEED_STATUS) {
		case VEND1_SPEED_10000:
			phydev->speed = SPEED_10000;
			phydev->duplex = DUPLEX_FULL;
			break;
		case VEND1_SPEED_5000:
			phydev->speed = SPEED_5000;
			phydev->duplex = DUPLEX_FULL;
			break;
		case VEND1_SPEED_2500:
			phydev->speed = SPEED_2500;
			phydev->duplex = DUPLEX_FULL;
			break;
		case VEND1_SPEED_1000:
			phydev->speed = SPEED_1000;
			if (bmcr & BMCR_FULLDPLX)
				phydev->duplex = DUPLEX_FULL;
			else
				phydev->duplex = DUPLEX_HALF;
			break;
		case VEND1_SPEED_100:
			phydev->speed = SPEED_100;
			phydev->duplex = DUPLEX_FULL;
			break;
		case VEND1_SPEED_10:
			phydev->speed = SPEED_10;
			phydev->duplex = DUPLEX_FULL;
			break;
		default:
			return -EINVAL;
		}
	}

	return 0;
}

static int as22xxx_read_link(struct phy_device *phydev, int *bmcr)
{
	int status = 0;
	bool link_up;

	*bmcr = phy_read_mmd(phydev, MDIO_MMD_AN,
			     AS21XXX_MDIO_AN_C22 + MII_BMCR);
	if (*bmcr < 0)
		return *bmcr;

	if (*bmcr & BMCR_ANRESTART)
		goto done;

	status = phy_read_mmd(phydev, MDIO_MMD_AN, AS22XXX_AN_STATES1);
	if (status < 0)
		return status;

done:
	link_up = FIELD_GET(AS22XXX_AN_STATES1_ARB_MASK, status) ==
		  AS22XXX_LINK_GOOD;
	phydev->link = link_up;
	phydev->autoneg_complete = link_up;

	if (phydev->autoneg == AUTONEG_ENABLE && !phydev->autoneg_complete)
		phydev->link = 0;

	return 0;
}

static int as22xxx_read_lpa(struct phy_device *phydev)
{
	int lpa, ret;

	if (phydev->autoneg == AUTONEG_ENABLE && !phydev->autoneg_complete) {
		mii_stat1000_mod_linkmode_lpa_t(phydev->lp_advertising, 0);
		mii_lpa_mod_linkmode_lpa_t(phydev->lp_advertising, 0);
		return 0;
	}

	ret = as21xxx_read_c22_lpa(phydev);
	if (ret)
		return ret;

	lpa = phy_read_mmd(phydev, MDIO_MMD_AN,
			   AS21XXX_MDIO_AN_C22 + MII_LPA);
	if (lpa < 0)
		return lpa;

	mii_lpa_mod_linkmode_lpa_t(phydev->lp_advertising, lpa);

	lpa = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_AN_10GBT_STAT);
	if (lpa < 0)
		return lpa;

	mii_10gbt_stat_mod_linkmode_lpa_t(phydev->lp_advertising, lpa);

	return 0;
}

static void as22xxx_read_speed(struct phy_device *phydev, int bmcr)
{
	int speed;

	speed = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_SPEED_STATUS);
	if (speed < 0)
		return;

	speed &= VEND1_SPEED_MASK;
	if (speed == VEND1_SPEED_10000) {
		phydev->speed = SPEED_10000;
		phydev->duplex = DUPLEX_FULL;
	} else if (speed == VEND1_SPEED_5000) {
		phydev->speed = SPEED_5000;
		phydev->duplex = DUPLEX_FULL;
	} else if (speed == VEND1_SPEED_2500) {
		phydev->speed = SPEED_2500;
		phydev->duplex = DUPLEX_FULL;
	} else if (speed == VEND1_SPEED_1000) {
		phydev->speed = SPEED_1000;
		if (bmcr & BMCR_FULLDPLX)
			phydev->duplex = DUPLEX_FULL;
		else
			phydev->duplex = DUPLEX_HALF;
	} else if (speed == VEND1_SPEED_100) {
		phydev->speed = SPEED_100;
		if (bmcr & BMCR_FULLDPLX)
			phydev->duplex = DUPLEX_FULL;
		else
			phydev->duplex = DUPLEX_HALF;
	} else {
		phydev->speed = SPEED_10;
		phydev->duplex = DUPLEX_FULL;
	}
}

static phy_interface_t as22xxx_speed_interface(int speed)
{
	switch (speed) {
	case SPEED_10:
	case SPEED_100:
	case SPEED_1000:
		return PHY_INTERFACE_MODE_SGMII;

	case SPEED_2500:
	case SPEED_5000:
	case SPEED_10000:
		return PHY_INTERFACE_MODE_USXGMII;

	default:
		return PHY_INTERFACE_MODE_NA;
	}
}

static int as22xxx_interface_sds(phy_interface_t interface, u8 *pcs_sel,
				 u8 *sds_spd)
{
	switch (interface) {
	case PHY_INTERFACE_MODE_SGMII:
		*pcs_sel = AEON_SDS_PCS_SEL_NORMAL;
		*sds_spd = AEON_SDS_SPD_1G;
		return 0;

	case PHY_INTERFACE_MODE_USXGMII:

		*pcs_sel = AEON_SDS_PCS_SEL_64B66B;
		*sds_spd = AEON_SDS_SPD_10G;
		return 0;

	default:
		return -EOPNOTSUPP;
	}
}

static int as22xxx_program_system_side(struct phy_device *phydev,
				       phy_interface_t interface,
				       u8 pcs_sel, u8 sds_spd);
static int as22xxx_sds_mii_config(struct phy_device *phydev,
				  phy_interface_t interface, int speed);

static bool as22xxx_update_interface(struct phy_device *phydev)
{
	struct as21xxx_priv *priv = phydev->priv;
	phy_interface_t interface;
	u8 pcs_sel, sds_spd;

	if (!priv->mode_switch || !phydev->link)
		return true;

	interface = as22xxx_speed_interface(phydev->speed);
	if (interface == PHY_INTERFACE_MODE_NA)
		return true;

	if (interface == priv->sds_interface) {
		if (interface == PHY_INTERFACE_MODE_SGMII &&
		    phydev->speed != priv->sds_speed) {
			if (as22xxx_sds_mii_config(phydev, interface,
						 phydev->speed))
				return false;
			if (aeon_dpc_ra_enable(phydev))
				return false;

			priv->sds_speed = phydev->speed;
		}

		phydev->interface = interface;
		return true;
	}

	if (as22xxx_interface_sds(interface, &pcs_sel, &sds_spd))
		return true;

	if (as22xxx_program_system_side(phydev, interface, pcs_sel, sds_spd))
		return false;

	priv->sds_interface = interface;
	priv->sds_speed = phydev->speed;
	phydev->interface = interface;

	return true;
}

static int as22xxx_select_system_side(struct phy_device *phydev,
				      phy_interface_t interface);
static int as22xxx_select_system_side(struct phy_device *phydev,
				      phy_interface_t interface)
{
	struct as21xxx_priv *priv = phydev->priv;
	u16 want = interface == PHY_INTERFACE_MODE_USXGMII ?
			   VEND1_CHIP_CTRL_SXGMII_MODE :
			   0;
	int ret, result = 0;

	mutex_lock(&priv->sds_lock);

	ret = phy_modify_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL,
			     VEND1_CHIP_CTRL_SXGMII_MODE, want);
	if (ret < 0) {
		result = ret;
		goto out;
	}

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL);
	if (ret < 0) {
		result = ret;
		goto out;
	}
	if ((ret & VEND1_CHIP_CTRL_SXGMII_MODE) != want) {
		phydev_err(phydev,
			   "system-side Chip Control personality did not take for %s (chip_ctrl=%04x)\n",
			   phy_modes(interface), ret);
		result = -EIO;
		goto out;
	}

out:
	mutex_unlock(&priv->sds_lock);
	return result;
}

static int as22xxx_sds_mii_config(struct phy_device *phydev,
				  phy_interface_t interface, int speed)
{
	struct as21xxx_priv *priv = phydev->priv;
	u16 mask = BMCR_ANENABLE | BMCR_ANRESTART | BMCR_SPEED1000 |
		   BMCR_SPEED100 | BMCR_FULLDPLX;
	u16 want, adv_speed = 0, adv_want = 0;
	int orig, paged, val, adv, adv_readback, restored;
	int ret = 0;

	if (interface == PHY_INTERFACE_MODE_SGMII) {
		switch (speed) {
		case SPEED_10:
			adv_speed = FIELD_PREP(AS22XXX_SDS_MII_ADV_SPEED, 0);
			break;
		case SPEED_100:
			adv_speed = FIELD_PREP(AS22XXX_SDS_MII_ADV_SPEED, 1);
			break;
		case SPEED_1000:
			adv_speed = FIELD_PREP(AS22XXX_SDS_MII_ADV_SPEED, 2);
			break;
		default:
			return -EOPNOTSUPP;
		}
		want = mii_bmcr_encode_fixed(speed, DUPLEX_FULL);
	} else if (interface == PHY_INTERFACE_MODE_USXGMII) {
		switch (speed) {
		case SPEED_2500:
			adv_speed = MDIO_USXGMII_2500;
			break;
		case SPEED_5000:
			adv_speed = MDIO_USXGMII_5000;
			break;
		case SPEED_10000:
			adv_speed = MDIO_USXGMII_10G;
			break;
		default:
			return -EOPNOTSUPP;
		}

		want = BMCR_SPEED100 | BMCR_SPEED1000 | BMCR_FULLDPLX;

		adv_want = MDIO_USXGMII_LINK | MDIO_USXGMII_FULL_DUPLEX |
			   adv_speed | AS22XXX_SDS_MII_ADV_SELECTOR;
	} else {
		return -EOPNOTSUPP;
	}

	mutex_lock(&priv->sds_lock);

	orig = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL);
	if (orig < 0) {
		ret = orig;
		goto out_unlock;
	}

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL,
			    orig | VEND1_CHIP_CTRL_XFI_ACCESS);
	if (ret)
		goto out_restore;

	paged = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL);
	if (paged < 0) {
		ret = paged;
		goto out_restore;
	}
	if (!(paged & VEND1_CHIP_CTRL_XFI_ACCESS)) {
		ret = -EIO;
		goto out_restore;
	}

	ret = phy_modify_mmd(phydev, MDIO_MMD_AN,
			     AS21XXX_MDIO_AN_C22 + MII_BMCR,
			     BMCR_ANENABLE | BMCR_ANRESTART, 0);
	if (ret < 0)
		goto out_restore;

	if (interface == PHY_INTERFACE_MODE_SGMII) {
		adv = phy_read_mmd(phydev, MDIO_MMD_AN,
				   AS21XXX_MDIO_AN_C22 + MII_ADVERTISE);
		if (adv < 0) {
			ret = adv;
			goto out_restore;
		}
		adv_want = (adv & ~AS22XXX_SDS_MII_ADV_CONTROL_MASK) |
			   AS22XXX_SDS_MII_ADV_LINK |
			   AS22XXX_SDS_MII_ADV_FULL |
			   adv_speed |
			   AS22XXX_SDS_MII_ADV_SELECTOR;

		ret = phy_modify_mmd(phydev, MDIO_MMD_AN,
				     AS21XXX_MDIO_AN_C22 + MII_ADVERTISE,
				     AS22XXX_SDS_MII_ADV_CONTROL_MASK,
				     adv_want);
		if (ret < 0)
			goto out_restore;

		adv_readback = phy_read_mmd(phydev, MDIO_MMD_AN,
					    AS21XXX_MDIO_AN_C22 + MII_ADVERTISE);
		if (adv_readback < 0) {
			ret = adv_readback;
			goto out_restore;
		}
		if ((adv_readback & AS22XXX_SDS_MII_ADV_CONTROL_MASK) !=
		    (adv_want & AS22XXX_SDS_MII_ADV_CONTROL_MASK)) {
			phydev_err(phydev,
				   "SGMII MII advertisement did not take (read %04x, want %04x)\n",
				   adv_readback, adv_want);
			ret = -EIO;
			goto out_restore;
		}
	} else {
		ret = phy_write_mmd(phydev, MDIO_MMD_AN,
				    AS21XXX_MDIO_AN_C22 + MII_ADVERTISE,
				    adv_want);
		if (ret)
			goto out_restore;

		adv_readback = phy_read_mmd(phydev, MDIO_MMD_AN,
					    AS21XXX_MDIO_AN_C22 + MII_ADVERTISE);
		if (adv_readback < 0) {
			ret = adv_readback;
			goto out_restore;
		}
		if (adv_readback != adv_want) {
			phydev_err(phydev,
				   "USXGMII MII advertisement did not take for %d Mbps (read %04x, want %04x)\n",
				   speed, adv_readback, adv_want);
			ret = -EIO;
			goto out_restore;
		}
	}

	ret = phy_modify_mmd(phydev, MDIO_MMD_AN,
			     AS21XXX_MDIO_AN_C22 + MII_BMCR, mask, want);
	if (ret < 0)
		goto out_restore;

	val = phy_read_mmd(phydev, MDIO_MMD_AN,
			   AS21XXX_MDIO_AN_C22 + MII_BMCR);
	if (val < 0) {
		ret = val;
		goto out_restore;
	}
	if ((val & mask) != want) {
		phydev_err(phydev,
			   "SerDes MII fixed control did not take for %d Mbps (read %04x, want %04x)\n",
			   speed, val, want);
		ret = -EIO;
		goto out_restore;
	}

	want |= BMCR_ANENABLE;
	ret = phy_modify_mmd(phydev, MDIO_MMD_AN,
			     AS21XXX_MDIO_AN_C22 + MII_BMCR, mask,
			     want | BMCR_ANRESTART);
	if (ret < 0)
		goto out_restore;

	ret = read_poll_timeout(phy_read_mmd, val,
				val < 0 || !(val & BMCR_ANRESTART),
				1000, 100000, false, phydev, MDIO_MMD_AN,
				AS21XXX_MDIO_AN_C22 + MII_BMCR);
	if (val < 0) {
		ret = val;
		goto out_restore;
	}
	if (ret) {
		phydev_err(phydev,
			   "SerDes MII AN restart did not self-clear for %d Mbps (read %04x)\n",
			   speed, val);
		goto out_restore;
	}
	if ((val & mask) != want) {
		phydev_err(phydev,
			   "SerDes MII control did not take for %d Mbps (read %04x, want %04x)\n",
			   speed, val, want);
		ret = -EIO;
		goto out_restore;
	}

	adv_readback = phy_read_mmd(phydev, MDIO_MMD_AN,
				    AS21XXX_MDIO_AN_C22 + MII_ADVERTISE);
	if (adv_readback < 0) {
		ret = adv_readback;
		goto out_restore;
	}
	if ((interface == PHY_INTERFACE_MODE_SGMII &&
	     (adv_readback & AS22XXX_SDS_MII_ADV_CONTROL_MASK) !=
	     (adv_want & AS22XXX_SDS_MII_ADV_CONTROL_MASK)) ||
	    (interface == PHY_INTERFACE_MODE_USXGMII &&
	     adv_readback != adv_want)) {
		phydev_err(phydev,
			   "%s MII advertisement changed after AN restart (read %04x, want %04x)\n",
			   phy_modes(interface), adv_readback, adv_want);
		ret = -EIO;
		goto out_restore;
	}

out_restore:
	restored = phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL,
				 orig);
	if (!restored)
		restored = phy_read_mmd(phydev, MDIO_MMD_VEND1,
					VEND1_CHIP_CTRL);
	if (restored != orig) {
		phydev_err(phydev,
			   "SerDes MII config failed to restore chip_ctrl %04x (readback/err %d)\n",
			   orig, restored);
		if (!ret)
			ret = restored < 0 ? restored : -EIO;
	}

out_unlock:
	mutex_unlock(&priv->sds_lock);
	return ret;
}

static int as22xxx_sds_pcs_reset(struct phy_device *phydev)
{
	struct as21xxx_priv *priv = phydev->priv;
	int orig, paged, after, restored;
	int ret = 0;

	mutex_lock(&priv->sds_lock);

	orig = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL);
	if (orig < 0) {
		ret = orig;
		goto out_unlock;
	}

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL,
			    orig | VEND1_CHIP_CTRL_XFI_ACCESS);
	if (ret)
		goto out_restore;

	paged = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL);
	if (paged < 0) {
		ret = paged;
		goto out_restore;
	}
	if (!(paged & VEND1_CHIP_CTRL_XFI_ACCESS)) {
		ret = -EIO;
		goto out_restore;
	}

	ret = phy_modify_mmd(phydev, MDIO_MMD_PCS, MDIO_CTRL1,
			     MDIO_CTRL1_RESET, MDIO_CTRL1_RESET);
	if (ret < 0)
		goto out_restore;

	ret = phy_read_mmd_poll_timeout(phydev, MDIO_MMD_PCS, MDIO_CTRL1, after,
					!(after & MDIO_CTRL1_RESET), 5000,
					1000000, true);
	if (ret)
		goto out_restore;

out_restore:
	restored = phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL, orig);
	if (!restored)
		restored =
			phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL);
	if (restored != orig) {
		phydev_err(phydev,
			   "SerDes PCS reset failed to restore chip_ctrl %04x (readback/err %d)\n",
			   orig, restored);
		if (!ret)
			ret = restored < 0 ? restored : -EIO;
	}

out_unlock:
	mutex_unlock(&priv->sds_lock);
	return ret;
}

static int as22xxx_sds_pma_reset(struct phy_device *phydev)
{
	struct as21xxx_priv *priv = phydev->priv;
	int orig, paged, after, restored;
	int ret = 0;

	mutex_lock(&priv->sds_lock);

	orig = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL);
	if (orig < 0) {
		ret = orig;
		goto out_unlock;
	}

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL,
			    orig | VEND1_CHIP_CTRL_XFI_ACCESS);
	if (ret)
		goto out_restore;

	paged = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL);
	if (paged < 0) {
		ret = paged;
		goto out_restore;
	}
	if (!(paged & VEND1_CHIP_CTRL_XFI_ACCESS)) {
		ret = -EIO;
		goto out_restore;
	}

	ret = phy_modify_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1,
			     MDIO_CTRL1_RESET, MDIO_CTRL1_RESET);
	if (ret < 0)
		goto out_restore;

	ret = phy_read_mmd_poll_timeout(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1,
					after,
					!(after & MDIO_CTRL1_RESET),
					5000, 1000000, true);
	if (ret)
		goto out_restore;

out_restore:
	restored = phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL, orig);
	if (!restored)
		restored =
			phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL);
	if (restored != orig) {
		phydev_err(phydev,
			   "SerDes PMA reset failed to restore chip_ctrl %04x (readback/err %d)\n",
			   orig, restored);
		if (!ret)
			ret = restored < 0 ? restored : -EIO;
	}

out_unlock:
	mutex_unlock(&priv->sds_lock);
	return ret;
}

static int as22xxx_sds_10g_ctrl_restore(struct phy_device *phydev)
{
	struct as21xxx_priv *priv = phydev->priv;
	int orig, paged, pma_after, pcs_after, restored;
	int ret = 0;

	mutex_lock(&priv->sds_lock);

	orig = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL);
	if (orig < 0) {
		ret = orig;
		goto out_unlock;
	}

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL,
			    orig | VEND1_CHIP_CTRL_XFI_ACCESS);
	if (ret)
		goto out_restore;

	paged = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL);
	if (paged < 0) {
		ret = paged;
		goto out_restore;
	}
	if (!(paged & VEND1_CHIP_CTRL_XFI_ACCESS)) {
		ret = -EIO;
		goto out_restore;
	}

	ret = phy_modify_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1,
			     MDIO_CTRL1_SPEEDSEL, MDIO_CTRL1_SPEED10G);
	if (ret < 0)
		goto out_restore;
	ret = phy_modify_mmd(phydev, MDIO_MMD_PCS, MDIO_CTRL1,
			     MDIO_CTRL1_SPEEDSEL, MDIO_CTRL1_SPEED10G);
	if (ret < 0)
		goto out_restore;

	pma_after = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_CTRL1);
	if (pma_after < 0) {
		ret = pma_after;
		goto out_restore;
	}
	pcs_after = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_CTRL1);
	if (pcs_after < 0) {
		ret = pcs_after;
		goto out_restore;
	}
	if ((pma_after & MDIO_CTRL1_SPEEDSEL) != MDIO_CTRL1_SPEED10G ||
	    (pcs_after & MDIO_CTRL1_SPEEDSEL) != MDIO_CTRL1_SPEED10G) {
		phydev_err(phydev,
			   "SerDes 10G controls did not take: PMA %04x, PCS %04x\n",
			   pma_after, pcs_after);
		ret = -EIO;
		goto out_restore;
	}

out_restore:
	restored = phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL, orig);
	if (!restored)
		restored =
			phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL);
	if (restored != orig) {
		phydev_err(phydev,
			   "SerDes 10G control restore failed to restore chip_ctrl %04x (readback/err %d)\n",
			   orig, restored);
		if (!ret)
			ret = restored < 0 ? restored : -EIO;
	}

out_unlock:
	mutex_unlock(&priv->sds_lock);
	return ret;
}

static int as22xxx_check_sds_config(struct phy_device *phydev, u8 pcs_sel,
				    u8 sds_spd)
{
	u8 rb_pcs_sel, rb_sds_spd;
	int ret;

	ret = aeon_dpc_sds_get(phydev, &rb_pcs_sel, &rb_sds_spd);
	if (ret)
		return ret;

	if (rb_pcs_sel != pcs_sel || rb_sds_spd != sds_spd) {
		phydev_err(phydev,
			   "SDS config mismatch: expected %u/%u, read %u/%u\n",
			   pcs_sel, sds_spd, rb_pcs_sel, rb_sds_spd);
		return -EIO;
	}

	return 0;
}

static int as22xxx_stage_system_side(struct phy_device *phydev,
				     phy_interface_t interface,
				     u8 pcs_sel, u8 sds_spd)
{
	int ret;

	ret = aeon_dpc_sds_set(phydev, pcs_sel, sds_spd);
	if (ret) {
		phydev_err(phydev, "failed to stage SDS config for %s: %d\n",
			   phy_modes(interface), ret);
		return ret;
	}

	return as22xxx_check_sds_config(phydev, pcs_sel, sds_spd);
}

static int as22xxx_program_system_side(struct phy_device *phydev,
				       phy_interface_t interface,
				       u8 pcs_sel, u8 sds_spd)
{
	struct as21xxx_priv *priv = phydev->priv;
	bool upshift = interface == PHY_INTERFACE_MODE_USXGMII &&
		       priv->sds_interface == PHY_INTERFACE_MODE_SGMII;
	u8 sds_id = 0;
	int ret;

	ret = as22xxx_select_system_side(phydev, interface);
	if (ret)
		return ret;

	ret = as22xxx_stage_system_side(phydev, interface, pcs_sel, sds_spd);
	if (ret)
		return ret;

	if (interface == PHY_INTERFACE_MODE_SGMII || upshift) {
		ret = as22xxx_sds_mii_config(phydev, interface, phydev->speed);
		if (ret)
			return ret;
	}

	if (interface == PHY_INTERFACE_MODE_USXGMII) {
		ret = as22xxx_select_system_side(phydev, interface);
		if (ret)
			return ret;

		if (upshift) {
			ret = as22xxx_sds_pma_reset(phydev);
			if (ret)
				return ret;

			ret = as22xxx_check_sds_config(phydev, pcs_sel, sds_spd);
			if (ret)
				return ret;
		}

		ret = aeon_cfg_xfer(phydev, IPC_DBGCMD_SDS, IPC_SDS_RST,
				    &sds_id, sizeof(sds_id), NULL, 0);
		if (ret)
			return ret;

		ret = aeon_dpc_ra_enable(phydev);
		if (ret) {
			phydev_err(phydev,
				   "failed to enable USXGMII rate adaptor: %d\n",
				   ret);
			return ret;
		}

		ret = as22xxx_check_sds_config(phydev, pcs_sel, sds_spd);
		if (ret)
			return ret;
	}

	ret = aeon_dpc_fc_apply_mode(phydev, pcs_sel);
	if (ret) {
		phydev_err(phydev, "failed to set system-side flow control: %d\n",
			   ret);
		return ret;
	}

	ret = as22xxx_select_system_side(phydev, interface);
	if (ret)
		return ret;

	if (upshift) {
		ret = as22xxx_sds_10g_ctrl_restore(phydev);
		if (ret)
			return ret;

		ret = as22xxx_sds_pcs_reset(phydev);
		if (ret)
			return ret;

		ret = as22xxx_check_sds_config(phydev, pcs_sel, sds_spd);
		if (ret)
			return ret;

		ret = aeon_dpc_ra_enable(phydev);
		if (ret) {
			phydev_err(phydev,
				   "failed to restore USXGMII rate adaptor: %d\n",
				   ret);
			return ret;
		}

		ret = aeon_dpc_fc_apply_mode(phydev, pcs_sel);
		if (ret)
			return ret;

		ret = as22xxx_select_system_side(phydev, interface);
		if (ret)
			return ret;

		ret = as22xxx_check_sds_config(phydev, pcs_sel, sds_spd);
		if (ret)
			return ret;

		ret = as22xxx_sds_10g_ctrl_restore(phydev);
		if (ret)
			return ret;

		ret = as22xxx_sds_mii_config(phydev, interface, phydev->speed);
		if (ret)
			return ret;

		priv->system_sync_state =
			AS22XXX_SYSTEM_SYNC_NEEDS_PHY_ENABLE;
	} else if (interface == PHY_INTERFACE_MODE_SGMII) {
		priv->system_sync_state = AS22XXX_SYSTEM_SYNC_IDLE;
	}

	return 0;
}

static int as22xxx_usxgmii_ready_paged(struct phy_device *phydev, int *pcs_sts,
				       int *mii_sts)
{
	int val;

	*pcs_sts = phy_read_mmd(phydev, MDIO_MMD_PCS, AS22XXX_SDS_PCS_STS20);
	if (*pcs_sts < 0)
		return *pcs_sts;

	val = phy_read_mmd(phydev, MDIO_MMD_AN, AS21XXX_MDIO_AN_C22 + MII_BMSR);
	if (val < 0)
		return val;
	*mii_sts = phy_read_mmd(phydev, MDIO_MMD_AN,
				AS21XXX_MDIO_AN_C22 + MII_BMSR);
	if (*mii_sts < 0)
		return *mii_sts;

	return ((*pcs_sts &
		 (AS22XXX_SDS_PCS_LINK_UP | AS22XXX_SDS_PCS_HIGH_BER |
		  AS22XXX_SDS_PCS_BLOCK_LOCK)) ==
			(AS22XXX_SDS_PCS_LINK_UP |
			 AS22XXX_SDS_PCS_BLOCK_LOCK) &&
		(*mii_sts & (BMSR_ANEGCOMPLETE | BMSR_LSTATUS | BMSR_RFAULT)) ==
			(BMSR_ANEGCOMPLETE | BMSR_LSTATUS));
}

static int as22xxx_usxgmii_ready_once(struct phy_device *phydev, int *pcs_sts,
				      int *mii_sts)
{
	struct as21xxx_priv *priv = phydev->priv;
	int orig, paged, chip_ctrl, restore, restored;
	int restore_err = 0, ret;

	mutex_lock(&priv->sds_lock);

	orig = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL);
	if (orig < 0) {
		ret = orig;
		goto unlock;
	}

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL,
			    orig | VEND1_CHIP_CTRL_XFI_ACCESS);
	if (ret)
		goto out_restore;

	paged = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL);
	if (paged < 0) {
		ret = paged;
		goto out_restore;
	}
	if (!(paged & VEND1_CHIP_CTRL_XFI_ACCESS)) {
		ret = -EIO;
		goto out_restore;
	}

	ret = as22xxx_usxgmii_ready_paged(phydev, pcs_sts, mii_sts);

out_restore:

	chip_ctrl = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL);
	if (chip_ctrl < 0) {
		restore_err = chip_ctrl;
		restore = orig;
	} else {
		restore = (chip_ctrl & ~VEND1_CHIP_CTRL_XFI_ACCESS) |
			  (orig & VEND1_CHIP_CTRL_XFI_ACCESS);
	}

	restored =
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL, restore);
	if (!restored)
		restored =
			phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_CHIP_CTRL);
	if (restored < 0 || ((restored ^ orig) & VEND1_CHIP_CTRL_XFI_ACCESS)) {
		phydev_err(phydev,
			   "USXGMII readiness sample failed to restore XFI gate from chip_ctrl %04x (readback/err %d)\n",
			   orig, restored);
		if (ret >= 0)
			ret = restored < 0 ? restored : -EIO;
	} else if (restore_err && ret >= 0) {
		ret = restore_err;
	}

unlock:
	mutex_unlock(&priv->sds_lock);
	return ret;
}

static int as22xxx_wait_usxgmii_ready(struct phy_device *phydev)
{
	int pcs_sts = 0, mii_sts = 0;
	int ready = 0, ret;

	ret = read_poll_timeout(as22xxx_usxgmii_ready_once, ready, ready,
				AS22XXX_SDS_READY_POLL_US,
				AS22XXX_SDS_READY_TIMEOUT_US, false,
				phydev, &pcs_sts, &mii_sts);
	if (ready < 0)
		ret = ready;

	if (ret)
		phydev_err(phydev,
			   "system-side USXGMII did not become ready: PCS_STS20=%04x MII_STS=%04x: %d\n",
			   pcs_sts, mii_sts, ret);

	return ret;
}

static int as22xxx_config_inband(struct phy_device *phydev, unsigned int modes)
{
	struct as21xxx_priv *priv = phydev->priv;
	int ret;

	if (modes != LINK_INBAND_BYPASS)
		return -EOPNOTSUPP;

	if (!priv->mode_switch ||
	    priv->system_sync_state == AS22XXX_SYSTEM_SYNC_IDLE)
		return 0;

	if (phydev->interface != PHY_INTERFACE_MODE_USXGMII ||
	    priv->sds_interface != PHY_INTERFACE_MODE_USXGMII)
		return -EINVAL;

	if (priv->system_sync_state ==
	    AS22XXX_SYSTEM_SYNC_NEEDS_PHY_ENABLE) {
		ret = aeon_ipc_phy_enable_mode(phydev, false);
		if (ret)
			return ret;

		ret = aeon_ipc_phy_enable_mode(phydev, true);
		if (ret)
			return ret;

		priv->system_sync_state =
			AS22XXX_SYSTEM_SYNC_WAIT_PHY_ENABLE;

		ret = aeon_ipc_get_fw_version(phydev);
		if (ret)
			return ret;

		return as22xxx_stage_system_side(phydev,
						PHY_INTERFACE_MODE_USXGMII,
						AEON_SDS_PCS_SEL_64B66B,
						AEON_SDS_SPD_10G);
	}

	ret = as22xxx_wait_usxgmii_ready(phydev);
	if (ret)
		return ret;

	ret = as22xxx_check_sds_config(phydev,
				       AEON_SDS_PCS_SEL_64B66B,
				       AEON_SDS_SPD_10G);
	if (ret)
		return ret;

	priv->system_sync_state = AS22XXX_SYSTEM_SYNC_IDLE;
	return 0;
}

static int as22xxx_read_status(struct phy_device *phydev)
{
	struct as21xxx_priv *priv = phydev->priv;
	int bmcr, old_link = phydev->link;
	int ret;

	ret = as22xxx_read_link(phydev, &bmcr);
	if (ret)
		return ret;

	if (!priv->mode_switch && phydev->autoneg == AUTONEG_ENABLE && old_link &&
	    phydev->link &&
	    priv->sds_interface != PHY_INTERFACE_MODE_NA)
		return 0;

	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;
	phydev->pause = 0;
	phydev->asym_pause = 0;

	if (phydev->autoneg == AUTONEG_ENABLE) {
		ret = genphy_c45_read_lpa(phydev);
		if (ret)
			return ret;

		ret = as22xxx_read_lpa(phydev);
		if (ret)
			return ret;

		if (phydev->autoneg_complete) {
			as22xxx_read_speed(phydev, bmcr);
			phy_resolve_aneg_linkmode(phydev);
		}
	} else {
		linkmode_zero(phydev->lp_advertising);
		as22xxx_read_speed(phydev, bmcr);
	}

	if (!as22xxx_update_interface(phydev))
		phydev->link = 0;

	return 0;
}

static int as21xxx_led_brightness_set(struct phy_device *phydev,
				      u8 index, enum led_brightness value)
{
	u16 val = VEND1_LED_REG_A_EVENT_OFF;

	if (index > AEON_MAX_LEDS)
		return -EINVAL;

	if (value)
		val = VEND1_LED_REG_A_EVENT_ON;

	return phy_modify_mmd(phydev, MDIO_MMD_VEND1,
			      VEND1_LED_REG(index),
			      VEND1_LED_REG_A_EVENT,
			      FIELD_PREP(VEND1_LED_REG_A_EVENT, val));
}

static int as21xxx_led_hw_is_supported(struct phy_device *phydev, u8 index,
				       unsigned long rules)
{
	int i;

	if (index > AEON_MAX_LEDS)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(as21xxx_led_supported_pattern); i++)
		if (rules == as21xxx_led_supported_pattern[i].pattern)
			return 0;

	return -EOPNOTSUPP;
}

static int as21xxx_led_hw_control_get(struct phy_device *phydev, u8 index,
				      unsigned long *rules)
{
	int i, val;

	if (index > AEON_MAX_LEDS)
		return -EINVAL;

	val = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_LED_REG(index));
	if (val < 0)
		return val;

	val = FIELD_GET(VEND1_LED_REG_A_EVENT, val);
	for (i = 0; i < ARRAY_SIZE(as21xxx_led_supported_pattern); i++)
		if (val == as21xxx_led_supported_pattern[i].val) {
			*rules = as21xxx_led_supported_pattern[i].pattern;
			return 0;
		}

	return -EINVAL;
}

static int as21xxx_led_hw_control_set(struct phy_device *phydev, u8 index,
				      unsigned long rules)
{
	u16 val = 0;
	int i;

	if (index > AEON_MAX_LEDS)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(as21xxx_led_supported_pattern); i++)
		if (rules == as21xxx_led_supported_pattern[i].pattern) {
			val = as21xxx_led_supported_pattern[i].val;
			break;
		}

	return phy_modify_mmd(phydev, MDIO_MMD_VEND1,
			      VEND1_LED_REG(index),
			      VEND1_LED_REG_A_EVENT,
			      FIELD_PREP(VEND1_LED_REG_A_EVENT, val));
}

static int as21xxx_led_polarity_set(struct phy_device *phydev, int index,
				    unsigned long modes)
{
	bool led_active_low = false;
	u16 mask, val = 0;
	u32 mode;

	if (index > AEON_MAX_LEDS)
		return -EINVAL;

	for_each_set_bit(mode, &modes, __PHY_LED_MODES_NUM) {
		switch (mode) {
		case PHY_LED_ACTIVE_LOW:
			led_active_low = true;
			break;
		case PHY_LED_ACTIVE_HIGH: /* default mode */
			led_active_low = false;
			break;
		default:
			return -EINVAL;
		}
	}

	mask = VEND1_GLB_CPU_CTRL_LED_POLARITY(index);
	if (led_active_low)
		val = VEND1_GLB_CPU_CTRL_LED_POLARITY(index);

	return phy_modify_mmd(phydev, MDIO_MMD_VEND1,
			      VEND1_GLB_REG_CPU_CTRL,
			      mask, val);
}

static int as21xxx_match_phy_device(struct phy_device *phydev,
				    const struct phy_driver *phydrv)
{
	struct as21xxx_priv *priv;
	u16 ret_sts;
	u32 phy_id;
	int ret;

	/* Skip PHY that are not AS21xxx */
	if (!phy_id_compare_vendor(phydev->c45_ids.device_ids[MDIO_MMD_PCS],
				   PHY_VENDOR_AEONSEMI))
		return genphy_match_phy_device(phydev, phydrv);

	/* Read PHY ID to handle firmware loaded or HW reset */
	ret = phy_read_mmd(phydev, MDIO_MMD_PCS, MII_PHYSID1);
	if (ret < 0)
		return ret;
	phy_id = ret << 16;

	ret = phy_read_mmd(phydev, MDIO_MMD_PCS, MII_PHYSID2);
	if (ret < 0)
		return ret;
	phy_id |= ret;

	/* With PHY ID not the generic AS21xxx one assume
	 * the firmware just loaded
	 */
	if (phy_id != PHY_ID_AS21XXX)
		return phy_id == phydrv->phy_id;

	/* Allocate temp priv and load the firmware */
	priv = kzalloc_obj(*priv);
	if (!priv)
		return -ENOMEM;

	mutex_init(&priv->ipc_lock);

	ret = aeon_firmware_load(phydev);
	if (ret)
		goto out;

	/* Sync parity... */
	ret = aeon_ipc_sync_parity(phydev, priv);
	if (ret)
		goto out;

	/* ...and send a third NOOP cmd to wait for firmware finish loading */
	ret = aeon_ipc_noop(phydev, priv, &ret_sts);
	if (ret)
		goto out;

out:
	mutex_destroy(&priv->ipc_lock);
	kfree(priv);

	/* Return can either be 0 or a negative error code.
	 * Returning 0 here means THIS is NOT a suitable PHY.
	 *
	 * For the specific case of the generic Aeonsemi PHY ID that
	 * needs the firmware the be loaded first to have a correct PHY ID,
	 * this is OK as a matching PHY ID will be found right after.
	 * This relies on the driver probe order where the first PHY driver
	 * probed is the generic one.
	 */
	return ret;
}

static int as22xxx_match_phy_device(struct phy_device *phydev,
				    const struct phy_driver *phydrv)
{
	u32 phy_id;
	int ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MII_PHYSID1);
	if (ret < 0)
		return ret;
	phy_id = ret << 16;

	ret = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MII_PHYSID2);
	if (ret < 0)
		return ret;
	phy_id |= ret;

	return phy_id == PHY_ID_AS22XXX;
}

static int as22xxx_bringup(struct phy_device *phydev)
{
	struct as21xxx_priv *priv = phydev->priv;
	int ret;

	priv->sds_interface = PHY_INTERFACE_MODE_NA;
	priv->sds_speed = SPEED_UNKNOWN;
	priv->system_sync_state = AS22XXX_SYSTEM_SYNC_IDLE;

	ret = aeon_firmware_load(phydev);
	if (ret)
		return ret;

	ret = read_poll_timeout(aeon_ipc_sync_parity, ret, !ret,
				0, 10000000, false, phydev, priv);
	if (ret) {
		phydev_err(phydev, "timed out waiting for firmware boot\n");
		return ret;
	}

	ret = aeon_ipc_get_fw_version(phydev);
	if (ret)
		return ret;

	ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_PTP_CLK,
			       VEND1_PTP_CLK_EN);
	if (ret)
		return ret;

	if (priv->mode_switch) {
		ret = as22xxx_program_system_side(phydev,
						  PHY_INTERFACE_MODE_USXGMII,
						  AEON_SDS_PCS_SEL_64B66B,
						  AEON_SDS_SPD_10G);
		if (ret)
			return ret;

		priv->sds_interface = PHY_INTERFACE_MODE_USXGMII;
		priv->sds_speed = SPEED_UNKNOWN;
	}

	if (priv->mode_switch) {
		ret = aeon_dpc_eth_sts_hw_update(phydev, true);
		if (ret) {
			phydev_err(phydev,
				   "failed to enable DPC Ethernet-status hardware follow: %d\n",
				   ret);
			return ret;
		}
	}

	return 0;
}

static int as22xxx_config_init(struct phy_device *phydev)
{
	struct as21xxx_priv *priv = phydev->priv;

	if (priv->mode_switch) {
		phy_interface_zero(phydev->possible_interfaces);
		__set_bit(PHY_INTERFACE_MODE_USXGMII,
			  phydev->possible_interfaces);
		__set_bit(PHY_INTERFACE_MODE_SGMII,
			  phydev->possible_interfaces);
	}

	if (!aeon_ipc_sync_parity(phydev, priv))
		return 0;

	return as22xxx_bringup(phydev);
}

static int as22xxx_probe(struct phy_device *phydev)
{
	struct as21xxx_priv *priv;
	int ret;

	phydev->c45_ids.mmds_present |= MDIO_DEVS_PMAPMD | MDIO_DEVS_PCS |
					MDIO_DEVS_AN;

	priv = devm_kzalloc(&phydev->mdio.dev,
			    sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	phydev->priv = priv;
	priv->mode_switch = device_property_read_bool(&phydev->mdio.dev,
						      "sgmii-usxgmii-switch-quirk");

	ret = devm_mutex_init(&phydev->mdio.dev,
			      &priv->ipc_lock);
	if (ret)
		return ret;
	ret = devm_mutex_init(&phydev->mdio.dev, &priv->sds_lock);
	if (ret)
		return ret;

	return as22xxx_bringup(phydev);
}

static struct phy_driver as21xxx_drivers[] = {
	{
		/* PHY expose in C45 as 0x7500 0x9410
		 * before firmware is loaded.
		 * This driver entry must be attempted first to load
		 * the firmware and thus update the ID registers.
		 */
		PHY_ID_MATCH_EXACT(PHY_ID_AS21XXX),
		.name		= "Aeonsemi AS21xxx",
		.match_phy_device = as21xxx_match_phy_device,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21011JB1),
		.name		= "Aeonsemi AS21011JB1",
		.probe		= as21xxx_probe,
		.match_phy_device = as21xxx_match_phy_device,
		.read_status	= as21xxx_read_status,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21011PB1),
		.name		= "Aeonsemi AS21011PB1",
		.probe		= as21xxx_probe,
		.match_phy_device = as21xxx_match_phy_device,
		.read_status	= as21xxx_read_status,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21010PB1),
		.name		= "Aeonsemi AS21010PB1",
		.probe		= as21xxx_probe,
		.match_phy_device = as21xxx_match_phy_device,
		.read_status	= as21xxx_read_status,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21010JB1),
		.name		= "Aeonsemi AS21010JB1",
		.probe		= as21xxx_probe,
		.match_phy_device = as21xxx_match_phy_device,
		.read_status	= as21xxx_read_status,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21210PB1),
		.name		= "Aeonsemi AS21210PB1",
		.probe		= as21xxx_probe,
		.match_phy_device = as21xxx_match_phy_device,
		.read_status	= as21xxx_read_status,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21510JB1),
		.name		= "Aeonsemi AS21510JB1",
		.probe		= as21xxx_probe,
		.match_phy_device = as21xxx_match_phy_device,
		.read_status	= as21xxx_read_status,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21510PB1),
		.name		= "Aeonsemi AS21510PB1",
		.probe		= as21xxx_probe,
		.match_phy_device = as21xxx_match_phy_device,
		.read_status	= as21xxx_read_status,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21511JB1),
		.name		= "Aeonsemi AS21511JB1",
		.probe		= as21xxx_probe,
		.match_phy_device = as21xxx_match_phy_device,
		.read_status	= as21xxx_read_status,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21210JB1),
		.name		= "Aeonsemi AS21210JB1",
		.probe		= as21xxx_probe,
		.match_phy_device = as21xxx_match_phy_device,
		.read_status	= as21xxx_read_status,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS21511PB1),
		.name		= "Aeonsemi AS21511PB1",
		.probe		= as21xxx_probe,
		.match_phy_device = as21xxx_match_phy_device,
		.read_status	= as21xxx_read_status,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
	{
		PHY_ID_MATCH_EXACT(PHY_ID_AS22XXX),
		.name		= "Aeonsemi AS22XXX",
		.probe		= as22xxx_probe,
		.match_phy_device = as22xxx_match_phy_device,
		.config_init	= as22xxx_config_init,
		.config_aneg	= as22xxx_config_aneg,
		.config_inband	= as22xxx_config_inband,
		.read_status	= as22xxx_read_status,
		.led_brightness_set = as21xxx_led_brightness_set,
		.led_hw_is_supported = as21xxx_led_hw_is_supported,
		.led_hw_control_set = as21xxx_led_hw_control_set,
		.led_hw_control_get = as21xxx_led_hw_control_get,
		.led_polarity_set = as21xxx_led_polarity_set,
	},
};
module_phy_driver(as21xxx_drivers);

static struct mdio_device_id __maybe_unused as21xxx_tbl[] = {
	{ PHY_ID_MATCH_VENDOR(PHY_VENDOR_AEONSEMI) },
	{ }
};
MODULE_DEVICE_TABLE(mdio, as21xxx_tbl);

MODULE_DESCRIPTION("Aeonsemi AS21xxx PHY driver");
MODULE_AUTHOR("Christian Marangi <ansuelsmth@gmail.com>");
MODULE_LICENSE("GPL");
