// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2014 Broadcom Corporation
 */
#ifndef BRCMFMAC_COMMON_H
#define BRCMFMAC_COMMON_H

#include <linux/platform_device.h>
#include <linux/platform_data/brcmfmac.h>
#include "fwil_types.h"

#define BRCMF_FW_ALTPATH_LEN			256

#define BRCMFMAC_DISABLE	0
#define BRCMFMAC_ENABLE		1
#define BRCMFMAC_AUTO		2

#define BRCMF_DEFAULT_SDIO_IDLE_CONFIG		2
/* Keeping these macro definition here because these are defined in mmc drivers.
 * So for 3rd party mmc, fmac build should not fail due to build error.
 */

/* SDIO IDLECLOCK Support - reusing pm_caps */
#ifndef SDIO_IDLECLOCK_DIS
#define SDIO_IDLECLOCK_DIS	BIT(2)	/* Start SDClock */
#define SDIO_IDLECLOCK_EN	BIT(3)	/* Stop SDClock */
#define SDIO_SDMODE_1BIT	BIT(4)	/* Set 1-bit Bus mode */
#define SDIO_SDMODE_4BIT	BIT(5)	/* Set 4-bit Bus mode */
#endif /* !SDIO_IDLECLOCK_DIS */

#ifndef unsafe_memcpy
#define unsafe_memcpy(dst, src, bytes, justification)		\
	memcpy(dst, src, bytes)
#endif

/* Definitions for the module global and device specific settings are defined
 * here. Two structs are used for them. brcmf_mp_global_t and brcmf_mp_device.
 * The mp_global is instantiated once in a global struct and gets initialized
 * by the common_attach function which should be called before any other
 * (module) initiliazation takes place. The device specific settings is part
 * of the drvr struct and should be initialized on every brcmf_attach.
 */

/**
 * struct brcmf_mp_global_t - Global module paramaters.
 *
 * @firmware_path: Alternative firmware path.
 */
struct brcmf_mp_global_t {
	char	firmware_path[BRCMF_FW_ALTPATH_LEN];
};

extern struct brcmf_mp_global_t brcmf_mp_global;

/**
 * struct brcmf_mp_device - Device module paramaters.
 *
 * @p2p_enable: Legacy P2P0 enable (old wpa_supplicant).
 * @feature_disable: Feature_disable bitmask.
 * @fcmode: FWS flow control.
 * @roamoff: Firmware roaming off?
 * @eap_restrict: Not allow data tx/rx until 802.1X auth succeeds
 * @default_pm: default power management (PM) mode.
 * @ignore_probe_fail: Ignore probe failure.
 * @fw_ap_select: Allow FW to select AP.
 * @disable_6ghz: Disable 6GHz operation
 * @sdio_in_isr: Handle SDIO DPC in ISR.
 * @offload_prof: Enable offloads configuration power profile (Low,Mid,High)
 * @offload_feat: offloads feature flags to be enabled for selected pwr profile
 * @country_codes: If available, pointer to struct for translating country codes
 * @bus: Bus specific platform data. Only SDIO at the mmoment.
 * @pkt_prio: Support customer dscp to WMM up mapping.
 * @idleclk_disable: SDIO bus clock output disable when bus is idle.
 * @idle_time_zero: Set idle interval to zero.
 */
#define BRCMF_MAX_FEATURE_BYTES DIV_ROUND_UP(BRCMF_FEAT_LAST, 8)
struct brcmf_mp_device {
	bool		p2p_enable;
	unsigned char	feature_disable[BRCMF_MAX_FEATURE_BYTES];
	int		fcmode;
	unsigned int	roamoff;
	bool		iapp;
	bool		eap_restrict;
	int		default_pm;
	bool		ignore_probe_fail;
	bool		fw_ap_select;
	bool		disable_6ghz;
	bool		sdio_in_isr;
	bool		sdio_rxf_in_kthread_enabled;
	unsigned int	offload_prof;
	unsigned int	offload_feat;
	bool		bt_over_sdio;
	bool		short_psq;
	struct brcmfmac_pd_cc *country_codes;
	const char	*board_type;
	unsigned char	mac[ETH_ALEN];
	const char	*antenna_sku;
	union {
		struct brcmfmac_sdio_pd sdio;
	} bus;
	bool		pkt_prio;
	int			idleclk_disable;
	int		sdio_bus_idle_time;
};

/**
 * enum brcmf_roamoff_mode - using fw roaming and report event mode if not use it.
 *
 * @BRCMF_ROAMOFF_DISABLE: use firmware roaming engine
 * @BRCMF_ROAMOFF_EN_BCNLOST_MSG:
 *	don't use firmware roaming engine, and report to cfg80211 layer by BCNLOST_MSG event
 * @BRCMF_ROAMOFF_EN_DISCONNECT_EVT:
 *	don't use firmware roaming engine, and report to cfg80211 layer by DISCONNECT event
 * @BRCMF_ROAMOFF_MAX:
 *	for sanity checking purpose.
 */

enum brcmf_roamoff_mode {
	BRCMF_ROAMOFF_DISABLE = 0,
	BRCMF_ROAMOFF_EN_BCNLOST_MSG = 1,
	BRCMF_ROAMOFF_EN_DISCONNECT_EVT = 2,
	BRCMF_ROAMOFF_MAX
};

void brcmf_c_set_joinpref_default(struct brcmf_if *ifp);

struct brcmf_mp_device *brcmf_get_module_param(struct device *dev,
					       enum brcmf_bus_type bus_type,
					       u32 chip, u32 chiprev);
int brcmf_debugfs_param_read(struct seq_file *s, void *data);
void brcmf_release_module_param(struct brcmf_mp_device *module_param);

/* Sets dongle media info (drv_version, mac address). */
int brcmf_c_preinit_dcmds(struct brcmf_if *ifp);
int brcmf_c_set_cur_etheraddr(struct brcmf_if *ifp, const u8 *addr);

#ifdef CONFIG_DMI
void brcmf_dmi_probe(struct brcmf_mp_device *settings, u32 chip, u32 chiprev);
#else
static inline void
brcmf_dmi_probe(struct brcmf_mp_device *settings, u32 chip, u32 chiprev) {}
#endif

u8 brcmf_map_prio_to_prec(void *cfg, u8 prio);

u8 brcmf_map_prio_to_aci(void *cfg, u8 prio);

void brcmf_generic_offload_config(struct brcmf_if *ifp, unsigned int ol_feat,
				  unsigned int ol_profile, bool reset);
void brcmf_generic_offload_enable(struct brcmf_if *ifp, unsigned int ol_feat,
				  bool enable);
void brcmf_generic_offload_host_ipv4_update(struct brcmf_if *ifp, unsigned int ol_feat,
					    u32 ipaddr, bool is_add);
int brcmf_generic_offload_host_ipv6_update(struct brcmf_if *ifp, unsigned int ol_feat,
					   void *ptr, u8 type, bool is_add);

#endif /* BRCMFMAC_COMMON_H */
