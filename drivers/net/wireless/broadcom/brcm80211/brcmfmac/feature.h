// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2014 Broadcom Corporation
 */
#ifndef _BRCMF_FEATURE_H
#define _BRCMF_FEATURE_H

/*
 * Features:
 *
 * MBSS: multiple BSSID support (eg. guest network in AP mode).
 * MCHAN: multi-channel for concurrent P2P.
 * PNO: preferred network offload.
 * WOWL: Wake-On-WLAN.
 * P2P: peer-to-peer
 * RSDB: Real Simultaneous Dual Band
 * TDLS: Tunneled Direct Link Setup
 * SCAN_RANDOM_MAC: Random MAC during (net detect) scheduled scan.
 * WOWL_ND: WOWL net detect (PNO)
 * WOWL_GTK: WOWL GTK rekey failure detect
 * WOWL_ARP_ND: ARP and Neighbor Discovery offload support during WOWL.
 * MFP: 802.11w Management Frame Protection.
 * GSCAN: enhanced scan offload feature.
 * FWSUP: Firmware supplicant.
 * MONITOR: firmware can pass monitor packets to host.
 * MONITOR_FLAG: firmware flags monitor packets.
 * MONITOR_FMT_RADIOTAP: firmware provides monitor packets with radiotap header
 * MONITOR_FMT_HW_RX_HDR: firmware provides monitor packets with hw/ucode header
 * DOT11H: firmware supports 802.11h
 * SAE: simultaneous authentication of equals
 * FWAUTH: Firmware authenticator
 * DUMP_OBSS: Firmware has capable to dump obss info to support ACS
 * SURVEY_DUMP: Firmware has capable to survey dump info
 * SAE_EXT: SAE be handled by userspace supplicant
 * GCMP: firmware has defined GCMP or not.
 * TWT: Firmware has the TWT Module Support.
 * OFFLOADS: Firmware can do the packet processing work offloaded by
 *	Host Driver, i.e, it can process specifc types of RX packets like
 *	ARP, ND, etc and send out a suitable response packet from within
 * 	Firmware.
 * ULP: Firmware supports Ultra Low Power mode of operation.
 * GTKO: GTK rekey offload.
 */
#define BRCMF_FEAT_LIST \
	BRCMF_FEAT_DEF(MBSS) \
	BRCMF_FEAT_DEF(MCHAN) \
	BRCMF_FEAT_DEF(PNO) \
	BRCMF_FEAT_DEF(WOWL) \
	BRCMF_FEAT_DEF(P2P) \
	BRCMF_FEAT_DEF(RSDB) \
	BRCMF_FEAT_DEF(TDLS) \
	BRCMF_FEAT_DEF(SCAN_RANDOM_MAC) \
	BRCMF_FEAT_DEF(WOWL_ND) \
	BRCMF_FEAT_DEF(WOWL_GTK) \
	BRCMF_FEAT_DEF(WOWL_ARP_ND) \
	BRCMF_FEAT_DEF(MFP) \
	BRCMF_FEAT_DEF(GSCAN) \
	BRCMF_FEAT_DEF(FWSUP) \
	BRCMF_FEAT_DEF(MONITOR) \
	BRCMF_FEAT_DEF(MONITOR_FLAG) \
	BRCMF_FEAT_DEF(MONITOR_FMT_RADIOTAP) \
	BRCMF_FEAT_DEF(MONITOR_FMT_HW_RX_HDR) \
	BRCMF_FEAT_DEF(DOT11H) \
	BRCMF_FEAT_DEF(SAE) \
	BRCMF_FEAT_DEF(FWAUTH) \
	BRCMF_FEAT_DEF(DUMP_OBSS) \
	BRCMF_FEAT_DEF(SURVEY_DUMP) \
	BRCMF_FEAT_DEF(SAE_EXT) \
	BRCMF_FEAT_DEF(FBT) \
	BRCMF_FEAT_DEF(OKC) \
	BRCMF_FEAT_DEF(GCMP) \
	BRCMF_FEAT_DEF(TWT) \
	BRCMF_FEAT_DEF(OFFLOADS) \
	BRCMF_FEAT_DEF(ULP) \
	BRCMF_FEAT_DEF(PROPTXSTATUS) \
	BRCMF_FEAT_DEF(OWE) \
	BRCMF_FEAT_DEF(GTKO) \
	BRCMF_FEAT_DEF(MCHAN_CONFIG)

/*
 * Quirks:
 *
 * AUTO_AUTH: workaround needed for automatic authentication type.
 * NEED_MPC: driver needs to disable MPC during scanning operation.
 */
#define BRCMF_QUIRK_LIST \
	BRCMF_QUIRK_DEF(AUTO_AUTH) \
	BRCMF_QUIRK_DEF(NEED_MPC)

#define BRCMF_FEAT_DEF(_f) \
	BRCMF_FEAT_ ## _f,
/*
 * expand feature list to enumeration.
 */
enum brcmf_feat_id {
	BRCMF_FEAT_LIST
	BRCMF_FEAT_LAST
};
#undef BRCMF_FEAT_DEF

#define BRCMF_QUIRK_DEF(_q) \
	BRCMF_FEAT_QUIRK_ ## _q,
/*
 * expand quirk list to enumeration.
 */
enum brcmf_feat_quirk {
	BRCMF_QUIRK_LIST
	BRCMF_FEAT_QUIRK_LAST
};
#undef BRCMF_QUIRK_DEF

/**
 * brcmf_feat_attach() - determine features and quirks.
 *
 * @drvr: driver instance.
 */
void brcmf_feat_attach(struct brcmf_pub *drvr);

/**
 * brcmf_feat_debugfs_create() - create debugfs entries.
 *
 * @drvr: driver instance.
 */
void brcmf_feat_debugfs_create(struct brcmf_pub *drvr);

/**
 * brcmf_feat_is_enabled() - query feature.
 *
 * @ifp: interface instance.
 * @id: feature id to check.
 *
 * Return: true is feature is enabled; otherwise false.
 */
bool brcmf_feat_is_enabled(struct brcmf_if *ifp, enum brcmf_feat_id id);

/**
 * brcmf_feat_is_quirk_enabled() - query chip quirk.
 *
 * @ifp: interface instance.
 * @quirk: quirk id to check.
 *
 * Return: true is quirk is enabled; otherwise false.
 */
bool brcmf_feat_is_quirk_enabled(struct brcmf_if *ifp,
				 enum brcmf_feat_quirk quirk);

/**
 * brcmf_feat_is_6ghz_enabled() - Find if 6GHZ Operation is allowed
 *
 * @ifp: interface instance.
 *
 * Return: true if 6GHz operation is allowed; otherwise false.
 */
bool brcmf_feat_is_6ghz_enabled(struct brcmf_if *ifp);

/**
 * brcmf_feat_is_sdio_rxf_in_kthread() - handle SDIO Rx frame in kthread.
 *
 * @drvr: driver instance.
 */
bool brcmf_feat_is_sdio_rxf_in_kthread(struct brcmf_pub *drvr);

/**
 * brcmf_feat_is_offloads_enabled() - Find if offload_prof power profile
 * is given by user
 *
 * @ifp: interface instance.
 *
 * Return: true if offloads_prof is set otherwise false.
 */
bool brcmf_feat_is_offloads_enabled(struct brcmf_if *ifp);

#endif /* _BRCMF_FEATURE_H */
