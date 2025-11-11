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

#ifndef IFX_VENDOR_H
#define IFX_VENDOR_H

#include <net/netlink.h>
#include <net/cfg80211.h>

/* This file is a registry of identifier assignments from the Infineon
 * OUI 00:03:19 for purposes other than MAC address assignment. New identifiers
 * can be assigned through normal review process for changes to the upstream
 * hostap.git repository.
 */
#define OUI_IFX		0x000319

#define SCMD(_CMD)	IFX_VENDOR_SCMD_##_CMD
#define IFX_SUBCMD(_CMD, _FLAGS, _POLICY, _FN) \
	{	\
		.vendor_id = OUI_IFX,	\
		.subcmd = SCMD(_CMD)	\
	},	\
	.flags = (_FLAGS),	\
	.policy = (_POLICY),	\
	.doit = (_FN)

struct bcm_iov_buf {
	u16	version;
	u16	len;
	u16	id;
	u16	data[1];
};

/*
 * enum ifx_nl80211_vendor_subcmds - IFX nl80211 vendor command identifiers
 *
 * @IFX_VENDOR_SCMD_UNSPEC: Reserved value 0
 *
 * @IFX_VENDOR_SCMD_DCMD: Handle the Dongle commands triggered from the userspace utilities.
 *	These commands will be passed to the Dongle for processing.
 *
 * @IFX_VENDOR_SCMD_FRAMEBURST: Control the Frameburst feature. This feature allows more
 *	efficient use of the airtime between the transmitting and receiving WLAN devices.
 *
 * @IFX_VENDOR_SCMD_ACS: Configure the Automatic Channel Selection (ACS) feature.
 *
 * @IFX_VENDOR_SCMD_SET_MAC_P2P_DEV: Set MAC address for a P2P Discovery device.
 *	Uses Vendor attribute IFX_VENDOR_ATTR_MAC_ADDR to pass the MAC address.
 *
 * @IFX_VENDOR_SCMD_MUEDCA_OPT: Configure Multi User Enhanced Distrubuted Channel Access (MU-EDCA).
 *
 * @IFX_VENDOR_SCMD_LDPC: Enable support for handling Low Density Parity Check (LDPC) Coding
 *	in received payload.
 *
 * @IFX_VENDOR_SCMD_AMSDU: Control AMSDU aggregation for both TX & RX on all the TID queues.
 *
 * @IFX_VENDOR_SCMD_TWT: Configure Target Wake Time (TWT) Session with the needed parameters.
 *	Uses Vendor attributes defined in the enum ifx_vendor_attr_twt.
 *
 * @IFX_VENDOR_SCMD_OCE: Configure the Optimized Connectivity Experience (OCE) functionality
 *	related parameters.
 *
 * @IFX_VENDOR_SCMD_BSSCOLOR: Set BSS Color (1-63) for AP Mode operation in HE.
 *
 * @IFX_VENDOR_SCMD_RAND_MAC: Configure the Random MAC module.
 *
 * @IFX_VENDOR_SCMD_MBO: Configure Multi Band Operation (MBO) functionality related parameters.
 *
 * @IFX_VENDOR_SCMD_MPC: Control the Minimum Power Consumption (MPC) feature.
 *	This is a STA-only power saving feature and not related to 802.11 power save.
 *
 * @IFX_VENDOR_SCMD_GIANTRX: Allow handling RX MGMT Packts of size 1840 bytes.
 *
 * @IFX_VENDOR_SCMD_PFN_CONFIG: Send the Preferred Network (PFN) information to the Dongle
 *
 * @IFX_VENDOR_SCMD_PFN_STATUS: Fetch the Preferred Network (PFN) information from the Dongle
 *	through the driver.
 *
 * @IFX_VENDOR_SCMD_WNM: Configure the Wireless Network Management (WNM) 802.11v functionaltiy
 *	related parameters.
 *
 * @IFX_VENDOR_SCMD_HWCAPS: Get device's capability.
 *
 * @IFX_VENDOR_SCMD_CMDSTR: New vendor string infra subcmd to handle user supplied strings.
 *	String parsing and calling corresponding function handler for a specific command
 *	given by user.
 *
 * @IFX_VENDOR_SCMD_SSID_PROT: Vendor command to enable/disable SSID protection
 *
 * @IFX_VENDOR_SCMD_MAX: This acts as a the tail of cmds list.
 *      Make sure it located at the end of the list.
 */
enum ifx_nl80211_vendor_subcmds {
	SCMD(UNSPEC)		= 0,
	SCMD(DCMD)		= 1,
	SCMD(RSV2)		= 2,
	SCMD(RSV3)		= 3,
	SCMD(RSV4)		= 4,
	SCMD(RSV5)		= 5,
	SCMD(FRAMEBURST)	= 6,
	SCMD(RSV7)		= 7,
	SCMD(RSV8)		= 8,
	SCMD(ACS)		= 9,
	SCMD(SET_MAC_P2P_DEV)	= 10,
	SCMD(MUEDCA_OPT)	= 11,
	SCMD(LDPC)		= 12,
	SCMD(AMSDU)		= 13,
	SCMD(TWT)		= 14,
	SCMD(OCE)		= 15,
	SCMD(BSSCOLOR)		= 16,
	SCMD(RAND_MAC)		= 17,
	SCMD(MBO)		= 18,
	SCMD(MPC)		= 19,
	SCMD(GIANTRX)		= 20,
	SCMD(PFN_CONFIG)	= 21,
	SCMD(PFN_STATUS)	= 22,
	SCMD(RSV22)		= 23,
	SCMD(RSV24)		= 24,
	SCMD(WNM)		= 25,
	SCMD(HWCAPS)		= 26,
	SCMD(WNM_WL_CAP)	= 27,
	SCMD(CMDSTR)		= 28,
	/* Reserved 29 */
	SCMD(SSID_PROT)		= 30,
	SCMD(MCHAN_CONFIG)	= 31,
	SCMD(MAX)		= 32
};

/* enum ifx_vendor_evts - IFX nl80211 vendor events
 *
 * @IFX_VENDOR_EVTS_RSV0: Reserved Event
 *
 * @IFX_VENDOR_EVTS_RSV1: Reserved Event
 *
 * @IFX_VENDOR_EVTS_ICMP_ECHO_REQ: ICMP Echo Request Event
 *
 * @IFX_VENDOR_EVTS_LAST: This acts as a the tail of evts list.
 *      Make sure it located at the end of the list.
 */
enum ifx_vendor_evts {
	IFX_VENDOR_EVTS_RSV0,
	IFX_VENDOR_EVTS_RSV1,
	IFX_VENDOR_EVTS_ICMP_ECHO_REQ,
	IFX_VENDOR_EVTS_LAST
};

/*
 * enum ifx_vendor_attr - IFX nl80211 vendor attributes
 *
 * @IFX_VENDOR_ATTR_UNSPEC: Reserved value 0
 *
 * @IFX_VENDOR_ATTR_LEN: Dongle Command Message Body Length.
 *
 * @IFX_VENDOR_ATTR_DATA: Dongle Commend Message Body.
 *
 * @IFX_VENDOR_ATTR_MAC_ADDR: Medium Access Control (MAC) address.
 *
 * @IFX_VENDOR_ATTR_MAX: This acts as a the tail of attrs list.
 *      Make sure it located at the end of the list.
 */
enum ifx_vendor_attr {
	IFX_VENDOR_ATTR_UNSPEC		= 0,
	IFX_VENDOR_ATTR_LEN		= 1,
	IFX_VENDOR_ATTR_DATA		= 2,
	IFX_VENDOR_ATTR_MAC_ADDR	= 3,
	/* Reserved 4-10 */
	IFX_VENDOR_ATTR_MAX
};

#define IFX_MBO_IOV_MAJOR_VER 1
#define IFX_MBO_IOV_MINOR_VER 1
#define IFX_MBO_IOV_MAJOR_VER_SHIFT 8
#define IFX_MBO_IOV_VERSION \
	((IFX_MBO_IOV_MAJOR_VER << IFX_MBO_IOV_MAJOR_VER_SHIFT) | \
	  IFX_MBO_IOV_MINOR_VER)

enum ifx_vendor_attr_mbo_param {
	IFX_VENDOR_ATTR_MBO_PARAM_UNSPEC = 0,
	IFX_VENDOR_ATTR_MBO_PARAM_OPCLASS = 1,
	IFX_VENDOR_ATTR_MBO_PARAM_CHAN = 2,
	IFX_VENDOR_ATTR_MBO_PARAM_PREFERENCE = 3,
	IFX_VENDOR_ATTR_MBO_PARAM_REASON_CODE = 4,
	IFX_VENDOR_ATTR_MBO_PARAM_CELL_DATA_CAP = 5,
	IFX_VENDOR_ATTR_MBO_PARAM_COUNTERS = 6,
	IFX_VENDOR_ATTR_MBO_PARAM_ENABLE = 7,
	IFX_VENDOR_ATTR_MBO_PARAM_SUB_ELEM_TYPE = 8,
	IFX_VENDOR_ATTR_MBO_PARAM_BTQ_TRIG_START_OFFSET = 9,
	IFX_VENDOR_ATTR_MBO_PARAM_BTQ_TRIG_RSSI_DELTA = 10,
	IFX_VENDOR_ATTR_MBO_PARAM_ANQP_CELL_SUPP = 11,
	IFX_VENDOR_ATTR_MBO_PARAM_BIT_MASK = 12,
	IFX_VENDOR_ATTR_MBO_PARAM_ASSOC_DISALLOWED = 13,
	IFX_VENDOR_ATTR_MBO_PARAM_CELLULAR_DATA_PREF = 14,
	IFX_VENDOR_ATTR_MBO_PARAM_MAX = 15
};

static const struct nla_policy
ifx_vendor_attr_mbo_param_policy[IFX_VENDOR_ATTR_MBO_PARAM_MAX + 1] = {
	[IFX_VENDOR_ATTR_MBO_PARAM_UNSPEC] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_MBO_PARAM_OPCLASS] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_MBO_PARAM_CHAN] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_MBO_PARAM_PREFERENCE] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_MBO_PARAM_REASON_CODE] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_MBO_PARAM_CELL_DATA_CAP] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_MBO_PARAM_COUNTERS] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_MBO_PARAM_ENABLE] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_MBO_PARAM_SUB_ELEM_TYPE] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_MBO_PARAM_BTQ_TRIG_START_OFFSET] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_MBO_PARAM_BTQ_TRIG_RSSI_DELTA] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_MBO_PARAM_ANQP_CELL_SUPP] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_MBO_PARAM_BIT_MASK] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_MBO_PARAM_ASSOC_DISALLOWED] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_MBO_PARAM_CELLULAR_DATA_PREF] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_MBO_PARAM_MAX] = {.type = NLA_U8},
};

enum ifx_vendor_attr_mbo {
	IFX_VENDOR_ATTR_MBO_UNSPEC,
	IFX_VENDOR_ATTR_MBO_CMD,
	IFX_VENDOR_ATTR_MBO_PARAMS,
	IFX_VENDOR_ATTR_MBO_MAX
};

static const struct nla_policy ifx_vendor_attr_mbo_policy[IFX_VENDOR_ATTR_MBO_MAX + 1] = {
	[IFX_VENDOR_ATTR_MBO_UNSPEC] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_MBO_CMD] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_MBO_PARAMS] =
		NLA_POLICY_NESTED(ifx_vendor_attr_mbo_param_policy),
	[IFX_VENDOR_ATTR_MBO_MAX] = {.type = NLA_U8},
};

enum {
	IFX_MBO_CMD_ADD_CHAN_PREF = 1,
	IFX_MBO_CMD_DEL_CHAN_PREF = 2,
	IFX_MBO_CMD_LIST_CHAN_PREF = 3,
	IFX_MBO_CMD_CELLULAR_DATA_CAP = 4,
	IFX_MBO_CMD_DUMP_COUNTERS = 5,
	IFX_MBO_CMD_CLEAR_COUNTERS = 6,
	IFX_MBO_CMD_FORCE_ASSOC = 7,
	IFX_MBO_CMD_BSSTRANS_REJECT = 8,
	IFX_MBO_CMD_SEND_NOTIF = 9,
	IFX_MBO_CMD_LAST
};

enum {
	IFX_MBO_XTLV_OPCLASS            = 0x1,
	IFX_MBO_XTLV_CHAN               = 0x2,
	IFX_MBO_XTLV_PREFERENCE         = 0x3,
	IFX_MBO_XTLV_REASON_CODE        = 0x4,
	IFX_MBO_XTLV_CELL_DATA_CAP      = 0x5,
	IFX_MBO_XTLV_COUNTERS           = 0x6,
	IFX_MBO_XTLV_ENABLE             = 0x7,
	IFX_MBO_XTLV_SUB_ELEM_TYPE      = 0x8,
	IFX_MBO_XTLV_BTQ_TRIG_START_OFFSET = 0x9,
	IFX_MBO_XTLV_BTQ_TRIG_RSSI_DELTA = 0xa,
	IFX_MBO_XTLV_ANQP_CELL_SUPP      = 0xb,
	IFX_MBO_XTLV_BIT_MASK		= 0xc,
	IFX_MBO_XTLV_ASSOC_DISALLOWED	= 0xd,
	IFX_MBO_XTLV_CELLULAR_DATA_PREF = 0xe
};

/*
 * enum ifx_vendor_attr_twt - Attributes for the TWT vendor command
 *
 * @IFX_VENDOR_ATTR_TWT_UNSPEC: Reserved value 0
 *
 * @IFX_VENDOR_ATTR_TWT_OPER: To specify the type of TWT operation
 *	to be performed. Uses attributes defined in enum ifx_twt_oper.
 *
 * @IFX_VENDOR_ATTR_TWT_PARAMS: Nester attributes representing the
 *	parameters configured for TWT. These parameters are defined in
 *	the enum ifx_vendor_attr_twt_param.
 *
 * @IFX_VENDOR_ATTR_TWT_MAX: This acts as a the tail of cmds list.
 *      Make sure it located at the end of the list.
 */
enum ifx_vendor_attr_twt {
	IFX_VENDOR_ATTR_TWT_UNSPEC,
	IFX_VENDOR_ATTR_TWT_OPER,
	IFX_VENDOR_ATTR_TWT_PARAMS,
	IFX_VENDOR_ATTR_TWT_MAX
};

/*
 * enum ifx_twt_oper - TWT operation to be specified using the vendor
 * attribute IFX_VENDOR_ATTR_TWT_OPER
 *
 * @IFX_TWT_OPER_UNSPEC: Reserved value 0
 *
 * @IFX_TWT_OPER_SETUP: Setup a TWT session. Required parameters are
 *	obtained through the nested attrs under %IFX_VENDOR_ATTR_TWT_PARAMS.
 *
 * @IFX_TWT_OPER_TEARDOWN: Teardown the already negotiated TWT session.
 *	Required parameters are obtained through the nested attrs under
 *	IFX_VENDOR_ATTR_TWT_PARAMS.
 *
 * @IFX_TWT_OPER_MAX: This acts as a the tail of the list.
 *      Make sure it located at the end of the list.
 */
enum ifx_twt_oper {
	IFX_TWT_OPER_UNSPEC,
	IFX_TWT_OPER_SETUP,
	IFX_TWT_OPER_TEARDOWN,
	IFX_TWT_OPER_MAX
};

/*
 * enum ifx_vendor_attr_twt_param - TWT parameters
 *
 * @IFX_VENDOR_ATTR_TWT_PARAM_UNSPEC: Reserved value 0
 *
 * @IFX_VENDOR_ATTR_TWT_PARAM_NEGO_TYPE: Specifies the type of Negotiation to be
 *	done during Setup. The four possible types are
 *	0 - Individual TWT Negotiation
 *	1 - Wake TBTT Negotiation
 *	2 - Broadcast TWT in Beacon
 *	3 - Broadcast TWT Membership Negotiation
 *
 *	The possible values are defined in the enum ifx_twt_param_nego_type
 *
 * @IFX_VENDOR_ATTR_TWT_PARAM_SETUP_CMD_TYPE: Specifies the type of TWT Setup frame
 *	when sent by the TWT Requesting STA
 *	0 - Request
 *	1 - Suggest
 *	2 - Demand
 *
 *	when sent by the TWT Responding STA.
 *	3 - Grouping
 *	4 - Accept
 *	5 - Alternate
 *	6 - Dictate
 *	7 - Reject
 *
 *	The possible values are defined in the enum ifx_twt_oper_setup_cmd_type.
 *
 * @IFX_VENDOR_ATTR_TWT_PARAM_DIALOG_TOKEN: Dialog Token used by the TWT Requesting STA to
 *	identify the TWT Setup request/response transaction.
 *
 * @IFX_VENDOR_ATTR_TWT_PARAM_WAKE_TIME: Target Wake Time TSF at which the STA has to wake up.
 *
 * @IFX_VENDOR_ATTR_TWT_PARAM_WAKE_TIME_OFFSET: Target Wake Time TSF Offset from current TSF
 *	in microseconds.
 *
 * @IFX_VENDOR_ATTR_TWT_PARAM_MIN_WAKE_DURATION: Nominal Minimum TWT Wake Duration.
 *	Used along with %IFX_VENDOR_ATTR_TWT_PARAM_MIN_WAKE_DURATION_UNIT to derive Wake Duration.
 *
 * @IFX_VENDOR_ATTR_TWT_PARAM_WAKE_INTVL_EXPONENT: TWT Wake Interval Exponent.
 *	Used along with %IFX_VENDOR_ATTR_TWT_PARAM_WAKE_INTVL_MANTISSA to derive Wake Interval.
 *
 * @IFX_VENDOR_ATTR_TWT_PARAM_WAKE_INTVL_MANTISSA: TWT Wake Interval Mantissa.
 *	Used along with %IFX_VENDOR_ATTR_TWT_PARAM_WAKE_INTVL_EXPONENT to derive Wake Interval.
 *
 * @IFX_VENDOR_ATTR_TWT_PARAM_REQUESTOR: Specify this is a TWT Requesting / Responding STA.
 *
 * @IFX_VENDOR_ATTR_TWT_PARAM_TRIGGER: Specify Trigger based / Non-Trigger based TWT Session.
 *
 * @IFX_VENDOR_ATTR_TWT_PARAM_IMPLICIT: Specify Implicit / Explicit TWT session.
 *
 * @IFX_VENDOR_ATTR_TWT_PARAM_FLOW_TYPE: Specify Un-Announced / Announced TWT session.
 *
 * @IFX_VENDOR_ATTR_TWT_PARAM_FLOW_ID: Flow ID is the unique identifier of an iTWT session.
 *
 * @IFX_VENDOR_ATTR_TWT_PARAM_BCAST_TWT_ID: Broadcast TWT ID is the unique identifier of a
 *	bTWT session.
 *
 * @IFX_VENDOR_ATTR_TWT_PARAM_PROTECTION: Specifies whether Tx within SP is protected.
 *	Set to 1 to indicate that TXOPs within the TWT SPs shall be initiated
 *	with a NAV protection mechanism, such as (MU) RTS/CTS or CTS-to-self frame;
 *	otherwise, it shall set it to 0.
 *
 * @IFX_VENDOR_ATTR_TWT_PARAM_CHANNEL: TWT channel field which is set to 0, unless
 *	the HE STA sets up a subchannel selective transmission operation.
 *
 * @IFX_VENDOR_ATTR_TWT_PARAM_TWT_INFO_FRAME_DISABLED: TWT Information frame RX handing
 *	disabled / enabled.
 *
 * @IFX_VENDOR_ATTR_TWT_PARAM_MIN_WAKE_DURATION_UNIT: Nominal Minimum TWT Wake Duration
 *	Unit. 0 represents unit in "256 usecs" and 1 represents unit in "TUs".
 *
 * @IFX_VENDOR_ATTR_TWT_PARAM_TEARDOWN_ALL_TWT: Teardown all negotiated TWT sessions.
 *
 * @IFX_VENDOR_ATTR_TWT_PARAM_MAX: This acts as a the tail of the list.
 *      Make sure it located at the end of the list.
 */
enum ifx_vendor_attr_twt_param {
	IFX_VENDOR_ATTR_TWT_PARAM_UNSPEC,
	IFX_VENDOR_ATTR_TWT_PARAM_NEGO_TYPE,
	IFX_VENDOR_ATTR_TWT_PARAM_SETUP_CMD_TYPE,
	IFX_VENDOR_ATTR_TWT_PARAM_DIALOG_TOKEN,
	IFX_VENDOR_ATTR_TWT_PARAM_WAKE_TIME,
	IFX_VENDOR_ATTR_TWT_PARAM_WAKE_TIME_OFFSET,
	IFX_VENDOR_ATTR_TWT_PARAM_MIN_WAKE_DURATION,
	IFX_VENDOR_ATTR_TWT_PARAM_WAKE_INTVL_EXPONENT,
	IFX_VENDOR_ATTR_TWT_PARAM_WAKE_INTVL_MANTISSA,
	IFX_VENDOR_ATTR_TWT_PARAM_REQUESTOR,
	IFX_VENDOR_ATTR_TWT_PARAM_TRIGGER,
	IFX_VENDOR_ATTR_TWT_PARAM_IMPLICIT,
	IFX_VENDOR_ATTR_TWT_PARAM_FLOW_TYPE,
	IFX_VENDOR_ATTR_TWT_PARAM_FLOW_ID,
	IFX_VENDOR_ATTR_TWT_PARAM_BCAST_TWT_ID,
	IFX_VENDOR_ATTR_TWT_PARAM_PROTECTION,
	IFX_VENDOR_ATTR_TWT_PARAM_CHANNEL,
	IFX_VENDOR_ATTR_TWT_PARAM_TWT_INFO_FRAME_DISABLED,
	IFX_VENDOR_ATTR_TWT_PARAM_MIN_WAKE_DURATION_UNIT,
	IFX_VENDOR_ATTR_TWT_PARAM_TEARDOWN_ALL_TWT,
	IFX_VENDOR_ATTR_TWT_PARAM_MAX
};

/*
 * enum ifx_twt_param_nego_type - TWT Session Negotiation Type Parameters
 *
 * @IFX_TWT_PARAM_NEGO_TYPE_ITWT: Individual TWT negotiation between TWT requesting STA
 *	and TWT responding STA or individual TWT announcement by TWT Responder
 *
 * @IFX_TWT_PARAM_NEGO_TYPE_WAKE_TBTT: Wake TBTT and Wake interval negotiation between
 *	TWT scheduled STA and TWT scheduling AP.
 *
 * @IFX_TWT_PARAM_NEGO_TYPE_BTWT_IE_BCN: Provide Broadcast TWT schedules to TWT scheduled
 *	STAs by including the TWT element in broadcast Managemnet frames sent by TWT
 *	scheduling AP.
 *
 * @IFX_TWT_PARAM_NEGO_TYPE_BTWT: Broadcast TWT negotiation between TWT requesting STA
 *	and TWT responding STA. Manage Memberships in broadcast TWT schedules by including
 *	the TWT element in individually addressed Management frames sent by either a TWT
 *	scheduled STA or a TWT scheduling AP.
 *
 * @IFX_TWT_PARAM_NEGO_TYPE_MAX: This acts as a the tail of the list.
 *      Make sure it located at the end of the list.
 */
enum ifx_twt_param_nego_type {
	IFX_TWT_PARAM_NEGO_TYPE_INVALID		= -1,
	IFX_TWT_PARAM_NEGO_TYPE_ITWT		= 0,
	IFX_TWT_PARAM_NEGO_TYPE_WAKE_TBTT	= 1,
	IFX_TWT_PARAM_NEGO_TYPE_BTWT_IE_BCN	= 2,
	IFX_TWT_PARAM_NEGO_TYPE_BTWT		= 3,
	IFX_TWT_PARAM_NEGO_TYPE_MAX		= 4
};

/*
 * enum ifx_vendor_attr_twt_param - TWT Session setup command types
 *
 * @IFX_TWT_OPER_SETUP_CMD_TYPE_REQUEST: A TWT requesting or TWT scheduled STA
 *	requests to join a TWT without specifying a target wake time. This type needs to
 *	be used only by the TWT requesting STA.
 *
 * @IFX_TWT_OPER_SETUP_CMD_TYPE_SUGGEST: A TWT requesting or TWT scheduled STA requests to
 *	join a TWT without specifying a target wake time. This type needs to be used only
 *	by the TWT requesting STA.
 *
 * @IFX_TWT_OPER_SETUP_CMD_TYPE_DEMAND: A TWT requesting or TWT scheduled STA requests to
 *	join a TWT and specifies a demanded set of TWT parameters. If the demanded set of
 *	TWT parameters is not accommodated by the responding STA or TWT scheduling AP, then
 *	the TWT requesting STA or TWT scheduled STA will reject the TWT setup. This type
 *	needs to be used only by the TWT requesting STA.
 *
 * @IFX_TWT_OPER_SETUP_CMD_TYPE_GROUPING: The TWT responding STA suggests TWT group
 *	parameters that are different from the suggested or demanded TWT parameters of the
 *	TWT requesting STA. This type needs to be used only by the S1G TWT Responding STA in
 *	case of ITWT Setup Negotiation.
 *
 * @IFX_TWT_OPER_SETUP_CMD_TYPE_ACCEPT: A TWT responding STA or TWT scheduling AP accepts
 *	the TWT request with the TWT parameters (see NOTE) indicated in the TWT element
 *	transmitted by the TWT requesting STA or TWT scheduled STA. This value is also used
 *	in unsolicited TWT responses. This needs type needs to be used only by the TWT
 *	responding STA.
 *
 * @IFX_TWT_OPER_SETUP_CMD_TYPE_ALTERNATE: A TWT responding STA or TWT scheduling AP suggests
 *	TWT parameters that are different from those suggested by the TWT requesting STA or
 *	TWT scheduled STA. This needs type needs to be used only by the TWT reponding STA.
 *
 * @IFX_TWT_OPER_SETUP_CMD_TYPE_DICTATE: A TWT responding STA or TWT scheduling AP indicates
 *	TWT parameters that are different from those suggested by the TWT requesting STA or
 *	TWT scheduled STA. This needs type needs to be used only by the TWT responding STA.
 *
 * @IFX_TWT_OPER_SETUP_CMD_TYPE_REJECT: A TWT responding STA or TWT scheduling AP rejects
 *	setup, or a TWT scheduling AP terminates an existing broadcast TWT, or a TWT
 *	scheduled STA terminates its membership in a broadcast TWT.
 *
 * @IFX_TWT_OPER_SETUP_CMD_TYPE_MAX: This acts as a the tail of the list.
 *	Make sure it located at the end of the list.
 */
enum ifx_twt_oper_setup_cmd_type {
	IFX_TWT_OPER_SETUP_CMD_TYPE_INVALID	= -1,
	IFX_TWT_OPER_SETUP_CMD_TYPE_REQUEST	= 0,
	IFX_TWT_OPER_SETUP_CMD_TYPE_SUGGEST	= 1,
	IFX_TWT_OPER_SETUP_CMD_TYPE_DEMAND	= 2,
	IFX_TWT_OPER_SETUP_CMD_TYPE_GROUPING	= 3,
	IFX_TWT_OPER_SETUP_CMD_TYPE_ACCEPT	= 4,
	IFX_TWT_OPER_SETUP_CMD_TYPE_ALTERNATE	= 5,
	IFX_TWT_OPER_SETUP_CMD_TYPE_DICTATE	= 6,
	IFX_TWT_OPER_SETUP_CMD_TYPE_REJECT	= 7,
	IFX_TWT_OPER_SETUP_CMD_TYPE_MAX		= 8
};

/**
 * HE top level command IDs
 */
enum {
	IFX_HE_CMD_ENAB = 0,
	IFX_HE_CMD_FEATURES = 1,
	IFX_HE_CMD_TWT_SETUP = 2,
	IFX_HE_CMD_TWT_TEARDOWN = 3,
	IFX_HE_CMD_TWT_INFO = 4,
	IFX_HE_CMD_BSSCOLOR = 5,
	IFX_HE_CMD_PARTIAL_BSSCOLOR = 6,
	IFX_HE_CMD_CAP = 7,
	IFX_HE_CMD_STAID = 8,
	IFX_HE_CMD_RTSDURTHRESH = 10,
	IFX_HE_CMD_PEDURATION = 11,
	IFX_HE_CMD_TESTBED_MODE = 12,
	IFX_HE_CMD_OMI = 13,
	IFX_HE_CMD_MAC_PAD_DUR = 14,
	IFX_HE_CMD_MUEDCA = 15,
	IFX_HE_CMD_MACCAP = 16,
	IFX_HE_CMD_PHYCAP = 17,
	IFX_HE_CMD_DISPLAY = 18,
	IFX_HE_CMD_ACTION = 19,
	IFX_HE_CMD_OFDMATX = 20,
	IFX_HE_CMD_20IN80_MODE = 21,
	IFX_HE_CMD_SMPS = 22,
	IFX_HE_CMD_PPETHRESH = 23,
	IFX_HE_CMD_HTC_OMI_EN = 24,
	IFX_HE_CMD_ERSU_EN = 25,
	IFX_HE_CMD_PREPUNCRX_EN = 26,
	IFX_HE_CMD_MIMOCAP_EN = 27,
	IFX_HE_CMD_MUEDCA_OPT = 28,
	IFX_HE_CMD_LAST
};

#define IFX_OCE_IOV_MAJOR_VER 1
#define IFX_OCE_IOV_MINOR_VER 1
#define IFX_OCE_IOV_MAJOR_VER_SHIFT 8
#define IFX_OCE_IOV_VERSION \
	((IFX_OCE_IOV_MAJOR_VER << IFX_OCE_IOV_MAJOR_VER_SHIFT) | \
	IFX_OCE_IOV_MINOR_VER)

enum {
	IFX_OCE_CMD_ENABLE = 1,
	IFX_OCE_CMD_PROBE_DEF_TIME = 2,
	IFX_OCE_CMD_FD_TX_PERIOD = 3,
	IFX_OCE_CMD_FD_TX_DURATION = 4,
	IFX_OCE_CMD_RSSI_TH = 5,
	IFX_OCE_CMD_RWAN_LINKS = 6,
	IFX_OCE_CMD_CU_TRIGGER = 7,
	IFX_OCE_CMD_LAST
};

enum {
	IFX_OCE_XTLV_ENABLE  = 0x1,
	IFX_OCE_XTLV_PROBE_DEF_TIME  = 0x2,
	IFX_OCE_XTLV_FD_TX_PERIOD    = 0x3,
	IFX_OCE_XTLV_FD_TX_DURATION  = 0x4,
	IFX_OCE_XTLV_RSSI_TH = 0x5,
	IFX_OCE_XTLV_RWAN_LINKS = 0x6,
	IFX_OCE_XTLV_CU_TRIGGER = 0x7
};

static const struct nla_policy
ifx_vendor_attr_twt_param_policy[IFX_VENDOR_ATTR_TWT_PARAM_MAX + 1] = {
	[IFX_VENDOR_ATTR_TWT_PARAM_UNSPEC] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_TWT_PARAM_NEGO_TYPE] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_TWT_PARAM_SETUP_CMD_TYPE] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_TWT_PARAM_DIALOG_TOKEN] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_TWT_PARAM_WAKE_TIME] = {.type = NLA_U64},
	[IFX_VENDOR_ATTR_TWT_PARAM_WAKE_TIME_OFFSET] = {.type = NLA_U64},
	[IFX_VENDOR_ATTR_TWT_PARAM_MIN_WAKE_DURATION] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_TWT_PARAM_WAKE_INTVL_EXPONENT] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_TWT_PARAM_WAKE_INTVL_MANTISSA] = {.type = NLA_U16},
	[IFX_VENDOR_ATTR_TWT_PARAM_REQUESTOR] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_TWT_PARAM_TRIGGER] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_TWT_PARAM_IMPLICIT] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_TWT_PARAM_FLOW_TYPE] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_TWT_PARAM_FLOW_ID] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_TWT_PARAM_BCAST_TWT_ID] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_TWT_PARAM_PROTECTION] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_TWT_PARAM_CHANNEL] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_TWT_PARAM_TWT_INFO_FRAME_DISABLED] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_TWT_PARAM_MIN_WAKE_DURATION_UNIT] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_TWT_PARAM_TEARDOWN_ALL_TWT] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_TWT_PARAM_MAX] = {.type = NLA_U8},
};

static const struct nla_policy ifx_vendor_attr_twt_policy[IFX_VENDOR_ATTR_TWT_MAX + 1] = {
	[IFX_VENDOR_ATTR_TWT_UNSPEC] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_TWT_OPER] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_TWT_PARAMS] =
		NLA_POLICY_NESTED(ifx_vendor_attr_twt_param_policy),
	[IFX_VENDOR_ATTR_TWT_MAX] = {.type = NLA_U8},
};

/* randmac define/enum/struct
 */
#define WL_RANDMAC_API_VERSION		0x0100 /**< version 1.0 */
#define WL_RANDMAC_API_MIN_VERSION	0x0100 /**< version 1.0 */

/** subcommands that can apply to randmac */
enum {
	WL_RANDMAC_SUBCMD_NONE				= 0,
	WL_RANDMAC_SUBCMD_GET_VERSION			= 1,
	WL_RANDMAC_SUBCMD_ENABLE			= 2,
	WL_RANDMAC_SUBCMD_DISABLE			= 3,
	WL_RANDMAC_SUBCMD_CONFIG			= 4,
	WL_RANDMAC_SUBCMD_STATS				= 5,
	WL_RANDMAC_SUBCMD_CLEAR_STATS			= 6,
	WL_RANDMAC_SUBCMD_MAX
};

struct ifx_randmac {
	u16 version;
	u16 len;			/* total length */
	u16 subcmd_id;	/* subcommand id */
	u8 data[0];			/* subcommand data */
};

enum ifx_vendor_attr_wnm_param {
	IFX_VENDOR_ATTR_WNM_PARAM_UNSPEC,
	IFX_VENDOR_ATTR_WNM_PARAM_GET_INFO,
	IFX_VENDOR_ATTR_WNM_PARAM_IDLE_PERIOD,
	IFX_VENDOR_ATTR_WNM_PARAM_PROTECTION_OPT,
	IFX_VENDOR_ATTR_WNM_PARAM_MAX
};

static const struct nla_policy
ifx_vendor_attr_wnm_param_policy[IFX_VENDOR_ATTR_WNM_PARAM_MAX + 1] = {
	[IFX_VENDOR_ATTR_WNM_PARAM_UNSPEC] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_WNM_PARAM_GET_INFO] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_WNM_PARAM_IDLE_PERIOD] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_WNM_PARAM_PROTECTION_OPT] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_WNM_PARAM_MAX] = {.type = NLA_U8},
};

enum ifx_vendor_attr_wnm {
	IFX_VENDOR_ATTR_WNM_UNSPEC,
	IFX_VENDOR_ATTR_WNM_CMD,
	IFX_VENDOR_ATTR_WNM_PARAMS,
	IFX_VENDOR_ATTR_WNM_MAX
};

enum ifx_vendor_hw_caps {
	IFX_VENDOR_HW_CAPS_REPLAYCNTS,
	IFX_VENDOR_HW_CAPS_MAX
};

static const char * const hw_caps_name[] = {
	[IFX_VENDOR_HW_CAPS_REPLAYCNTS] = "replay counters"
};

static const struct nla_policy ifx_vendor_attr_wnm_policy[IFX_VENDOR_ATTR_WNM_MAX + 1] = {
	[IFX_VENDOR_ATTR_WNM_UNSPEC] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_WNM_CMD] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_WNM_PARAMS] =
		NLA_POLICY_NESTED(ifx_vendor_attr_wnm_param_policy),
	[IFX_VENDOR_ATTR_WNM_MAX] = {.type = NLA_U8},
};

enum {
	IFX_WNM_CMD_IOV_WNM = 1,
	IFX_WNM_CMD_IOV_WNM_MAXIDLE = 2,
	IFX_WNM_CMD_IOV_WNM_TIMBC_OFFSET = 3,
	IFX_WNM_CMD_IOV_WNM_BSSTRANS_URL = 4,
	IFX_WNM_CMD_IOV_WNM_BSSTRANS_REQ = 5,
	IFX_WNM_CMD_IOV_WNM_TFS_TCLASTYPE = 6,
	IFX_WNM_CMD_IOV_WNM_PARP_DISCARD = 7,
	IFX_WNM_CMD_IOV_WNM_PARP_ALLNODE = 8,
	IFX_WNM_CMD_IOV_WNM_TIMBC_SET = 9,
	IFX_WNM_CMD_IOV_WNM_TIMBC_STATUS = 10,
	IFX_WNM_CMD_IOV_WNM_DMS_SET = 11,
	IFX_WNM_CMD_IOV_WNM_DMS_TERM = 12,
	IFX_WNM_CMD_IOV_WNM_SERVICE_TERM = 13,
	IFX_WNM_CMD_IOV_WNM_SLEEP_INTV = 14,
	IFX_WNM_CMD_IOV_WNM_SLEEP_MODE = 15,
	IFX_WNM_CMD_IOV_WNM_BSSTRANS_QUERY = 16,
	IFX_WNM_CMD_IOV_WNM_BSSTRANS_RESP = 17,
	IFX_WNM_CMD_IOV_WNM_TCLAS_ADD = 18,
	IFX_WNM_CMD_IOV_WNM_TCLAS_DEL = 19,
	IFX_WNM_CMD_IOV_WNM_TCLAS_LIST = 20,
	IFX_WNM_CMD_IOV_WNM_DMS_STATUS = 21,
	IFX_WNM_CMD_IOV_WNM_KEEPALIVES_MAX_IDLE = 22,
	IFX_WNM_CMD_IOV_WNM_PM_IGNORE_BCMC = 23,
	IFX_WNM_CMD_IOV_WNM_DMS_DEPENDENCY = 24,
	IFX_WNM_CMD_IOV_WNM_BSSTRANS_ROAMTHROTTLE = 25,
	IFX_WNM_CMD_IOV_WNM_TFS_SET  = 26,
	IFX_WNM_CMD_IOV_WNM_TFS_TERM = 27,
	IFX_WNM_CMD_IOV_WNM_TFS_STATUS = 28,
	IFX_WNM_CMD_IOV_WNM_BTQ_NBR_ADD = 29,
	IFX_WNM_CMD_IOV_WNM_BTQ_NBR_DEL = 30,
	IFX_WNM_CMD_IOV_WNM_BTQ_NBR_LIST = 31,
	IFX_WNM_CMD_IOV_WNM_BSSTRANS_RSSI_RATE_MAP = 32,
	IFX_WNM_CMD_IOV_WNM_KEEPALIVE_PKT_TYPE = 33,
	IFX_WNM_CONFIG_CMD_IOV_WNM_TYPE_MAX
};

struct ifx_maxidle_wnm {
	u8  get_info;
	int period;
	int protect;
};

enum ifx_vendor_attr_ssid_prot {
	IFX_VENDOR_ATTR_SSID_PROT_UNSPEC,
	IFX_VENDOR_ATTR_SSID_PROT_ENABLE,
	IFX_VENDOR_ATTR_SSID_PROT_MAX
};

static const struct nla_policy
ifx_vendor_attr_ssid_prot_policy[IFX_VENDOR_ATTR_SSID_PROT_MAX + 1] = {
	[IFX_VENDOR_ATTR_SSID_PROT_UNSPEC] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_SSID_PROT_ENABLE] = {.type = NLA_U8},
	[IFX_VENDOR_ATTR_SSID_PROT_MAX] = {.type = NLA_U8},
};

#define WL_MKEEP_ALIVE_VERSION		1
#define WL_MKEEP_ALIVE_IMMEDIATE	0x80000000

struct ifx_mkeep_alive {
	u16 version;		/* Version for mkeep_alive */
	u16 length;		/* length of fixed parameters in the structure */
	u32 period_msec;	/* high bit on means immediate send */
	u16 len_bytes;
	u8 keep_alive_id;	/* 0 - 3 for N = 4 */
	u8 data[1];
};

struct ifx_tko {
	u16 subcmd_id;		/* subcommand id */
	u16 len;		/* total length of data[] */
	u8 data[1];		/* subcommand data */
};

/* subcommand ids */
#define WL_TKO_SUBCMD_PARAM		1	/* configure offload common parameters  */
#define WL_TKO_SUBCMD_ENABLE		3	/* enable/disable */

/* WL_TKO_SUBCMD_PARAM subcommand data */
struct ifx_tko_param {
	u16 interval;		/* keepalive tx interval (secs) */
	u16 retry_interval;	/* keepalive retry interval (secs) */
	u16 retry_count;	/* retry_count */
	s16 rst_delay;		/* delay to delay a RST frame from reaching the host */
};

struct ifx_tko_enable {
	u8 enable;		/* 1 - enable, 0 - disable */
	u8 pad[3];		/* 4-byte struct alignment */
};

/* String based vendor commands infra */
#define VNDR_CMD_STR_NUM	15
#define VNDR_CMD_STR_MAX_LEN	50
#define VNDR_CMD_VAL_NUM	50
#define VNDR_CMD_HASH_BITS	4

struct ifx_vendor_cmdstr {
	const char *name;
	int (*func)(struct wiphy *wiphy, struct wireless_dev *wdev,
		    char cmd_str[VNDR_CMD_STR_NUM][VNDR_CMD_STR_MAX_LEN],
		    long cmd_val[VNDR_CMD_VAL_NUM]);
};

struct ifx_vndr_cmdstr_hashtbl {
	struct ifx_vendor_cmdstr *vndr_cmd_addr;
	struct hlist_node node;
};

int ifx_cfg80211_vndr_cmds_twt(struct wiphy *wiphy,
			       struct wireless_dev *wdev, const void  *data, int len);
int ifx_cfg80211_vndr_cmds_bsscolor(struct wiphy *wiphy,
				    struct wireless_dev *wdev,
				    const void *data, int len);
int ifx_cfg80211_vndr_cmds_muedca_opt(struct wiphy *wiphy,
				      struct wireless_dev *wdev,
				      const void *data, int len);
int ifx_cfg80211_vndr_cmds_amsdu(struct wiphy *wiphy,
				 struct wireless_dev *wdev,
				 const void *data, int len);
int ifx_cfg80211_vndr_cmds_ldpc_cap(struct wiphy *wiphy,
				    struct wireless_dev *wdev,
				    const void *data, int len);
int ifx_cfg80211_vndr_cmds_oce_enable(struct wiphy *wiphy,
				      struct wireless_dev *wdev,
				      const void *data, int len);
int ifx_cfg80211_vndr_cmds_randmac(struct wiphy *wiphy,
				   struct wireless_dev *wdev,
				   const void *data, int len);
int ifx_cfg80211_vndr_cmds_mbo(struct wiphy *wiphy,
			       struct wireless_dev *wdev,
			       const void *data, int len);
int ifx_cfg80211_vndr_cmds_mpc(struct wiphy *wiphy,
			       struct wireless_dev *wdev,
			       const void *data, int len);
int ifx_cfg80211_vndr_cmds_giantrx(struct wiphy *wiphy,
				   struct wireless_dev *wdev,
				   const void *data, int len);
int ifx_cfg80211_vndr_cmds_wnm_max_idle(struct wiphy *wiphy,
					struct wireless_dev *wdev,
					const void *data, int len);
int ifx_cfg80211_vndr_cmds_hwcaps(struct wiphy *wiphy,
				  struct wireless_dev *wdev,
				  const void *data, int len);
int ifx_cfg80211_vndr_cmds_wnm_wl_cap(struct wiphy *wiphy,
				      struct wireless_dev *wdev,
				      const void *data, int len);
int ifx_vndr_cmdstr_offload_config(struct wiphy *wiphy, struct wireless_dev *wdev,
				   char cmd_str[VNDR_CMD_STR_NUM][VNDR_CMD_STR_MAX_LEN],
				   long cmd_val[VNDR_CMD_VAL_NUM]);
int ifx_vndr_cmdstr_mkeep_alive(struct wiphy *wiphy, struct wireless_dev *wdev,
				char cmd_str[VNDR_CMD_STR_NUM][VNDR_CMD_STR_MAX_LEN],
				long *cmd_val);
int ifx_vndr_cmdstr_tko(struct wiphy *wiphy, struct wireless_dev *wdev,
			char cmd_str[VNDR_CMD_STR_NUM][VNDR_CMD_STR_MAX_LEN],
			long *cmd_val);
int ifx_vndr_cmdstr_icmp_echo_req(struct wiphy *wiphy, struct wireless_dev *wdev,
				  char cmd_str[VNDR_CMD_STR_NUM][VNDR_CMD_STR_MAX_LEN],
				  long *cmd_val);
int ifx_cfg80211_vndr_cmds_str(struct wiphy *wiphy, struct wireless_dev *wdev,
			       const void *data, int len);
int ifx_cfg80211_vndr_cmds_config_pfn(struct wiphy *wiphy,
				      struct wireless_dev *wdev, const void  *data, int len);
int ifx_cfg80211_vndr_cmds_get_pfn_status(struct wiphy *wiphy,
					  struct wireless_dev *wdev, const void  *data, int len);
int ifx_cfg80211_vndr_cmds_ssid_prot(struct wiphy *wiphy,
				     struct wireless_dev *wdev, const void *data, int len);
int ifx_cfg80211_vndr_cmds_mchan_config(struct wiphy *wiphy,
					struct wireless_dev *wdev, const void *data, int len);

#endif /* IFX_VENDOR_H */
