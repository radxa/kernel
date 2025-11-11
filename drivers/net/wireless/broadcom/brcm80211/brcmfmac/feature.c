// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2014 Broadcom Corporation
 */

#include <linux/netdevice.h>
#include <linux/module.h>

#include <brcm_hw_ids.h>
#include <brcmu_wifi.h>
#include "core.h"
#include "bus.h"
#include "debug.h"
#include "fwil.h"
#include "fwil_types.h"
#include "feature.h"
#include "common.h"
#include "xtlv.h"
#include "twt.h"


/*
 * expand feature list to array of feature strings.
 */
#define BRCMF_FEAT_DEF(_f) \
	#_f,
static const char *brcmf_feat_names[] = {
	BRCMF_FEAT_LIST
};
#undef BRCMF_FEAT_DEF

struct brcmf_feat_fwcap {
	enum brcmf_feat_id feature;
	const char * const fwcap_id;
};

static const struct brcmf_feat_fwcap brcmf_fwcap_map[] = {
	{ BRCMF_FEAT_MBSS, "mbss" },
	{ BRCMF_FEAT_MCHAN, "mchan" },
	{ BRCMF_FEAT_P2P, "p2p" },
	{ BRCMF_FEAT_MONITOR, "monitor" },
	{ BRCMF_FEAT_MONITOR_FLAG, "rtap" },
	{ BRCMF_FEAT_MONITOR_FMT_RADIOTAP, "rtap" },
	{ BRCMF_FEAT_DOT11H, "802.11h" },
	{ BRCMF_FEAT_SAE, "sae " },
	{ BRCMF_FEAT_FWAUTH, "idauth" },
	{ BRCMF_FEAT_SAE_EXT, "sae_ext " },
	{ BRCMF_FEAT_FBT, "fbt " },
	{ BRCMF_FEAT_OKC, "okc" },
	{ BRCMF_FEAT_GCMP, "gcmp" },
	{ BRCMF_FEAT_OFFLOADS, "offloads" },
	{ BRCMF_FEAT_ULP, "ulp" },
	{ BRCMF_FEAT_PROPTXSTATUS, "proptxstatus" },
	{ BRCMF_FEAT_OWE, "owe" },
	{ BRCMF_FEAT_GTKO, "gtko" },
	{ BRCMF_FEAT_MCHAN_CONFIG, "mchan_config" },
};

#ifdef DEBUG
/*
 * expand quirk list to array of quirk strings.
 */
#define BRCMF_QUIRK_DEF(_q) \
	#_q,
static const char * const brcmf_quirk_names[] = {
	BRCMF_QUIRK_LIST
};
#undef BRCMF_QUIRK_DEF

/**
 * brcmf_feat_debugfs_read() - expose feature info to debugfs.
 *
 * @seq: sequence for debugfs entry.
 * @data: raw data pointer.
 */
static int brcmf_feat_debugfs_read(struct seq_file *seq, void *data)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(seq->private);
	u8 feats[DIV_ROUND_UP(BRCMF_FEAT_LAST, 8)] = {0};
	u32 quirks = bus_if->drvr->chip_quirks;
	int id, i;
	u8 size = BRCMF_FEAT_LAST / 8;

	memcpy(feats, bus_if->drvr->feat_flags, sizeof(feats));

	seq_puts(seq, "Features: ");
	for (i = 0; i < size; i++)
		seq_printf(seq, "%02x", feats[i]);
	seq_puts(seq, "\n");

	for (id = 0; id < BRCMF_FEAT_LAST; id++)
		if (feats[id / 8] & BIT(id % 8))
			seq_printf(seq, "\t%s\n", brcmf_feat_names[id]);

	seq_printf(seq, "\nQuirks:   %08x\n", quirks);
	for (id = 0; id < BRCMF_FEAT_QUIRK_LAST; id++)
		if (quirks & BIT(id))
			seq_printf(seq, "\t%s\n", brcmf_quirk_names[id]);
	return 0;
}
#else
static int brcmf_feat_debugfs_read(struct seq_file *seq, void *data)
{
	return 0;
}
#endif /* DEBUG */

/**
 * brcmf_feat_iovar_int_get() - determine feature through iovar query.
 *
 * @ifp: interface to query.
 * @id: feature id.
 * @name: iovar name.
 */
static void brcmf_feat_iovar_int_get(struct brcmf_if *ifp,
				     enum brcmf_feat_id id, char *name)
{
	u32 data;
	int err;

	/* we need to know firmware error */
	ifp->fwil_fwerr = true;

	err = brcmf_fil_iovar_int_get(ifp, name, &data);
	if (err != -BRCMF_FW_UNSUPPORTED) {
		brcmf_dbg(INFO, "enabling feature: %s\n", brcmf_feat_names[id]);
		ifp->drvr->feat_flags[id / 8] |= BIT(id % 8);
	} else {
		brcmf_dbg(TRACE, "%s feature check failed: %d\n",
			  brcmf_feat_names[id], err);
	}

	ifp->fwil_fwerr = false;
}

static void brcmf_feat_iovar_data_set(struct brcmf_if *ifp,
				      enum brcmf_feat_id id, char *name,
				      const void *data, size_t len)
{
	int err;

	/* we need to know firmware error */
	ifp->fwil_fwerr = true;

	err = brcmf_fil_iovar_data_set(ifp, name, data, len);
	if (err != -BRCMF_FW_UNSUPPORTED) {
		brcmf_dbg(INFO, "enabling feature: %s\n", brcmf_feat_names[id]);
		ifp->drvr->feat_flags[id / 8] |= BIT(id % 8);
	} else {
		brcmf_dbg(TRACE, "%s feature check failed: %d\n",
			  brcmf_feat_names[id], err);
	}

	ifp->fwil_fwerr = false;
}

static void brcmf_feat_iovar_enab_get(struct brcmf_if *ifp,
					enum brcmf_feat_id id, char *name,
					u16 subcmd_id)
{
	int err;
	u8 val;

	/* we need to know firmware error */
	ifp->fwil_fwerr = true;

	err = brcmf_fil_xtlv_data_get(ifp, name, subcmd_id,
				      (void *)&val, sizeof(val));

	if (!err) {
		brcmf_dbg(INFO, "enabling feature: %s\n", brcmf_feat_names[id]);
		ifp->drvr->feat_flags[id / 8] |= BIT(id % 8);
	} else {
		brcmf_dbg(TRACE, "%s feature check failed: %d\n",
			  brcmf_feat_names[id], err);
	}

	ifp->fwil_fwerr = false;
}

#define MAX_CAPS_BUFFER_SIZE	768
static void brcmf_feat_firmware_capabilities(struct brcmf_if *ifp)
{
	struct brcmf_pub *drvr = ifp->drvr;
	char caps[MAX_CAPS_BUFFER_SIZE];
	enum brcmf_feat_id id;
	int i, err;

	err = brcmf_fil_iovar_data_get(ifp, "cap", caps, sizeof(caps));
	if (err) {
		bphy_err(drvr, "could not get firmware cap (%d)\n", err);
		return;
	}

	brcmf_dbg(INFO, "[ %s]\n", caps);

	for (i = 0; i < ARRAY_SIZE(brcmf_fwcap_map); i++) {
		if (strnstr(caps, brcmf_fwcap_map[i].fwcap_id, sizeof(caps))) {
			id = brcmf_fwcap_map[i].feature;
			brcmf_dbg(INFO, "enabling feature: %s\n",
				  brcmf_feat_names[id]);
			ifp->drvr->feat_flags[id / 8] |= BIT(id % 8);
		}
	}
}

/**
 * brcmf_feat_fwcap_debugfs_read() - expose firmware capabilities to debugfs.
 *
 * @seq: sequence for debugfs entry.
 * @data: raw data pointer.
 */
static int brcmf_feat_fwcap_debugfs_read(struct seq_file *seq, void *data)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(seq->private);
	struct brcmf_pub *drvr = bus_if->drvr;
	struct brcmf_if *ifp = brcmf_get_ifp(drvr, 0);
	char caps[MAX_CAPS_BUFFER_SIZE + 1] = { };
	char *tmp;
	int err;

	err = brcmf_fil_iovar_data_get(ifp, "cap", caps, sizeof(caps));
	if (err) {
		bphy_err(drvr, "could not get firmware cap (%d)\n", err);
		return err;
	}

	/* Put every capability in a new line */
	for (tmp = caps; *tmp; tmp++) {
		if (*tmp == ' ')
			*tmp = '\n';
	}

	/* Usually there is a space at the end of capabilities string */
	seq_printf(seq, "%s", caps);
	/* So make sure we don't print two line breaks */
	if (tmp > caps && *(tmp - 1) != '\n')
		seq_printf(seq, "\n");

	return 0;
}

void brcmf_feat_attach(struct brcmf_pub *drvr)
{
	struct brcmf_if *ifp = brcmf_get_ifp(drvr, 0);
	struct brcmf_pno_macaddr_le pfn_mac;
	struct brcmf_gscan_config gscan_cfg;
	u32 wowl_cap;
	s32 err;
	int i;

	brcmf_feat_firmware_capabilities(ifp);
	memset(&gscan_cfg, 0, sizeof(gscan_cfg));
	if (drvr->bus_if->chip != BRCM_CC_43430_CHIP_ID &&
	    drvr->bus_if->chip != BRCM_CC_4345_CHIP_ID &&
	    drvr->bus_if->chip != BRCM_CC_43454_CHIP_ID &&
	    drvr->bus_if->chip != CY_CC_43439_CHIP_ID)
		brcmf_feat_iovar_data_set(ifp, BRCMF_FEAT_GSCAN,
					  "pfn_gscan_cfg",
					  &gscan_cfg, sizeof(gscan_cfg));
	brcmf_feat_iovar_int_get(ifp, BRCMF_FEAT_PNO, "pfn");
	if (drvr->bus_if->wowl_supported)
		brcmf_feat_iovar_int_get(ifp, BRCMF_FEAT_WOWL, "wowl");
	if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_WOWL)) {
		err = brcmf_fil_iovar_int_get(ifp, "wowl_cap", &wowl_cap);
		if (!err) {
			ifp->drvr->feat_flags[BRCMF_FEAT_WOWL_ARP_ND / 8] |=
				BIT(BRCMF_FEAT_WOWL_ARP_ND % 8);
			if (wowl_cap & BRCMF_WOWL_PFN_FOUND)
				ifp->drvr->feat_flags[BRCMF_FEAT_WOWL_ND / 8] |=
					BIT(BRCMF_FEAT_WOWL_ND % 8);
			if (wowl_cap & BRCMF_WOWL_GTK_FAILURE)
				ifp->drvr->feat_flags[BRCMF_FEAT_WOWL_GTK / 8] |=
					BIT(BRCMF_FEAT_WOWL_GTK % 8);
		}
	}
	/* MBSS does not work for all chips */
	switch (drvr->bus_if->chip) {
	case BRCM_CC_4330_CHIP_ID:
	case BRCM_CC_43362_CHIP_ID:
		ifp->drvr->feat_flags[BRCMF_FEAT_MBSS / 8] &= ~BIT(BRCMF_FEAT_MBSS % 8);
		break;
	default:
		break;
	}
	brcmf_feat_iovar_int_get(ifp, BRCMF_FEAT_RSDB, "rsdb_mode");
	brcmf_feat_iovar_int_get(ifp, BRCMF_FEAT_TDLS, "tdls_enable");
	brcmf_feat_iovar_int_get(ifp, BRCMF_FEAT_MFP, "mfp");
	brcmf_feat_iovar_int_get(ifp, BRCMF_FEAT_DUMP_OBSS, "dump_obss");
	brcmf_feat_iovar_int_get(ifp, BRCMF_FEAT_SURVEY_DUMP, "cca_survey_dump");

	pfn_mac.version = BRCMF_PFN_MACADDR_CFG_VER;
	err = brcmf_fil_iovar_data_get(ifp, "pfn_macaddr", &pfn_mac,
				       sizeof(pfn_mac));
	if (!err)
		ifp->drvr->feat_flags[BRCMF_FEAT_SCAN_RANDOM_MAC / 8] |=
			BIT(BRCMF_FEAT_SCAN_RANDOM_MAC % 8);

	brcmf_feat_iovar_int_get(ifp, BRCMF_FEAT_FWSUP, "sup_wpa");
	brcmf_feat_iovar_enab_get(ifp, BRCMF_FEAT_TWT, "twt", BRCMF_TWT_CMD_ENAB);

	for (i = 0; i < BRCMF_MAX_FEATURE_BYTES; i++) {
		if (drvr->settings->feature_disable[i]) {
			brcmf_dbg(INFO, "Features: 0x%02x, disable: 0x%02x\n",
				  ifp->drvr->feat_flags[i],
				  drvr->settings->feature_disable[i]);
			ifp->drvr->feat_flags[i] &= ~drvr->settings->feature_disable[i];
		}
	}

	/* set chip related quirks */
	switch (drvr->bus_if->chip) {
	case BRCM_CC_43236_CHIP_ID:
		drvr->chip_quirks |= BIT(BRCMF_FEAT_QUIRK_AUTO_AUTH);
		break;
	case BRCM_CC_4329_CHIP_ID:
		drvr->chip_quirks |= BIT(BRCMF_FEAT_QUIRK_NEED_MPC);
		break;
	default:
		/* no quirks */
		break;
	}
}

void brcmf_feat_debugfs_create(struct brcmf_pub *drvr)
{
	brcmf_debugfs_add_entry(drvr, "features", brcmf_feat_debugfs_read);
	brcmf_debugfs_add_entry(drvr, "fwcap", brcmf_feat_fwcap_debugfs_read);
}

bool brcmf_feat_is_enabled(struct brcmf_if *ifp, enum brcmf_feat_id id)
{
	return (ifp->drvr->feat_flags[id / 8] & BIT(id % 8));
}

bool brcmf_feat_is_quirk_enabled(struct brcmf_if *ifp,
				 enum brcmf_feat_quirk quirk)
{
	return (ifp->drvr->chip_quirks & BIT(quirk));
}

bool brcmf_feat_is_6ghz_enabled(struct brcmf_if *ifp)
{
	return (!ifp->drvr->settings->disable_6ghz);
}

bool brcmf_feat_is_sdio_rxf_in_kthread(struct brcmf_pub *drvr)
{
	if (drvr)
		return drvr->settings->sdio_rxf_in_kthread_enabled;
	else
		return false;
}

bool brcmf_feat_is_offloads_enabled(struct brcmf_if *ifp)
{
	if (ifp && ifp->drvr)
		return ifp->drvr->settings->offload_prof;

	return false;
}
