/* Infineon WLAN driver: vendor specific implement
 *
 * Copyright 2022-2023 Cypress Semiconductor Corporation (an Infineon company)
 * or an affiliate of Cypress Semiconductor Corporation. All rights reserved.
 * This software, including source code, documentation and related materials
 * ("Software") is owned by Cypress Semiconductor Corporation or one of its
 * affiliates ("Cypress") and is protected by and subject to
 * worldwide patent protection (United States and foreign),
 * United States copyright laws and international treaty provisions.
 * Therefore, you may use this Software only as provided in the license agreement
 * accompanying the software package from which you obtained this Software ("EULA").
 * If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
 * non-transferable license to copy, modify, and compile the Software source code
 * solely for use in connection with Cypress's integrated circuit products.
 * Any reproduction, modification, translation, compilation, or representation
 * of this Software except as specified above is prohibited without
 * the expresswritten permission of Cypress.
 * Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT,
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 * Cypress reserves the right to make changes to the Software without notice.
 * Cypress does not assume any liability arising out of the application or
 * use of the Software or any product or circuit described in the Software.
 * Cypress does not authorize its products for use in any products where a malfunction
 * or failure of the Cypress product may reasonably be expected to result in
 * significant property damage, injury or death ("High Risk Product").
 * By including Cypress's product in a High Risk Product, the manufacturer
 * of such system or application assumes all risk of such use and in doing so
 * agrees to indemnify Cypress against all liability.
 */

#include <net/netlink.h>
#include <brcm_hw_ids.h>
#include "core.h"
#include "cfg80211.h"
#include "debug.h"
#include "fwil.h"
#include "vendor_ifx.h"
#include "xtlv.h"
#include "twt.h"
#include "pno.h"
#include "bus.h"
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/jhash.h>
#include <linux/hashtable.h>
#include <linux/inet.h>
#include "common.h"

static const struct ifx_vendor_cmdstr ifx_vndr_cmdstr[] = {
	{ "offload_config", ifx_vndr_cmdstr_offload_config},
	{ "mkeep_alive", ifx_vndr_cmdstr_mkeep_alive},
	{ "tko", ifx_vndr_cmdstr_tko},
	{ "icmp_echo_req", ifx_vndr_cmdstr_icmp_echo_req},
	{ NULL, NULL }
};

DEFINE_HASHTABLE(vndr_cmd_hashtbl, VNDR_CMD_HASH_BITS);

static int ifx_cfg80211_vndr_send_cmd_reply(struct wiphy *wiphy,
					    const void  *data, int len)
{
	struct sk_buff *skb;

	/* Alloc the SKB for vendor_event */
	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, len);
	if (unlikely(!skb)) {
		brcmf_err("skb alloc failed\n");
		return -ENOMEM;
	}

	/* Push the data to the skb */
	nla_put_nohdr(skb, len, data);
	return cfg80211_vendor_cmd_reply(skb);
}

int ifx_vndr_cmdstr_hashtbl_init(void)
{
	int i;
	u32 jhash_key;

	brcmf_dbg(TRACE, "enter\n");

	hash_init(vndr_cmd_hashtbl);

	/* Initializing the VENDOR CMD hashtable with all the string commmands
	 * and func_handler in ifx_vndr_str_cmds
	 */
	for (i = 0; ifx_vndr_cmdstr[i].name; i++) {
		struct ifx_vndr_cmdstr_hashtbl *vndr_hashtbl;

		vndr_hashtbl = kzalloc(sizeof(*vndr_hashtbl), GFP_KERNEL);
		if (!vndr_hashtbl)
			return -ENOMEM;

		vndr_hashtbl->vndr_cmd_addr = (struct ifx_vendor_cmdstr *)&ifx_vndr_cmdstr[i];
		jhash_key = jhash(ifx_vndr_cmdstr[i].name, strlen(ifx_vndr_cmdstr[i].name), 0);
		hash_add(vndr_cmd_hashtbl, &vndr_hashtbl->node, jhash_key);
	}

	return 0;
}

void ifx_vndr_cmdstr_hashtbl_deinit(void)
{
	struct ifx_vndr_cmdstr_hashtbl *vndr_hashtbl;
	struct hlist_node *tmp_node;
	int i;

	hash_for_each_safe(vndr_cmd_hashtbl, i, tmp_node, vndr_hashtbl, node) {
		hash_del(&vndr_hashtbl->node);
		kfree(vndr_hashtbl);
	}
}

static void
ifx_cfgvendor_twt_parse_params(const struct nlattr *attr_iter,
			       struct brcmf_twt_params *twt_params)
{
	int tmp, twt_param;
	const struct nlattr *twt_param_iter;

	nla_for_each_nested(twt_param_iter, attr_iter, tmp) {
		twt_param = nla_type(twt_param_iter);
		switch (twt_param) {
		case IFX_VENDOR_ATTR_TWT_PARAM_NEGO_TYPE:
			twt_params->negotiation_type = nla_get_u8(twt_param_iter);
			break;
		case IFX_VENDOR_ATTR_TWT_PARAM_SETUP_CMD_TYPE:
			twt_params->setup_cmd = nla_get_u8(twt_param_iter);
			break;
		case IFX_VENDOR_ATTR_TWT_PARAM_DIALOG_TOKEN:
			twt_params->dialog_token = nla_get_u8(twt_param_iter);
			break;
		case IFX_VENDOR_ATTR_TWT_PARAM_WAKE_TIME:
			twt_params->twt = nla_get_u64(twt_param_iter);
			break;
		case IFX_VENDOR_ATTR_TWT_PARAM_WAKE_TIME_OFFSET:
			twt_params->twt_offset = nla_get_u64(twt_param_iter);
			break;
		case IFX_VENDOR_ATTR_TWT_PARAM_MIN_WAKE_DURATION:
			twt_params->min_twt = nla_get_u8(twt_param_iter);
			break;
		case IFX_VENDOR_ATTR_TWT_PARAM_WAKE_INTVL_EXPONENT:
			twt_params->exponent = nla_get_u8(twt_param_iter);
			break;
		case IFX_VENDOR_ATTR_TWT_PARAM_WAKE_INTVL_MANTISSA:
			twt_params->mantissa = nla_get_u16(twt_param_iter);
			break;
		case IFX_VENDOR_ATTR_TWT_PARAM_REQUESTOR:
			twt_params->requestor = nla_get_u8(twt_param_iter);
			break;
		case IFX_VENDOR_ATTR_TWT_PARAM_TRIGGER:
			twt_params->trigger = nla_get_u8(twt_param_iter);
			break;
		case IFX_VENDOR_ATTR_TWT_PARAM_IMPLICIT:
			twt_params->implicit = nla_get_u8(twt_param_iter);
			break;
		case IFX_VENDOR_ATTR_TWT_PARAM_FLOW_TYPE:
			twt_params->flow_type = nla_get_u8(twt_param_iter);
			break;
		case IFX_VENDOR_ATTR_TWT_PARAM_FLOW_ID:
			twt_params->flow_id = nla_get_u8(twt_param_iter);
			break;
		case IFX_VENDOR_ATTR_TWT_PARAM_BCAST_TWT_ID:
			twt_params->bcast_twt_id = nla_get_u8(twt_param_iter);
			break;
		case IFX_VENDOR_ATTR_TWT_PARAM_PROTECTION:
			twt_params->protection = nla_get_u8(twt_param_iter);
			break;
		case IFX_VENDOR_ATTR_TWT_PARAM_CHANNEL:
			twt_params->twt_channel = nla_get_u8(twt_param_iter);
			break;
		case IFX_VENDOR_ATTR_TWT_PARAM_TWT_INFO_FRAME_DISABLED:
			twt_params->twt_info_frame_disabled = nla_get_u8(twt_param_iter);
			break;
		case IFX_VENDOR_ATTR_TWT_PARAM_MIN_WAKE_DURATION_UNIT:
			twt_params->min_twt_unit = nla_get_u8(twt_param_iter);
			break;
		case IFX_VENDOR_ATTR_TWT_PARAM_TEARDOWN_ALL_TWT:
			twt_params->teardown_all_twt = nla_get_u8(twt_param_iter);
			break;
		default:
			brcmf_dbg(TRACE, "Unknown TWT param %d, skipping\n",
				  twt_param);
			break;
		}
	}
}

int ifx_cfg80211_vndr_cmds_twt(struct wiphy *wiphy, struct wireless_dev *wdev,
			       const void  *data, int len)
{
	int tmp, attr_type;
	const struct nlattr *attr_iter;

	struct brcmf_twt_params twt_params = {
		.twt_oper = 0,
		.negotiation_type = IFX_TWT_PARAM_NEGO_TYPE_ITWT,
		.setup_cmd = IFX_TWT_OPER_SETUP_CMD_TYPE_REQUEST,
		.dialog_token = 1,
		.twt = 0,
		.twt_offset = 0,
		.requestor = 1,
		.trigger = 0,
		.implicit = 1,
		.flow_type = 0,
		.flow_id = 0,
		.bcast_twt_id = 0,
		.protection = 0,
		.twt_channel = 0,
		.twt_info_frame_disabled = 0,
		.min_twt_unit = 0,
		.teardown_all_twt = 0
	};

	nla_for_each_attr(attr_iter, data, len, tmp) {
		attr_type = nla_type(attr_iter);

		switch (attr_type) {
		case IFX_VENDOR_ATTR_TWT_OPER:
			twt_params.twt_oper = nla_get_u8(attr_iter);
			break;
		case IFX_VENDOR_ATTR_TWT_PARAMS:
			ifx_cfgvendor_twt_parse_params(attr_iter, &twt_params);
			break;
		default:
			brcmf_dbg(TRACE, "Unknown TWT attribute %d, skipping\n",
				  attr_type);
			break;
		}
	}

	return (int)brcmf_twt_oper(wiphy, wdev, twt_params);
}

int ifx_cfg80211_vndr_cmds_bsscolor(struct wiphy *wiphy,
				    struct wireless_dev *wdev,
				    const void *data, int len)
{
	int ret = 0;
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_if *ifp;
	struct bcm_xtlv *he_tlv;
	u8 val = *(u8 *)data;
	u8 param[8] = {0};

	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);
	ifp = vif->ifp;
	he_tlv = (struct bcm_xtlv *)param;
	he_tlv->id = cpu_to_le16(IFX_HE_CMD_BSSCOLOR);

	if (val == 0xa) {
		/* To get fw iovars of the form "wl he bsscolor" using iw,
		 * call the parent iovar "he" with the subcmd filled and
		 * passed along ./iw dev wlan0 vendor recv 0x000319 0x10 0xa
		 */
		ret = brcmf_fil_iovar_data_get(ifp, "he", param, sizeof(param));
		if (ret) {
			brcmf_err("get he bss_color error:%d\n", ret);
		} else {
			brcmf_dbg(INFO, "get he bss_color: %d\n", *param);
			ifx_cfg80211_vndr_send_cmd_reply(wiphy, param, 1);
		}
	} else {
		brcmf_dbg(INFO, "not support set bsscolor during runtime!\n");
	}

	return ret;
}

int ifx_cfg80211_vndr_cmds_muedca_opt(struct wiphy *wiphy,
				      struct wireless_dev *wdev,
				      const void *data, int len)
{
	int ret = 0;
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_if *ifp;
	struct bcm_xtlv *he_tlv;
	u8 val = *(u8 *)data;
	u8 param[8] = {0};

	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);
	ifp = vif->ifp;
	he_tlv = (struct bcm_xtlv *)param;
	he_tlv->id = cpu_to_le16(IFX_HE_CMD_MUEDCA_OPT);

	if (val == 0xa) {
		/* To get fw iovars of the form "wl he muedca_opt_enable"
		 * using iw, call the parent iovar "he" with the subcmd
		 * filled and passed along
		 * ./iw dev wlan0 vendor recv 0x000319 0xb 0xa
		 */
		ret = brcmf_fil_iovar_data_get(ifp, "he", param, sizeof(param));
		if (ret) {
			brcmf_err("get he muedca_opt_enable error:%d\n", ret);
		} else {
			brcmf_dbg(INFO,
				  "get he muedca_opt_enable: %d\n", *param);
			ifx_cfg80211_vndr_send_cmd_reply(wiphy, param, 1);
		}
	} else {
		he_tlv->len = cpu_to_le16(1);
		he_tlv->data[0] = val;
		ret = brcmf_fil_iovar_data_set(ifp, "he",
					       param, sizeof(param));
		if (ret)
			brcmf_err("set he muedca_opt_enable error:%d\n", ret);
	}

	return ret;
}

int ifx_cfg80211_vndr_cmds_amsdu(struct wiphy *wiphy,
				 struct wireless_dev *wdev,
				 const void *data, int len)
{
	int ret = 0;
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_if *ifp;
	int val = *(s32 *)data;
	s32 get_amsdu = 0;

	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);
	ifp = vif->ifp;

	if (val == 0xa) {
		ret = brcmf_fil_iovar_int_get(ifp, "amsdu", &get_amsdu);
		if (ret) {
			brcmf_err("get amsdu error:%d\n", ret);

			return ret;
		}

		brcmf_dbg(INFO, "get amsdu: %d\n", get_amsdu);
		ifx_cfg80211_vndr_send_cmd_reply(
						wiphy, &get_amsdu, sizeof(int));
	} else {
		ret = brcmf_fil_iovar_int_set(ifp, "amsdu", val);
		if (ret)
			brcmf_err("set amsdu error:%d\n", ret);
	}

	return ret;
}

int ifx_cfg80211_vndr_cmds_ldpc_cap(struct wiphy *wiphy,
				    struct wireless_dev *wdev,
				    const void *data, int len)
{
	int ret = 0;
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_if *ifp;
	int val = *(s32 *)data;
	s32 buf = 0;

	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);
	ifp = vif->ifp;

	if (val == 0xa) {
		ret = brcmf_fil_iovar_int_get(ifp, "ldpc_cap", &buf);
		if (ret) {
			brcmf_err("get ldpc_cap error:%d\n", ret);

			return ret;
		}

		brcmf_dbg(INFO, "get ldpc_cap: %d\n", buf);
		ifx_cfg80211_vndr_send_cmd_reply(wiphy, &buf, sizeof(int));
	} else {
		ret = brcmf_fil_iovar_int_set(ifp, "ldpc_cap", val);
		if (ret)
			brcmf_err("set ldpc_cap error:%d\n", ret);
	}

	return ret;
}

int ifx_cfg80211_vndr_cmds_oce_enable(struct wiphy *wiphy,
				      struct wireless_dev *wdev,
				      const void *data, int len)
{
	int ret = 0;
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_if *ifp;
	struct bcm_iov_buf *oce_iov;
	struct bcm_xtlv *oce_xtlv;
	u8 val = *(u8 *)data;
	u8 param[16] = {0};

	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);
	ifp = vif->ifp;
	oce_iov = (struct bcm_iov_buf *)param;
	oce_iov->version = cpu_to_le16(IFX_OCE_IOV_VERSION);
	oce_iov->id = cpu_to_le16(IFX_OCE_CMD_ENABLE);
	oce_xtlv = (struct bcm_xtlv *)oce_iov->data;

	if (val == 0xa) {
		/* To get fw iovars of the form "wl oce enable"
		 * using iw, call the parent iovar "oce" with the subcmd
		 * filled and passed along
		 * ./iw dev wlan0 vendor recv 0x000319 0xf 0xa
		 */
		ret = brcmf_fil_iovar_data_get(ifp, "oce",
					       param, sizeof(param));
		if (ret) {
			brcmf_err("get oce enable error:%d\n", ret);
		} else {
			brcmf_dbg(INFO,
				  "get oce enable: %d\n", oce_xtlv->data[0]);
			ifx_cfg80211_vndr_send_cmd_reply(wiphy, oce_xtlv->data,
							 sizeof(int));
		}
	} else {
		oce_iov->len = cpu_to_le16(8);
		oce_xtlv->id = cpu_to_le16(IFX_OCE_XTLV_ENABLE);
		oce_xtlv->len = cpu_to_le16(1);
		oce_xtlv->data[0] = val;
		ret = brcmf_fil_iovar_data_set(ifp, "oce",
					       param, sizeof(param));
		if (ret)
			brcmf_err("set oce enable error:%d\n", ret);
	}

	return ret;
}

int ifx_cfg80211_vndr_cmds_randmac(struct wiphy *wiphy,
				   struct wireless_dev *wdev, const void *data, int len)
{
	int ret = 0;
	struct ifx_randmac iov_buf = {0};
	u8 val = *(u8 *)data;
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_if *ifp;

	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);
	ifp = vif->ifp;
	iov_buf.version = WL_RANDMAC_API_VERSION;
	iov_buf.subcmd_id = WL_RANDMAC_SUBCMD_ENABLE;
	iov_buf.len = offsetof(struct ifx_randmac, data);

	if (val == 0x1) {
		/* To set fw iovars of the form "wl randmac enable" using iw, call the
		 * parent iovar "randmac" with the subcmd filled and passed along
		 * ./iw dev wlan0 vendor send 0x000319 0x11 0x1
		 */
		ret = brcmf_fil_bsscfg_data_set(ifp, "randmac", (void *)&iov_buf, iov_buf.len);
		if (ret)
			brcmf_err("Failed to set randmac enable: %d\n", ret);
	} else if (val == 0x0) {
		iov_buf.subcmd_id = WL_RANDMAC_SUBCMD_DISABLE;
		/* To set fw iovars of the form "wl randmac disable" using iw, call the
		 * parent iovar "randmac" with the subcmd filled and passed along
		 * ./iw dev wlan0 vendor send 0x000319 0x11 0x0
		 */
		ret = brcmf_fil_bsscfg_data_set(ifp, "randmac", (void *)&iov_buf, iov_buf.len);
		if (ret)
			brcmf_err("Failed to set randmac disable: %d\n", ret);
	} else if (val == 0xa) {
		int result_data = 0;
		struct ifx_randmac *iov_resp = NULL;
		u8 buf[64] = {0};
		/* To get fw iovars of the form "wl randmac" using iw, call the
		 * parent iovar "randmac" with the subcmd filled and passed along
		 * ./iw dev wlan0 vendor recv 0x000319 0x11 0xa
		 */
		memcpy(buf, (void *)&iov_buf, iov_buf.len);
		ret = brcmf_fil_iovar_data_get(ifp, "randmac", (void *)buf, sizeof(buf));
		if (ret) {
			brcmf_err("Failed to get randmac enable or disable: %d\n", ret);
		} else {
			iov_resp = (struct ifx_randmac *)buf;
			if (iov_resp->subcmd_id == WL_RANDMAC_SUBCMD_ENABLE)
				result_data = 1;
			ifx_cfg80211_vndr_send_cmd_reply(wiphy, &result_data, sizeof(int));
		}
	}
	return ret;
}

int ifx_cfg80211_vndr_cmds_mbo(struct wiphy *wiphy,
			       struct wireless_dev *wdev,
			       const void *data, int len)
{
	int ret = 0;
	int tmp, attr_type, mbo_param;
	const struct nlattr *attr_iter, *mbo_param_iter;

	struct brcmf_cfg80211_vif *vif;
	struct brcmf_if *ifp;
	struct bcm_iov_buf *mbo_iov;
	struct bcm_xtlv *mbo_xtlv;
	u8 param[64] = {0};
	u16 buf_len = 0, buf_len_start = 0;

	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);
	ifp = vif->ifp;
	mbo_iov = (struct bcm_iov_buf *)param;
	mbo_iov->version = cpu_to_le16(IFX_MBO_IOV_VERSION);
	mbo_xtlv = (struct bcm_xtlv *)mbo_iov->data;
	buf_len_start = sizeof(param) - sizeof(struct bcm_iov_buf);
	buf_len = buf_len_start;

	nla_for_each_attr(attr_iter, data, len, tmp) {
		attr_type = nla_type(attr_iter);

		switch (attr_type) {
		case IFX_VENDOR_ATTR_MBO_CMD:
			mbo_iov->id = cpu_to_le16(nla_get_u8(attr_iter));
			break;
		case IFX_VENDOR_ATTR_MBO_PARAMS:
			nla_for_each_nested(mbo_param_iter, attr_iter, tmp) {
				mbo_param = nla_type(mbo_param_iter);

				switch (mbo_param) {
				case IFX_VENDOR_ATTR_MBO_PARAM_OPCLASS:
				{
					u8 op_class;

					op_class = nla_get_u8(mbo_param_iter);
					brcmf_pack_xtlv(IFX_VENDOR_ATTR_MBO_PARAM_OPCLASS,
							&op_class, sizeof(op_class),
							(char **)&mbo_xtlv, &buf_len);
				}
					break;
				case IFX_VENDOR_ATTR_MBO_PARAM_CHAN:
				{
					u8 chan;

					chan = nla_get_u8(mbo_param_iter);
					brcmf_pack_xtlv(IFX_VENDOR_ATTR_MBO_PARAM_CHAN,
							&chan, sizeof(chan),
							(char **)&mbo_xtlv, &buf_len);
				}
					break;
				case IFX_VENDOR_ATTR_MBO_PARAM_PREFERENCE:
				{
					u8 pref;

					pref = nla_get_u8(mbo_param_iter);
					brcmf_pack_xtlv(IFX_VENDOR_ATTR_MBO_PARAM_PREFERENCE,
							&pref, sizeof(pref),
							(char **)&mbo_xtlv, &buf_len);
				}
					break;
				case IFX_VENDOR_ATTR_MBO_PARAM_REASON_CODE:
				{
					u8 reason;

					reason = nla_get_u8(mbo_param_iter);
					brcmf_pack_xtlv(IFX_VENDOR_ATTR_MBO_PARAM_REASON_CODE,
							&reason, sizeof(reason),
							(char **)&mbo_xtlv, &buf_len);
				}
					break;
				case IFX_VENDOR_ATTR_MBO_PARAM_CELL_DATA_CAP:
				{
					u8 cell_data_cap;

					cell_data_cap = nla_get_u8(mbo_param_iter);
					brcmf_pack_xtlv(IFX_VENDOR_ATTR_MBO_PARAM_CELL_DATA_CAP,
							&cell_data_cap, sizeof(cell_data_cap),
							(char **)&mbo_xtlv, &buf_len);
				}
					break;
				case IFX_VENDOR_ATTR_MBO_PARAM_COUNTERS:
					break;
				case IFX_VENDOR_ATTR_MBO_PARAM_ENABLE:
				{
					u8 enable;

					enable = nla_get_u8(mbo_param_iter);
					brcmf_pack_xtlv(IFX_VENDOR_ATTR_MBO_PARAM_ENABLE,
							&enable, sizeof(enable),
							(char **)&mbo_xtlv, &buf_len);
				}
					break;
				case IFX_VENDOR_ATTR_MBO_PARAM_SUB_ELEM_TYPE:
				{
					u8 type;

					type = nla_get_u8(mbo_param_iter);
					brcmf_pack_xtlv(IFX_VENDOR_ATTR_MBO_PARAM_SUB_ELEM_TYPE,
							&type, sizeof(type),
							(char **)&mbo_xtlv, &buf_len);
				}
					break;
				case IFX_VENDOR_ATTR_MBO_PARAM_BTQ_TRIG_START_OFFSET:
				case IFX_VENDOR_ATTR_MBO_PARAM_BTQ_TRIG_RSSI_DELTA:
				case IFX_VENDOR_ATTR_MBO_PARAM_ANQP_CELL_SUPP:
				case IFX_VENDOR_ATTR_MBO_PARAM_BIT_MASK:
				case IFX_VENDOR_ATTR_MBO_PARAM_ASSOC_DISALLOWED:
				case IFX_VENDOR_ATTR_MBO_PARAM_CELLULAR_DATA_PREF:
					return -EOPNOTSUPP;
				default:
					brcmf_err("unknown mbo param attr:%d\n", mbo_param);
					return -EINVAL;
				}
			}
			break;
		default:
			brcmf_err("Unknown MBO attribute %d, skipping\n",
				  attr_type);
			return -EINVAL;
		}
	}

	buf_len = buf_len_start - buf_len;
	mbo_xtlv->len = cpu_to_le16(buf_len);
	mbo_iov->len = cpu_to_le16(buf_len);
	buf_len += sizeof(struct bcm_iov_buf);
	ret = brcmf_fil_iovar_data_set(ifp, "mbo", param, buf_len);

	if (ret)
		brcmf_err("set mbo enable error:%d\n", ret);

	return ret;
}

int ifx_cfg80211_vndr_cmds_mpc(struct wiphy *wiphy,
			       struct wireless_dev *wdev,
			       const void *data, int len)
{
	int ret = 0;
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_if *ifp;
	int val = *(s32 *)data;
	s32 buf = 0;

	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);
	ifp = vif->ifp;

	if (val == 0xa) {
		ret = brcmf_fil_iovar_int_get(ifp, "mpc", &buf);
		if (ret) {
			brcmf_err("get mpc error:%d\n", ret);
			return ret;
		}

		brcmf_dbg(INFO, "get mpc: %d\n", buf);
		ifx_cfg80211_vndr_send_cmd_reply(wiphy, &buf, sizeof(int));
	} else {
		ret = brcmf_fil_iovar_int_set(ifp, "mpc", val);
		if (ret)
			brcmf_err("set mpc error:%d\n", ret);
	}

	return ret;
}

int ifx_cfg80211_vndr_cmds_giantrx(struct wiphy *wiphy,
				   struct wireless_dev *wdev,
				   const void *data, int len)
{
	int ret = 0;
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_if *ifp;
	int val = *(s32 *)data;
	s32 buf = 0;

	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);
	ifp = vif->ifp;

	if (val == 0xa) {
		ret = brcmf_fil_iovar_int_get(ifp, "giantrx", &buf);
		if (ret) {
			brcmf_err("get giantrx error:%d\n", ret);
			return ret;
		}

		brcmf_dbg(INFO, "get giantrx: %d\n", buf);
		ifx_cfg80211_vndr_send_cmd_reply(wiphy, &buf, sizeof(int));
	} else {
		brcmf_fil_cmd_int_set(ifp, BRCMF_C_DOWN, 1);
		ret = brcmf_fil_iovar_int_set(ifp, "giantrx", val);
		brcmf_fil_cmd_int_set(ifp, BRCMF_C_UP, 1);
		if (ret)
			brcmf_err("set giantrx error:%d\n", ret);
	}
	return ret;
}

int ifx_cfg80211_vndr_cmds_wnm_max_idle(struct wiphy *wiphy,
					struct wireless_dev *wdev,
					const void  *data, int len)
{
	int tmp, attr_type = 0, wnm_param = 0, ret = 0;
	const struct nlattr *attr_iter, *wnm_param_iter;

	struct brcmf_cfg80211_vif *vif;
	struct brcmf_if *ifp;
	u8 param[64] = {0}, get_info = 0;
	u16 buf_len = 0, wnm_id = 0;

	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);
	ifp = vif->ifp;

	nla_for_each_attr(attr_iter, data, len, tmp) {
		attr_type = nla_type(attr_iter);

		switch (attr_type) {
		case IFX_VENDOR_ATTR_WNM_CMD:
			wnm_id = cpu_to_le16(nla_get_u8(attr_iter));
			break;
		case IFX_VENDOR_ATTR_WNM_PARAMS:
			nla_for_each_nested(wnm_param_iter, attr_iter, tmp) {
				wnm_param = nla_type(wnm_param_iter);
				switch (wnm_param) {
				case IFX_VENDOR_ATTR_WNM_PARAM_GET_INFO:
				{
					get_info = (int)nla_get_u8(wnm_param_iter);
				}
					break;
				case IFX_VENDOR_ATTR_WNM_PARAM_IDLE_PERIOD:
				{
					int period;

					period = (int)nla_get_u8(wnm_param_iter);
					memcpy(&param[buf_len], &period, sizeof(period));
					buf_len += sizeof(period);
				}
					break;
				case IFX_VENDOR_ATTR_WNM_PARAM_PROTECTION_OPT:
				{
					int option;

					option = (int)nla_get_u8(wnm_param_iter);
					memcpy(&param[buf_len], &option, sizeof(option));
					buf_len += sizeof(option);
				}
					break;
				default:
					brcmf_err("unknown wnm param attr:%d\n", wnm_param);
					return -EINVAL;
				}
			}
			break;
		default:
			brcmf_err("Unknown wnm attribute %d, skipping\n",
				  attr_type);
			return -EINVAL;
		}
	}

	switch (wnm_id) {
	case IFX_WNM_CMD_IOV_WNM_MAXIDLE:
	{
		if (get_info) {
			int get_period = 0;

			ret = brcmf_fil_iovar_int_get(ifp, "wnm_maxidle", &get_period);
			if (!ret)
				ret = ifx_cfg80211_vndr_send_cmd_reply(
					wiphy, &get_period, sizeof(get_period));
		} else
			ret = brcmf_fil_iovar_data_set(ifp, "wnm_maxidle", param, buf_len);
	}
	break;

	default:
		brcmf_err("unsupport wnm cmd:%d\n", wnm_id);
		return -EINVAL;
	}

	if (ret)
		brcmf_err("wnm %s error:%d\n", get_info?"get":"set", ret);

	return ret;
}

int ifx_cfg80211_vndr_cmds_hwcaps(struct wiphy *wiphy,
				  struct wireless_dev *wdev,
				  const void *data, int len)
{
	int ret = 0, i;
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_if *ifp;
	struct brcmf_bus *bus_if;
	s32 buf[IFX_VENDOR_HW_CAPS_MAX] = {0};

	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);
	ifp = vif->ifp;
	bus_if = ifp->drvr->bus_if;

	if (bus_if->chip == CY_CC_43022_CHIP_ID)
		buf[IFX_VENDOR_HW_CAPS_REPLAYCNTS] = 4;
	else
		buf[IFX_VENDOR_HW_CAPS_REPLAYCNTS] = 16;

	ret = ifx_cfg80211_vndr_send_cmd_reply(wiphy, buf, sizeof(int));
	if (ret) {
		brcmf_dbg(INFO, "get HW capability error %d\n", ret);
	} else {
		for (i = 0; i < IFX_VENDOR_HW_CAPS_MAX; i++)
			brcmf_dbg(INFO, "get %s: %d\n", hw_caps_name[i], buf[i]);
	}

	return ret;
}

int ifx_cfg80211_vndr_cmds_wnm_wl_cap(struct wiphy *wiphy,
				      struct wireless_dev *wdev,
				      const void *data, int len)
{
	int ret = 0;
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_if *ifp;
	int val = *(s32 *)data;
	s32 buf = 0;

	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);
	ifp = vif->ifp;

	if (val == 0xffff) {
		ret = brcmf_fil_iovar_int_get(ifp, "wnm", &buf);
		if (ret) {
			brcmf_err("get wnm_wl_cap error:%d\n", ret);
			return ret;
		}

		brcmf_dbg(INFO, "get wnm_wl_cap: %d\n", buf);
		ifx_cfg80211_vndr_send_cmd_reply(wiphy, &buf, sizeof(int));
	} else {
		ret = brcmf_fil_iovar_int_set(ifp, "wnm", val);
		if (ret)
			brcmf_err("set wnm_wl_cap error:%d\n", ret);
	}

	return ret;
}

int ifx_vndr_cmdstr_offload_config(struct wiphy *wiphy, struct wireless_dev *wdev,
				   char cmd_str[VNDR_CMD_STR_NUM][VNDR_CMD_STR_MAX_LEN],
				   long cmd_val[VNDR_CMD_VAL_NUM])
{
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_if *ifp;
	int ret = 0;

	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);
	ifp = vif->ifp;

	/* IW CMDSTR TEMPLATE.
	 * echo 'offload_config Enable 1 ' | iw dev wlan0 vendor send 0x000319
	 * 0x1C -
	 *
	 * echo 'offload_config Profile LowPwr 1 -s 0x3df ' | iw dev wlan0 vendor
	 * send 0x000319 0x1C -
	 *
	 */
	if (cmd_str[1][0] != '\0' && (strlen(cmd_str[1]) == 6) &&
	    (memcmp(cmd_str[1], "Enable", 6)) == 0 &&
	    (cmd_val[0] == 0 || cmd_val[0] == 1)) {
		brcmf_generic_offload_enable(ifp, brcmf_offload_feat, cmd_val[0]);
	} else if (cmd_str[1][0] != '\0' && (strlen(cmd_str[1]) == 7) &&
		  (memcmp(cmd_str[1], "Profile", 7)) == 0) {
		if (cmd_str[2][0] != '\0') {
			unsigned int ol_prof;

			if ((strlen(cmd_str[2]) == 6) &&
			    (memcmp(cmd_str[2], "LowPwr", 6)) == 0) {
				ol_prof = BRCMF_OL_PROF_TYPE_LOW_PWR;
			} else if ((strlen(cmd_str[2]) == 6) &&
				 (memcmp(cmd_str[2], "MidPwr", 6)) == 0) {
				ol_prof = BRCMF_OL_PROF_TYPE_MID_PWR;
			} else if ((strlen(cmd_str[2]) == 7) &&
				 (memcmp(cmd_str[2], "HighPwr", 7)) == 0) {
				ol_prof = BRCMF_OL_PROF_TYPE_HIGH_PWR;
			} else {
				brcmf_err("unknown offload_config Profile attr\n");
				return -EINVAL;
			}
			if (cmd_str[3][0] != '\0' && (strlen(cmd_str[3]) == 2) &&
			    (memcmp(cmd_str[3], "-s", 2)) == 0)
				brcmf_generic_offload_config(ifp, ~cmd_val[1], ol_prof, cmd_val[0]);
			else
				brcmf_generic_offload_config(ifp, brcmf_offload_feat, ol_prof,
							     cmd_val[0]);
		} else {
			brcmf_err("unknown offload_config Profile attr\n");
			return -EINVAL;
		}
	} else {
		brcmf_err("unknown offload_config attr\n");
		return -EINVAL;
	}

	return ret;
}

int ifx_vndr_cmdstr_mkeep_alive(struct wiphy *wiphy, struct wireless_dev *wdev,
				char cmd_str[VNDR_CMD_STR_NUM][VNDR_CMD_STR_MAX_LEN],
				long *cmd_val)
{
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_if *ifp;
	int ret = 0, i = 0, j = 0;
	struct ifx_mkeep_alive *mkeep_alive;
	u8 buf[150] = {0};
	bool immed_flag = 0;

	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);
	ifp = vif->ifp;

	/* NULL Keep-Alive
	 * echo 'mkeep_alive 0 1000 ' | iw dev wlan0 vendor
	 * send 0x000319 0x1C -
	 *
	 * NAT Keep-Alive
	 * echo 'mkeep_alive 0 1000 0x080027b1050a00904c3104
	 * 0008004500001e000040004011c52a0a8830700a88302513c
	 * 413c5000a00000a0d ' | iw dev wlan0 vendor
	 * send 0x000319 0x1C -
	 */
	if (cmd_val[0] < 0 || cmd_val[0] > 4 || cmd_val[1] < 0) {
		brcmf_err("Invalid command value\n");
		ret = -EINVAL;
		goto exit;
	}
	mkeep_alive = (struct ifx_mkeep_alive *)buf;

	mkeep_alive->period_msec = cmd_val[1];
	if (cmd_str[1][0] != '\0' && (strlen(cmd_str[1]) == 9) &&
	    (memcmp(cmd_str[1], "immediate", 9)) == 0) {
		immed_flag = 1;

		if (mkeep_alive->period_msec & WL_MKEEP_ALIVE_IMMEDIATE) {
			brcmf_err("Period %d too large\n", mkeep_alive->period_msec);
			ret = -EINVAL;
			goto exit;
		}
		if (immed_flag && mkeep_alive->period_msec)
			mkeep_alive->period_msec |= WL_MKEEP_ALIVE_IMMEDIATE;
	}
	mkeep_alive->version = WL_MKEEP_ALIVE_VERSION;
	mkeep_alive->keep_alive_id = cmd_val[0];
	mkeep_alive->length = offsetof(struct ifx_mkeep_alive, data);

	/* If there is no hex value for pkt data, it is treated as NULL KA.
	 * If there is hex value for pkt data, then copy hex as data and is
	 * treated as NAT KA.
	 */
	if (mkeep_alive->period_msec > 0) {
		j = 2;
		if (cmd_val[j] < 0) {
			mkeep_alive->len_bytes = 0;
		} else if (cmd_val[j + 14] < 0) {
			brcmf_err("Invalid pkt data. Required len bytes >= 14.\n");
			ret = -EINVAL;
			goto exit;
		} else {
			while (cmd_val[j] != ' ') {
				if (j <= VNDR_CMD_VAL_NUM) {
					mkeep_alive->data[i] = cmd_val[j];
					j++;
				}
				i++;
			}
			mkeep_alive->len_bytes = i;
		}
	}
	ret = brcmf_fil_iovar_data_set(ifp, "mkeep_alive", buf, sizeof(buf));
	if (ret)
		brcmf_err("Failed to set mkeeplive params: %d\n", ret);

exit:
	return ret;
}

int ifx_vndr_cmdstr_tko(struct wiphy *wiphy, struct wireless_dev *wdev,
			char cmd_str[VNDR_CMD_STR_NUM][VNDR_CMD_STR_MAX_LEN],
			long *cmd_val)
{
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_if *ifp;
	int ret = 0;
	struct ifx_tko *tko;
	struct ifx_tko_param *tko_param;
	struct ifx_tko_enable *tko_enable;
	u8 buf[128] = {0};
	int length;

	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);
	ifp = vif->ifp;

	tko = (struct ifx_tko *)buf;

	/* echo 'tko param 10 4 10 0 ' | iw dev wlan0 vendor
	 * send 0x000319 0x1C -
	 */
	if (cmd_str[1][0] != '\0' && (strlen(cmd_str[1]) == 5) &&
	    (memcmp(cmd_str[1], "param", 5) == 0) &&
	    (cmd_val[0] >= 0 && cmd_val[1] >= 0 &&
	     cmd_val[2] >= 0 && cmd_val[3] >= 0)) {
		tko_param = (struct ifx_tko_param *)tko->data;
		tko->subcmd_id = WL_TKO_SUBCMD_PARAM;
		tko->len = sizeof(*tko_param);
		tko_param->interval = cmd_val[0];
		tko_param->retry_interval = cmd_val[1];
		tko_param->retry_count = cmd_val[2];
		tko_param->rst_delay = cmd_val[3];

	} else if (cmd_str[1][0] != '\0' && (strlen(cmd_str[1]) == 6) &&
		   (memcmp(cmd_str[1], "enable", 6) == 0) &&
		   (cmd_val[0] == 0 || cmd_val[0] == 1)) {
		/* echo 'tko enable 1 ' | iw dev wlan0 vendor
		 * send 0x000319 0x1C -
		 */
		tko_enable = (struct ifx_tko_enable *)tko->data;
		tko->subcmd_id = WL_TKO_SUBCMD_ENABLE;
		tko->len = sizeof(*tko_enable);
		tko_enable->enable = cmd_val[0];
	} else {
		brcmf_err("Invalid tko command format\n");
		return -EINVAL;
	}

	length = offsetof(struct ifx_tko, data) + tko->len;
	ret = brcmf_fil_iovar_data_set(ifp, "tko", buf, length);
	if (ret)
		brcmf_err("Failed to configure tko: %d\n", ret);

	return ret;
}

/* ifx_vndr_cmd_str_parse_ip()
 * Get ip version. based on the ip version parse the command string into IP.
 * In Param
 * @cmd_str - String to be parsed.
 * @ip_addr - Parsed IP address storage.
 * Out Param
 * @ip_ver -  Pointer to IP version.
 * Return
 * true - success.
 * false - otherwise.
 */
static
int ifx_vndr_cmdstr_parse_ip(char *cmd_str, u8 *ip_addr, u8 *ip_ver)
{
	if (in4_pton(cmd_str, strlen(cmd_str), ip_addr, -1, NULL)) {
		*ip_ver = ICMP_ECHO_REQ_IP_V4;
		brcmf_dbg(INFO, "Peer IP Version: %d Peer IPv4 Address: %pI4\n",
			  *ip_ver, ip_addr);
		return true;
	} else if (in6_pton(cmd_str, strlen(cmd_str), ip_addr, -1, NULL)) {
		*ip_ver = ICMP_ECHO_REQ_IP_V6;
		brcmf_dbg(INFO, "Peer IP Version: %d Peer IPv6 Address: %pI6\n",
			  *ip_ver, ip_addr);
		return true;
	}

	return false;
}

/* ifx_vndr_icmp_echo_req_config()
 * Prepare ICMP Echo Request IOVAR based on the ICMP Echo Request Parameters.
 * In Param
 * @ifp - Pointer to brcmf_if structure.
 * @u8 - Sub Command Type.
 * @enable - Enable.
 * @ip_addr - IP Address to be filled.
 * @ip_ver - IP version.
 * @mac_addr - MAC Address to be filled.
 * @periodicity - Periodicity of ping in sec.
 * @duration - Duration in sec.
 * Return
 * 0 - success
 * Non Zero  - otherwise
 */
static
int ifx_vndr_icmp_echo_req_config(struct brcmf_if *ifp, u8 cmd_type,
				  u8 enable, u8 *ip_addr, u8 ip_ver,
				  u8 *mac_addr, u32 periodicity, u32 duration)
{
	struct brcmf_cfg80211_info *cfg = ifp->drvr->config;
	struct wiphy *wiphy = cfg_to_wiphy(cfg);
	struct ifx_icmp_echo_req_cmd *icmp_echo_req_cmd;
	struct ifx_icmp_echo_req_peer_config *icmp_echo_req_peer_config;
	struct ifx_icmp_echo_req_peer_ip *icmp_echo_req_peer_ip;
	struct ifx_icmp_echo_req_get_info *icmp_echo_req_get_info;
	int ret = 0;

	memset(cfg->extra_buf, '\0', WL_EXTRA_BUF_MAX);
	icmp_echo_req_cmd = (struct ifx_icmp_echo_req_cmd *)cfg->extra_buf;

	icmp_echo_req_cmd->version = WL_ICMP_ECHO_REQ_VER;
	icmp_echo_req_cmd->cmd_type = cmd_type;

	switch (icmp_echo_req_cmd->cmd_type) {
	case WL_ICMP_ECHO_REQ_ENAB:
		icmp_echo_req_cmd->data[0] = enable;
		icmp_echo_req_cmd->length = sizeof(struct ifx_icmp_echo_req_cmd) +
					    sizeof(u8);
		break;
	case WL_ICMP_ECHO_REQ_ADD:
		icmp_echo_req_peer_config = (struct ifx_icmp_echo_req_peer_config *)
					    icmp_echo_req_cmd->data;
		icmp_echo_req_cmd->length = sizeof(*icmp_echo_req_peer_config) +
					    sizeof(struct ifx_icmp_echo_req_cmd);
		icmp_echo_req_peer_config->version = WL_ICMP_ECHO_REQ_VER;
		icmp_echo_req_peer_config->ip_ver = ip_ver;
		memcpy(icmp_echo_req_peer_config->u.ipv6.addr, ip_addr,
		       (icmp_echo_req_peer_config->ip_ver == ICMP_ECHO_REQ_IP_V6) ?
			BRCMF_IPV6_ADDR_LEN : BRCMF_IPV4_ADDR_LEN);
		memcpy(icmp_echo_req_peer_config->mac_addr, mac_addr, ETH_ALEN);
		icmp_echo_req_peer_config->periodicity = periodicity;
		icmp_echo_req_peer_config->duration = duration;
		icmp_echo_req_peer_config->length = sizeof(struct ifx_icmp_echo_req_peer_config);
		break;
	case WL_ICMP_ECHO_REQ_DEL:
	case WL_ICMP_ECHO_REQ_START:
	case WL_ICMP_ECHO_REQ_STOP:
		icmp_echo_req_peer_ip = (struct ifx_icmp_echo_req_peer_ip *)
					icmp_echo_req_cmd->data;
		icmp_echo_req_cmd->length = sizeof(*icmp_echo_req_peer_ip) +
					    sizeof(struct ifx_icmp_echo_req_cmd);
		icmp_echo_req_peer_ip->version = WL_ICMP_ECHO_REQ_VER;
		icmp_echo_req_peer_ip->ip_ver = ip_ver;
		memcpy(icmp_echo_req_peer_ip->u.ipv6.addr, ip_addr,
		       (icmp_echo_req_peer_ip->ip_ver == ICMP_ECHO_REQ_IP_V6) ?
			BRCMF_IPV6_ADDR_LEN : BRCMF_IPV4_ADDR_LEN);
		icmp_echo_req_peer_ip->length = sizeof(struct ifx_icmp_echo_req_peer_ip);
		break;
	case WL_ICMP_ECHO_REQ_INFO:
		icmp_echo_req_peer_ip = (struct ifx_icmp_echo_req_peer_ip *)
					icmp_echo_req_cmd->data;
		icmp_echo_req_cmd->length = sizeof(*icmp_echo_req_peer_ip) +
					    sizeof(struct ifx_icmp_echo_req_cmd);
		icmp_echo_req_peer_ip->version = WL_ICMP_ECHO_REQ_VER;
		icmp_echo_req_peer_ip->ip_ver = ip_ver;
		if (ip_ver != ICMP_ECHO_REQ_IP_BOTH) {
			memcpy(icmp_echo_req_peer_ip->u.ipv6.addr, ip_addr,
			       (icmp_echo_req_peer_ip->ip_ver == ICMP_ECHO_REQ_IP_V6) ?
				BRCMF_IPV6_ADDR_LEN : BRCMF_IPV4_ADDR_LEN);
		}
		icmp_echo_req_peer_ip->length = sizeof(struct ifx_icmp_echo_req_peer_ip);
		break;
	default:
		brcmf_err("offload icmp_echo_req subcmd id %d not recognized",
			  icmp_echo_req_cmd->cmd_type);
		return -EOPNOTSUPP;
	}

	if (icmp_echo_req_cmd->cmd_type == WL_ICMP_ECHO_REQ_INFO) {
		icmp_echo_req_get_info = (struct ifx_icmp_echo_req_get_info *)cfg->extra_buf;
		ret = brcmf_fil_iovar_data_get(ifp, "icmp_echo_req", cfg->extra_buf,
					       WL_EXTRA_BUF_MAX);
		if (ret)
			brcmf_err("Failed to get icmp_echo_req info: %d\n", ret);
		else
			ifx_cfg80211_vndr_send_cmd_reply(wiphy, (void *)icmp_echo_req_get_info,
							 icmp_echo_req_get_info->length);
	} else {
		ret = brcmf_fil_iovar_data_set(ifp, "icmp_echo_req", (u8 *)icmp_echo_req_cmd,
					       icmp_echo_req_cmd->length);
		if (ret)
			brcmf_err("Failed to configure icmp_echo_req: %d\n", ret);
	}
	return ret;
}

int ifx_vndr_cmdstr_icmp_echo_req(struct wiphy *wiphy, struct wireless_dev *wdev,
				  char cmd_str[VNDR_CMD_STR_NUM][VNDR_CMD_STR_MAX_LEN],
				  long *cmd_val)
{
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_if *ifp;
	bool is_ip = false;
	u8 cmd_type, enable, ip_ver;
	u32 periodicity = 0, duration = 0;
	u8 ip_addr[BRCMF_IPV6_ADDR_LEN];
	u8 mac_addr[ETH_ALEN];

	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);
	ifp = vif->ifp;

	if (cmd_str[1][0] != '\0' && (strlen(cmd_str[1]) == 6) &&
	    (memcmp(cmd_str[1], "enable", 6) == 0) &&
	     (cmd_val[0] == 0 || cmd_val[0] == 1)) {
		/* echo 'icmp_echo_req enable 0/1 ' | iw dev wlan0 vendor
		 * send 0x000319 0x1C -
		 */
		cmd_type = WL_ICMP_ECHO_REQ_ENAB;
		enable = cmd_val[0];
		brcmf_dbg(INFO, "Cmd Type: %d enable: %d\n", cmd_type, enable);

	} else if (cmd_str[1][0] != '\0' && (strlen(cmd_str[1]) == 3) &&
		   (memcmp(cmd_str[1], "add", 3)) == 0) {
		/* echo 'icmp_echo_req add <peer_ip> <peer_mac>
		 *  <periodicity> <duration> ' | iw dev wlan0 vendor
		 * send 0x000319 0x1C -
		 */
		cmd_type = WL_ICMP_ECHO_REQ_ADD;
		is_ip = true;

		if (cmd_str[3][0] != '\0') {
			if (!mac_pton(cmd_str[3], mac_addr)) {
				brcmf_err("Invalid icmp_echo_req peer MAC address\n");
				return -EINVAL;
			}
		}

		if (cmd_val[0] >= 0 && cmd_val[1] >= 0) {
			periodicity = cmd_val[0];
			duration = cmd_val[1];
		}
		brcmf_dbg(INFO, "Cmd Type: %d MAC Address: %pM Periodicity: %d Duration: %d\n",
			  cmd_type, mac_addr, periodicity, duration);

	} else if (cmd_str[1][0] != '\0' && (strlen(cmd_str[1]) == 3) &&
		   (memcmp(cmd_str[1], "del", 3)) == 0) {
		/* echo 'icmp_echo_req del <peer_ip> ' | iw dev wlan0 vendor
		 * send 0x000319 0x1C -
		 */
		cmd_type = WL_ICMP_ECHO_REQ_DEL;
		is_ip = true;
		brcmf_dbg(INFO, "Cmd Type: %d\n", cmd_type);

	} else if (cmd_str[1][0] != '\0' && (strlen(cmd_str[1]) == 5) &&
		   (memcmp(cmd_str[1], "start", 5)) == 0) {
		/* echo 'icmp_echo_req start <peer_ip> ' | iw dev wlan0 vendor
		 * send 0x000319 0x1C -
		 */
		cmd_type = WL_ICMP_ECHO_REQ_START;
		is_ip = true;
		brcmf_dbg(INFO, "Cmd Type: %d\n", cmd_type);

	} else if (cmd_str[1][0] != '\0' && (strlen(cmd_str[1]) == 4) &&
		   (memcmp(cmd_str[1], "stop", 4)) == 0) {
		/* echo 'icmp_echo_req stop <peer_ip> ' | iw dev wlan0 vendor
		 * send 0x000319 0x1C -
		 */
		cmd_type = WL_ICMP_ECHO_REQ_STOP;
		is_ip = true;
		brcmf_dbg(INFO, "Cmd type: %d\n", cmd_type);

	} else if (cmd_str[1][0] != '\0' && (strlen(cmd_str[1]) == 4) &&
		   (memcmp(cmd_str[1], "info", 4)) == 0) {
		 /* echo 'icmp_echo_req info <peer_ip> ' | iw dev wlan0 vendor
		  * recv 0x000319 0x1C -
		  *
		  * echo 'icmp_echo_req info all ' | iw dev wlan0 vendor
		  * recv 0x000319 0x1C -
		  */
		cmd_type = WL_ICMP_ECHO_REQ_INFO;

		if (cmd_str[2][0] != '\0' && (strlen(cmd_str[2]) == 3) &&
		    (memcmp(cmd_str[2], "all", 3)) == 0)
			ip_ver = ICMP_ECHO_REQ_IP_BOTH;
		else
			is_ip = true;
		brcmf_dbg(INFO, "Cmd Type: %d\n", cmd_type);

	} else {
		brcmf_err("Invalid icmp_echo_req command format\n");
		return -EINVAL;
	}

	if (is_ip && (cmd_str[2][0] != '\0')) {
		if (!ifx_vndr_cmdstr_parse_ip(cmd_str[2], ip_addr, &ip_ver)) {
			brcmf_err("Invalid  peer IP address\n");
			return -EINVAL;
		}
	}

	return ifx_vndr_icmp_echo_req_config(ifp, cmd_type, enable, ip_addr,
					     ip_ver, mac_addr, periodicity,
					     duration);
}

int ifx_cfg80211_vndr_cmds_str(struct wiphy *wiphy, struct wireless_dev *wdev,
			       const void *data, int len)
{
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_if *ifp;
	struct ifx_vndr_cmdstr_hashtbl *hash_entry;
	u32 jhash_key;
	int ret = 0, idx_str = 0, idx_val = 0;
	unsigned long val;
	char cmd_str[VNDR_CMD_STR_NUM][VNDR_CMD_STR_MAX_LEN];
	long cmd_val[VNDR_CMD_VAL_NUM];
	char *tok = NULL, *buf = NULL;

	buf = (char *)data;
	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);
	ifp = vif->ifp;

	memset(cmd_str, '\0', VNDR_CMD_STR_NUM * VNDR_CMD_STR_MAX_LEN * sizeof(char));
	memset(cmd_val, -1, VNDR_CMD_VAL_NUM * sizeof(*cmd_val));

	while (idx_str < VNDR_CMD_STR_NUM && idx_val < VNDR_CMD_VAL_NUM &&
	       ((tok = strsep(&buf, " ")) != NULL)) {
		if (kstrtol(tok, 10, &val) == 0) {
			cmd_val[idx_val] = val;
			idx_val++;
		} else if ((strncmp(tok, "0x", 2) == 0) || (strncmp(tok, "0X", 2) == 0)) {
			if (kstrtol(tok, 16, &val) == 0) {
				cmd_val[idx_val] = val;
				idx_val++;

			} else if (strnlen(tok, VNDR_CMD_VAL_NUM) >= 20) {
			/* For larger input hex, split the hex pattern into 2 bytes each
			 * and store it individually.
			 */
				tok = tok + 2;/* Skip past 0x */
				if (strlen(tok) % 2 != 0) {
					brcmf_err("Data invalid format. Even length required\n");
					return -EINVAL;
				}
				while (*tok != '\0') {
					char num[3];

					if (idx_val >= VNDR_CMD_VAL_NUM) {
						brcmf_err("pkt header hex length exceeded\n");
						return -EINVAL;
					}
					memcpy(num, tok, 2);
					num[2] = '\0';
					if (kstrtol(num, 16, &val) == 0) {
						cmd_val[idx_val] = val;
					} else {
						brcmf_err("Invalid hex pkt data\n");
						return -EINVAL;
					}
					tok += 2;
					idx_val++;
				}
				cmd_val[idx_val] = ' ';
			} else {
				brcmf_err("Failed to parse hex token\n");
				return -EINVAL;
			}
		} else if (strnlen(tok, VNDR_CMD_STR_MAX_LEN) <= VNDR_CMD_STR_MAX_LEN) {
			strncpy(cmd_str[idx_str], tok, strnlen(tok, VNDR_CMD_STR_MAX_LEN));
			idx_str++;
		} else {
			brcmf_err("Failed to parse token\n");
			return -EINVAL;
		}
	}
	if (idx_str >= VNDR_CMD_STR_NUM || idx_val >= VNDR_CMD_VAL_NUM) {
		brcmf_err("CMD parameter limit exceeded\n");
		return -EINVAL;
	}
	/* Run the user cmd string input via Jenkins hash to pass and search the entry in
	 * vendor cmd hashtable initialized at load time.
	 */
	jhash_key = jhash(cmd_str[0], strlen(cmd_str[0]), 0);

	/* Search the user entered vndr cmd entry in the hash table and call its corresponding
	 * function handler.
	 */
	hash_for_each_possible(vndr_cmd_hashtbl, hash_entry, node, jhash_key) {
		if (hash_entry->vndr_cmd_addr &&
		    (strlen(cmd_str[0]) == strlen(hash_entry->vndr_cmd_addr->name)) &&
		    memcmp(hash_entry->vndr_cmd_addr->name, cmd_str[0],
			   strlen(hash_entry->vndr_cmd_addr->name)) == 0) {
			ret = hash_entry->vndr_cmd_addr->func(wiphy, wdev,
					cmd_str, cmd_val);
			break;
		}
	}

	return ret;
}

int ifx_cfg80211_vndr_cmds_config_pfn(struct wiphy *wiphy,
				      struct wireless_dev *wdev,
				      const void *data, int len)
{
	int buflen;
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	struct drv_config_pfn_params *pfn_data;

	brcmf_dbg(TRACE, "Enter pfn_enable %d Network_blob count %d\n",
		  cfg->pfn_enable, *((u8 *)data));

	cfg->pfn_enable = 1;
	pfn_data = (struct drv_config_pfn_params *)data;
	cfg->pfn_data.pfn_config = pfn_data->pfn_config;
	cfg->pfn_data.count = pfn_data->count;

	if (cfg->pfn_data.count > BRCMF_PNO_MAX_PFN_COUNT) {
		brcmf_dbg(TRACE, "Not in range. Max 16 ssids allowed to add in pfn list");
		cfg->pfn_data.count = BRCMF_PNO_MAX_PFN_COUNT;
	}

	buflen = cfg->pfn_data.count * sizeof(struct network_blob);
	cfg->pfn_data.network_blob_data = kmalloc(buflen, GFP_KERNEL);
	memset(cfg->pfn_data.network_blob_data, '\0', buflen);
	memcpy(cfg->pfn_data.network_blob_data, (u8 *)data + PFN_CONFIG_AND_COUNT_SIZE, buflen);
	pfn_send_network_blob_fw(wiphy, wdev);
	brcmf_dbg(TRACE, "Exit\n");
	return 0;
}

int ifx_cfg80211_vndr_cmds_get_pfn_status(struct wiphy *wiphy,
					  struct wireless_dev *wdev,
					  const void *data, int len)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	u8 *buf = NULL;
	struct brcmf_bss_info_le *bi = NULL;
	int err = 0, i = 0;
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_if *ifp;
	struct network_blob *network_blob_data = NULL;
	struct brcmu_chan ch;
	struct pfn_conn_info curr_bssid;

	brcmf_dbg(TRACE, "Enter\n");
	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);
	ifp = vif->ifp;
	if (cfg->pfn_enable != 1)
		return 0;
	buf = kzalloc(WL_BSS_INFO_MAX, GFP_KERNEL);
	if (!buf) {
		err = -ENOMEM;
		return err;
	}

	*(u32 *)buf = cpu_to_le32(WL_BSS_INFO_MAX);
	err = brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_BSS_INFO,
				     buf, WL_BSS_INFO_MAX);
	if (err) {
		brcmf_err("pfn_status buf error:%d\n", err);
		return err;
	}
	bi = (struct brcmf_bss_info_le *)(buf + 4);
	memset(&curr_bssid, '\0', sizeof(struct pfn_conn_info));

	if (bi->SSID_len > 0) {
		memcpy(curr_bssid.SSID, bi->SSID, bi->SSID_len);
		memcpy(curr_bssid.BSSID, bi->BSSID, ETH_ALEN);
		curr_bssid.SSID_len = bi->SSID_len;
		curr_bssid.RSSI = bi->RSSI;
		curr_bssid.phy_noise = bi->phy_noise;
		ch.chspec = le16_to_cpu(bi->chanspec);
		cfg->d11inf.decchspec(&ch);
		curr_bssid.channel = ch.control_ch_num;
		curr_bssid.SNR = bi->SNR;

		network_blob_data = cfg->pfn_data.network_blob_data;
		for (; i < cfg->pfn_data.count && network_blob_data; i++) {
			if (!strncmp(network_blob_data->ssid, bi->SSID, bi->SSID_len)) {
				curr_bssid.proto = network_blob_data->proto;
				curr_bssid.key_mgmt = network_blob_data->key_mgmt;
				break;
			}
			network_blob_data++;
		}
	}
	if (curr_bssid.SSID_len)
		ifx_cfg80211_vndr_send_cmd_reply(wiphy, (void *)&curr_bssid,
						 sizeof(struct pfn_conn_info));
	kfree(buf);
	brcmf_dbg(TRACE, "Exit\n");
	return 0;
}

int ifx_cfg80211_vndr_cmds_ssid_prot(struct wiphy *wiphy,
				     struct wireless_dev *wdev,
				     const void *data, int len)
{
	int ret = 0;
	int val = 0;
	struct brcmf_cfg80211_vif *vif;
	struct brcmf_if *ifp;
	const struct nlattr *attr_iter;
	int tmp, attr_type = 0;

	vif = container_of(wdev, struct brcmf_cfg80211_vif, wdev);
	ifp = vif->ifp;

	nla_for_each_attr(attr_iter, data, len, tmp) {
		attr_type = nla_type(attr_iter);
		if (attr_type == IFX_VENDOR_ATTR_SSID_PROT_ENABLE) {
			val = nla_get_u8(attr_iter);
			ret = brcmf_fil_iovar_int_set(ifp, "ssid_protection", val);
			if (ret < 0)
				brcmf_err("Failed set ssid_protection, ret=%d\n", ret);
			else
				brcmf_dbg(INFO, "ssid_protection is %s\n",
					  val ? "enabled" : "disabled");
		}
	}
	return ret;
}

int ifx_cfg80211_vndr_cmds_mchan_config(struct wiphy *wiphy,
					struct wireless_dev *wdev,
					const void *data, int len)
{
	struct brcmf_cfg80211_info *cfg = wiphy_to_cfg(wiphy);
	u8 val = *(u8 *)data;

	brcmf_dbg(TRACE, "enter, config: %d\n", val);

	if (val <= BRCMF_MCHAN_CONF_AUDIO)
		cfg->mchan_conf = val;

	return 0;
}
