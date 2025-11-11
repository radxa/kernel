// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2016 Broadcom
 */
#ifndef _BRCMF_PNO_H
#define _BRCMF_PNO_H

#define BRCMF_PNO_SCAN_COMPLETE			1
#define BRCMF_PNO_MAX_PFN_COUNT			16
#define BRCMF_PNO_SCHED_SCAN_MIN_PERIOD	10
#define BRCMF_PNO_SCHED_SCAN_MAX_PERIOD	508
#define AUTO_CONNECT_MASK		0x0010
#define AUTO_NET_SWITCH_MASK		0x0002
#define BRCMF_PNO_WPA_AUTH_ANY		0xFFFFFFFF
#define MAXNUM_SSID_PER_ADD		16
#define WSEC_MIN_PASSWORD_LEN		8
#define WSEC_MAX_PASSWORD_LEN		64
#define PFN_VERSION			2
#define PFN_LIST_ORDER			0
#define PFN_RSSI			1
#define SORT_CRITERIA_BIT		0
#define ENABLE				1
#define IMMEDIATE_SCAN_BIT		3
#define DEFAULT_BESTN			2
#define DEFAULT_MSCAN			0
#define DEFAULT_REPEAT			10
#define DEFAULT_EXP			2
#define PFN_SET				1
#define PFN_UNSET			0
#define PFN_CONFIG_AND_COUNT_SIZE	2
#define PFN_SSID_INFRA			1
#define KEY_MGMT_WPA			0x00000001
#define KEY_MGMT_WPA2			0x00000002
#define KEY_MGMT_WPA_PSK		0x00000004
#define KEY_MGMT_WPA2_PSK		0x00000008
#define KEY_MGMT_WPA_NONE		0x00000010
#define KEY_MGMT_FT			0x00000020
#define KEY_MGMT_FT_PSK			0x00000040
#define KEY_MGMT_WAPI_PSK		0x00000080
#define KEY_MGMT_SUITE_B		0x00000100
#define KEY_MGMT_SUITE_B_192		0x00000200
#define KEY_MGMT_OWE			0x00000400
#define KEY_MGMT_DPP			0x00000800
#define KEY_MGMT_FILS_SHA256		0x00001000
#define KEY_MGMT_FILS_SHA384		0x00002000
#define KEY_MGMT_FT_FILS_SHA256		0x00004000
#define KEY_MGMT_FT_FILS_SHA384		0x00008000
#define KEY_MGMT_SAE			0x00010000
#define KEY_MGMT_802_1X_SHA256		0x00020000
#define KEY_MGMT_PSK_SHA256		0x00040000
#define KEY_MGMT_TPK_HANDSHAKE		0x00080000
#define KEY_MGMT_FT_SAE			0x00100000
#define KEY_MGMT_FT_802_1X_SHA384	0x00200000
#define KEY_MGMT_CCKM			0x00400000
#define KEY_MGMT_OSEN			0x00800000

struct brcm_pfn_param {
	s32 version;
	s32 scan_freq;
	s32 lost_network_timeout;
	s16 flags;
	s16 rssi_margin;
	u8 bestn;
	u8 mscan;
	u8 repeat;
	u8 exp;
	s32 slow_freq;
};

struct brcm_pfn {
	struct brcmf_ssid_le ssid; /*ssid and its length*/
	s32 flags; /*bit2: hidden*/
	s32 infra; /*BSS Vs IBSS*/
	s32 auth; /*Open Vs Closed*/
	s32 wpa_auth; /*WPA type*/
	s32 wsec; /*wsec value*/
	struct brcmf_wsec_pmk_le psk; /*Password*/
};

struct pfn_conn_info {
	u8 SSID_len;
	u8 SSID[IEEE80211_MAX_SSID_LEN];
	u8 BSSID[ETH_ALEN];
	s16 RSSI;
	s8 phy_noise;
	u16 channel;
	s16 SNR;
	u8 proto;
	int key_mgmt;
};

enum {
	PFN_CONFIG_AUTOCONNECT,
	PFN_CONFIG_AUTOSWITCH_LISTORDER,
	PFN_CONFIG_AUTOSWITCH_RSSI,
};

/* forward declaration */
struct brcmf_pno_info;

/**
 * brcmf_pno_start_sched_scan - initiate scheduled scan on device.
 *
 * @ifp: interface object used.
 * @req: configuration parameters for scheduled scan.
 */
int brcmf_pno_start_sched_scan(struct brcmf_if *ifp,
			       struct cfg80211_sched_scan_request *req);

/**
 * brcmf_pno_stop_sched_scan - terminate scheduled scan on device.
 *
 * @ifp: interface object used.
 * @reqid: unique identifier of scan to be stopped.
 */
int brcmf_pno_stop_sched_scan(struct brcmf_if *ifp, u64 reqid);

/**
 * brcmf_pno_wiphy_params - fill scheduled scan parameters in wiphy instance.
 *
 * @wiphy: wiphy instance to be used.
 * @gscan: indicates whether the device has support for g-scan feature.
 */
void brcmf_pno_wiphy_params(struct wiphy *wiphy, bool gscan);

/**
 * brcmf_pno_attach - allocate and attach module information.
 *
 * @cfg: cfg80211 context used.
 */
int brcmf_pno_attach(struct brcmf_cfg80211_info *cfg);

/**
 * brcmf_pno_detach - detach and free module information.
 *
 * @cfg: cfg80211 context used.
 */
void brcmf_pno_detach(struct brcmf_cfg80211_info *cfg);

/**
 * brcmf_pno_find_reqid_by_bucket - find request id for given bucket index.
 *
 * @pi: pno instance used.
 * @bucket: index of firmware bucket.
 */
u64 brcmf_pno_find_reqid_by_bucket(struct brcmf_pno_info *pi, u32 bucket);

/**
 * brcmf_pno_get_bucket_map - determine bucket map for given netinfo.
 *
 * @pi: pno instance used.
 * @netinfo: netinfo to compare with bucket configuration.
 */
u32 brcmf_pno_get_bucket_map(struct brcmf_pno_info *pi,
			     struct brcmf_pno_net_info_le *netinfo);

int pfn_send_network_blob_fw(struct wiphy *wiphy,
			     struct wireless_dev *wdev);
#endif /* _BRCMF_PNO_H */
