// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2010 Broadcom Corporation
 */

#include <linux/kernel.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/module.h>
#include <linux/inetdevice.h>
#include <linux/property.h>
#include <net/cfg80211.h>
#include <net/rtnetlink.h>
#include <net/addrconf.h>
#include <net/ieee80211_radiotap.h>
#include <net/ipv6.h>
#include <brcmu_utils.h>
#include <brcmu_wifi.h>
#include <defs.h>

#include "core.h"
#include "bus.h"
#include "debug.h"
#include "fwil_types.h"
#include "p2p.h"
#include "pno.h"
#include "cfg80211.h"
#include "fwil.h"
#include "feature.h"
#include "proto.h"
#include "pcie.h"
#include "common.h"
#include "twt.h"
#include "bt_shared_sdio_ifx.h"
#include "sdio.h"

#define MAX_WAIT_FOR_8021X_TX			msecs_to_jiffies(950)

#define BRCMF_BSSIDX_INVALID			-1

#define	RXS_PBPRES				BIT(2)

#define	D11_PHY_HDR_LEN				6

#define WL_CNT_XTLV_SLICE_IDX		256

#define IOVAR_XTLV_BEGIN		4

#define XTLV_TYPE_SIZE		2

#define XTLV_TYPE_LEN_SIZE		4

#define WL_CNT_IOV_BUF		2048

#define CNT_VER_6	6
#define CNT_VER_10	10
#define CNT_VER_30	30

/* Macro to calculate packing factor with scalar 4 in a xTLV */
#define PACKING_FACTOR(args) ((args) % 4 == 0 ? 0 : (4 - ((args) % 4)))

struct d11rxhdr_le {
	__le16 RxFrameSize;
	u16 PAD;
	__le16 PhyRxStatus_0;
	__le16 PhyRxStatus_1;
	__le16 PhyRxStatus_2;
	__le16 PhyRxStatus_3;
	__le16 PhyRxStatus_4;
	__le16 PhyRxStatus_5;
	__le16 RxStatus1;
	__le16 RxStatus2;
	__le16 RxTSFTime;
	__le16 RxChan;
	u8 unknown[12];
} __packed;

struct wlc_d11rxhdr {
	struct d11rxhdr_le rxhdr;
	__le32 tsf_l;
	s8 rssi;
	s8 rxpwr0;
	s8 rxpwr1;
	s8 do_rssi_ma;
	s8 rxpwr[4];
} __packed;

static const char fmac_ethtool_string_stats_v6[][ETH_GSTRING_LEN] = {
	"txbyte", "txerror", "txprshort", "txnobuf", "txrunt", "txcmiss", "txphyerr",
	"rxframe", "rxerror", "rxnobuf", "rxbadds", "rxfragerr",
	"rxgiant", "rxbadproto", "rxbadda", "rxoflo", "d11cnt_rxcrc_off", "dmade",
	"dmape", "tbtt", "pkt_callback_reg_fail", "txackfrm", "txbcnfrm", "rxtoolate",
	"txtplunfl", "rxinvmachdr", "rxbadplcp", "rxstrt", "rxmfrmucastmbss",
	"rxrtsucast", "rxackucast", "rxmfrmocast", "rxrtsocast", "rxdfrmmcast",
	"rxcfrmmcast", "rxdfrmucastobss", "rxrsptmout", "rxf0ovfl", "rxf2ovfl", "pmqovfl",
	"frmscons", "rxback", "txfrag", "txfail", "txretrie", "txrts", "txnoack", "rxmulti",

	"txfrmsnt", "tkipmicfaill", "tkipreplay", "ccmpreplay", "fourwayfail", "wepicverr",
	"tkipicverr", "tkipmicfaill_mcst", "tkipreplay_mcst", "ccmpreplay_mcst",
	"fourwayfail_mcst", "wepicverr_mcst", "tkipicverr_mcst", "txexptime", "phywatchdog",
	"prq_undirected_entries", "atim_suppress_count", "bcn_template_not_ready_done",

	"rx1mbps", "rx5mbps5", "rx9mbps", "rx12mbps", "rx24mbps", "rx48mbps",
	"rx108mbps", "rx216mbps", "rx324mbps", "rx432mbps", "rx540mbps",
	"pktengrxdmcast", "bphy_txmpdu_sgi", "txmpdu_stbc", "rxdrop20s",
};

static const char fmac_ethtool_string_stats_v10[][ETH_GSTRING_LEN] = {
	"txframe", "txbyte", "txretrans", "txerror", "txctl", "txprshort",
	"txserr", "txnobuf", "txnoassoc", "txrunt",
	"txchit", "txcmiss", "txphyerr", "txphycrs", "rxframe", "rxbyte",
	"rxerror", "rxctl", "rxnobuf", "rxnondata",
	"rxbadds", "rxbadcm", "rxfragerr", "rxrunt", "rxgiant", "rxnoscb",
	"rxbadprot", "rxbadsrcma", "rxbadda", "rxfilter",
	"rxoflo", "rxuflo[0]", "rxuflo[1]", "rxuflo[2]", "rxuflo[3]",
	"rxuflo[4]", "rxuflo[5]", "d11cnt_rxcrc_off", "d11cnt_txnocts_off",
	"dmade", "dmada", "dmape", "reset", "tbtt", "txdmawar",
	"pkt_callback_reg_fail", "txallfrm", "txrtsfrm", "txctsfrm",
	"txackfrm", "txdnlfrm", "txbcnfrm", "txfunfl[0]", "txfunfl[1]",
	"txfunfl[2]", "txfunfl[3]", "txfunfl[4]", "txfunfl[5]", "rxtoolate",
	"txfbw", "txtplunfl", "txphyerror", "rxfrmtoolong", "rxfrmtooshrt",
	"rxinvmachdr", "rxbadfcs", "rxbadplcp", "rxcrsglitch",
	"rxstrt", "rxdfrmucastmbss", "rxmfrmucastmbss", "rxcfrmucast",
	"rxrtsucast", "rxctsucast", "rxackucast",
	"rxdfrmocast", "rxmfrmocast", "rxcfrmocast", "rxrtsocast",
	"rxctsocast", "rxdfrmmcast", "rxmfrmmcast", "rxcfrmmcast",
	"rxbeaconmbss", "rxdfrmucastobss", "rxbeaconobss", "rxrsptmout",
	"bcntxcancl", "rxf0ovfl", "rxf1ovfl", "rxf2ovfl", "txsfovfl",
	"pmqovfl", "rxcgprqfrm", "rxcgprsqovfl", "txcgprsfail", "txcgprssuc", "prs_timeout",
	"rxnack", "frmscons", "txnack", "rxback", "txback", "txfrag", "txmulti", "txfail",
	"txretry", "txretrie", "rxdup", "txrts", "txnocts", "txnoack", "rxfrag", "rxmulti",
	"rxcrc", "txfrmsnt", "rxundec", "tkipmicfaill", "tkipcntrmsr", "tkipreplay", "ccmpfmterr",
	"ccmpreplay", "ccmpundec", "fourwayfail", "wepundec", "wepicverr", "decsuccess",
	"tkipicverr", "wepexcluded", "psmwds", "phywatchdog", "prq_entries_handled",
	"prq_undirected_entries", "prq_bad_entries", "atim_suppress_count",
	"bcn_template_not_ready", "bcn_template_not_ready_done", "late_tbtt_dpc",
	"rx1mbps", "rx2mbps", "rx5mbps5", "rx6mbps", "rx9mbps", "rx11mbps", "rx12mbps", "rx18mbps",
	"rx24mbps", "rx36mbps", "rx48mbps", "rx54mbps", "rx108mbps", "rx162mbps",
	"rx216mbps", "rx270mbps", "rx324mbps", "rx378mbps", "rx432mbps", "rx486mbps", "rx540mbps",
	"pktengrxducast", "pktengrxdmcast", "bphy_rxcrsglitch", "bphy_b", "txexptime",
	"rxmpdu_sgi", "txmpdu_stbc", "rxmpdu_stbc", "tkipmicfaill_mcst", "tkipcntrmsr_mcst",
	"tkipreplay_mcst", "ccmpfmterr_mcst", "ccmpreplay_mcst", "ccmpundec_mcst",
	"fourwayfail_mcst", "wepundec_mcst", "wepicverr_mcst", "decsuccess_mcst",
	"tkipicverr_mcst", "wepexcluded_mcst", "reinit", "pstatxnoassoc",
	"pstarxucast", "pstarxbcmc", "pstatxbcmc", "cso_normal", "chained",
	"chainedsz1", "unchained", "maxchainsz", "currchainsz", "rxdrop20s",
	"pciereset", "cfgrestore", "reinitreason[0]", "reinitreason[1]",
	"reinitreason[2]", "reinitreason[3]", "reinitreason[4]",
	"reinitreason[5]", "reinitreason[6]", "reinitreason[7]", "rxrtry",
};

static const char fmac_ethtool_string_stats_v30[][ETH_GSTRING_LEN] = {
	"txframe", "txbyte", "txretrans", "txerror", "txctl", "txprshort", "txserr", "txnobuf",
	"txnoassoc", "txrunt", "txchit", "txcmiss", "txuflo", "txphyerr", "txphycrs",
	"rxframe", "rxbyte", "rxerror", "rxctl", "rxnobuf", "rxnondata", "rxbadds", "rxbadcm",
	"rxfragerr", "rxrunt", "rxgiant", "rxnoscb", "rxbadproto", "rxbadsrcmac",
	"rxbadda", "rxfilter", "rxoflo", "rxuflo[0]", "rxuflo[1]",
	"rxuflo[2]", "rxuflo[3]", "rxuflo[4]", "rxuflo[5]",

	"d11cnt_txrts_off", "d11cnt_rxcrc_off", "d11cnt_txnocts_off", "dmade", "dmada",
	"dmape", "reset", "tbtt", "txdmawar", "pkt_callback_reg_fail",
	"txfrag", "txmulti", "txfail", "txretry",
	"txretrie", "rxdup", "txrts", "txnocts", "txnoack", "rxfrag",
	"rxmulti", "rxcrc", "txfrmsnt", "rxundec",
	"tkipmicfaill", "tkipcntrmsr", "tkipreplay", "ccmpfmterr",
	"ccmpreplay", "ccmpundec", "fourwayfail", "wepundec",
	"wepicverr", "decsuccess", "tkipicverr", "wepexcluded",
	"txchanrej", "psmwds", "phywatchdog",
	"prq_entries_handled", "prq_undirected_entries", "prq_bad_entries",
	"atim_suppress_count", "bcn_template_not_ready", "bcn_template_not_ready_done",
	"late_tbtt_dpc",

	"rx1mbps", "rx2mbps", "rx5mbps5", "rx6mbps", "rx9mbps",
	"rx11mbps", "rx12mbps", "rx18mbps", "rx24mbps", "rx36mbps",
	"rx48mbps", "rx54mbps", "rx108mbps", "rx162mbps", "rx216mbps",
	"rx270mbps", "rx324mbps", "rx378mbps", "rx432mbps", "rx486mbps",
	"rx540mbps", "rfdisable", "txexptime", "txmpdu_sgi", "rxmpdu_sgi",
	"txmpdu_stbc", "rxmpdu_stbc", "rxundec_mcst",

	"tkipmicfaill_mcst", "tkipcntrmsr_mcst", "tkipreplay_mcst",
	"ccmpfmterr_mcst", "ccmpreplay_mcst", "ccmpundec_mcst",
	"fourwayfail_mcst", "wepundec_mcst", "wepicverr_mcst",
	"decsuccess_mcst", "tkipicverr_mcst", "wepexcluded_mcst",
	"dma_hang", "reinit", "pstatxucast",
	"pstatxnoassoc", "pstarxucast", "pstarxbcmc",
	"pstatxbcmc", "cso_passthrough", "cso_normal",
	"chained", "chainedsz1", "unchained",
	"maxchainsz", "currchainsz", "pciereset",
	"cfgrestore", "reinitreason[0]", "reinitreason[1]",
	"reinitreason[2]", "reinitreason[3]", "reinitreason[4]",
	"reinitreason[5]", "reinitreason[6]", "reinitreason[7]",
	"rxrtry", "rxmpdu_mu",

	"txbar", "rxbar", "txpspoll", "rxpspoll", "txnull",
	"rxnull", "txqosnull", "rxqosnull", "txassocreq", "rxassocreq",
	"txreassocreq", "rxreassocreq", "txdisassoc", "rxdisassoc",
	"txassocrsp", "rxassocrsp", "txreassocrsp", "rxreassocrsp",
	"txauth", "rxauth", "txdeauth", "rxdeauth", "txprobereq",
	"rxprobereq", "txprobersp", "rxprobersp", "txaction",
	"rxaction", "ampdu_wds", "txlost", "txdatamcast",
	"txdatabcast", "psmxwds", "rxback", "txback",
	"p2p_tbtt", "p2p_tbtt_miss", "txqueue_start", "txqueue_end",
	"txbcast", "txdropped", "rxbcast", "rxdropped",
	"txq_end_assoccb",
};

#define BRCMF_IF_STA_LIST_LOCK_INIT(ifp) spin_lock_init(&(ifp)->sta_list_lock)
#define BRCMF_IF_STA_LIST_LOCK(ifp, flags) \
	spin_lock_irqsave(&(ifp)->sta_list_lock, (flags))
#define BRCMF_IF_STA_LIST_UNLOCK(ifp, flags) \
	spin_unlock_irqrestore(&(ifp)->sta_list_lock, (flags))

#define BRCMF_STA_NULL ((struct brcmf_sta *)NULL)

/* dscp exception format {dscp hex, up}  */
struct cfg80211_dscp_exception dscp_excpt[] = {
{DSCP_EF, 6}, {DSCP_CS4, 5}, {DSCP_AF41, 5}, {DSCP_CS3, 4} };

/* dscp range : up[0 ~ 7] */
struct cfg80211_dscp_range dscp_range[8] = {
{0, 7}, {8, 15}, {16, 23}, {24, 31},
{32, 39}, {40, 47}, {48, 55}, {56, 63} };

char *brcmf_ifname(struct brcmf_if *ifp)
{
	if (!ifp)
		return "<if_null>";

	if (ifp->ndev)
		return ifp->ndev->name;

	return "<if_none>";
}

struct brcmf_if *brcmf_get_ifp(struct brcmf_pub *drvr, int ifidx)
{
	struct brcmf_if *ifp;
	s32 bsscfgidx;

	if (ifidx < 0 || ifidx >= BRCMF_MAX_IFS) {
		bphy_err(drvr, "ifidx %d out of range\n", ifidx);
		return NULL;
	}

	ifp = NULL;
	bsscfgidx = drvr->if2bss[ifidx];
	if (bsscfgidx >= 0)
		ifp = drvr->iflist[bsscfgidx];

	return ifp;
}

void brcmf_configure_arp_nd_offload(struct brcmf_if *ifp, bool enable)
{
	s32 err;
	u32 mode;

	if (enable && brcmf_is_apmode_operating(ifp->drvr->wiphy)) {
		brcmf_dbg(TRACE, "Skip ARP/ND offload enable when soft AP is running\n");
		return;
	}

	if (enable)
		mode = BRCMF_ARP_OL_AGENT | BRCMF_ARP_OL_PEER_AUTO_REPLY;
	else
		mode = 0;

	if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_OFFLOADS)) {
		u32 feat_set = brcmf_offload_feat & (BRCMF_OL_ARP | BRCMF_OL_ND);

		if (!feat_set)
			return;

		if (enable)
			brcmf_generic_offload_config(ifp, feat_set,
						     brcmf_offload_prof, false);
		else
			brcmf_generic_offload_config(ifp, feat_set,
						     brcmf_offload_prof, true);
	} else {
		/* Try to set and enable ARP offload feature, this may fail, then it  */
		/* is simply not supported and err 0 will be returned                 */
		err = brcmf_fil_iovar_int_set(ifp, "arp_ol", mode);
		if (err) {
			brcmf_dbg(TRACE, "failed to set ARP offload mode to 0x%x, err = %d\n",
				  mode, err);
		} else {
			err = brcmf_fil_iovar_int_set(ifp, "arpoe", enable);
			if (err) {
				brcmf_dbg(TRACE, "failed to configure (%d) ARP offload err = %d\n",
					  enable, err);
			} else {
				brcmf_dbg(TRACE, "successfully configured (%d) ARP offload to 0x%x\n",
					  enable, mode);
			}
		}

		err = brcmf_fil_iovar_int_set(ifp, "ndoe", enable);
		if (err) {
			brcmf_dbg(TRACE, "failed to configure (%d) ND offload err = %d\n",
				  enable, err);
		} else {
			brcmf_dbg(TRACE, "successfully configured (%d) ND offload to 0x%x\n",
				  enable, mode);
		}
	}
}

static void _brcmf_set_multicast_list(struct work_struct *work)
{
	struct brcmf_if *ifp = container_of(work, struct brcmf_if,
					    multicast_work);
	struct brcmf_pub *drvr = ifp->drvr;
	struct net_device *ndev;
	struct netdev_hw_addr *ha;
	u32 cmd_value, cnt;
	__le32 cnt_le;
	char *buf, *bufp;
	u32 buflen;
	s32 err;

	brcmf_dbg(TRACE, "Enter, bsscfgidx=%d\n", ifp->bsscfgidx);

	ndev = ifp->ndev;

	/* Determine initial value of allmulti flag */
	cmd_value = (ndev->flags & IFF_ALLMULTI) ? true : false;

	/* Send down the multicast list first. */
	cnt = netdev_mc_count(ndev);
	buflen = sizeof(cnt) + (cnt * ETH_ALEN);
	buf = kmalloc(buflen, GFP_KERNEL);
	if (!buf)
		return;
	bufp = buf;

	cnt_le = cpu_to_le32(cnt);
	memcpy(bufp, &cnt_le, sizeof(cnt_le));
	bufp += sizeof(cnt_le);

	netdev_for_each_mc_addr(ha, ndev) {
		if (!cnt)
			break;
		memcpy(bufp, ha->addr, ETH_ALEN);
		bufp += ETH_ALEN;
		cnt--;
	}

	err = brcmf_fil_iovar_data_set(ifp, "mcast_list", buf, buflen);
	if (err < 0) {
		bphy_err(drvr, "Setting mcast_list failed, %d\n", err);
		cmd_value = cnt ? true : cmd_value;
	}

	kfree(buf);

	/*
	 * Now send the allmulti setting.  This is based on the setting in the
	 * net_device flags, but might be modified above to be turned on if we
	 * were trying to set some addresses and dongle rejected it...
	 */
	err = brcmf_fil_iovar_int_set(ifp, "allmulti", cmd_value);
	if (err < 0)
		bphy_err(drvr, "Setting allmulti failed, %d\n", err);

	/*Finally, pick up the PROMISC flag */
	cmd_value = (ndev->flags & IFF_PROMISC) ? true : false;
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_PROMISC, cmd_value);
	if (err < 0) {
		/* PROMISC unsupported by firmware of older chips */
		if (err == -EBADE)
			bphy_info_once(drvr, "BRCMF_C_SET_PROMISC unsupported\n");
		else
			bphy_err(drvr, "Setting BRCMF_C_SET_PROMISC failed, err=%d\n",
				 err);
	}
	brcmf_configure_arp_nd_offload(ifp, !cmd_value);
}

#if IS_ENABLED(CONFIG_IPV6)
static void brcmf_update_ipv6_addr(struct work_struct *work)
{
	struct brcmf_if *ifp = container_of(work, struct brcmf_if,
					    ndoffload_work);
	struct brcmf_pub *drvr = ifp->drvr;
	int i, ret;
	struct ipv6_addr addr = {0};

	/* clear the table in firmware */

	if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_OFFLOADS))
		ret = brcmf_generic_offload_host_ipv6_update(ifp, BRCMF_OL_ICMP | BRCMF_OL_ND,
							     &addr, 1, false);
	else
		ret = brcmf_fil_iovar_data_set(ifp, "nd_hostip_clear", NULL, 0);

	if (ret) {
		brcmf_dbg(TRACE, "fail to clear nd ip table err:%d\n", ret);
		return;
	}

	for (i = 0; i < ifp->ipv6addr_idx; i++) {
		if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_OFFLOADS))
			ret = brcmf_generic_offload_host_ipv6_update(ifp,
								     BRCMF_OL_ICMP | BRCMF_OL_ND,
								     &ifp->ipv6_addr_tbl[i],
								     0, true);
		else
			ret = brcmf_fil_iovar_data_set(ifp, "nd_hostip",
						       &ifp->ipv6_addr_tbl[i],
						       sizeof(struct in6_addr));
		if (ret)
			bphy_err(drvr, "add nd ip err %d\n", ret);
	}
}
#else
static void brcmf_update_ipv6_addr(struct work_struct *work)
{
}
#endif

static int brcmf_netdev_set_mac_address(struct net_device *ndev, void *addr)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct sockaddr *sa = (struct sockaddr *)addr;
	int err;

	brcmf_dbg(TRACE, "Enter, bsscfgidx=%d\n", ifp->bsscfgidx);

	err = brcmf_c_set_cur_etheraddr(ifp, sa->sa_data);
	if (err >= 0) {
		brcmf_dbg(TRACE, "updated to %pM\n", sa->sa_data);
		memcpy(ifp->mac_addr, sa->sa_data, ETH_ALEN);
		eth_hw_addr_set(ifp->ndev, ifp->mac_addr);
	}
	return err;
}

static void brcmf_netdev_set_multicast_list(struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);

	schedule_work(&ifp->multicast_work);
}

/**
 * brcmf_skb_is_iapp - checks if skb is an IAPP packet
 *
 * @skb: skb to check
 */
static bool brcmf_skb_is_iapp(struct sk_buff *skb)
{
	static const u8 iapp_l2_update_packet[6] __aligned(2) = {
		0x00, 0x01, 0xaf, 0x81, 0x01, 0x00,
	};
	unsigned char *eth_data;
#if !defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	const u16 *a, *b;
#endif

	if (skb->len - skb->mac_len != 6 ||
	    !is_multicast_ether_addr(eth_hdr(skb)->h_dest))
		return false;

	eth_data = skb_mac_header(skb) + ETH_HLEN;
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	return !(((*(const u32 *)eth_data) ^ (*(const u32 *)iapp_l2_update_packet)) |
		 ((*(const u16 *)(eth_data + 4)) ^ (*(const u16 *)(iapp_l2_update_packet + 4))));
#else
	a = (const u16 *)eth_data;
	b = (const u16 *)iapp_l2_update_packet;

	return !((a[0] ^ b[0]) | (a[1] ^ b[1]) | (a[2] ^ b[2]));
#endif
}

static netdev_tx_t brcmf_netdev_start_xmit(struct sk_buff *skb,
					   struct net_device *ndev)
{
	int ret;
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_pub *drvr = ifp->drvr;
	struct ethhdr *eh;
	int head_delta;
	unsigned int tx_bytes = skb->len;

	brcmf_dbg(DATA, "Enter, bsscfgidx=%d\n", ifp->bsscfgidx);

	/* Can the device send data? */
	if (drvr->bus_if->state != BRCMF_BUS_UP) {
		bphy_err(drvr, "xmit rejected state=%d\n", drvr->bus_if->state);
		netif_stop_queue(ndev);
		dev_kfree_skb(skb);
		ret = -ENODEV;
		goto done;
	}

	/* Some recent Broadcom's firmwares disassociate STA when they receive
	 * an 802.11f ADD frame. This behavior can lead to a local DoS security
	 * issue. Attacker may trigger disassociation of any STA by sending a
	 * proper Ethernet frame to the wireless interface.
	 *
	 * Moreover this feature may break AP interfaces in some specific
	 * setups. This applies e.g. to the bridge with hairpin mode enabled and
	 * IFLA_BRPORT_MCAST_TO_UCAST set. IAPP packet generated by a firmware
	 * will get passed back to the wireless interface and cause immediate
	 * disassociation of a just-connected STA.
	 */
	if (!drvr->settings->iapp && brcmf_skb_is_iapp(skb)) {
		dev_kfree_skb(skb);
		ret = -EINVAL;
		goto done;
	}

	/* Make sure there's enough writeable headroom */
	if (skb_headroom(skb) < drvr->hdrlen || skb_header_cloned(skb)) {
		head_delta = max_t(int, drvr->hdrlen - skb_headroom(skb), 0);

		brcmf_dbg(INFO, "%s: insufficient headroom (%d)\n",
			  brcmf_ifname(ifp), head_delta);
		atomic_inc(&drvr->bus_if->stats.pktcowed);
		ret = pskb_expand_head(skb, ALIGN(head_delta, NET_SKB_PAD), 0,
				       GFP_ATOMIC);
		if (ret < 0) {
			bphy_err(drvr, "%s: failed to expand headroom\n",
				 brcmf_ifname(ifp));
			atomic_inc(&drvr->bus_if->stats.pktcow_failed);
			dev_kfree_skb(skb);
			goto done;
		}
	}

	/* validate length for ether packet */
	if (skb->len < sizeof(*eh)) {
		ret = -EINVAL;
		dev_kfree_skb(skb);
		goto done;
	}

	eh = (struct ethhdr *)(skb->data);

	if (eh->h_proto == htons(ETH_P_PAE))
		atomic_inc(&ifp->pend_8021x_cnt);

	/* Look into dscp to WMM UP mapping with cfg80211_qos_map */
	if (drvr->settings->pkt_prio) {
		skb->priority = cfg80211_classify8021d(skb, drvr->qos_map);
	/* determine the priority */
	} else if ((skb->priority == 0) || (skb->priority > 7)) {
		skb->priority = cfg80211_classify8021d(skb, NULL);
	}

	/* set pacing shift for packet aggregation */
	sk_pacing_shift_update(skb->sk, 8);

	ret = brcmf_proto_tx_queue_data(drvr, ifp->ifidx, skb);
	if (ret < 0)
		brcmf_txfinalize(ifp, skb, false);

done:
	if (ret) {
		ndev->stats.tx_dropped++;
	} else {
		ndev->stats.tx_packets++;
		ndev->stats.tx_bytes += tx_bytes;
	}

	/* Return ok: we always eat the packet */
	return NETDEV_TX_OK;
}

void brcmf_txflowblock_if(struct brcmf_if *ifp,
			  enum brcmf_netif_stop_reason reason, bool state)
{
	unsigned long flags;

	if (!ifp || !ifp->ndev)
		return;

	brcmf_dbg(TRACE, "enter: bsscfgidx=%d stop=0x%X reason=%d state=%d\n",
		  ifp->bsscfgidx, ifp->netif_stop, reason, state);

	spin_lock_irqsave(&ifp->netif_stop_lock, flags);
	if (state) {
		if (!ifp->netif_stop)
			netif_stop_queue(ifp->ndev);
		ifp->netif_stop |= reason;
	} else {
		ifp->netif_stop &= ~reason;
		if (!ifp->netif_stop)
			netif_wake_queue(ifp->ndev);
	}
	spin_unlock_irqrestore(&ifp->netif_stop_lock, flags);
}

void brcmf_netif_rx(struct brcmf_if *ifp, struct sk_buff *skb, bool inirq)
{
	/* Most of Broadcom's firmwares send 802.11f ADD frame every time a new
	 * STA connects to the AP interface. This is an obsoleted standard most
	 * users don't use, so don't pass these frames up unless requested.
	 */
	if (!ifp->drvr->settings->iapp && brcmf_skb_is_iapp(skb)) {
		brcmu_pkt_buf_free_skb(skb);
		return;
	}

	if (skb->pkt_type == PACKET_MULTICAST)
		ifp->ndev->stats.multicast++;

	if (!(ifp->ndev->flags & IFF_UP)) {
		brcmu_pkt_buf_free_skb(skb);
		return;
	}

	ifp->ndev->stats.rx_bytes += skb->len;
	ifp->ndev->stats.rx_packets++;

	brcmf_dbg(DATA, "rx proto=0x%X\n", ntohs(skb->protocol));
#if (KERNEL_VERSION(5, 18, 0) <= LINUX_VERSION_CODE)
	netif_rx(skb);
#else
	if (inirq) {
		netif_rx(skb);
	} else {
		/* If the receive is not processed inside an ISR,
		 * the softirqd must be woken explicitly to service
		 * the NET_RX_SOFTIRQ.  This is handled by netif_rx_ni().
		 */
		netif_rx_ni(skb);
	}
#endif /* kernel 5.18.0 */
}

void brcmf_netif_mon_rx(struct brcmf_if *ifp, struct sk_buff *skb)
{
	if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_MONITOR_FMT_RADIOTAP)) {
		/* Do nothing */
	} else if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_MONITOR_FMT_HW_RX_HDR)) {
		struct wlc_d11rxhdr *wlc_rxhdr = (struct wlc_d11rxhdr *)skb->data;
		struct ieee80211_radiotap_header *radiotap;
		unsigned int offset;
		u16 RxStatus1;

		RxStatus1 = le16_to_cpu(wlc_rxhdr->rxhdr.RxStatus1);

		offset = sizeof(struct wlc_d11rxhdr);
		/* MAC inserts 2 pad bytes for a4 headers or QoS or A-MSDU
		 * subframes
		 */
		if (RxStatus1 & RXS_PBPRES)
			offset += 2;
		offset += D11_PHY_HDR_LEN;

		skb_pull(skb, offset);

		/* TODO: use RX header to fill some radiotap data */
		radiotap = skb_push(skb, sizeof(*radiotap));
		memset(radiotap, 0, sizeof(*radiotap));
		radiotap->it_len = cpu_to_le16(sizeof(*radiotap));

		/* TODO: 4 bytes with receive status? */
		skb->len -= 4;
	} else {
		struct ieee80211_radiotap_header *radiotap;

		/* TODO: use RX status to fill some radiotap data */
		radiotap = skb_push(skb, sizeof(*radiotap));
		memset(radiotap, 0, sizeof(*radiotap));
		radiotap->it_len = cpu_to_le16(sizeof(*radiotap));

		/* TODO: 4 bytes with receive status? */
		skb->len -= 4;
	}

	skb->dev = ifp->ndev;
	skb_reset_mac_header(skb);
	skb->pkt_type = PACKET_OTHERHOST;
	skb->protocol = htons(ETH_P_802_2);
}

static int brcmf_rx_hdrpull(struct brcmf_pub *drvr, struct sk_buff *skb,
			    struct brcmf_if **ifp)
{
	int ret;

	/* process and remove protocol-specific header */
	ret = brcmf_proto_hdrpull(drvr, true, skb, ifp);

	if (ret || !(*ifp) || !(*ifp)->ndev) {
		if (ret != -ENODATA && *ifp && (*ifp)->ndev)
			(*ifp)->ndev->stats.rx_errors++;
		brcmu_pkt_buf_free_skb(skb);
		return -ENODATA;
	}

	skb->protocol = eth_type_trans(skb, (*ifp)->ndev);
	brcmf_dbg(DATA, "protocol: 0x%04X\n", skb->protocol);

	return 0;
}

struct sk_buff *brcmf_rx_frame(struct device *dev, struct sk_buff *skb, bool handle_event,
			       bool inirq)
{
	struct brcmf_if *ifp;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	brcmf_dbg(DATA, "Enter: %s: rxp=%p\n", dev_name(dev), skb);

	if (brcmf_rx_hdrpull(drvr, skb, &ifp))
		return NULL;

	if (brcmf_proto_is_reorder_skb(skb)) {
		brcmf_proto_rxreorder(ifp, skb, inirq);
	} else {
		/* Process special event packets */
		if (handle_event) {
			gfp_t gfp = inirq ? GFP_ATOMIC : GFP_KERNEL;

			brcmf_fweh_process_skb(ifp->drvr, skb,
					       BCMILCP_SUBTYPE_VENDOR_LONG, gfp);
		}

		/* if sdio_rxf_in_kthread, enqueue it and process it later. */
		if (brcmf_feat_is_sdio_rxf_in_kthread(drvr))
			return skb;
		else
			brcmf_netif_rx(ifp, skb, inirq);
	}
	return NULL;
}

void brcmf_rx_event(struct device *dev, struct sk_buff *skb)
{
	struct brcmf_if *ifp;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	brcmf_dbg(EVENT, "Enter: %s: rxp=%p\n", dev_name(dev), skb);

	if (brcmf_rx_hdrpull(drvr, skb, &ifp))
		return;

	brcmf_fweh_process_skb(ifp->drvr, skb, 0, GFP_KERNEL);
	brcmu_pkt_buf_free_skb(skb);
}

void brcmf_txfinalize(struct brcmf_if *ifp, struct sk_buff *txp, bool success)
{
	struct ethhdr *eh;
	u16 type;

	eh = (struct ethhdr *)(txp->data);
	type = ntohs(eh->h_proto);

	if (type == ETH_P_PAE) {
		atomic_dec(&ifp->pend_8021x_cnt);
		if (waitqueue_active(&ifp->pend_8021x_wait))
			wake_up(&ifp->pend_8021x_wait);
	}

	if (!success && ifp->ndev)
		ifp->ndev->stats.tx_errors++;

	brcmu_pkt_buf_free_skb(txp);
}

static void brcmf_ethtool_get_drvinfo(struct net_device *ndev,
				    struct ethtool_drvinfo *info)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_pub *drvr = ifp->drvr;
	char drev[BRCMU_DOTREV_LEN] = "n/a";

	if (drvr->revinfo.result == 0)
		brcmu_dotrev_str(drvr->revinfo.driverrev, drev);
	strscpy(info->driver, KBUILD_MODNAME, sizeof(info->driver));
	strscpy(info->version, drev, sizeof(info->version));
	strscpy(info->fw_version, drvr->fwver, sizeof(info->fw_version));
	strscpy(info->bus_info, dev_name(drvr->bus_if->dev),
		sizeof(info->bus_info));
	if (!drvr->cnt_ver) {
		int ret;
		u8 *iovar_out;

		iovar_out = kzalloc(WL_CNT_IOV_BUF, GFP_KERNEL);
		if (!iovar_out)
			return;
		ret = brcmf_fil_iovar_data_get(ifp, "counters", iovar_out, WL_CNT_IOV_BUF);
		if (ret) {
			brcmf_err("Failed to get counters, code :%d\n", ret);
			goto done;
		}
		memcpy(&drvr->cnt_ver, iovar_out, sizeof(drvr->cnt_ver));
done:
	kfree(iovar_out);
	iovar_out = NULL;
	}
}

static void brcmf_et_get_strings(struct net_device *net_dev,
				u32 sset, u8 *strings)
{
	struct brcmf_if *ifp = netdev_priv(net_dev);
	struct brcmf_pub *drvr = ifp->drvr;

	if (sset == ETH_SS_STATS) {
		switch (drvr->cnt_ver) {
		case CNT_VER_6:
			memcpy(strings, fmac_ethtool_string_stats_v6,
				sizeof(fmac_ethtool_string_stats_v6));
			break;
		case CNT_VER_10:
			memcpy(strings, fmac_ethtool_string_stats_v10,
				sizeof(fmac_ethtool_string_stats_v10));
			break;
		case CNT_VER_30:
			memcpy(strings, fmac_ethtool_string_stats_v30,
				sizeof(fmac_ethtool_string_stats_v30));
			break;
		default:
			brcmf_err("Unsupported counters version\n");
		}
	}
}

static int brcmf_find_wlc_cntr_tlv(u8 *src, u16 *len)
{
	u16 tlv_id, data_len;
	u16 packing_offset, cur_tlv = IOVAR_XTLV_BEGIN;

	while (cur_tlv < *len) {
		memcpy(&tlv_id, (src + cur_tlv), sizeof(*len));
		memcpy(&data_len, (src + cur_tlv + XTLV_TYPE_SIZE), sizeof(*len));
		if (tlv_id == WL_CNT_XTLV_SLICE_IDX) {
			*len = data_len;
			return cur_tlv;
		}
		/* xTLV data has 4 bytes packing. So caclculate the packing offset using the data */
		packing_offset = PACKING_FACTOR(data_len);
		cur_tlv += XTLV_TYPE_LEN_SIZE + data_len + packing_offset;
	}
	return -EINVAL;
}

static void brcmf_et_get_stats(struct net_device *netdev,
				struct ethtool_stats *et_stats, u64 *results_buf)
{
	struct brcmf_if *ifp = netdev_priv(netdev);
	u8 *iovar_out, *src, ret;
	u16 version, len, xTLV_wl_cnt_offset = 0;
	u16 soffset = 0, idx = 0;

	iovar_out = kzalloc(WL_CNT_IOV_BUF, GFP_KERNEL);

	if (!iovar_out)
		return;

	ret = brcmf_fil_iovar_data_get(ifp, "counters", iovar_out, WL_CNT_IOV_BUF);
	if (ret) {
		brcmf_err("Failed to get counters, code :%d\n", ret);
		goto done;
	}
	src = iovar_out;

	memcpy(&version, src, sizeof(version));
	soffset += sizeof(version);
	memcpy(&len, (src + soffset), sizeof(len));
	soffset += sizeof(len);

	/* Check counters version and decide if its non-TLV or TLV (version>=30)*/
	if (version >= CNT_VER_30) {
		xTLV_wl_cnt_offset = brcmf_find_wlc_cntr_tlv(src, &len);
		len = (len / sizeof(u32));
	} else {
		len = (len / sizeof(u32)) - sizeof(u32);
	}

	src = src + soffset + xTLV_wl_cnt_offset;
	while (idx < (len)) {
		results_buf[idx++] = *((u32 *)src);
		src += sizeof(u32);
	}
done:
	kfree(iovar_out);
	iovar_out = NULL;
}

static int brcmf_et_get_scount(struct net_device *dev, int sset)
{
	u16 array_size;
	struct brcmf_if *ifp = netdev_priv(dev);
	struct brcmf_pub *drvr = ifp->drvr;

	if (sset == ETH_SS_STATS) {
		switch (drvr->cnt_ver) {
		case CNT_VER_6:
			array_size = ARRAY_SIZE(fmac_ethtool_string_stats_v6);
			break;
		case CNT_VER_10:
			array_size = ARRAY_SIZE(fmac_ethtool_string_stats_v10);
			break;
		case CNT_VER_30:
			array_size = ARRAY_SIZE(fmac_ethtool_string_stats_v30);
			break;
		default:
			brcmf_err("Unsupported counters version\n");
			return -EOPNOTSUPP;
		}
	} else {
		brcmf_dbg(INFO, "Does not support ethtool string set %d\n", sset);
		return -EOPNOTSUPP;
	}
	return array_size;
}

static const struct ethtool_ops brcmf_ethtool_ops = {
	.get_drvinfo = brcmf_ethtool_get_drvinfo,
	.get_ts_info = ethtool_op_get_ts_info,
	.get_strings		= brcmf_et_get_strings,
	.get_ethtool_stats	= brcmf_et_get_stats,
	.get_sset_count		= brcmf_et_get_scount,
};

static int brcmf_netdev_stop(struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);

	brcmf_dbg(TRACE, "Enter, bsscfgidx=%d\n", ifp->bsscfgidx);

	brcmf_cfg80211_down(ndev);

	brcmf_net_setcarrier(ifp, false);

	return 0;
}

static int brcmf_netdev_open(struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_pub *drvr = ifp->drvr;
	struct brcmf_bus *bus_if = drvr->bus_if;
	u32 toe_ol;

	brcmf_dbg(TRACE, "Enter, bsscfgidx=%d\n", ifp->bsscfgidx);

	/* If bus is not ready, can't continue */
	if (bus_if->state != BRCMF_BUS_UP) {
		bphy_err(drvr, "failed bus is not ready\n");
		return -EAGAIN;
	}

	atomic_set(&ifp->pend_8021x_cnt, 0);

	/* Get current TOE mode from dongle */
	if (brcmf_fil_iovar_int_get(ifp, "toe_ol", &toe_ol) >= 0
	    && (toe_ol & TOE_TX_CSUM_OL) != 0)
		ndev->features |= NETIF_F_IP_CSUM;
	else
		ndev->features &= ~NETIF_F_IP_CSUM;

	if (brcmf_cfg80211_up(ndev)) {
		bphy_err(drvr, "failed to bring up cfg80211\n");
		return -EIO;
	}

	/* Clear, carrier, set when connected or AP mode. */
	netif_carrier_off(ndev);
	return 0;
}

static const struct net_device_ops brcmf_netdev_ops_pri = {
	.ndo_open = brcmf_netdev_open,
	.ndo_stop = brcmf_netdev_stop,
	.ndo_start_xmit = brcmf_netdev_start_xmit,
	.ndo_set_mac_address = brcmf_netdev_set_mac_address,
	.ndo_set_rx_mode = brcmf_netdev_set_multicast_list
};

int brcmf_net_attach(struct brcmf_if *ifp, bool locked)
{
	struct brcmf_pub *drvr = ifp->drvr;
	struct net_device *ndev;
	s32 err;

	brcmf_dbg(TRACE, "Enter, bsscfgidx=%d mac=%pM\n", ifp->bsscfgidx,
		  ifp->mac_addr);
	ndev = ifp->ndev;

	/* set appropriate operations */
	ndev->netdev_ops = &brcmf_netdev_ops_pri;

	ndev->needed_headroom += drvr->hdrlen;
	ndev->ethtool_ops = &brcmf_ethtool_ops;

	/* set the mac address & netns */
	eth_hw_addr_set(ndev, ifp->mac_addr);
	dev_net_set(ndev, wiphy_net(cfg_to_wiphy(drvr->config)));

	INIT_WORK(&ifp->multicast_work, _brcmf_set_multicast_list);
	INIT_WORK(&ifp->ndoffload_work, brcmf_update_ipv6_addr);

	if (locked)
		err = cfg80211_register_netdevice(ndev);
	else
		err = register_netdev(ndev);
	if (err != 0) {
		bphy_err(drvr, "couldn't register the net device\n");
		goto fail;
	}

	netif_carrier_off(ndev);

	ndev->priv_destructor = brcmf_cfg80211_free_netdev;
	brcmf_dbg(INFO, "%s: Broadcom Dongle Host Driver\n", ndev->name);
	return 0;

fail:
	drvr->iflist[ifp->bsscfgidx] = NULL;
	ndev->netdev_ops = NULL;
	return -EBADE;
}

void brcmf_net_detach(struct net_device *ndev, bool locked)
{
	if (ndev->reg_state == NETREG_REGISTERED) {
		if (locked)
			cfg80211_unregister_netdevice(ndev);
		else
			unregister_netdev(ndev);
	} else {
		brcmf_cfg80211_free_netdev(ndev);
		free_netdev(ndev);
	}
}

static int brcmf_net_mon_open(struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_pub *drvr = ifp->drvr;
	u32 monitor;
	int err;

	brcmf_dbg(TRACE, "Enter\n");

	err = brcmf_fil_cmd_int_get(ifp, BRCMF_C_GET_MONITOR, &monitor);
	if (err) {
		bphy_err(drvr, "BRCMF_C_GET_MONITOR error (%d)\n", err);
		return err;
	} else if (monitor) {
		bphy_err(drvr, "Monitor mode is already enabled\n");
		return -EEXIST;
	}

	monitor = 3;
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_MONITOR, monitor);
	if (err)
		bphy_err(drvr, "BRCMF_C_SET_MONITOR error (%d)\n", err);

	return err;
}

static int brcmf_net_mon_stop(struct net_device *ndev)
{
	struct brcmf_if *ifp = netdev_priv(ndev);
	struct brcmf_pub *drvr = ifp->drvr;
	u32 monitor;
	int err;

	brcmf_dbg(TRACE, "Enter\n");

	monitor = 0;
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_MONITOR, monitor);
	if (err)
		bphy_err(drvr, "BRCMF_C_SET_MONITOR error (%d)\n", err);

	return err;
}

static netdev_tx_t brcmf_net_mon_start_xmit(struct sk_buff *skb,
					    struct net_device *ndev)
{
	dev_kfree_skb_any(skb);

	return NETDEV_TX_OK;
}

static const struct net_device_ops brcmf_netdev_ops_mon = {
	.ndo_open = brcmf_net_mon_open,
	.ndo_stop = brcmf_net_mon_stop,
	.ndo_start_xmit = brcmf_net_mon_start_xmit,
};

int brcmf_net_mon_attach(struct brcmf_if *ifp)
{
	struct brcmf_pub *drvr = ifp->drvr;
	struct net_device *ndev;
	int err;

	brcmf_dbg(TRACE, "Enter\n");

	ndev = ifp->ndev;
	ndev->netdev_ops = &brcmf_netdev_ops_mon;

	err = cfg80211_register_netdevice(ndev);
	if (err)
		bphy_err(drvr, "Failed to register %s device\n", ndev->name);

	return err;
}

void brcmf_net_setcarrier(struct brcmf_if *ifp, bool on)
{
	struct net_device *ndev;

	brcmf_dbg(TRACE, "Enter, bsscfgidx=%d carrier=%d\n", ifp->bsscfgidx,
		  on);

	ndev = ifp->ndev;
	brcmf_txflowblock_if(ifp, BRCMF_NETIF_STOP_REASON_DISCONNECTED, !on);
	if (on) {
		if (!netif_carrier_ok(ndev))
			netif_carrier_on(ndev);

	} else {
		if (netif_carrier_ok(ndev))
			netif_carrier_off(ndev);
	}
}

static int brcmf_net_p2p_open(struct net_device *ndev)
{
	brcmf_dbg(TRACE, "Enter\n");

	return brcmf_cfg80211_up(ndev);
}

static int brcmf_net_p2p_stop(struct net_device *ndev)
{
	brcmf_dbg(TRACE, "Enter\n");

	return brcmf_cfg80211_down(ndev);
}

static netdev_tx_t brcmf_net_p2p_start_xmit(struct sk_buff *skb,
					    struct net_device *ndev)
{
	if (skb)
		dev_kfree_skb_any(skb);

	return NETDEV_TX_OK;
}

static const struct net_device_ops brcmf_netdev_ops_p2p = {
	.ndo_open = brcmf_net_p2p_open,
	.ndo_stop = brcmf_net_p2p_stop,
	.ndo_start_xmit = brcmf_net_p2p_start_xmit
};

static int brcmf_net_p2p_attach(struct brcmf_if *ifp)
{
	struct brcmf_pub *drvr = ifp->drvr;
	struct net_device *ndev;

	brcmf_dbg(TRACE, "Enter, bsscfgidx=%d mac=%pM\n", ifp->bsscfgidx,
		  ifp->mac_addr);
	ndev = ifp->ndev;

	ndev->netdev_ops = &brcmf_netdev_ops_p2p;

	/* set the mac address */
	eth_hw_addr_set(ndev, ifp->mac_addr);

	if (register_netdev(ndev) != 0) {
		bphy_err(drvr, "couldn't register the p2p net device\n");
		goto fail;
	}

	brcmf_dbg(INFO, "%s: Broadcom Dongle Host Driver\n", ndev->name);

	return 0;

fail:
	ifp->drvr->iflist[ifp->bsscfgidx] = NULL;
	ndev->netdev_ops = NULL;
	return -EBADE;
}

struct brcmf_if *brcmf_add_if(struct brcmf_pub *drvr, s32 bsscfgidx, s32 ifidx,
			      bool is_p2pdev, const char *name, u8 *mac_addr)
{
	struct brcmf_if *ifp;
	struct net_device *ndev;

	brcmf_dbg(TRACE, "Enter, bsscfgidx=%d, ifidx=%d\n", bsscfgidx, ifidx);

	ifp = drvr->iflist[bsscfgidx];
	/*
	 * Delete the existing interface before overwriting it
	 * in case we missed the BRCMF_E_IF_DEL event.
	 */
	if (ifp) {
		if (ifidx) {
			bphy_err(drvr, "ERROR: netdev:%s already exists\n",
				 ifp->ndev->name);
			netif_stop_queue(ifp->ndev);
			brcmf_net_detach(ifp->ndev, false);
			drvr->iflist[bsscfgidx] = NULL;
		} else {
			brcmf_dbg(INFO, "netdev:%s ignore IF event\n",
				  ifp->ndev->name);
			return ERR_PTR(-EINVAL);
		}
	}

	if (!drvr->settings->p2p_enable && is_p2pdev) {
		/* this is P2P_DEVICE interface */
		brcmf_dbg(INFO, "allocate non-netdev interface\n");
		ifp = kzalloc(sizeof(*ifp), GFP_KERNEL);
		if (!ifp)
			return ERR_PTR(-ENOMEM);
	} else {
		brcmf_dbg(INFO, "allocate netdev interface\n");
		/* Allocate netdev, including space for private structure */
		ndev = alloc_netdev(sizeof(*ifp), is_p2pdev ? "p2p%d" : name,
				    NET_NAME_UNKNOWN, ether_setup);
		if (!ndev)
			return ERR_PTR(-ENOMEM);

		ndev->needs_free_netdev = true;
		ifp = netdev_priv(ndev);
		ifp->ndev = ndev;
		/* store mapping ifidx to bsscfgidx */
		if (drvr->if2bss[ifidx] == BRCMF_BSSIDX_INVALID)
			drvr->if2bss[ifidx] = bsscfgidx;
	}

	ifp->drvr = drvr;
	drvr->iflist[bsscfgidx] = ifp;
	ifp->ifidx = ifidx;
	ifp->bsscfgidx = bsscfgidx;

	init_waitqueue_head(&ifp->pend_8021x_wait);
	spin_lock_init(&ifp->netif_stop_lock);
	BRCMF_IF_STA_LIST_LOCK_INIT(ifp);
	 /* Initialize STA info list */
	INIT_LIST_HEAD(&ifp->sta_list);

	spin_lock_init(&ifp->twt_sess_list_lock);
	 /* Initialize TWT Session list */
	INIT_LIST_HEAD(&ifp->twt_sess_list);
	/* Setup the aperiodic TWT Session cleanup activity */
	timer_setup(&ifp->twt_evt_timeout, brcmf_twt_event_timeout_handler, 0);

	if (mac_addr != NULL)
		memcpy(ifp->mac_addr, mac_addr, ETH_ALEN);

	brcmf_dbg(TRACE, " ==== pid:%x, if:%s (%pM) created ===\n",
		  current->pid, name, ifp->mac_addr);

	return ifp;
}

static void brcmf_del_if(struct brcmf_pub *drvr, s32 bsscfgidx,
			 bool locked)
{
	struct brcmf_if *ifp;
	int ifidx;

	ifp = drvr->iflist[bsscfgidx];
	if (!ifp) {
		bphy_err(drvr, "Null interface, bsscfgidx=%d\n", bsscfgidx);
		return;
	}
	brcmf_dbg(TRACE, "Enter, bsscfgidx=%d, ifidx=%d\n", bsscfgidx,
		  ifp->ifidx);
	ifidx = ifp->ifidx;

	/* Stop the aperiodic TWT Session cleanup activity */
	if (timer_pending(&ifp->twt_evt_timeout))
		del_timer_sync(&ifp->twt_evt_timeout);

	if (ifp->ndev) {
		if (bsscfgidx == 0) {
			if (ifp->ndev->netdev_ops == &brcmf_netdev_ops_pri) {
				rtnl_lock();
				brcmf_netdev_stop(ifp->ndev);
				rtnl_unlock();
			}
		} else {
			netif_stop_queue(ifp->ndev);
		}

		if (ifp->ndev->netdev_ops == &brcmf_netdev_ops_pri) {
			cancel_work_sync(&ifp->multicast_work);
			cancel_work_sync(&ifp->ndoffload_work);
		}
		brcmf_net_detach(ifp->ndev, locked);
	} else {
		/* Only p2p device interfaces which get dynamically created
		 * end up here. In this case the p2p module should be informed
		 * about the removal of the interface within the firmware. If
		 * not then p2p commands towards the firmware will cause some
		 * serious troublesome side effects. The p2p module will clean
		 * up the ifp if needed.
		 */
		brcmf_p2p_ifp_removed(ifp, locked);
		kfree(ifp);
	}

	drvr->iflist[bsscfgidx] = NULL;
	if (drvr->if2bss[ifidx] == bsscfgidx)
		drvr->if2bss[ifidx] = BRCMF_BSSIDX_INVALID;
}

void brcmf_remove_interface(struct brcmf_if *ifp, bool locked)
{
	if (!ifp || !(ifp->drvr) || WARN_ON(ifp->drvr->iflist[ifp->bsscfgidx] != ifp)) {
		brcmf_err("Invalid interface or driver\n");
		return;
	}
	brcmf_dbg(TRACE, "Enter, bsscfgidx=%d, ifidx=%d\n", ifp->bsscfgidx, ifp->ifidx);
	brcmf_proto_del_if(ifp->drvr, ifp);
	brcmf_del_if(ifp->drvr, ifp->bsscfgidx, locked);
}

static int brcmf_psm_watchdog_notify(struct brcmf_if *ifp,
				     const struct brcmf_event_msg *evtmsg,
				     void *data)
{
	struct brcmf_pub *drvr = ifp->drvr;
	int err;

	brcmf_dbg(TRACE, "enter: bsscfgidx=%d\n", ifp->bsscfgidx);

	bphy_err(drvr, "PSM's watchdog has fired!\n");

	err = brcmf_debug_create_memdump(ifp->drvr->bus_if, data,
					 evtmsg->datalen);
	if (err)
		bphy_err(drvr, "Failed to get memory dump, %d\n", err);

	return err;
}

#ifdef CONFIG_INET
#define ARPOL_MAX_ENTRIES	8
static int brcmf_inetaddr_changed(struct notifier_block *nb,
				  unsigned long action, void *data)
{
	struct brcmf_pub *drvr = container_of(nb, struct brcmf_pub,
					      inetaddr_notifier);
	struct in_ifaddr *ifa = data;
	struct net_device *ndev = ifa->ifa_dev->dev;
	struct brcmf_if *ifp;
	int idx, i = 0, ret;
	u32 val;
	__be32 addr_table[ARPOL_MAX_ENTRIES] = {0};

	/* Find out if the notification is meant for us */
	for (idx = 0; idx < BRCMF_MAX_IFS; idx++) {
		ifp = drvr->iflist[idx];
		if (ifp && ifp->ndev == ndev)
			break;
		if (idx == BRCMF_MAX_IFS - 1)
			return NOTIFY_DONE;
	}

	if (!brcmf_feat_is_enabled(ifp, BRCMF_FEAT_OFFLOADS)) {
		/* check if arp offload is supported */
		ret = brcmf_fil_iovar_int_get(ifp, "arpoe", &val);
		if (ret)
			return NOTIFY_OK;

		/* old version only support primary index */
		ret = brcmf_fil_iovar_int_get(ifp, "arp_version", &val);
		if (ret)
			val = 1;
		if (val == 1)
			ifp = drvr->iflist[0];

		/* retrieve the table from firmware */
		ret = brcmf_fil_iovar_data_get(ifp, "arp_hostip", addr_table,
					       sizeof(addr_table));
		if (ret) {
			bphy_err(drvr, "fail to get arp ip table err:%d\n", ret);
			return NOTIFY_OK;
		}

		for (i = 0; i < ARPOL_MAX_ENTRIES; i++)
			if (ifa->ifa_address == addr_table[i])
				break;
	}

	switch (action) {
	case NETDEV_UP:
		if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_OFFLOADS)) {
			brcmf_generic_offload_host_ipv4_update(ifp,
							       BRCMF_OL_ARP | BRCMF_OL_ICMP,
							       ifa->ifa_address, true);
		} else {
			if (i == ARPOL_MAX_ENTRIES) {
				brcmf_dbg(TRACE, "add %pI4 to arp table\n",
					  &ifa->ifa_address);
				/* set it directly */
				ret = brcmf_fil_iovar_data_set(ifp, "arp_hostip",
							       &ifa->ifa_address,
							       sizeof(ifa->ifa_address));
				if (ret)
					bphy_err(drvr, "add arp ip err %d\n", ret);
			}
		}
		break;
	case NETDEV_DOWN:
		if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_OFFLOADS)) {
			brcmf_generic_offload_host_ipv4_update(ifp,
							       BRCMF_OL_ARP | BRCMF_OL_ICMP,
							       ifa->ifa_address, false);
		} else {
			if (i < ARPOL_MAX_ENTRIES) {
				addr_table[i] = 0;
				brcmf_dbg(TRACE, "remove %pI4 from arp table\n",
					  &ifa->ifa_address);
				/* clear the table in firmware */
				ret = brcmf_fil_iovar_data_set(ifp, "arp_hostip_clear",
							       NULL, 0);
				if (ret) {
					bphy_err(drvr, "fail to clear arp ip table err:%d\n",
						 ret);
					return NOTIFY_OK;
				}
				for (i = 0; i < ARPOL_MAX_ENTRIES; i++) {
					if (addr_table[i] == 0)
						continue;
					ret = brcmf_fil_iovar_data_set(ifp, "arp_hostip",
								       &addr_table[i],
								       sizeof(addr_table[i]));
					if (ret)
						bphy_err(drvr, "add arp ip err %d\n",
							 ret);
				}
			}
		}
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}
#endif

#if IS_ENABLED(CONFIG_IPV6)
static int brcmf_inet6addr_changed(struct notifier_block *nb,
				   unsigned long action, void *data)
{
	struct brcmf_pub *drvr = container_of(nb, struct brcmf_pub,
					      inet6addr_notifier);
	struct inet6_ifaddr *ifa = data;
	struct brcmf_if *ifp;
	int i;
	struct in6_addr *table;

	/* Only handle primary interface */
	ifp = drvr->iflist[0];
	if (!ifp)
		return NOTIFY_DONE;
	if (ifp->ndev != ifa->idev->dev)
		return NOTIFY_DONE;

	table = ifp->ipv6_addr_tbl;
	for (i = 0; i < NDOL_MAX_ENTRIES; i++)
		if (ipv6_addr_equal(&ifa->addr, &table[i]))
			break;

	switch (action) {
	case NETDEV_UP:
		if (i == NDOL_MAX_ENTRIES) {
			if (ifp->ipv6addr_idx < NDOL_MAX_ENTRIES) {
				table[ifp->ipv6addr_idx++] = ifa->addr;
			} else {
				for (i = 0; i < NDOL_MAX_ENTRIES - 1; i++)
					table[i] = table[i + 1];
				table[NDOL_MAX_ENTRIES - 1] = ifa->addr;
			}
		}
		break;
	case NETDEV_DOWN:
		if (i < NDOL_MAX_ENTRIES) {
			for (; i < ifp->ipv6addr_idx - 1; i++)
				table[i] = table[i + 1];
			memset(&table[i], 0, sizeof(table[i]));
			ifp->ipv6addr_idx--;
		}
		break;
	default:
		break;
	}

	schedule_work(&ifp->ndoffload_work);

	return NOTIFY_OK;
}
#endif


int brcmf_fwlog_attach(struct device *dev)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	return brcmf_debug_fwlog_init(drvr);
}

static int brcmf_revinfo_read(struct seq_file *s, void *data)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(s->private);
	struct brcmf_rev_info *ri = &bus_if->drvr->revinfo;
	char drev[BRCMU_DOTREV_LEN];
	char brev[BRCMU_BOARDREV_LEN];

	seq_printf(s, "vendorid: 0x%04x\n", ri->vendorid);
	seq_printf(s, "deviceid: 0x%04x\n", ri->deviceid);
	seq_printf(s, "radiorev: %s\n", brcmu_dotrev_str(ri->radiorev, drev));
	seq_printf(s, "chip: %s\n", ri->chipname);
	seq_printf(s, "chippkg: %u\n", ri->chippkg);
	seq_printf(s, "corerev: %u\n", ri->corerev);
	seq_printf(s, "boardid: 0x%04x\n", ri->boardid);
	seq_printf(s, "boardvendor: 0x%04x\n", ri->boardvendor);
	seq_printf(s, "boardrev: %s\n", brcmu_boardrev_str(ri->boardrev, brev));
	seq_printf(s, "driverrev: %s\n", brcmu_dotrev_str(ri->driverrev, drev));
	seq_printf(s, "ucoderev: %u\n", ri->ucoderev);
	seq_printf(s, "bus: %u\n", ri->bus);
	seq_printf(s, "phytype: %u\n", ri->phytype);
	seq_printf(s, "phyrev: %u\n", ri->phyrev);
	seq_printf(s, "anarev: %u\n", ri->anarev);
	seq_printf(s, "nvramrev: %08x\n", ri->nvramrev);

	seq_printf(s, "clmver: %s\n", bus_if->drvr->clmver);

	return 0;
}

static void brcmf_core_bus_reset(struct work_struct *work)
{
	struct brcmf_pub *drvr = container_of(work, struct brcmf_pub,
					      bus_reset);

	brcmf_bus_reset(drvr->bus_if);
}

static ssize_t bus_reset_write(struct file *file, const char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	struct brcmf_pub *drvr = file->private_data;
	u8 value;

	if (kstrtou8_from_user(user_buf, count, 0, &value))
		return -EINVAL;

	if (value != 1)
		return -EINVAL;

	schedule_work(&drvr->bus_reset);

	return count;
}

static const struct file_operations bus_reset_fops = {
	.open	= simple_open,
	.llseek	= no_llseek,
	.write	= bus_reset_write,
};

static int brcmf_bus_started(struct brcmf_pub *drvr, struct cfg80211_ops *ops)
{
	int ret = -1;
	struct brcmf_bus *bus_if = drvr->bus_if;
	struct brcmf_if *ifp;
	struct brcmf_if *p2p_ifp;
	int i, num;

	brcmf_dbg(TRACE, "\n");

	/* add primary networking interface */
	ifp = brcmf_add_if(drvr, 0, 0, false, "wlan%d",
			   is_valid_ether_addr(drvr->settings->mac) ? drvr->settings->mac : NULL);
	if (IS_ERR(ifp))
		return PTR_ERR(ifp);

	p2p_ifp = NULL;

	/* signal bus ready */
	brcmf_bus_change_state(bus_if, BRCMF_BUS_UP);

	/* do bus specific preinit here */
	ret = brcmf_bus_preinit(bus_if);
	if (ret < 0)
		goto fail;

	/* Bus is ready, do any initialization */
	ret = brcmf_c_preinit_dcmds(ifp);
	if (ret < 0)
		goto fail;

	brcmf_feat_attach(drvr);
	ret = brcmf_bus_set_fcmode(bus_if);
	/* Set fcmode = 0 for PCIe/USB */
	if (ret < 0)
		drvr->settings->fcmode = 0;

	ret = brcmf_proto_init_done(drvr);
	if (ret < 0)
		goto fail;

	brcmf_proto_add_if(drvr, ifp);

	drvr->config = brcmf_cfg80211_attach(drvr, ops,
					     drvr->settings->p2p_enable);
	if (drvr->config == NULL) {
		ret = -ENOMEM;
		goto fail;
	}

	/* update custom DSCP to PRIO mapping */
	if (drvr->settings->pkt_prio) {
		drvr->qos_map = kzalloc(sizeof(struct cfg80211_qos_map), GFP_KERNEL);
		if (!drvr->qos_map) {
			ret = -ENOMEM;
			goto fail;
		}
		num = sizeof(dscp_excpt) / (sizeof(struct cfg80211_dscp_exception));
		drvr->qos_map->num_des = num;
		for (i = 0; i < num; i++) {
			drvr->qos_map->dscp_exception[i].dscp = dscp_excpt[i].dscp;
			drvr->qos_map->dscp_exception[i].up = dscp_excpt[i].up;
		}
		memcpy(drvr->qos_map->up, dscp_range, sizeof(dscp_range[8]));
	}

	ret = brcmf_net_attach(ifp, false);

	if ((!ret) && (drvr->settings->p2p_enable)) {
		p2p_ifp = drvr->iflist[1];
		if (p2p_ifp)
			ret = brcmf_net_p2p_attach(p2p_ifp);
	}

	if (ret)
		goto fail;

#ifdef CONFIG_INET
	drvr->inetaddr_notifier.notifier_call = brcmf_inetaddr_changed;
	ret = register_inetaddr_notifier(&drvr->inetaddr_notifier);
	if (ret)
		goto fail;

#if IS_ENABLED(CONFIG_IPV6)
	drvr->inet6addr_notifier.notifier_call = brcmf_inet6addr_changed;
	ret = register_inet6addr_notifier(&drvr->inet6addr_notifier);
	if (ret) {
		unregister_inetaddr_notifier(&drvr->inetaddr_notifier);
		goto fail;
	}
#endif
#endif /* CONFIG_INET */

	INIT_WORK(&drvr->bus_reset, brcmf_core_bus_reset);

	/* populate debugfs */
	brcmf_debugfs_add_entry(drvr, "revinfo", brcmf_revinfo_read);
	brcmf_debugfs_add_entry(drvr, "parameter", brcmf_debugfs_param_read);
	debugfs_create_file("reset", 0600, brcmf_debugfs_get_devdir(drvr), drvr,
			    &bus_reset_fops);
	brcmf_feat_debugfs_create(drvr);
	brcmf_proto_debugfs_create(drvr);
	brcmf_bus_debugfs_create(bus_if);
	brcmf_twt_debugfs_create(drvr);
	inf_btsdio_debugfs_create(drvr);

	return 0;

fail:
	bphy_err(drvr, "failed: %d\n", ret);
	if (drvr->config) {
		brcmf_cfg80211_detach(drvr->config);
		drvr->config = NULL;
	}
	brcmf_net_detach(ifp->ndev, false);
	if (p2p_ifp)
		brcmf_net_detach(p2p_ifp->ndev, false);
	drvr->iflist[0] = NULL;
	drvr->iflist[1] = NULL;
	if (drvr->settings->ignore_probe_fail)
		ret = 0;

	return ret;
}

int brcmf_alloc(struct device *dev, struct brcmf_mp_device *settings)
{
	struct wiphy *wiphy;
	struct cfg80211_ops *ops;
	struct brcmf_pub *drvr = NULL;

	brcmf_dbg(TRACE, "Enter\n");

	ops = brcmf_cfg80211_get_ops(settings);
	if (!ops)
		return -ENOMEM;

	wiphy = wiphy_new(ops, sizeof(*drvr));
	if (!wiphy) {
		kfree(ops);
		return -ENOMEM;
	}

	set_wiphy_dev(wiphy, dev);
	drvr = wiphy_priv(wiphy);
	drvr->wiphy = wiphy;
	drvr->ops = ops;
	drvr->bus_if = dev_get_drvdata(dev);
	drvr->bus_if->drvr = drvr;
	drvr->settings = settings;

	return 0;
}

int brcmf_attach(struct device *dev, bool start_bus)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;
	int ret = 0;
	int i;

	brcmf_dbg(TRACE, "Enter\n");

	for (i = 0; i < ARRAY_SIZE(drvr->if2bss); i++)
		drvr->if2bss[i] = BRCMF_BSSIDX_INVALID;

	mutex_init(&drvr->proto_block);

	/* Link to bus module */
	drvr->hdrlen = 0;

	drvr->req_mpc = 1;
	/* Attach and link in the protocol */
	ret = brcmf_proto_attach(drvr);
	if (ret != 0) {
		bphy_err(drvr, "brcmf_prot_attach failed\n");
		goto fail;
	}

	/* Attach to events important for core code */
	brcmf_fweh_register(drvr, BRCMF_E_PSM_WATCHDOG,
			    brcmf_psm_watchdog_notify);

	/* attach firmware event handler */
	brcmf_fweh_attach(drvr);

	if (start_bus) {
		ret = brcmf_bus_started(drvr, drvr->ops);
		if (ret != 0) {
			bphy_err(drvr, "dongle is not responding: err=%d\n",
				 ret);
			goto fail;
		}
	}

	return 0;

fail:
	brcmf_detach(dev);

	return ret;
}

void brcmf_bus_add_txhdrlen(struct device *dev, uint len)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	if (drvr) {
		drvr->hdrlen += len;
	}
}

void brcmf_dev_reset(struct device *dev)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	if (drvr == NULL)
		return;

	if (drvr->iflist[0])
		brcmf_fil_cmd_int_set(drvr->iflist[0], BRCMF_C_TERMINATED, 1);
}

void brcmf_dev_coredump(struct device *dev)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);

	if (brcmf_debug_create_memdump(bus_if, NULL, 0) < 0)
		brcmf_dbg(TRACE, "failed to create coredump\n");
}

void brcmf_fw_crashed(struct device *dev)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	bphy_err(drvr, "Firmware has halted or crashed\n");

	brcmf_dev_coredump(dev);

	if (drvr->bus_reset.func)
		schedule_work(&drvr->bus_reset);
}

void brcmf_detach(struct device *dev)
{
	s32 i;
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	brcmf_dbg(TRACE, "Enter\n");

	if (drvr == NULL)
		return;

#ifdef CONFIG_INET
	unregister_inetaddr_notifier(&drvr->inetaddr_notifier);
#endif

#if IS_ENABLED(CONFIG_IPV6)
	unregister_inet6addr_notifier(&drvr->inet6addr_notifier);
#endif

	brcmf_bus_change_state(bus_if, BRCMF_BUS_DOWN);
	/* make sure primary interface removed last */
	for (i = BRCMF_MAX_IFS - 1; i > -1; i--) {
		if (drvr->iflist[i])
			brcmf_remove_interface(drvr->iflist[i], false);
	}
	brcmf_bus_stop(drvr->bus_if);

	if (drvr->settings->pkt_prio) {
		kfree(drvr->qos_map);
		drvr->qos_map = NULL;
	}

	brcmf_fweh_detach(drvr);
	brcmf_proto_detach(drvr);

	if (drvr->mon_if) {
		brcmf_net_detach(drvr->mon_if->ndev, false);
		drvr->mon_if = NULL;
	}

	if (drvr->config) {
		kfree(drvr->config->pfn_data.network_blob_data);
		brcmf_p2p_detach(&drvr->config->p2p);
		brcmf_cfg80211_detach(drvr->config);
		drvr->config = NULL;
	}
}

void brcmf_free(struct device *dev)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_pub *drvr = bus_if->drvr;

	if (!drvr)
		return;

	bus_if->drvr = NULL;

	kfree(drvr->ops);

	wiphy_free(drvr->wiphy);
}

s32 brcmf_iovar_data_set(struct device *dev, char *name, void *data, u32 len)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(dev);
	struct brcmf_if *ifp = bus_if->drvr->iflist[0];

	return brcmf_fil_iovar_data_set(ifp, name, data, len);
}

static int brcmf_get_pend_8021x_cnt(struct brcmf_if *ifp)
{
	return atomic_read(&ifp->pend_8021x_cnt);
}

int brcmf_netdev_wait_pend8021x(struct brcmf_if *ifp)
{
	struct brcmf_pub *drvr = ifp->drvr;
	int err;

	err = wait_event_timeout(ifp->pend_8021x_wait,
				 !brcmf_get_pend_8021x_cnt(ifp),
				 MAX_WAIT_FOR_8021X_TX);

	if (!err) {
		bphy_err(drvr, "Timed out waiting for no pending 802.1x packets\n");
		atomic_set(&ifp->pend_8021x_cnt, 0);
	}

	return !err;
}

void brcmf_bus_change_state(struct brcmf_bus *bus, enum brcmf_bus_state state)
{
	struct brcmf_pub *drvr = bus->drvr;
	struct net_device *ndev;
	int ifidx;

	brcmf_dbg(TRACE, "%d -> %d\n", bus->state, state);

	if (!drvr) {
		brcmf_dbg(INFO, "ignoring transition, bus not attached yet\n");
		return;
	}

	bus->state = state;

	if (state == BRCMF_BUS_UP) {
		for (ifidx = 0; ifidx < BRCMF_MAX_IFS; ifidx++) {
			if ((drvr->iflist[ifidx]) &&
			    (drvr->iflist[ifidx]->ndev)) {
				ndev = drvr->iflist[ifidx]->ndev;
				if (netif_queue_stopped(ndev))
					netif_wake_queue(ndev);
			}
		}
	}
}

int __init brcmf_core_init(void)
{
	int err;

	err = brcmf_sdio_register();
	if (err)
		return err;

	err = brcmf_usb_register();
	if (err)
		goto error_usb_register;

	err = brcmf_pcie_register();
	if (err)
		goto error_pcie_register;
	return 0;

error_pcie_register:
	brcmf_usb_exit();
error_usb_register:
	brcmf_sdio_exit();
	return err;
}

void brcmf_core_exit(void)
{
	brcmf_sdio_exit();
	brcmf_usb_exit();
	brcmf_pcie_exit();
}

int
brcmf_pktfilter_add_remove(struct net_device *ndev, int filter_num, bool add)
{
	struct brcmf_if *ifp =  netdev_priv(ndev);
	struct brcmf_pub *drvr = ifp->drvr;
	struct brcmf_pkt_filter_le *pkt_filter;
	int filter_fixed_len = offsetof(struct brcmf_pkt_filter_le, u);
	int pattern_fixed_len = offsetof(struct brcmf_pkt_filter_pattern_le,
				  mask_and_pattern);
	u16 mask_and_pattern[MAX_PKTFILTER_PATTERN_SIZE];
	int buflen = 0;
	int ret = 0;

	brcmf_dbg(INFO, "%s packet filter number %d\n",
		  (add ? "add" : "remove"), filter_num);

	pkt_filter = kzalloc(sizeof(*pkt_filter) +
			(MAX_PKTFILTER_PATTERN_FILL_SIZE), GFP_ATOMIC);
	if (!pkt_filter)
		return -ENOMEM;

	switch (filter_num) {
	case BRCMF_UNICAST_FILTER_NUM:
		pkt_filter->id = 100;
		pkt_filter->type = 0;
		pkt_filter->negate_match = 0;
		pkt_filter->u.pattern.offset = 0;
		pkt_filter->u.pattern.size_bytes = 1;
		mask_and_pattern[0] = 0x0001;
		break;
	case BRCMF_BROADCAST_FILTER_NUM:
		//filter_pattern = "101 0 0 0 0xFFFFFFFFFFFF 0xFFFFFFFFFFFF";
		pkt_filter->id = 101;
		pkt_filter->type = 0;
		pkt_filter->negate_match = 0;
		pkt_filter->u.pattern.offset = 0;
		pkt_filter->u.pattern.size_bytes = 6;
		mask_and_pattern[0] = 0xFFFF;
		mask_and_pattern[1] = 0xFFFF;
		mask_and_pattern[2] = 0xFFFF;
		mask_and_pattern[3] = 0xFFFF;
		mask_and_pattern[4] = 0xFFFF;
		mask_and_pattern[5] = 0xFFFF;
		break;
	case BRCMF_MULTICAST4_FILTER_NUM:
		//filter_pattern = "102 0 0 0 0xFFFFFF 0x01005E";
		pkt_filter->id = 102;
		pkt_filter->type = 0;
		pkt_filter->negate_match = 0;
		pkt_filter->u.pattern.offset = 0;
		pkt_filter->u.pattern.size_bytes = 3;
		mask_and_pattern[0] = 0xFFFF;
		mask_and_pattern[1] = 0x01FF;
		mask_and_pattern[2] = 0x5E00;
		break;
	case BRCMF_MULTICAST6_FILTER_NUM:
		//filter_pattern = "103 0 0 0 0xFFFF 0x3333";
		pkt_filter->id = 103;
		pkt_filter->type = 0;
		pkt_filter->negate_match = 0;
		pkt_filter->u.pattern.offset = 0;
		pkt_filter->u.pattern.size_bytes = 2;
		mask_and_pattern[0] = 0xFFFF;
		mask_and_pattern[1] = 0x3333;
		break;
	case BRCMF_MDNS_FILTER_NUM:
		//filter_pattern = "104 0 0 0 0xFFFFFFFFFFFF 0x01005E0000FB";
		pkt_filter->id = 104;
		pkt_filter->type = 0;
		pkt_filter->negate_match = 0;
		pkt_filter->u.pattern.offset = 0;
		pkt_filter->u.pattern.size_bytes = 6;
		mask_and_pattern[0] = 0xFFFF;
		mask_and_pattern[1] = 0xFFFF;
		mask_and_pattern[2] = 0xFFFF;
		mask_and_pattern[3] = 0x0001;
		mask_and_pattern[4] = 0x005E;
		mask_and_pattern[5] = 0xFB00;
		break;
	case BRCMF_ARP_FILTER_NUM:
		//filter_pattern = "105 0 0 12 0xFFFF 0x0806";
		pkt_filter->id = 105;
		pkt_filter->type = 0;
		pkt_filter->negate_match = 0;
		pkt_filter->u.pattern.offset = 12;
		pkt_filter->u.pattern.size_bytes = 2;
		mask_and_pattern[0] = 0xFFFF;
		mask_and_pattern[1] = 0x0608;
		break;
	case BRCMF_BROADCAST_ARP_FILTER_NUM:
		//filter_pattern = "106 0 0 0
		//0xFFFFFFFFFFFF0000000000000806
		//0xFFFFFFFFFFFF0000000000000806";
		pkt_filter->id = 106;
		pkt_filter->type = 0;
		pkt_filter->negate_match = 0;
		pkt_filter->u.pattern.offset = 0;
		pkt_filter->u.pattern.size_bytes = 14;
		mask_and_pattern[0] = 0xFFFF;
		mask_and_pattern[1] = 0xFFFF;
		mask_and_pattern[2] = 0xFFFF;
		mask_and_pattern[3] = 0x0000;
		mask_and_pattern[4] = 0x0000;
		mask_and_pattern[5] = 0x0000;
		mask_and_pattern[6] = 0x0608;
		mask_and_pattern[7] = 0xFFFF;
		mask_and_pattern[8] = 0xFFFF;
		mask_and_pattern[9] = 0xFFFF;
		mask_and_pattern[10] = 0x0000;
		mask_and_pattern[11] = 0x0000;
		mask_and_pattern[12] = 0x0000;
		mask_and_pattern[13] = 0x0608;
		break;
	default:
		ret = -EINVAL;
		goto failed;
	}
	memcpy(pkt_filter->u.pattern.mask_and_pattern, mask_and_pattern,
	       pkt_filter->u.pattern.size_bytes * 2);
	buflen = filter_fixed_len + pattern_fixed_len +
		  pkt_filter->u.pattern.size_bytes * 2;

	if (add) {
		/* Add filter */
		ifp->fwil_fwerr = true;
		ret = brcmf_fil_iovar_data_set(ifp, "pkt_filter_add",
					       pkt_filter, buflen);
		ifp->fwil_fwerr = false;
		if (ret)
			goto failed;
		drvr->pkt_filter[filter_num].id = pkt_filter->id;
		drvr->pkt_filter[filter_num].enable  = 0;

	} else {
		/* Delete filter */
		ifp->fwil_fwerr = true;
		ret = brcmf_fil_iovar_int_set(ifp, "pkt_filter_delete",
					      pkt_filter->id);
		ifp->fwil_fwerr = false;
		if (ret == -BRCMF_FW_BADARG)
			ret = 0;
		if (ret)
			goto failed;

		drvr->pkt_filter[filter_num].id = 0;
		drvr->pkt_filter[filter_num].enable  = 0;
	}
failed:
	if (ret)
		brcmf_err("%s packet filter failed, ret=%d\n",
			  (add ? "add" : "remove"), ret);

	kfree(pkt_filter);
	return ret;
}

int brcmf_pktfilter_enable(struct net_device *ndev, bool enable)
{
	struct brcmf_if *ifp =  netdev_priv(ndev);
	struct brcmf_pub *drvr = ifp->drvr;
	int ret = 0;
	int idx = 0;

	for (idx = 0; idx < MAX_PKT_FILTER_COUNT; ++idx) {
		if (drvr->pkt_filter[idx].id != 0) {
			drvr->pkt_filter[idx].enable = enable;
			ret = brcmf_fil_iovar_data_set(ifp, "pkt_filter_enable",
						       &drvr->pkt_filter[idx],
				sizeof(struct brcmf_pkt_filter_enable_le));
			if (ret) {
				brcmf_err("%s packet filter id(%d) failed, ret=%d\n",
					  (enable ? "enable" : "disable"),
					  drvr->pkt_filter[idx].id, ret);
			}
		}
	}
	return ret;
}

/** Find STA with MAC address ea in an interface's STA list. */
struct brcmf_sta *
brcmf_find_sta(struct brcmf_if *ifp, const u8 *ea)
{
	struct brcmf_sta  *sta;
	unsigned long flags;

	BRCMF_IF_STA_LIST_LOCK(ifp, flags);
	list_for_each_entry(sta, &ifp->sta_list, list) {
		if (!memcmp(sta->ea.octet, ea, ETH_ALEN)) {
			brcmf_dbg(INFO, "Found STA: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x into sta list\n",
				  sta->ea.octet[0], sta->ea.octet[1],
				  sta->ea.octet[2], sta->ea.octet[3],
				  sta->ea.octet[4], sta->ea.octet[5]);
			BRCMF_IF_STA_LIST_UNLOCK(ifp, flags);
			return sta;
		}
	}
	BRCMF_IF_STA_LIST_UNLOCK(ifp, flags);

	return BRCMF_STA_NULL;
}

/** Add STA into the interface's STA list. */
struct brcmf_sta *
brcmf_add_sta(struct brcmf_if *ifp, const u8 *ea)
{
	struct brcmf_sta *sta;
	unsigned long flags;

	sta =  kzalloc(sizeof(*sta), GFP_KERNEL);
	if (sta == BRCMF_STA_NULL) {
		brcmf_err("Alloc failed\n");
		return BRCMF_STA_NULL;
	}
	memcpy(sta->ea.octet, ea, ETH_ALEN);
	brcmf_dbg(INFO, "Add STA: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x into sta list\n",
		  sta->ea.octet[0], sta->ea.octet[1],
		  sta->ea.octet[2], sta->ea.octet[3],
		  sta->ea.octet[4], sta->ea.octet[5]);

	/* link the sta and the dhd interface */
	sta->ifp = ifp;
	INIT_LIST_HEAD(&sta->list);

	BRCMF_IF_STA_LIST_LOCK(ifp, flags);

	list_add_tail(&sta->list, &ifp->sta_list);

	BRCMF_IF_STA_LIST_UNLOCK(ifp, flags);
	return sta;
}

/** Delete STA from the interface's STA list. */
void
brcmf_del_sta(struct brcmf_if *ifp, const u8 *ea)
{
	struct brcmf_sta *sta, *next;
	unsigned long flags;

	BRCMF_IF_STA_LIST_LOCK(ifp, flags);
	list_for_each_entry_safe(sta, next, &ifp->sta_list, list) {
		if (!memcmp(sta->ea.octet, ea, ETH_ALEN)) {
			brcmf_dbg(INFO, "del STA: 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x from sta list\n",
				  ea[0], ea[1], ea[2], ea[3],
				  ea[4], ea[5]);
			list_del(&sta->list);
			kfree(sta);
		}
	}

	BRCMF_IF_STA_LIST_UNLOCK(ifp, flags);
}

/** Add STA if it doesn't exist. Not reentrant. */
struct brcmf_sta*
brcmf_findadd_sta(struct brcmf_if *ifp, const u8 *ea)
{
	struct brcmf_sta *sta = NULL;

	sta = brcmf_find_sta(ifp, ea);

	if (!sta) {
		/* Add entry */
		sta = brcmf_add_sta(ifp, ea);
	}
	return sta;
}
