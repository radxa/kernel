// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2010 Broadcom Corporation
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/netdevice.h>
#include <linux/module.h>
#include <linux/firmware.h>
#include <brcmu_wifi.h>
#include <brcmu_utils.h>
#include "core.h"
#include "bus.h"
#include "debug.h"
#include "fwil.h"
#include "fwil_types.h"
#include "tracepoint.h"
#include "common.h"
#include "of.h"
#include "firmware.h"
#include "chip.h"
#include "defs.h"
#include "fweh.h"
#include <brcm_hw_ids.h>
#include <linux/reboot.h>
#include <linux/notifier.h>
#include "pcie.h"
#include "sdio.h"

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("Broadcom 802.11 wireless LAN fullmac driver.");
MODULE_LICENSE("Dual BSD/GPL");

#define BRCMF_DEFAULT_SCAN_CHANNEL_TIME	40
#define BRCMF_DEFAULT_SCAN_UNASSOC_TIME	40

/* default boost value for RSSI_DELTA in preferred join selection */
#define BRCMF_JOIN_PREF_RSSI_BOOST	8

#define BRCMF_DEFAULT_TXGLOM_SIZE	32  /* max tx frames in glom chain */

static int brcmf_sdiod_txglomsz = BRCMF_DEFAULT_TXGLOM_SIZE;
module_param_named(txglomsz, brcmf_sdiod_txglomsz, int, 0);
MODULE_PARM_DESC(txglomsz, "Maximum tx packet chain size [SDIO]");

/* Debug level configuration. See debug.h for bits, sysfs modifiable */
int brcmf_msg_level;
module_param_named(debug, brcmf_msg_level, int, 0600);
MODULE_PARM_DESC(debug, "Level of debug output");

static int brcmf_p2p_enable;
module_param_named(p2pon, brcmf_p2p_enable, int, 0);
MODULE_PARM_DESC(p2pon, "Enable legacy p2p management functionality");

static u8 brcmf_feature_disable[BRCMF_MAX_FEATURE_BYTES] = {0};
static int brcmf_feature_disable_size;
module_param_array_named(feature_disable, brcmf_feature_disable, byte,
			 &brcmf_feature_disable_size, 0644);
MODULE_PARM_DESC(feature_disable, "Disable features (bitmap)");

static char brcmf_firmware_path[BRCMF_FW_ALTPATH_LEN];
module_param_string(alternative_fw_path, brcmf_firmware_path,
		    BRCMF_FW_ALTPATH_LEN, 0400);
MODULE_PARM_DESC(alternative_fw_path, "Alternative firmware path");

static int brcmf_fcmode = 2;
module_param_named(fcmode, brcmf_fcmode, int, 0);
MODULE_PARM_DESC(fcmode, "Mode of firmware signalled flow control");

static int brcmf_roamoff;
module_param_named(roamoff, brcmf_roamoff, int, 0400);
MODULE_PARM_DESC(roamoff,
		 "Do not use fw roaming engine: 0=use fw_roam, 1=fw_roam off & report BCNLOST_MSG, 2=fw_roam off & report DISCONNECTED");

static int brcmf_iapp_enable;
module_param_named(iapp, brcmf_iapp_enable, int, 0);
MODULE_PARM_DESC(iapp, "Enable partial support for the obsoleted Inter-Access Point Protocol");

static int brcmf_eap_restrict;
module_param_named(eap_restrict, brcmf_eap_restrict, int, 0400);
MODULE_PARM_DESC(eap_restrict, "Block non-802.1X frames until auth finished");

static int brcmf_max_pm;
module_param_named(max_pm, brcmf_max_pm, int, 0);
MODULE_PARM_DESC(max_pm, "Use max power management mode by default");

int brcmf_pkt_prio_enable;
module_param_named(pkt_prio, brcmf_pkt_prio_enable, int, 0);
MODULE_PARM_DESC(pkt_prio, "Support for update the packet priority");

#ifdef DEBUG
/* always succeed brcmf_bus_started() */
static int brcmf_ignore_probe_fail;
module_param_named(ignore_probe_fail, brcmf_ignore_probe_fail, int, 0);
MODULE_PARM_DESC(ignore_probe_fail, "always succeed probe for debugging");
#endif

static int brcmf_fw_ap_select;
module_param_named(fw_ap_select, brcmf_fw_ap_select, int, 0400);
MODULE_PARM_DESC(fw_ap_select, "Allow FW for AP selection");

static int brcmf_disable_6ghz;
module_param_named(disable_6ghz, brcmf_disable_6ghz, int, 0400);
MODULE_PARM_DESC(disable_6ghz, "Disable 6GHz Operation");

static int brcmf_sdio_in_isr;
module_param_named(sdio_in_isr, brcmf_sdio_in_isr, int, 0400);
MODULE_PARM_DESC(sdio_in_isr, "Handle SDIO DPC in ISR");

static int brcmf_sdio_rxf_in_kthread;
module_param_named(sdio_rxf_thread, brcmf_sdio_rxf_in_kthread, int, 0400);
MODULE_PARM_DESC(sdio_rxf_thread, "SDIO RX Frame in Kthread");

unsigned int brcmf_offload_prof = BRCMF_OL_PROF_TYPE_LOW_PWR;
module_param_named(offload_prof, brcmf_offload_prof, uint, 0400);
MODULE_PARM_DESC(offload_prof,
		 "Offload power profile: 1:low 2:mid 3:high (default:1)");

unsigned int brcmf_offload_feat = BRCMF_OL_ARP |
				  BRCMF_OL_ND |
				  BRCMF_OL_BDO |
				  BRCMF_OL_ICMP |
				  BRCMF_OL_TKO |
				  BRCMF_OL_DLTRO |
				  BRCMF_OL_PNO |
				  BRCMF_OL_KEEPALIVE |
				  BRCMF_OL_GTKOE |
				  BRCMF_OL_WOWLPF;
module_param_named(offload_feat, brcmf_offload_feat, uint, 0400);
MODULE_PARM_DESC(offload_feat,
		 "Offload feat bitmap: 0:arp 1:nd 2:mdns 3:icmp 4:tcp-keepalive "
		 "5:dhcp-renewal 6:pno 7:keepalive 8:gtk 9:wowlpf (default: 0x1FF)");

static int brcmf_bt_over_sdio;
module_param_named(bt_over_sdio, brcmf_bt_over_sdio, int, 0);
MODULE_PARM_DESC(bt_over_sdio, "Enable BT over SDIO");

static int brcmf_sdio_idleclk_disable = BRCMFMAC_AUTO;
module_param_named(sdio_idleclk_disable, brcmf_sdio_idleclk_disable, int, 0644);
MODULE_PARM_DESC(sdio_idleclk_disable, "Disable SDIO idle clock");

static int brcmf_sdio_bus_idle_time = BRCMF_DEFAULT_SDIO_IDLE_CONFIG;
module_param_named(bus_idle_time, brcmf_sdio_bus_idle_time, int, 0644);
MODULE_PARM_DESC(bus_idle_time, "Config SDIO bus idle time");

static int brcmf_short_psq;
module_param_named(short_psq, brcmf_short_psq, int, 0);
MODULE_PARM_DESC(short_psq, "Use shorter PS Queue");

static struct brcmfmac_platform_data *brcmfmac_pdata;
struct brcmf_mp_global_t brcmf_mp_global;

static int brcmf_reboot_callback(struct notifier_block *this, unsigned long code, void *unused);
static struct notifier_block brcmf_reboot_notifier = {
	.notifier_call = brcmf_reboot_callback,
	.priority = 1,
};

/* Offload features to firmware based on a user based power profile using module param
 * offload_prof and offload_feat (provides flag list of all offloads).
 * Default power profile : LowPwr with all offloads enabled.
 */
void brcmf_generic_offload_config(struct brcmf_if *ifp, unsigned int ol_feat,
				  unsigned int ol_profile, bool reset)
{
	struct brcmf_ol_cfg_v1 ol_cfg = {0};
	u32 ol_feat_skip = ~ol_feat;
	int err = 0;

	ol_cfg.ver = BRCMF_OL_CFG_VER_1;
	ol_cfg.len = sizeof(ol_cfg);
	ol_cfg.id = BRCMF_OL_CFG_ID_PROF;
	ol_cfg.offload_skip = ol_feat_skip;
	ol_cfg.u.ol_profile.reset = reset;
	ol_cfg.u.ol_profile.type = ol_profile;

	err = brcmf_fil_iovar_data_set(ifp, "offload_config", &ol_cfg,
				       sizeof(ol_cfg));
	if (err < 0)
		brcmf_err("failed to %s generic offload profile:%u feat:0x%x, err = %d",
			  reset ? "reset" : "set", ol_profile, ol_feat, err);
	else
		brcmf_info("successfully %s generic offload profile:%u feat:0x%x",
			   reset ? "reset" : "set", ol_profile, ol_feat);
}

/* Enable specific offloads that are not enabled in a power profile but have
 * to be enabled in suspend state as host goes to sleep.
 */
void brcmf_generic_offload_enable(struct brcmf_if *ifp, unsigned int ol_feat,
				  bool enable)
{
	struct brcmf_ol_cfg_v1 ol_cfg = {0};
	u32 ol_feat_skip = ~ol_feat;
	int err = 0;

	ol_cfg.ver = BRCMF_OL_CFG_VER_1;
	ol_cfg.len = sizeof(ol_cfg);
	ol_cfg.id = BRCMF_OL_CFG_ID_ACTIVATE;
	ol_cfg.u.ol_activate.enable = enable;
	ol_cfg.offload_skip = ol_feat_skip;

	err = brcmf_fil_iovar_data_set(ifp, "offload_config", &ol_cfg,
				       sizeof(ol_cfg));
	if (err < 0)
		brcmf_err("failed to %s generic offload feat:0x%x, err = %d",
			  enable ? "enable" : "disable", ol_feat, err);
	else
		brcmf_info("successfully %s generic offload feat:0x%x",
			   enable ? "enabled" : "disabled", ol_feat);
}

void brcmf_generic_offload_host_ipv4_update(struct brcmf_if *ifp, unsigned int ol_feat,
					    u32 ipaddr, bool is_add)
{
	struct brcmf_ol_cfg_v1 ol_cfg = {0};
	u32 ol_feat_skip = ~ol_feat;
	int err = 0;

	ol_cfg.ver = BRCMF_OL_CFG_VER_1;
	ol_cfg.len = sizeof(ol_cfg);
	ol_cfg.id = BRCMF_OL_CFG_ID_INET_V4;
	ol_cfg.u.ol_inet_v4.del = !is_add;
	memcpy(ol_cfg.u.ol_inet_v4.host_ipv4.addr, &ipaddr, sizeof(struct ipv4_addr));
	ol_cfg.offload_skip = ol_feat_skip;

	err = brcmf_fil_iovar_data_set(ifp, "offload_config", &ol_cfg,
				       sizeof(ol_cfg));
	if (err < 0)
		brcmf_err("failed to %s generic offload host address %pI4, err = %d",
			  is_add ? "add" : "del", &ipaddr, err);
	else
		brcmf_dbg(TRACE, "successfully %s generic offload host address %pI4",
			  is_add ? "added" : "deleted", &ipaddr);
}

int brcmf_generic_offload_host_ipv6_update(struct brcmf_if *ifp, unsigned int ol_feat,
					   void *ptr, u8 type, bool is_add)
{
	struct brcmf_ol_cfg_v1 ol_cfg = {0};
	u32 ol_feat_skip = ~ol_feat;
	int err = 0;

	ol_cfg.ver = BRCMF_OL_CFG_VER_1;
	ol_cfg.len = sizeof(ol_cfg);
	ol_cfg.id = BRCMF_OL_CFG_ID_INET_V6;
	ol_cfg.u.ol_inet_v6.del = !is_add;
	ol_cfg.u.ol_inet_v6.type = type;
	memcpy(ol_cfg.u.ol_inet_v6.host_ipv6.addr, ptr, sizeof(struct ipv6_addr));
	ol_cfg.offload_skip = ol_feat_skip;

	err = brcmf_fil_iovar_data_set(ifp, "offload_config", &ol_cfg,
				       sizeof(ol_cfg));
	if (err < 0)
		brcmf_err("failed to %s host address %pI6 err = %d",
			  is_add ? "add" : "del", ptr, err);
	else
		brcmf_dbg(TRACE, "successfully %s host address %pI6",
			  is_add ? "add" : "del", ptr);

	return err;
}

void brcmf_c_set_joinpref_default(struct brcmf_if *ifp)
{
	struct brcmf_pub *drvr = ifp->drvr;
	struct brcmf_join_pref_params join_pref_params[2];
	int err;

	/* Setup join_pref to select target by RSSI (boost on 5GHz) */
	join_pref_params[0].type = BRCMF_JOIN_PREF_RSSI_DELTA;
	join_pref_params[0].len = 2;
	join_pref_params[0].rssi_gain = BRCMF_JOIN_PREF_RSSI_BOOST;
	join_pref_params[0].band = WLC_BAND_5G;

	join_pref_params[1].type = BRCMF_JOIN_PREF_RSSI;
	join_pref_params[1].len = 2;
	join_pref_params[1].rssi_gain = 0;
	join_pref_params[1].band = 0;
	err = brcmf_fil_iovar_data_set(ifp, "join_pref", join_pref_params,
				       sizeof(join_pref_params));
	if (err)
		bphy_err(drvr, "Set join_pref error (%d)\n", err);
}

static int brcmf_c_download(struct brcmf_if *ifp, u16 flag,
			    struct brcmf_dload_data_le *dload_buf,
			    u32 len)
{
	s32 err;

	flag |= (DLOAD_HANDLER_VER << DLOAD_FLAG_VER_SHIFT);
	dload_buf->flag = cpu_to_le16(flag);
	dload_buf->dload_type = cpu_to_le16(DL_TYPE_CLM);
	dload_buf->len = cpu_to_le32(len);
	dload_buf->crc = cpu_to_le32(0);
	len = sizeof(*dload_buf) + len - 1;

	err = brcmf_fil_iovar_data_set(ifp, "clmload", dload_buf, len);

	return err;
}

static int brcmf_c_process_clm_blob(struct brcmf_if *ifp)
{
	struct brcmf_pub *drvr = ifp->drvr;
	struct brcmf_bus *bus = drvr->bus_if;
	struct brcmf_dload_data_le *chunk_buf;
	const struct firmware *clm = NULL;
	u32 chunk_len;
	u32 datalen;
	u32 cumulative_len;
	u16 dl_flag = DL_BEGIN;
	u32 status;
	s32 err;

	brcmf_dbg(TRACE, "Enter\n");

	err = brcmf_bus_get_blob(bus, &clm, BRCMF_BLOB_CLM);
	if (err || !clm) {
		brcmf_info("no clm_blob available (err=%d), device may have limited channels available\n",
			   err);
		return 0;
	}

	chunk_buf = kzalloc(sizeof(*chunk_buf) + MAX_CHUNK_LEN - 1, GFP_KERNEL);
	if (!chunk_buf) {
		err = -ENOMEM;
		goto done;
	}

	datalen = clm->size;
	cumulative_len = 0;
	do {
		if (datalen > MAX_CHUNK_LEN) {
			chunk_len = MAX_CHUNK_LEN;
		} else {
			chunk_len = datalen;
			dl_flag |= DL_END;
		}
		memcpy(chunk_buf->data, clm->data + cumulative_len, chunk_len);

		err = brcmf_c_download(ifp, dl_flag, chunk_buf, chunk_len);

		dl_flag &= ~DL_BEGIN;

		cumulative_len += chunk_len;
		datalen -= chunk_len;
	} while ((datalen > 0) && (err == 0));

	if (err) {
		bphy_err(drvr, "clmload (%zu byte file) failed (%d)\n",
			 clm->size, err);
		/* Retrieve clmload_status and print */
		err = brcmf_fil_iovar_int_get(ifp, "clmload_status", &status);
		if (err)
			bphy_err(drvr, "get clmload_status failed (%d)\n", err);
		else
			brcmf_dbg(INFO, "clmload_status=%d\n", status);
		err = -EIO;
	}

	kfree(chunk_buf);
done:
	release_firmware(clm);
	return err;
}

int brcmf_c_set_cur_etheraddr(struct brcmf_if *ifp, const u8 *addr)
{
	s32 err;

	err = brcmf_fil_iovar_data_set(ifp, "cur_etheraddr", addr, ETH_ALEN);
	if (err < 0)
		bphy_err(ifp->drvr, "Setting cur_etheraddr failed, %d\n", err);

	return err;
}

/* On some boards there is no eeprom to hold the nvram, in this case instead
 * a board specific nvram is loaded from /lib/firmware. On most boards the
 * macaddr setting in the /lib/firmware nvram file is ignored because the
 * wifibt chip has a unique MAC programmed into the chip itself.
 * But in some cases the actual MAC from the /lib/firmware nvram file gets
 * used, leading to MAC conflicts.
 * The MAC addresses in the troublesome nvram files seem to all come from
 * the same nvram file template, so we only need to check for 1 known
 * address to detect this.
 */
static const u8 brcmf_default_mac_address[ETH_ALEN] = {
	0x00, 0x90, 0x4c, 0xc5, 0x12, 0x38
};

int brcmf_c_preinit_dcmds(struct brcmf_if *ifp)
{
	struct brcmf_pub *drvr = ifp->drvr;
	s8 eventmask[BRCMF_EVENTING_MASK_LEN];
	u8 buf[BRCMF_DCMD_SMLEN];
	struct brcmf_bus *bus;
	struct brcmf_rev_info_le revinfo;
	struct brcmf_rev_info *ri;
	struct brcmf_wlc_version_le wlc_ver;
	char *clmver;
	char *ptr;
	s32 err;
	struct eventmsgs_ext *eventmask_msg = NULL;
	u8 msglen;

	if (is_valid_ether_addr(ifp->mac_addr)) {
		/* set mac address */
		err = brcmf_c_set_cur_etheraddr(ifp, ifp->mac_addr);
		if (err < 0)
			goto done;
	} else {
		/* retrieve mac address */
		err = brcmf_fil_iovar_data_get(ifp, "cur_etheraddr", ifp->mac_addr,
					       sizeof(ifp->mac_addr));
		if (err < 0) {
			bphy_err(drvr, "Retrieving cur_etheraddr failed, %d\n", err);
			goto done;
		}

		if (ether_addr_equal_unaligned(ifp->mac_addr, brcmf_default_mac_address)) {
			bphy_err(drvr, "Default MAC is used, replacing with random MAC to avoid conflicts\n");
			eth_random_addr(ifp->mac_addr);
			ifp->ndev->addr_assign_type = NET_ADDR_RANDOM;
			err = brcmf_c_set_cur_etheraddr(ifp, ifp->mac_addr);
			if (err < 0)
				goto done;
		}
	}

	memcpy(ifp->drvr->mac, ifp->mac_addr, sizeof(ifp->drvr->mac));
	memcpy(ifp->drvr->wiphy->perm_addr, ifp->drvr->mac, ETH_ALEN);

	bus = ifp->drvr->bus_if;
	ri = &ifp->drvr->revinfo;

	err = brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_REVINFO,
				     &revinfo, sizeof(revinfo));
	if (err < 0) {
		bphy_err(drvr, "retrieving revision info failed, %d\n", err);
		strscpy(ri->chipname, "UNKNOWN", sizeof(ri->chipname));
	} else {
		ri->vendorid = le32_to_cpu(revinfo.vendorid);
		ri->deviceid = le32_to_cpu(revinfo.deviceid);
		ri->radiorev = le32_to_cpu(revinfo.radiorev);
		ri->corerev = le32_to_cpu(revinfo.corerev);
		ri->boardid = le32_to_cpu(revinfo.boardid);
		ri->boardvendor = le32_to_cpu(revinfo.boardvendor);
		ri->boardrev = le32_to_cpu(revinfo.boardrev);
		ri->driverrev = le32_to_cpu(revinfo.driverrev);
		ri->ucoderev = le32_to_cpu(revinfo.ucoderev);
		ri->bus = le32_to_cpu(revinfo.bus);
		ri->phytype = le32_to_cpu(revinfo.phytype);
		ri->phyrev = le32_to_cpu(revinfo.phyrev);
		ri->anarev = le32_to_cpu(revinfo.anarev);
		ri->chippkg = le32_to_cpu(revinfo.chippkg);
		ri->nvramrev = le32_to_cpu(revinfo.nvramrev);

		/* use revinfo if not known yet */
		if (!bus->chip) {
			bus->chip = le32_to_cpu(revinfo.chipnum);
			bus->chiprev = le32_to_cpu(revinfo.chiprev);
		}
	}
	ri->result = err;

	if (bus->chip)
		brcmf_chip_name(bus->chip, bus->chiprev,
				ri->chipname, sizeof(ri->chipname));

	/* Do any CLM downloading */
	err = brcmf_c_process_clm_blob(ifp);
	if (err < 0) {
		bphy_err(drvr, "download CLM blob file failed, %d\n", err);
		goto done;
	}

	/* query for 'ver' to get version info from firmware */
	memset(buf, 0, sizeof(buf));
	err = brcmf_fil_iovar_data_get(ifp, "ver", buf, sizeof(buf));
	if (err < 0) {
		bphy_err(drvr, "Retrieving version information failed, %d\n",
			 err);
		goto done;
	}
	buf[sizeof(buf) - 1] = '\0';
	ptr = (char *)buf;
	strsep(&ptr, "\n");

	/* Print fw version info */
	brcmf_info("Firmware: %s %s\n", ri->chipname, buf);

	/* locate firmware version number for ethtool */
	ptr = strrchr(buf, ' ');
	if (!ptr) {
		bphy_err(drvr, "Retrieving version number failed");
		goto done;
	}
	strscpy(ifp->drvr->fwver, ptr + 1, sizeof(ifp->drvr->fwver));

	/* Get wlc interface version, set to 0 for legacy chip
	 * that is not supporting wlc_ver iovar
	 */
	err = brcmf_fil_iovar_data_get(ifp, "wlc_ver", &wlc_ver, sizeof(wlc_ver));
	if (err < 0) {
		ifp->drvr->wlc_ver.wlc_ver_major = 0;
		ifp->drvr->wlc_ver.wlc_ver_minor = 0;
	} else {
		ifp->drvr->wlc_ver.wlc_ver_major = le16_to_cpu(wlc_ver.wlc_ver_major);
		ifp->drvr->wlc_ver.wlc_ver_minor = le16_to_cpu(wlc_ver.wlc_ver_minor);
	}
	brcmf_dbg(TRACE, "wlc interface version, major=%d, minor=%d\n",
		  ifp->drvr->wlc_ver.wlc_ver_major,
		  ifp->drvr->wlc_ver.wlc_ver_minor);

	/* Query for 'clmver' to get CLM version info from firmware */
	memset(buf, 0, sizeof(buf));
	err = brcmf_fil_iovar_data_get(ifp, "clmver", buf, sizeof(buf));
	if (err) {
		brcmf_dbg(TRACE, "retrieving clmver failed, %d\n", err);
	} else {
		buf[sizeof(buf) - 1] = '\0';
		clmver = (char *)buf;

		/* Replace all newline/linefeed characters with space
		 * character
		 */
		strreplace(clmver, '\n', ' ');

		/* store CLM version for adding it to revinfo debugfs file */
		memcpy(ifp->drvr->clmver, clmver, sizeof(ifp->drvr->clmver));

		brcmf_dbg(INFO, "CLM version = %s\n", clmver);
	}

	/* set apsta */
	err = brcmf_fil_iovar_int_set(ifp, "apsta", 1);
	if (err)
		brcmf_info("failed setting apsta, %d\n", err);

	/* set mpc */
	err = brcmf_fil_iovar_int_set(ifp, "mpc", 1);
	if (err) {
		bphy_err(drvr, "failed setting mpc\n");
		goto done;
	}

	brcmf_c_set_joinpref_default(ifp);

	/* Setup event_msgs, enable E_IF */
	err = brcmf_fil_iovar_data_get(ifp, "event_msgs", eventmask,
				       BRCMF_EVENTING_MASK_LEN);
	if (err) {
		bphy_err(drvr, "Get event_msgs error (%d)\n", err);
		goto done;
	}
	setbit(eventmask, BRCMF_E_IF);
	err = brcmf_fil_iovar_data_set(ifp, "event_msgs", eventmask,
				       BRCMF_EVENTING_MASK_LEN);
	if (err) {
		bphy_err(drvr, "Set event_msgs error (%d)\n", err);
		goto done;
	}

	/* Enable event_msg_ext specific to 43012/43022 chip */
	if (bus->chip == CY_CC_43012_CHIP_ID || bus->chip == CY_CC_43022_CHIP_ID) {
		/* Program event_msg_ext to support event larger than 128 */
		msglen = (roundup(BRCMF_E_LAST, NBBY) / NBBY) +
				  EVENTMSGS_EXT_STRUCT_SIZE;
		/* Allocate buffer for eventmask_msg */
		eventmask_msg = kzalloc(msglen, GFP_KERNEL);
		if (!eventmask_msg) {
			err = -ENOMEM;
			goto done;
		}

		/* Read the current programmed event_msgs_ext */
		eventmask_msg->ver = EVENTMSGS_VER;
		eventmask_msg->len = roundup(BRCMF_E_LAST, NBBY) / NBBY;
		err = brcmf_fil_iovar_data_get(ifp, "event_msgs_ext",
					       eventmask_msg,
					       msglen);

		/* Enable ULP event */
		brcmf_dbg(EVENT, "enable event ULP\n");
		setbit(eventmask_msg->mask, BRCMF_E_ULP);

		/* Write updated Event mask */
		eventmask_msg->ver = EVENTMSGS_VER;
		eventmask_msg->command = EVENTMSGS_SET_MASK;
		eventmask_msg->len = (roundup(BRCMF_E_LAST, NBBY) / NBBY);

		err = brcmf_fil_iovar_data_set(ifp, "event_msgs_ext",
					       eventmask_msg, msglen);
		if (err) {
			brcmf_err("Set event_msgs_ext error (%d)\n", err);
			kfree(eventmask_msg);
			goto done;
		}
		kfree(eventmask_msg);
	}
	/* Setup default scan channel time */
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_CHANNEL_TIME,
				    BRCMF_DEFAULT_SCAN_CHANNEL_TIME);
	if (err) {
		bphy_err(drvr, "BRCMF_C_SET_SCAN_CHANNEL_TIME error (%d)\n",
			 err);
		goto done;
	}

	/* Setup default scan unassoc time */
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_UNASSOC_TIME,
				    BRCMF_DEFAULT_SCAN_UNASSOC_TIME);
	if (err) {
		bphy_err(drvr, "BRCMF_C_SET_SCAN_UNASSOC_TIME error (%d)\n",
			 err);
		goto done;
	}

	/* Enable tx beamforming, errors can be ignored (not supported) */
	(void)brcmf_fil_iovar_int_set(ifp, "txbf", 1);
	err = brcmf_fil_iovar_int_set(ifp, "chanspec", 0x1001);
	if (err < 0) {
		bphy_err(drvr, "Initial Channel failed %d\n", err);
		goto done;
	}
	/* add unicast packet filter */
	err = brcmf_pktfilter_add_remove(ifp->ndev,
					 BRCMF_UNICAST_FILTER_NUM, true);
	if (err == -BRCMF_FW_UNSUPPORTED) {
		/* FW not support can be ignored */
		err = 0;
		goto done;
	} else if (err) {
		bphy_err(drvr, "Add unicast filter error (%d)\n", err);
	}

done:
	return err;
}

#ifndef CONFIG_BRCM_TRACING
void __brcmf_err(struct brcmf_bus *bus, const char *func, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;
	if (bus)
		dev_err(bus->dev, "%s: %pV", func, &vaf);
	else
		pr_err("%s: %pV", func, &vaf);

	va_end(args);
}
#endif

#if defined(CONFIG_BRCM_TRACING) || defined(CONFIG_BRCMDBG)
void __brcmf_dbg(u32 level, const char *func, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	if (brcmf_msg_level & level)
		pr_debug("%s %pV", func, &vaf);
	trace_brcmf_dbg(level, func, &vaf);
	va_end(args);
}
#endif

static void brcmf_mp_attach(void)
{
	/* If module param firmware path is set then this will always be used,
	 * if not set then if available use the platform data version. To make
	 * sure it gets initialized at all, always copy the module param version
	 */
	strscpy(brcmf_mp_global.firmware_path, brcmf_firmware_path,
		BRCMF_FW_ALTPATH_LEN);
	if ((brcmfmac_pdata) && (brcmfmac_pdata->fw_alternative_path) &&
	    (brcmf_mp_global.firmware_path[0] == '\0')) {
		strscpy(brcmf_mp_global.firmware_path,
			brcmfmac_pdata->fw_alternative_path,
			BRCMF_FW_ALTPATH_LEN);
	}
}

int brcmf_debugfs_param_read(struct seq_file *s, void *data)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(s->private);

	seq_printf(s, "%-20s: %s\n", "Name", "Value");
	seq_printf(s, "%-20s: 0x%x\n", "debug", brcmf_msg_level);
	seq_printf(s, "%-20s: %s\n", "alternative_fw_path", brcmf_firmware_path);
	seq_printf(s, "%-20s: %d\n", "p2pon", !!brcmf_p2p_enable);
	seq_printf(s, "%-20s: %d\n", "fcmode", bus_if->drvr->settings->fcmode);
	seq_printf(s, "%-20s: %d\n", "roamoff", !!brcmf_roamoff);
	seq_printf(s, "%-20s: %d\n", "iapp", !!brcmf_iapp_enable);
	seq_printf(s, "%-20s: %d\n", "eap_restrict", !!brcmf_eap_restrict);
	seq_printf(s, "%-20s: %d\n", "max_pm", !!brcmf_max_pm);
#ifdef DEBUG
	seq_printf(s, "%-20s: %d\n", "ignore_probe_fail", !!brcmf_ignore_probe_fail);
#endif
	seq_printf(s, "%-20s: %d\n", "fw_ap_select", !!brcmf_fw_ap_select);
	seq_printf(s, "%-20s: %d\n", "disable_6ghz", !!brcmf_disable_6ghz);
	seq_printf(s, "%-20s: %d\n", "sdio_in_isr", !!brcmf_sdio_in_isr);
	seq_printf(s, "%-20s: %d\n", "pkt_prio", !!brcmf_pkt_prio_enable);
	seq_printf(s, "%-20s: %d\n", "sdio_rxf_thread", !!brcmf_sdio_rxf_in_kthread);
	seq_printf(s, "%-20s: %d\n", "offload_prof", brcmf_offload_prof);
	seq_printf(s, "%-20s: 0x%x\n", "offload_feat", brcmf_offload_feat);
	seq_printf(s, "%-20s: %d\n", "txglomsz", brcmf_sdiod_txglomsz);
	seq_printf(s, "%-20s: %d\n", "bt_over_sdio", !!brcmf_bt_over_sdio);
	seq_printf(s, "%-20s: %d\n", "bus_idle_time", brcmf_sdio_bus_idle_time);
	seq_printf(s, "%-20s: %d\n", "short_psq", !!brcmf_short_psq);

	return 0;
}

struct brcmf_mp_device *brcmf_get_module_param(struct device *dev,
					       enum brcmf_bus_type bus_type,
					       u32 chip, u32 chiprev)
{
	struct brcmf_mp_device *settings;
	struct brcmfmac_pd_device *device_pd;
	bool found;
	int i;

	brcmf_dbg(INFO, "Enter, bus=%d, chip=%d, rev=%d\n", bus_type, chip,
		  chiprev);
	settings = kzalloc(sizeof(*settings), GFP_ATOMIC);
	if (!settings)
		return NULL;

	/* start by using the module parameters */
	brcmf_dbg(INFO, "debug: 0x%x\n", brcmf_msg_level);
	brcmf_dbg(INFO, "alternative_fw_path: %s\n", brcmf_firmware_path);
	settings->p2p_enable = !!brcmf_p2p_enable;
	brcmf_dbg(INFO, "p2pon: %d\n", settings->p2p_enable);
	memcpy(settings->feature_disable,
	       brcmf_feature_disable,
	       sizeof(brcmf_feature_disable));
	brcmf_dbg(INFO, "feature_disable: ");
	for (i = 0; i < BRCMF_MAX_FEATURE_BYTES; i++)
		brcmf_dbg(INFO, "0x%x ", settings->feature_disable[i]);
	brcmf_dbg(INFO, "\n");

	settings->fcmode = brcmf_fcmode;
	brcmf_dbg(INFO, "fcmode: %d\n", settings->fcmode);
	settings->roamoff = brcmf_roamoff;
	brcmf_dbg(INFO, "roamoff: %d\n", settings->roamoff);
	settings->iapp = !!brcmf_iapp_enable;
	brcmf_dbg(INFO, "iapp: %d\n", settings->iapp);
	settings->eap_restrict = !!brcmf_eap_restrict;
	brcmf_dbg(INFO, "eap_restrict: %d\n", settings->eap_restrict);
	settings->default_pm = !!brcmf_max_pm ? PM_MAX : PM_FAST;
	brcmf_dbg(INFO, "max_pm: %d\n", !!brcmf_max_pm);
#ifdef DEBUG
	settings->ignore_probe_fail = !!brcmf_ignore_probe_fail;
	brcmf_dbg(INFO, "ignore_probe_fail: %d\n", settings->ignore_probe_fail);
#endif
	settings->fw_ap_select = !!brcmf_fw_ap_select;
	brcmf_dbg(INFO, "fw_ap_select: %d\n", settings->fw_ap_select);
	settings->disable_6ghz = !!brcmf_disable_6ghz;
	brcmf_dbg(INFO, "disable_6ghz: %d\n", settings->disable_6ghz);
	settings->sdio_in_isr = !!brcmf_sdio_in_isr;
	brcmf_dbg(INFO, "sdio_in_isr: %d\n", settings->sdio_in_isr);
	settings->pkt_prio = !!brcmf_pkt_prio_enable;
	brcmf_dbg(INFO, "pkt_prio: %d\n", settings->pkt_prio);
	settings->sdio_rxf_in_kthread_enabled = !!brcmf_sdio_rxf_in_kthread;
	brcmf_dbg(INFO, "sdio_rxf_thread: %d\n", settings->sdio_rxf_in_kthread_enabled);

	brcmf_dbg(INFO, "offload_prof: %d\n", brcmf_offload_prof);
	if (brcmf_offload_prof >= BRCMF_OL_PROF_TYPE_MAX) {
		brcmf_err("Invalid Offload power profile %u, using default profile 1",
			  brcmf_offload_prof);
		brcmf_offload_prof = BRCMF_OL_PROF_TYPE_LOW_PWR;
	}
	settings->offload_prof = brcmf_offload_prof;
	settings->offload_feat = brcmf_offload_feat;
	brcmf_dbg(INFO, "offload_feat: 0x%x\n", settings->offload_feat);

	settings->bt_over_sdio = !!brcmf_bt_over_sdio;
	brcmf_dbg(INFO, "bt_over_sdio: %d\n", settings->bt_over_sdio);
	settings->short_psq = !!brcmf_short_psq;
	brcmf_dbg(INFO, "brcmf_short_psq: %d\n", settings->short_psq);

	settings->idleclk_disable = brcmf_sdio_idleclk_disable;
	brcmf_dbg(INFO, "idleclk_disable: %d\n", settings->idleclk_disable);

	if (bus_type == BRCMF_BUSTYPE_SDIO) {
		settings->bus.sdio.txglomsz = brcmf_sdiod_txglomsz;
		brcmf_dbg(INFO, "txglomsz: %d\n", settings->bus.sdio.txglomsz);
	}

	settings->sdio_bus_idle_time = brcmf_sdio_bus_idle_time;
	brcmf_dbg(INFO, "sdio_bus_idle_time: %d\n", settings->sdio_bus_idle_time);

	/* See if there is any device specific platform data configured */
	found = false;
	if (brcmfmac_pdata) {
		for (i = 0; i < brcmfmac_pdata->device_count; i++) {
			device_pd = &brcmfmac_pdata->devices[i];
			if ((device_pd->bus_type == bus_type) &&
			    (device_pd->id == chip) &&
			    ((device_pd->rev == chiprev) ||
			     (device_pd->rev == -1))) {
				brcmf_dbg(INFO, "Platform data for device found\n");
				settings->country_codes =
						device_pd->country_codes;
				if (device_pd->bus_type == BRCMF_BUSTYPE_SDIO)
					memcpy(&settings->bus.sdio,
					       &device_pd->bus.sdio,
					       sizeof(settings->bus.sdio));
				found = true;
				break;
			}
		}
	}
	if (!found) {
		/* No platform data for this device, try OF and DMI data */
		brcmf_dmi_probe(settings, chip, chiprev);
		brcmf_of_probe(dev, bus_type, settings);
	}
	return settings;
}

void brcmf_release_module_param(struct brcmf_mp_device *module_param)
{
	kfree(module_param);
}

static int
brcmf_reboot_callback(struct notifier_block *this, unsigned long code, void *unused)
{
	brcmf_dbg(INFO, "code = %ld\n", code);
	if (code == SYS_RESTART)
		brcmf_core_exit();
	return NOTIFY_DONE;
}

static int __init brcmf_common_pd_probe(struct platform_device *pdev)
{
	brcmf_dbg(INFO, "Enter\n");

	brcmfmac_pdata = dev_get_platdata(&pdev->dev);

	if (brcmfmac_pdata && brcmfmac_pdata->power_on)
		brcmfmac_pdata->power_on();

	return 0;
}

static int brcmf_common_pd_remove(struct platform_device *pdev)
{
	brcmf_dbg(INFO, "Enter\n");

	if (brcmfmac_pdata->power_off)
		brcmfmac_pdata->power_off();

	return 0;
}

static struct platform_driver brcmf_pd = {
	.remove		= brcmf_common_pd_remove,
	.driver		= {
		.name	= BRCMFMAC_PDATA_NAME,
	}
};

static int __init brcmfmac_module_init(void)
{
	int err;

	/* Get the platform data (if available) for our devices */
	err = platform_driver_probe(&brcmf_pd, brcmf_common_pd_probe);
	if (err == -ENODEV)
		brcmf_dbg(INFO, "No platform data available.\n");

	/* Initialize global module parameters */
	brcmf_mp_attach();

	/* Continue the initialization by registering the different busses */
	err = brcmf_core_init();
	if (err) {
		if (brcmfmac_pdata)
			platform_driver_unregister(&brcmf_pd);
	} else {
		register_reboot_notifier(&brcmf_reboot_notifier);
	}

	return err;
}

static void __exit brcmfmac_module_exit(void)
{
	brcmf_core_exit();
	unregister_reboot_notifier(&brcmf_reboot_notifier);
	if (brcmfmac_pdata)
		platform_driver_unregister(&brcmf_pd);
}

module_init(brcmfmac_module_init);
module_exit(brcmfmac_module_exit);

