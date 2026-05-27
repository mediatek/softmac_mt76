// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (C) 2026 MediaTek Inc.
 */

#include <linux/firmware.h>
#include <linux/fs.h>
#include "mt7999.h"
#include "mcu.h"
#include "mac.h"
#include "eeprom.h"
#include "mt7999_trace.h"
#include "muldf.h"

#define fw_name(_dev, name, ...)	({			\
	char *_fw;						\
	switch (mt76_chip(&(_dev)->mt76)) {			\
	case MT7999_DEVICE_ID:					\
	default:						\
		_fw = MT7999_##name;				\
		break;						\
	}							\
	_fw;							\
})

struct mt7999_firmware_hdr {
	/* Image hdr */
	u8 build_date[MT7999_BUILD_TIME_LEN];
	u8 chip_id[8];
	__le32 version;

	/* Global Description */
	u8 rsv1[4];
	__le32 subsys;
	u8 rsv2[4];
	__le32 section_num;
	u8 rsv3[48];
} __packed;

struct mt7999_firmware_section {
	__le32 type;
	__le32 offset;
	__le32 size;
	__le32 rsv[13];
} __packed;

struct slot_tbl {
	__le64 *ptr;
	dma_addr_t dma_addr;
	u16 slot_ready;
};

struct slot_entry {
	void *ptr;
	dma_addr_t dma_addr;
};

struct mbssid_sub_off {
	bool valid;
	u16 offset;
};

struct mt7999_mbssid_data {
	struct mbssid_sub_off mbssid_idx;
	struct mbssid_sub_off ntx_bss_cap;
	struct mbssid_sub_off attlm;
	bool is_cu_link;
};

#define MCU_PATCH_ADDRESS		0x200000

#define HE_PHY(p, c)			u8_get_bits(c, IEEE80211_HE_PHY_##p)
#define HE_MAC(m, c)			u8_get_bits(c, IEEE80211_HE_MAC_##m)
#define EHT_PHY(p, c)			u8_get_bits(c, IEEE80211_EHT_PHY_##p)

static bool sr_scene_detect = true;
module_param(sr_scene_detect, bool, 0644);
MODULE_PARM_DESC(sr_scene_detect, "Enable firmware scene detection algorithm");

static u8
mt7999_mcu_get_sta_nss(u16 mcs_map)
{
	u8 nss;

	for (nss = 8; nss > 0; nss--) {
		u8 nss_mcs = (mcs_map >> (2 * (nss - 1))) & 3;

		if (nss_mcs != IEEE80211_VHT_MCS_NOT_SUPPORTED)
			break;
	}

	return nss - 1;
}

static void
mt7999_mcu_set_sta_he_mcs(struct ieee80211_link_sta *link_sta,
			  struct mt7999_vif_link *link,
			  __le16 *he_mcs, u16 mcs_map)
{
	struct mt76_phy *mphy = mt76_vif_link_phy(&link->mt76);
	int nss, max_nss = link_sta->rx_nss > 3 ? 4 : link_sta->rx_nss;
	enum nl80211_band band;
	const u16 *mask;

	if (!mphy)
		return;

	band = mphy->chandef.chan->band;
	mask = link->bitrate_mask.control[band].he_mcs;

	for (nss = 0; nss < max_nss; nss++) {
		int mcs;

		switch ((mcs_map >> (2 * nss)) & 0x3) {
		case IEEE80211_HE_MCS_SUPPORT_0_11:
			mcs = GENMASK(11, 0);
			break;
		case IEEE80211_HE_MCS_SUPPORT_0_9:
			mcs = GENMASK(9, 0);
			break;
		case IEEE80211_HE_MCS_SUPPORT_0_7:
			mcs = GENMASK(7, 0);
			break;
		default:
			mcs = 0;
		}

		mcs = mcs ? fls(mcs & mask[nss]) - 1 : -1;

		switch (mcs) {
		case 0 ... 7:
			mcs = IEEE80211_HE_MCS_SUPPORT_0_7;
			break;
		case 8 ... 9:
			mcs = IEEE80211_HE_MCS_SUPPORT_0_9;
			break;
		case 10 ... 11:
			mcs = IEEE80211_HE_MCS_SUPPORT_0_11;
			break;
		default:
			mcs = IEEE80211_HE_MCS_NOT_SUPPORTED;
			break;
		}
		mcs_map &= ~(0x3 << (nss * 2));
		mcs_map |= mcs << (nss * 2);
	}

	*he_mcs = cpu_to_le16(mcs_map);
}

static void
mt7999_mcu_set_sta_vht_mcs(struct ieee80211_link_sta *link_sta,
			   __le16 *vht_mcs, const u16 *mask)
{
	u16 mcs, mcs_map = le16_to_cpu(link_sta->vht_cap.vht_mcs.rx_mcs_map);
	int nss, max_nss = link_sta->rx_nss > 3 ? 4 : link_sta->rx_nss;

	for (nss = 0; nss < max_nss; nss++, mcs_map >>= 2) {
		switch (mcs_map & 0x3) {
		case IEEE80211_VHT_MCS_SUPPORT_0_9:
			mcs = GENMASK(9, 0);
			break;
		case IEEE80211_VHT_MCS_SUPPORT_0_8:
			mcs = GENMASK(8, 0);
			break;
		case IEEE80211_VHT_MCS_SUPPORT_0_7:
			mcs = GENMASK(7, 0);
			break;
		default:
			mcs = 0;
		}

		vht_mcs[nss] = cpu_to_le16(mcs & mask[nss]);
	}
}

static void
mt7999_mcu_set_sta_ht_mcs(struct ieee80211_link_sta *link_sta,
			  u8 *ht_mcs, const u8 *mask)
{
	int nss, max_nss = link_sta->rx_nss > 3 ? 4 : link_sta->rx_nss;

	for (nss = 0; nss < max_nss; nss++)
		ht_mcs[nss] = link_sta->ht_cap.mcs.rx_mask[nss] & mask[nss];
}

static int
mt7999_mcu_parse_response(struct mt76_dev *mdev, int cmd,
			  struct sk_buff *skb, int seq)
{
	struct mt7999_mcu_rxd *rxd;
	struct mt7999_mcu_uni_event *event;
	int mcu_cmd = FIELD_GET(__MCU_CMD_FIELD_ID, cmd);
	int ret = 0;

	if (!skb) {
		dev_err(mdev->dev, "Message %08x (seq %d) timeout\n",
			cmd, seq);
#ifdef CONFIG_MULDF_HELPER
		mt76_muldf_drv_notify(mdev, CONSYS_MONITOR_CMD_TIMEOUT, true);
#endif
		return -ETIMEDOUT;
	}

	rxd = (struct mt7999_mcu_rxd *)skb->data;
	if (seq != rxd->seq)
		return -EAGAIN;

	if (!(rxd->option & MCU_UNI_CMD_EVENT))
		return -EINVAL;

	skb_pull(skb, sizeof(*rxd));
	if (rxd->eid == MCU_UNI_EVENT_RESULT) {
		event = (struct mt7999_mcu_uni_event *)skb->data;
		ret = le32_to_cpu(event->status);
		/* skip invalid event */
		if (mcu_cmd != le16_to_cpu(event->cid))
			ret = -EAGAIN;
	}

	return ret;
}

static void
mt7999_mcu_set_timeout(struct mt76_dev *mdev, int cmd)
{
	mdev->mcu.timeout = 5 * HZ;

	switch (FIELD_GET(__MCU_CMD_FIELD_ID, cmd)) {
	case MCU_UNI_CMD_EFUSE_CTRL:
	case MCU_UNI_CMD_EXT_EEPROM_CTRL:
	case MCU_UNI_CMD_BF:
	case MCU_UNI_CMD_TESTMODE_CTRL:
		mdev->mcu.timeout = 20 * HZ;
		return;
	default:
		break;
	}
}

static int
mt7999_mcu_send_message(struct mt76_dev *mdev, struct sk_buff *skb,
			int cmd, int *wait_seq)
{
	struct mt7999_dev *dev = container_of(mdev, struct mt7999_dev, mt76);
	int mcu_cmd = FIELD_GET(__MCU_CMD_FIELD_ID, cmd);
	struct mt76_connac2_mcu_uni_txd *uni_txd;
	__le32 *txd;
	u32 val;
	u8 seq;

	if (!(cmd & __MCU_CMD_FIELD_UNI))
		return -EINVAL;

	if (dev->recovery.l1_reset_last != dev->recovery.l1_reset) {
		dev_info(dev->mt76.dev,"\n%s L1 SER recovery overlap, drop message %08x.",
			 wiphy_name(dev->mt76.hw->wiphy), cmd);

		dev_kfree_skb(skb);
		return -EPERM;
	}

	mt7999_mcu_set_timeout(mdev, cmd);

	seq = ++dev->mt76.mcu.msg_seq & 0xf;
	if (!seq)
		seq = ++dev->mt76.mcu.msg_seq & 0xf;

	txd = (__le32 *)skb_push(skb, sizeof(*uni_txd));

	val = FIELD_PREP(MT_TXD0_TX_BYTES, skb->len) |
	      FIELD_PREP(MT_TXD0_PKT_FMT, MT_TX_TYPE_CMD) |
	      FIELD_PREP(MT_TXD0_Q_IDX, MT_TX_MCU_PORT_RX_Q0);
	txd[0] = cpu_to_le32(val);

	val = FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_CMD);
	txd[1] = cpu_to_le32(val);

	uni_txd = (struct mt76_connac2_mcu_uni_txd *)txd;
	uni_txd->len = cpu_to_le16(skb->len - sizeof(uni_txd->txd));
	uni_txd->cid = cpu_to_le16(mcu_cmd);
	uni_txd->s2d_index = cmd & __MCU_CMD_FIELD_WA ? MCU_S2D_H2CN : MCU_S2D_H2N;
	uni_txd->pkt_type = MCU_PKT_ID;
	uni_txd->seq = seq;

	uni_txd->option = MCU_CMD_UNI;
	if (!(cmd & __MCU_CMD_FIELD_QUERY))
		uni_txd->option |= MCU_CMD_SET;

	if (wait_seq) {
		uni_txd->option |= MCU_CMD_ACK;
		*wait_seq = seq;
	}

	mt7999_packet_log_to_host(dev, skb->data, skb->len, PKT_BIN_DEBUG_MCU, 0);
	trace_mt7999_mcu_cmd(dev, 1, uni_txd->cid, 0,
			     skb->data, skb->len);

	return mt76_tx_queue_skb_raw(dev, mdev->q_mcu[MT_MCUQ_WM], skb, 0);
}

int mt7999_mcu_sdo_cmd(struct mt7999_dev *dev, bool query, u32 a1, u32 a2, u32 a3)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
		__le32 args[3];
	} __packed req = {
		.tag = cpu_to_le16(query ? UNI_SDO_QUERY : UNI_SDO_SET),
		.len = cpu_to_le16(sizeof(req) - 4),
		.args = {
			cpu_to_le32(a1),
			cpu_to_le32(a2),
			cpu_to_le32(a3),
		},
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WA_UNI_CMD(SDO), &req,
				 sizeof(req), false);
}

struct mt7999_mcu_countdown_data {
	struct mt76_phy *mphy;
	u8 omac_idx;
};

static void
mt7999_mcu_csa_finish(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct mt7999_mcu_countdown_data *cdata = (void *)priv;
	struct mt7999_vif *mvif = (struct mt7999_vif *)vif->drv_priv;
	struct ieee80211_bss_conf *link_conf = NULL;
	unsigned long valid_links = vif->valid_links ?: BIT(0);
	unsigned int link_id;

	if (vif->type == NL80211_IFTYPE_STATION)
		return;

	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt76_vif_link *mlink =
			rcu_dereference(mvif->mt76.link[link_id]);

		if (mlink && mlink->band_idx == cdata->mphy->band_idx &&
		    mlink->omac_idx == cdata->omac_idx) {
			link_conf = rcu_dereference(vif->link_conf[link_id]);
			break;
		}
	}

	if (!link_conf || !link_conf->csa_active)
		return;

	ieee80211_csa_finish(vif, link_conf->link_id);
	/* remove CSA for affiliated links */
	valid_links = vif->valid_links & ~BIT(link_conf->link_id);
	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS)
		ieee80211_csa_finish(vif, link_id);
}

static void
mt7999_mcu_cca_finish(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct mt7999_mcu_countdown_data *cdata = (void *)priv;
	struct mt7999_vif *mvif = (struct mt7999_vif *)vif->drv_priv;
	struct ieee80211_bss_conf *link_conf = NULL;
	unsigned long valid_links = vif->valid_links ?: BIT(0);
	unsigned int link_id;

	if (vif->type == NL80211_IFTYPE_STATION)
		return;

	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt76_vif_link *mlink =
			rcu_dereference(mvif->mt76.link[link_id]);

		if (mlink && mlink->band_idx == cdata->mphy->band_idx &&
		    mlink->omac_idx == cdata->omac_idx) {
			link_conf = rcu_dereference(vif->link_conf[link_id]);
			break;
		}
	}

	if (!link_conf || !link_conf->color_change_active)
		return;

	ieee80211_color_change_finish(vif, link_conf->link_id);
}

static void
mt7999_mcu_ie_countdown(struct mt7999_dev *dev, struct sk_buff *skb)
{
#define UNI_EVENT_IE_COUNTDOWN_CSA 0
#define UNI_EVENT_IE_COUNTDOWN_BCC 1
	struct header {
		u8 band;
		u8 rsv[3];
	};
	struct mt7999_mcu_rxd *rxd = (struct mt7999_mcu_rxd *)skb->data;
	const char *data = (char *)&rxd[1], *tail;
	struct header *hdr = (struct header *)data;
	struct tlv *tlv = (struct tlv *)(data + 4);
	struct mt7999_mcu_countdown_notify *event;
	struct mt7999_mcu_countdown_data cdata;

	if (hdr->band >= ARRAY_SIZE(dev->mt76.phys))
		return;

	cdata.mphy = dev->mt76.phys[hdr->band];
	if (!cdata.mphy)
		return;

	tail = skb->data + skb->len;
	data += sizeof(*hdr);
	while (data + sizeof(*tlv) < tail && le16_to_cpu(tlv->len)) {
		event = (struct mt7999_mcu_countdown_notify *)tlv->data;

		cdata.omac_idx = event->omac_idx;

		switch (le16_to_cpu(tlv->tag)) {
		case UNI_EVENT_IE_COUNTDOWN_CSA:
			ieee80211_iterate_active_interfaces_atomic(mt76_hw(dev),
					IEEE80211_IFACE_ITER_RESUME_ALL,
					mt7999_mcu_csa_finish, &cdata);
			break;
		case UNI_EVENT_IE_COUNTDOWN_BCC:
			ieee80211_iterate_active_interfaces_atomic(mt76_hw(dev),
					IEEE80211_IFACE_ITER_RESUME_ALL,
					mt7999_mcu_cca_finish, &cdata);
			break;
		default:
			break;
		}

		data += le16_to_cpu(tlv->len);
		tlv = (struct tlv *)data;
	}
}

static void
mt7999_mcu_rx_radar_detected(struct mt7999_dev *dev, struct sk_buff *skb)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7999_mcu_rdd_report *r;
	struct mt7999_phy *phy;

	r = (struct mt7999_mcu_rdd_report *)skb->data;

	switch (r->rdd_idx) {
	case MT_RDD_IDX_BAND2:
		mphy = dev->mt76.phys[MT_BAND2];
		break;
	case MT_RDD_IDX_BAND1:
		mphy = dev->mt76.phys[MT_BAND1];
		break;
	case MT_RDD_IDX_BACKGROUND:
		if (!dev->rdd2_phy)
			goto err;
		mphy = dev->rdd2_phy->mt76;
		break;
	default:
		goto err;
	}

	if (!mphy)
		goto err;

	phy = mphy->priv;

#ifdef CONFIG_MTK_DEBUG
	mt7999_mcu_dump_radar_pulse(phy, r);
#endif

	if (!r->long_detected && !r->constant_prf_detected &&
	    !r->staggered_prf_detected && !phy->rdd_emulate) {
		dev_err(dev->mt76.dev, "Unknown pulse\n");
		phy->rdd_emulate = false;
		return;
	}

	if (r->rdd_idx == MT_RDD_IDX_BACKGROUND) {
		dev->bg_nxt_freq = 0;
		cfg80211_background_radar_event(mphy->hw->wiphy,
						&dev->rdd2_chandef,
						GFP_ATOMIC);
	} else {
		struct mt7999_phy *phy = mphy->priv;

		phy->rdd_tx_paused = true;
		ieee80211_radar_detected(mphy->hw, NULL);
	}
	dev->hw_pattern++;
	phy->rdd_emulate = false;

	dev_info(dev->mt76.dev, "radar detected on %s\n",
		 r->rdd_idx == MT_RDD_IDX_BACKGROUND ?
		 "background radar" : "main radio (pause TXQ until csa is finished)");
	return;

err:
	dev_err(dev->mt76.dev, "Invalid RDD idx %d\n", r->rdd_idx);
}

static void
mt7999_mcu_rx_log_message(struct mt7999_dev *dev, struct sk_buff *skb)
{
#define UNI_EVENT_FW_LOG_FORMAT 0
#define UNI_EVENT_FW_LOG_MEMORY	1
	struct mt7999_mcu_rxd *rxd = (struct mt7999_mcu_rxd *)skb->data;
	const char *data = (char *)&rxd[1] + 4, *type;
	struct tlv *tlv = (struct tlv *)data;
	int len;

	if (!(rxd->option & MCU_UNI_CMD_EVENT)) {
		len = skb->len - sizeof(*rxd);
		data = (char *)&rxd[1];
		goto out;
	}

	if (le16_to_cpu(tlv->tag) != UNI_EVENT_FW_LOG_FORMAT &&
	    le16_to_cpu(tlv->tag) != UNI_EVENT_FW_LOG_MEMORY)
		return;

	data += sizeof(*tlv) + 4;
	len = le16_to_cpu(tlv->len) - sizeof(*tlv) - 4;

out:
	switch (rxd->s2d_index) {
	case 0:
		if (mt7999_debugfs_rx_log(dev, data, len))
			return;

		type = "WM";
		break;
	case 2:
		type = "WA";
		break;
	default:
		type = "unknown";
		break;
	}

	wiphy_info(mt76_hw(dev)->wiphy, "%s: %.*s", type, len, data);
}

static int
mt7999_mcu_update_rate(struct rate_info *rate, struct ieee80211_supported_band *sband,
		       u8 mode, u8 bw, u8 mcs, u8 nss, u8 stbc, u8 gi)
{
	struct rate_info tmp_rate = {};
	int ret = 0;

	tmp_rate.mcs = mcs;
	tmp_rate.nss = (stbc && nss > 1) ? nss / 2 : nss;

	switch (mode) {
	case MT_PHY_TYPE_CCK:
	case MT_PHY_TYPE_OFDM:
		if (mcs >= sband->n_bitrates) {
			ret = -EINVAL;
			goto out;
		}

		tmp_rate.legacy = sband->bitrates[mcs].bitrate;
		break;
	case MT_PHY_TYPE_HT:
	case MT_PHY_TYPE_HT_GF:
		if (mcs > 31) {
			ret = -EINVAL;
			goto out;
		}

		tmp_rate.flags |= RATE_INFO_FLAGS_MCS;
		if (gi)
			tmp_rate.flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case MT_PHY_TYPE_VHT:
		/* FW will use VHT 1024-QAM if the peer can receive */
		if (mcs > 11) {
			ret = -EINVAL;
			goto out;
		}

		tmp_rate.flags |= RATE_INFO_FLAGS_VHT_MCS;
		if (gi)
			tmp_rate.flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case MT_PHY_TYPE_PLR:
		break;
	case MT_PHY_TYPE_HE_SU:
	case MT_PHY_TYPE_HE_EXT_SU:
	case MT_PHY_TYPE_HE_TB:
	case MT_PHY_TYPE_HE_MU:
		tmp_rate.mcs = mcs & CMDRPT_TX_RATE_HE;
		if (tmp_rate.mcs > 13 || gi > NL80211_RATE_INFO_HE_GI_3_2) {
			ret = -EINVAL;
			goto out;
		}

		tmp_rate.flags |= RATE_INFO_FLAGS_HE_MCS;
		tmp_rate.he_gi = gi;
		tmp_rate.he_dcm = mcs & CMDRPT_TX_RATE_HE_DCM;
		break;
	case MT_PHY_TYPE_EHT_SU:
	case MT_PHY_TYPE_EHT_TRIG:
	case MT_PHY_TYPE_EHT_MU:
		tmp_rate.mcs = mcs & CMDRPT_TX_RATE_EHT;
		if (tmp_rate.mcs > 15 || gi > NL80211_RATE_INFO_EHT_GI_3_2) {
			ret = -EINVAL;
			goto out;
		}

		tmp_rate.flags |= RATE_INFO_FLAGS_EHT_MCS;
		tmp_rate.eht_gi = gi;
		break;
	case MT_PHY_TYPE_UNKNOWN:
		return 0;
	default:
		ret = -EINVAL;
		goto out;
	}

	switch (bw) {
	case IEEE80211_STA_RX_BW_20:
		tmp_rate.bw = RATE_INFO_BW_20;
		break;
	case IEEE80211_STA_RX_BW_40:
		tmp_rate.bw = RATE_INFO_BW_40;
		break;
	case IEEE80211_STA_RX_BW_80:
		tmp_rate.bw = RATE_INFO_BW_80;
		break;
	case IEEE80211_STA_RX_BW_160:
		tmp_rate.bw = RATE_INFO_BW_160;
		break;
	case IEEE80211_STA_RX_BW_320:
		tmp_rate.bw = RATE_INFO_BW_320;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	if (mode == MT_PHY_TYPE_HE_EXT_SU && mcs & PRXV_TX_RATE_HE_ER_SU_106T) {
		tmp_rate.bw = RATE_INFO_BW_HE_RU;
		tmp_rate.he_ru_alloc = NL80211_RATE_INFO_HE_RU_ALLOC_106;
	}
	*rate = tmp_rate;

out:
	if (ret)
		pr_err("Invalid rate (mode=%u nss=%u mcs=%u gi=%u bw=%u)",
		       mode, nss, mcs, gi, bw);

	return ret;
}

static int
mt7999_mcu_update_trx_rates(struct mt7999_dev *dev, struct mt76_wcid *wcid,
			    struct all_sta_trx_rate *mcu_rate)
{
	struct mt76_phy *phy = mt76_dev_phy(&dev->mt76, wcid->phy_idx);
	struct ieee80211_supported_band *sband = NULL;
	int ret;

	/* TX rate */
	switch (mcu_rate->tx_mode) {
	case MT_PHY_TYPE_CCK:
	case MT_PHY_TYPE_OFDM:
		if (phy->chandef.chan->band == NL80211_BAND_2GHZ)
			sband = &phy->sband_2g.sband;
		else if (phy->chandef.chan->band == NL80211_BAND_5GHZ)
			sband = &phy->sband_5g.sband;
		else
			sband = &phy->sband_6g.sband;

		mcu_rate->tx_mcs = mt76_get_rate(&dev->mt76, sband, mcu_rate->tx_mcs,
						 mcu_rate->tx_mode == MT_PHY_TYPE_CCK);
		break;
	default:
		break;
	}

	ret = mt7999_mcu_update_rate(&wcid->rate, sband, mcu_rate->tx_mode,
				     mcu_rate->tx_bw, mcu_rate->tx_mcs,
				     mcu_rate->tx_nss, mcu_rate->tx_stbc,
				     mcu_rate->tx_gi);
	if (ret) {
		dev_err(dev->mt76.dev, "Failed to update TX rate.\n");
		return ret;
	}

	/* RX rate */
	switch (mcu_rate->rx_mode) {
	case MT_PHY_TYPE_CCK:
	case MT_PHY_TYPE_OFDM:
		if (phy->chandef.chan->band == NL80211_BAND_2GHZ)
			sband = &phy->sband_2g.sband;
		else if (phy->chandef.chan->band == NL80211_BAND_5GHZ)
			sband = &phy->sband_5g.sband;
		else
			sband = &phy->sband_6g.sband;

		mcu_rate->rx_rate = mt76_get_rate(&dev->mt76, sband, mcu_rate->rx_rate,
						  mcu_rate->rx_mode == MT_PHY_TYPE_CCK);
		break;
	default:
		break;
	}

	ret = mt7999_mcu_update_rate(&wcid->rx_rate, sband, mcu_rate->rx_mode,
				     mcu_rate->rx_bw, mcu_rate->rx_rate,
				     mcu_rate->rx_nsts + 1, mcu_rate->rx_stbc,
				     mcu_rate->rx_gi);
	if (ret)
		dev_err(dev->mt76.dev, "Failed to update RX rate.\n");

	return ret;
}

static inline void __mt7999_stat_to_netdev(struct mt76_phy *mphy,
					   struct mt76_wcid *wcid,
					   u32 tx_bytes, u32 rx_bytes,
					   u32 tx_packets, u32 rx_packets)
{
	struct mt7999_sta_link *msta_link;
	struct ieee80211_vif *vif;
	struct wireless_dev *wdev;

	if (wiphy_ext_feature_isset(mphy->hw->wiphy,
				    NL80211_EXT_FEATURE_STAS_COUNT)) {
		msta_link = container_of(wcid, struct mt7999_sta_link, wcid);
		vif = container_of((void *)msta_link->sta->vif, struct ieee80211_vif,
				   drv_priv);
		wdev = ieee80211_vif_to_wdev(vif);

		if (vif->type == NL80211_IFTYPE_MONITOR)
			return;

		dev_sw_netstats_tx_add(wdev->netdev, tx_packets, tx_bytes);
		__dev_sw_netstats_rx_add(wdev->netdev, rx_packets, rx_bytes);
	}
}
#define TRX_PACKETS 0
#define TRX_BYTES 1
static void mt7999_update_txop_stats(struct mt7999_dev *dev, u8 type,
				     struct mt76_wcid *wcid, u32 tx, u32 rx)
{
	struct mt7999_sta_link *msta_link = container_of(wcid, struct mt7999_sta_link, wcid);
	struct ieee80211_sta *sta = wcid_to_sta(wcid);
	struct ieee80211_vif *vif;
	unsigned long valid_links;
	struct mt7999_sta *msta;
	unsigned int link_id;
	u8 wmm_idx;

	if (!sta || !msta_link->sta || !msta_link->sta->vif)
		return;

	msta = (struct mt7999_sta *)sta->drv_priv;
	vif = container_of((void *)msta_link->sta->vif, struct ieee80211_vif, drv_priv);
	wmm_idx = vif->type == NL80211_IFTYPE_AP ? 0 : 3;
	valid_links = sta->valid_links ?: BIT(0);

	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt7999_sta_link *msta_link_temp;
		struct mt7999_phy *phy;

		msta_link_temp = mt7999_sta_link(msta, link_id);
		if (!msta_link_temp)
			continue;

		phy = __mt7999_phy(dev, msta_link_temp->wcid.phy_idx);

		if (!phy)
			continue;

		if (type == TRX_BYTES) {
			phy->tx_bytes[wmm_idx] += tx;
			phy->rx_bytes[wmm_idx] += rx;
		} else if (type == TRX_PACKETS) {
			phy->tx_packets[wmm_idx] += tx;
			phy->rx_packets[wmm_idx] += rx;
		}
	}
}

static void
mt7999_mcu_rx_all_sta_info_event(struct mt7999_dev *dev, struct sk_buff *skb)
{
	struct mt7999_mcu_all_sta_info_event *res;
	u16 i;

	skb_pull(skb, sizeof(struct mt7999_mcu_rxd));

	res = (struct mt7999_mcu_all_sta_info_event *)skb->data;

	for (i = 0; i < le16_to_cpu(res->sta_num); i++) {
		u8 ac;
		u16 wlan_idx;
		struct mt76_wcid *wcid;
		struct mt76_phy *mphy;
		struct ieee80211_sta *sta;
		u32 tx_bytes, rx_bytes, tx_bytes_failed = 0, tx_airtime, rx_airtime,
		    tx_packets, rx_packets;

		switch (le16_to_cpu(res->tag)) {
		case UNI_ALL_STA_TXRX_RATE:
			wlan_idx = le16_to_cpu(res->rate[i].wlan_idx);
			wcid = mt76_wcid_ptr(dev, wlan_idx);
			if (!wcid)
				break;

			mt7999_mcu_update_trx_rates(dev, wcid, &res->rate[i]);
			break;
		case UNI_ALL_STA_TX_STAT:
			wlan_idx = le16_to_cpu(res->tx_mpdu_stat[i].wlan_idx);
			wcid = mt76_wcid_ptr(dev, wlan_idx);
			if (!wcid)
				break;

			wcid->stats.tx_mpdus += le32_to_cpu(res->tx_mpdu_stat[i].total);
			wcid->stats.tx_failed += le32_to_cpu(res->tx_mpdu_stat[i].fails);
			break;
		case UNI_ALL_STA_TXRX_ADM_STAT:
			wlan_idx = le16_to_cpu(res->adm_stat[i].wlan_idx);
			wcid = mt76_wcid_ptr(dev, wlan_idx);
			if (!wcid)
				break;

			mphy = mt76_dev_phy(&dev->mt76, wcid->phy_idx);
			for (ac = IEEE80211_AC_VO; ac < IEEE80211_NUM_ACS; ac++) {
				u8 lmac_ac = mt76_connac_lmac_mapping(ac);

				tx_bytes = le32_to_cpu(res->adm_stat[i].tx_bytes[lmac_ac]);
				rx_bytes = le32_to_cpu(res->adm_stat[i].rx_bytes[lmac_ac]);
				tx_bytes_failed =
					le32_to_cpu(res->adm_stat[i].tx_bytes_failed[lmac_ac]);

				wcid->stats.tx_bytes_per_ac[ac] += tx_bytes;
				wcid->stats.rx_bytes_per_ac[ac] += rx_bytes;
				wcid->stats.tx_bytes_failed_per_ac[ac] += tx_bytes_failed;

				wcid->stats.tx_bytes += tx_bytes;
				wcid->stats.rx_bytes += rx_bytes;
				wcid->stats.tx_bytes_failed += tx_bytes_failed;

				__mt7999_stat_to_netdev(mphy, wcid,
							tx_bytes, rx_bytes, 0, 0);
				mt7999_update_txop_stats(dev, TRX_BYTES, wcid,
							 tx_bytes, rx_bytes);

				ieee80211_tpt_led_trig_tx(mphy->hw, tx_bytes);
				ieee80211_tpt_led_trig_rx(mphy->hw, rx_bytes);
			}
			break;
		case UNI_ALL_STA_TXRX_AIR_TIME:
			wlan_idx = le16_to_cpu(res->airtime[i].wlan_idx);
			wcid = mt76_wcid_ptr(dev, wlan_idx);
			sta = wcid_to_sta(wcid);
			if (!sta)
				continue;

			for (ac = IEEE80211_AC_VO; ac < IEEE80211_NUM_ACS; ++ac) {
				u8 lmac_ac = mt76_connac_lmac_mapping(ac);
				tx_airtime = le32_to_cpu(res->airtime[i].tx[lmac_ac]);
				rx_airtime = le32_to_cpu(res->airtime[i].rx[lmac_ac]);

				wcid->stats.tx_airtime += tx_airtime;
				wcid->stats.rx_airtime += rx_airtime;
				ieee80211_sta_register_airtime(sta, mt76_ac_to_tid(ac),
				                               tx_airtime, rx_airtime);
			}
			break;
		/* Multiple retries of a single MPDU only increase the counter by 1 */
		case UNI_ALL_STA_DATA_TX_RETRY_COUNT:
			wlan_idx = le16_to_cpu(res->tx_mpdu_retry_cnt[i].wlan_idx);
			wcid = mt76_wcid_ptr(dev, wlan_idx);
			if (!wcid)
				break;

			wcid->stats.tx_retries += le16_to_cpu(res->tx_mpdu_retry_cnt[i].val);
			break;
		case UNI_ALL_STA_TXRX_MSDU_COUNT:
			wlan_idx = le16_to_cpu(res->msdu_cnt[i].wlan_idx);
			wcid = mt76_wcid_ptr(dev, wlan_idx);
			if (!wcid)
				break;

			mphy = mt76_dev_phy(&dev->mt76, wcid->phy_idx);

			tx_packets = le32_to_cpu(res->msdu_cnt[i].tx_msdu_cnt);
			rx_packets = le32_to_cpu(res->msdu_cnt[i].rx_msdu_cnt);

			wcid->stats.tx_packets += tx_packets;
			wcid->stats.rx_packets += rx_packets;

			__mt7999_stat_to_netdev(mphy, wcid, 0, 0,
						tx_packets, rx_packets);
			mt7999_update_txop_stats(dev, TRX_PACKETS, wcid,
						 tx_packets, rx_packets);
			break;
		case UNI_ALL_STA_RX_MPDU_COUNT:
			wlan_idx = le16_to_cpu(res->rx_mpdu_cnt[i].wlan_idx);
			wcid = mt76_wcid_ptr(dev, wlan_idx);
			if (!wcid)
				break;

			wcid->stats.rx_mpdus += le32_to_cpu(res->rx_mpdu_cnt[i].total);
			wcid->stats.rx_fcs_err += le32_to_cpu(res->rx_mpdu_cnt[i].total) -
						  le32_to_cpu(res->rx_mpdu_cnt[i].success);
			break;
		default:
			break;
		}
	}
}

static int
csi_integrate_segment_data(struct mt7999_phy *phy, struct csi_data *csi)
{
	struct csi_data *csi_temp = NULL;

	if (csi->segment_num == 0 && csi->remain_last == 0)
		return CSI_CHAIN_COMPLETE;
	else if (csi->segment_num == 0 && csi->remain_last == 1) {
		memcpy(&phy->csi.buffered_csi,
		       csi, sizeof(struct csi_data));

		return CSI_CHAIN_SEGMENT_FIRST;
	} else if (csi->segment_num != 0) {
		csi_temp = &phy->csi.buffered_csi;
		if (csi->chain_info != csi_temp->chain_info ||
		csi->segment_num != (csi_temp->segment_num + 1))
			return CSI_CHAIN_SEGMENT_ERR;

		memcpy(&csi_temp->data_i[csi_temp->data_num],
		       csi->data_i, csi->data_num * sizeof(s16));

		memcpy(&csi_temp->data_q[csi_temp->data_num],
		       csi->data_q, csi->data_num * sizeof(s16));

		csi_temp->data_num += csi->data_num;
		csi_temp->segment_num = csi->segment_num;
		csi_temp->remain_last = csi->remain_last;

		if (csi->remain_last == 0)
			return CSI_CHAIN_SEGMENT_LAST;
		else if (csi->remain_last == 1)
			return CSI_CHAIN_SEGMENT_MIDDLE;
	}

	return CSI_CHAIN_ERR;
}

static int
mt7999_mcu_csi_report_data(struct mt7999_phy *phy, u8 *tlv_buf, u32 len)
{
	int ret, i;
	struct csi_data *current_csi;
	struct csi_data *target_csi;
	struct csi_tlv *tlv_data;
	u8 *buf_tmp;
	u32 rx_info, tx_rx_idx;
	u32 buf_len_last, offset;

	buf_tmp = tlv_buf;
	buf_len_last = len;
	offset = sizeof(((struct csi_tlv *)0)->basic);

	current_csi = kzalloc(sizeof(*current_csi), GFP_KERNEL);
	if (!current_csi)
		return -ENOMEM;

	while (buf_len_last >= offset) {
		u32 tag, len;
		s16 *data_tmp = NULL;

		tlv_data = (struct csi_tlv *)buf_tmp;
		tag = le32_to_cpu(tlv_data->basic.tag);
		len = le32_to_cpu(tlv_data->basic.len);

		switch (tag) {
		case CSI_EVENT_FW_VER:
			current_csi->fw_ver = le32_to_cpu(tlv_data->info);
			break;
		case CSI_EVENT_CBW:
			current_csi->ch_bw = le32_to_cpu(tlv_data->info);
			break;
		case CSI_EVENT_RSSI:
			current_csi->rssi = le32_to_cpu(tlv_data->info);
			break;
		case CSI_EVENT_SNR:
			current_csi->snr = le32_to_cpu(tlv_data->info);
			break;
		case CSI_EVENT_BAND:
			current_csi->band = le32_to_cpu(tlv_data->info);

			if (current_csi->band != phy->mt76->band_idx) {
				kfree(current_csi);
				return -EINVAL;
			}

			break;
		case CSI_EVENT_CSI_NUM:
			current_csi->data_num = le32_to_cpu(tlv_data->info);

			if (current_csi->data_num > CSI_BW80_DATA_COUNT) {
				kfree(current_csi);
				return -EINVAL;
			}

			break;
		case CSI_EVENT_CSI_I_DATA:
			if (len != sizeof(s16) * current_csi->data_num) {
				kfree(current_csi);
				return -EINVAL;
			}

			data_tmp = tlv_data->data;
			for (i = 0; i < current_csi->data_num; i++)
				current_csi->data_i[i] = le16_to_cpu(*(data_tmp + i));
			break;
		case CSI_EVENT_CSI_Q_DATA:
			if (len != sizeof(s16) * current_csi->data_num) {
				kfree(current_csi);
				return -EINVAL;
			}

			data_tmp = tlv_data->data;
			for (i = 0; i < current_csi->data_num; i++)
				current_csi->data_q[i] = le16_to_cpu(*(data_tmp + i));
			break;
		case CSI_EVENT_DBW:
			current_csi->data_bw = le32_to_cpu(tlv_data->info);
			break;
		case CSI_EVENT_CH_IDX:
			current_csi->pri_ch_idx = le32_to_cpu(tlv_data->info);
			break;
		case CSI_EVENT_TA:
			memcpy(current_csi->ta, tlv_data->mac, ETH_ALEN);
			break;
		case CSI_EVENT_EXTRA_INFO:
			current_csi->ext_info = le32_to_cpu(tlv_data->info);
			break;
		case CSI_EVENT_RX_MODE:
			rx_info = le32_to_cpu(tlv_data->info);
			current_csi->rx_mode = u32_get_bits(rx_info, GENMASK(15, 0));
			current_csi->rx_rate = u32_get_bits(rx_info, GENMASK(31, 16));
			break;
		case CSI_EVENT_H_IDX:
			current_csi->chain_info = le32_to_cpu(tlv_data->info);
			break;
		case CSI_EVENT_TX_RX_IDX:
			tx_rx_idx = le32_to_cpu(tlv_data->info);
			current_csi->tx_idx = u32_get_bits(tx_rx_idx, GENMASK(31, 16));
			current_csi->rx_idx = u32_get_bits(tx_rx_idx, GENMASK(15, 0));
			break;
		case CSI_EVENT_TS:
			current_csi->ts = le32_to_cpu(tlv_data->info);

			if (phy->csi.interval &&
				current_csi->ts < phy->csi.last_record + phy->csi.interval) {
				kfree(current_csi);
				return 0;
			}

			break;
		case CSI_EVENT_PKT_SN:
			current_csi->pkt_sn = le32_to_cpu(tlv_data->info);
			break;
		case CSI_EVENT_BW_SEG:
			current_csi->segment_num = le32_to_cpu(tlv_data->info);
			break;
		case CSI_EVENT_REMAIN_LAST:
			current_csi->remain_last = le32_to_cpu(tlv_data->info);
			break;
		case CSI_EVENT_TR_STREAM:
			current_csi->tr_stream = le32_to_cpu(tlv_data->info);
			break;
		default:
			break;
		};

		buf_len_last -= (offset + len);

		if (buf_len_last >= offset)
			buf_tmp += (offset + len);
	}

	/* integret the bw80 segment */
	if (current_csi->ch_bw >= CSI_BW80) {
		ret = csi_integrate_segment_data(phy, current_csi);

		switch (ret) {
		case CSI_CHAIN_ERR:
		case CSI_CHAIN_SEGMENT_ERR:
			kfree(current_csi);
			return -EINVAL;
			break;
		case CSI_CHAIN_SEGMENT_FIRST:
		case CSI_CHAIN_SEGMENT_MIDDLE:
			kfree(current_csi);
			return 0;
			break;
		case CSI_CHAIN_COMPLETE:
			target_csi = current_csi;
			break;
		case CSI_CHAIN_SEGMENT_LAST:
			target_csi = current_csi;
			memcpy(target_csi, &phy->csi.buffered_csi, sizeof(struct csi_data));
			memset(&phy->csi.buffered_csi, 0, sizeof(struct csi_data));
			break;
		default:
			break;
		}
	} else {
		target_csi = current_csi;
	}

	/* put the csi data into list */
	INIT_LIST_HEAD(&target_csi->node);
	spin_lock_bh(&phy->csi.lock);

	if (!phy->csi.enable) {
		kfree(target_csi);
		goto out;
	}

	list_add_tail(&target_csi->node, &phy->csi.list);
	phy->csi.count++;

	if (phy->csi.count > CSI_MAX_BUF_NUM) {
		struct csi_data *old;

		old = list_first_entry(&phy->csi.list,
				       struct csi_data, node);

		list_del(&old->node);
		kfree(old);
		phy->csi.count--;
	}

	if (target_csi->chain_info & BIT(15)) /* last chain */
		phy->csi.last_record = target_csi->ts;

out:
	spin_unlock_bh(&phy->csi.lock);
	return 0;
}

static void
mt7999_mcu_csi_report_event(struct mt7999_dev *dev, struct sk_buff *skb)
{
	struct mt7999_mcu_csi_event *event;
	struct mt76_phy *mphy;
	struct mt7999_phy *phy;

	event = (struct mt7999_mcu_csi_event *)skb->data;

	mphy = dev->mt76.phys[event->band_idx];
	if (!mphy)
		return;

	phy = mphy->priv;

	switch (le16_to_cpu(event->tag)) {
	case UNI_EVENT_CSI_DATA:
		mt7999_mcu_csi_report_data(phy, event->tlv_buf, le16_to_cpu(event->len) - 4);
		break;
	default:
		break;
	}
}

static void
mt7999_mcu_rx_thermal_info_query(struct mt7999_dev *dev, struct sk_buff *skb)
{
#define THERMAL_QUERY_TAG 0x3
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7999_mcu_thermal_query_info *n;
	struct mt7999_phy *phy;

	n = (struct mt7999_mcu_thermal_query_info *)skb->data;

	if (le16_to_cpu(n->tag) != THERMAL_QUERY_TAG)
		return;

	if (n->event_id != THERMAL_QUERY_TAG)
		return;

	if (n->band_idx > MT_BAND2)
		return;

	mphy = dev->mt76.phys[n->band_idx];
	if (!mphy)
		return;
	phy = (struct mt7999_phy *)mphy->priv;
	phy->throttle_state = phy->throttle_level[n->state];
}

void mt7999_mcu_wmm_pbc_work(struct work_struct *work)
{
#define WMM_PBC_QUEUE_NUM	5
#define WMM_PBC_BSS_ALL		0xff
#define WMM_PBC_WLAN_IDX_ALL	0xffff
#define WMM_PBC_BOUND_DEFAULT	0xffff
	struct mt7999_dev *dev = container_of(work, struct mt7999_dev, wmm_pbc_work);

	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		u8 bss_idx;
		u8 queue_num;
		__le16 wlan_idx;
		u8 band_idx;
		u8 __rsv[3];
		struct {
			__le16 low;
			__le16 up;
		} __packed bound[WMM_PBC_QUEUE_NUM];
	} __packed req = {
		.tag = cpu_to_le16(UNI_SDO_PKT_BUDGET_CTRL_CFG),
		.len = cpu_to_le16(sizeof(req) - 4),
		.bss_idx = WMM_PBC_BSS_ALL,
		.queue_num = WMM_PBC_QUEUE_NUM,
		.wlan_idx = cpu_to_le16(WMM_PBC_WLAN_IDX_ALL),
		.band_idx = dev->mphy.band_idx,
	};
	int i, ret;

	for (i = 0; i < WMM_PBC_QUEUE_NUM; ++i) {
		req.bound[i].low = cpu_to_le16(dev->wmm_pbc_enable ?
					       dev->pbc_lower_bound[i] : 0);
		req.bound[i].up = cpu_to_le16(dev->wmm_pbc_enable ?
					      dev->pbc_upper_bound[i] :
					      WMM_PBC_BOUND_DEFAULT);
	}

	ret = mt76_mcu_send_msg(&dev->mt76, MCU_WA_UNI_CMD(SDO),
				&req, sizeof(req), false);
	if (ret)
		dev_err(dev->mt76.dev, "Failed to configure WMM PBC.\n");
}

int mt7999_mcu_ba_trigger_enable(struct mt7999_dev *dev, u8 enable)
{
#define MT7999_BA_TIMEOUT 1000
	struct {
		u8 rsv[4];
		__le16 tag;
		__le16 len;

		u8 enable;
		u8 target;
		u8 rsv2[2];
		__le32 timeout;
	} __packed req = {
		.tag = cpu_to_le16(UNI_SDO_AUTO_BA),
		.len = cpu_to_le16(sizeof(req) - 4),
		.enable = enable,
		.target = 0,
		.timeout = cpu_to_le32(MT7999_BA_TIMEOUT),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WA_UNI_CMD(SDO),
				 &req, sizeof(req), false);
}

static void
mt7999_mcu_wed_rro_event(struct mt7999_dev *dev, struct sk_buff *skb)
{
	struct mt7999_mcu_wed_rro_event *event = (void *)skb->data;

	if (!mt7999_has_hwrro(dev))
		return;

	skb_pull(skb, sizeof(struct mt7999_mcu_rxd) + 4);

	switch (le16_to_cpu(event->tag)) {
	case UNI_EVENT_WED_RRO_BA_SESSION_STATUS: {
		struct mt7999_mcu_wed_rro_ba_event *e;

		while (skb->len >= sizeof(*e)) {
			struct mt76_rx_tid *tid;
			struct mt76_wcid *wcid;
			u16 idx;

			e = (void *)skb->data;
			idx = le16_to_cpu(e->wlan_id);
			wcid = mt76_wcid_ptr(dev, idx);
			if (!wcid || !wcid->sta)
				break;

			if (e->tid >= ARRAY_SIZE(wcid->aggr))
				break;

			tid = rcu_dereference(wcid->aggr[e->tid]);
			if (!tid)
				break;

			tid->id = le16_to_cpu(e->id);
			skb_pull(skb, sizeof(*e));
		}
		break;
	}
	case UNI_EVENT_WED_RRO_BA_SESSION_DELETE: {
		struct mt7999_mcu_wed_rro_ba_delete_event *e;

		while (skb->len >= sizeof(*e)) {
			struct mt7999_wed_rro_session_id *session;

			e = (void *)skb->data;
			session = kzalloc(sizeof(*session), GFP_ATOMIC);
			if (!session)
				break;

			session->id = le16_to_cpu(e->session_id);

			spin_lock_bh(&dev->wed_rro.lock);
			list_add_tail(&session->list, &dev->wed_rro.poll_list);
			spin_unlock_bh(&dev->wed_rro.lock);

			ieee80211_queue_work(mt76_hw(dev), &dev->wed_rro.work);
			skb_pull(skb, sizeof(*e));
		}
		break;
	}
	default:
		break;
	}
}

static void
mt7999_dump_pp_statistic_event(struct mt7999_dev *dev,
			       struct mt7999_mcu_pp_alg_ctrl_event *event)
{
	u32 unit_time = le32_to_cpu(event->pp_timer_intv);

	dev_info(dev->mt76.dev, "band idx = %u\n", le32_to_cpu(event->band_idx));
	dev_info(dev->mt76.dev, "x2 value = %u\n", le32_to_cpu(event->thr_x2_value));
	dev_info(dev->mt76.dev, "x2 shift = %u\n", le32_to_cpu(event->thr_x2_shift));
	dev_info(dev->mt76.dev, "x3 value = %u\n", le32_to_cpu(event->thr_x3_value));
	dev_info(dev->mt76.dev, "x3 shift = %u\n", le32_to_cpu(event->thr_x3_shift));
	dev_info(dev->mt76.dev, "x4 value = %u\n", le32_to_cpu(event->thr_x4_value));
	dev_info(dev->mt76.dev, "x4 shift = %u\n", le32_to_cpu(event->thr_x4_shift));
	dev_info(dev->mt76.dev, "x5 value = %u\n", le32_to_cpu(event->thr_x5_value));
	dev_info(dev->mt76.dev, "x5 shift = %u\n", le32_to_cpu(event->thr_x5_shift));
	dev_info(dev->mt76.dev, "x6 value = %u\n", le32_to_cpu(event->thr_x6_value));
	dev_info(dev->mt76.dev, "x6 shift = %u\n", le32_to_cpu(event->thr_x6_shift));
	dev_info(dev->mt76.dev, "x7 value = %u\n", le32_to_cpu(event->thr_x7_value));
	dev_info(dev->mt76.dev, "x7 shift = %u\n", le32_to_cpu(event->thr_x7_shift));
	dev_info(dev->mt76.dev, "x8 value = %u\n", le32_to_cpu(event->thr_x8_value));
	dev_info(dev->mt76.dev, "x8 shift = %u\n", le32_to_cpu(event->thr_x8_shift));
	dev_info(dev->mt76.dev, "sw_pp_time = %u (Unit: %u ms)\n",
		 le32_to_cpu(event->sw_pp_time), unit_time);
	dev_info(dev->mt76.dev, "hw_pp_time = %u (Unit: %u ms)\n",
		 le32_to_cpu(event->hw_pp_time), unit_time);
	dev_info(dev->mt76.dev, "no_pp_time = %u (Unit: %u ms)\n",
		 le32_to_cpu(event->no_pp_time), unit_time);
	dev_info(dev->mt76.dev, "auto_bw_time = %u (Unit: %u ms)\n",
		 le32_to_cpu(event->auto_bw_time), unit_time);
	dev_info(dev->mt76.dev, "punct_bitmap = 0x%04x\n",
		 le16_to_cpu(event->punct_bitmap));
}

static void
mt7999_mcu_pp_event(struct mt7999_dev *dev, struct sk_buff *skb)
{
	struct mt7999_mcu_pp_basic_event *event;
	struct mt7999_mcu_pp_dscb_event *dscb_event;
	struct mt7999_phy *phy;
	struct mt76_phy *mphy;
	u16 report_bitmap;

	event = (struct mt7999_mcu_pp_basic_event *)skb->data;

	switch (le16_to_cpu(event->tag)) {
	case UNI_EVENT_STATIC_PP_TAG_CSA_DSCB_IE:
	case UNI_EVENT_STATIC_PP_TAG_DSCB_IE:
		if (!mt7999_band_valid(dev, event->band_idx))
			return;

		mphy = mt76_dev_phy(&dev->mt76, event->band_idx);
		phy = mphy->priv;

		dscb_event = (struct mt7999_mcu_pp_dscb_event *)event;
		report_bitmap = le16_to_cpu(dscb_event->punct_bitmap);

		if (phy->punct_bitmap == report_bitmap)
			return;

		if (phy->pp_mode == PP_FW_MODE) {
			phy->punct_bitmap = report_bitmap;
			mt7999_vendor_pp_bitmap_update(phy, report_bitmap);
		}
		break;
	case UNI_EVENT_PP_TAG_ALG_CTRL:
		mt7999_dump_pp_statistic_event(dev, (struct mt7999_mcu_pp_alg_ctrl_event *)event);
		break;
	}
}

static void
mt7999_mcu_mld_reconf_finish(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct mt7999_mld_event_data *data = priv;
	struct mt7999_mcu_mld_ap_reconf_event *reconf = (void *)data->data;

	if (!ether_addr_equal(vif->addr, data->mld_addr))
		return;

	ieee80211_links_removed(vif, le16_to_cpu(reconf->link_bitmap));
}

static void
mt7999_mcu_mld_attlm_event(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct mt7999_mld_event_data *data = priv;
	struct mt7999_mcu_mld_attlm_timeout_event *ttlm = (void *)data->data;

	if (!ether_addr_equal(vif->addr, data->mld_addr))
		return;

	/*
	 * TODO: Remap the FW event type to MAC80211 event type.
	 * For now, we align it because this is a proprietary implementation.
	 */
	ieee80211_attlm_notify(vif, 0, ttlm->event_type, GFP_ATOMIC);
}

static void
mt7999_mcu_mld_event(struct mt7999_dev *dev, struct sk_buff *skb)
{
	struct mt7999_mcu_mld_event *event = (void *)skb->data;
	struct mt7999_mld_event_data data = {};
	struct tlv *tlv;
	int len;

	memcpy(data.mld_addr, event->mld_addr, ETH_ALEN);
	skb_pull(skb, sizeof(*event));
	tlv = (struct tlv *)skb->data;
	len = skb->len;

	while (len > 0 && le16_to_cpu(tlv->len) <= len && tlv->len) {
		data.data = (u8 *)tlv;

		switch (le16_to_cpu(tlv->tag)) {
		case UNI_EVENT_MLD_ATTLM_TIMEOUT:
			ieee80211_iterate_active_interfaces_atomic(dev->mt76.hw,
					IEEE80211_IFACE_ITER_RESUME_ALL,
					mt7999_mcu_mld_attlm_event, &data);
			break;
		case UNI_EVENT_MLD_RECONF_AP_REM_TIMER:
			ieee80211_iterate_active_interfaces_atomic(dev->mt76.hw,
					IEEE80211_IFACE_ITER_RESUME_ALL,
					mt7999_mcu_mld_reconf_finish, &data);
			break;
		default:
			break;
		}

		len -= le16_to_cpu(tlv->len);
		tlv = (struct tlv *)((u8 *)(tlv) + le16_to_cpu(tlv->len));
	}
}

static void
mt7999_mcu_uni_bss_acq_pkt_cnt(struct mt7999_dev *dev, struct tlv *tlv)
{
	struct mt7999_mld_sdo_bss_acq_pkt_cnt *data =
		(struct mt7999_mld_sdo_bss_acq_pkt_cnt *)tlv->data;
	u64 sum[IEEE80211_NUM_ACS] = {0};
	u8 ac_cnt = 0;
	int i, j;

	for (i = 0; i < SDO_CFG_BSS_NUM; i++) {
		for (j = IEEE80211_AC_VO; j < IEEE80211_NUM_ACS; j++)
			sum[j] += le32_to_cpu(data->pkt_cnt[i][mt76_connac_lmac_mapping(j)]);
	}

	for (i = IEEE80211_AC_VO; i < IEEE80211_NUM_ACS; i++) {
		if (sum[i] > WMM_PKT_THRESHOLD)
			ac_cnt++;
	}

	if (ac_cnt > 1 && !dev->wmm_pbc_enable) {
		dev->wmm_pbc_enable = true;
		queue_work(dev->mt76.wq, &dev->wmm_pbc_work);
	} else if (ac_cnt <= 1 && dev->wmm_pbc_enable) {
		dev->wmm_pbc_enable = false;
		queue_work(dev->mt76.wq, &dev->wmm_pbc_work);
	}
}

static void
mt7999_mcu_sdo_event(struct mt7999_dev *dev, struct sk_buff *skb)
{
	struct mt7999_mcu_sdo_event *event = (void *)skb->data;
	struct tlv *tlv;
	int len;

	skb_pull(skb, sizeof(*event));
	tlv = (struct tlv *)skb->data;
	len = skb->len;

	while (len > 0 && le16_to_cpu(tlv->len) <= len && tlv->len) {
		switch (le16_to_cpu(tlv->tag)) {
		case UNI_EVENT_SDO_BSS_ACQ_PKT_CNT:
			mt7999_mcu_uni_bss_acq_pkt_cnt(dev, tlv);
			break;
		default:
			break;
		}

		len -= le16_to_cpu(tlv->len);
		tlv = (struct tlv *)((u8 *)(tlv) + le16_to_cpu(tlv->len));
	}

}

static void
mt7999_mcu_bss_bcn_crit_finish(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct mt7999_mcu_bss_event *data = priv;
	struct mt7999_vif *mvif = (struct mt7999_vif *)vif->drv_priv;
	struct mt7999_vif_link *mconf;
	unsigned long valid_links = vif->valid_links;
	unsigned int link_id;

	if (!ieee80211_vif_is_mld(vif))
		return;

	rcu_read_lock();
	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		mconf = (struct mt7999_vif_link *)rcu_dereference(mvif->mt76.link[link_id]);
		if (!mconf)
			continue;

		if (mconf->mt76.idx == data->bss_idx) {
			ieee80211_crit_update_notify(vif, link_id,
						     NL80211_CRIT_UPDATE_NONE,
						     GFP_ATOMIC);
			rcu_read_unlock();
			return;
		}
	}
	rcu_read_unlock();
}

static void
mt7999_mcu_bss_event(struct mt7999_dev *dev, struct sk_buff *skb)
{
	struct mt7999_mcu_bss_event *event = (void *)skb->data;
	struct tlv *tlv = (struct tlv *)event->buf;

	switch (le16_to_cpu(tlv->tag)) {
	case UNI_EVENT_BSS_BCN_CRIT_UPDATE:
		ieee80211_iterate_active_interfaces_atomic(dev->mt76.hw,
				IEEE80211_IFACE_ITER_RESUME_ALL,
				mt7999_mcu_bss_bcn_crit_finish, event);
		break;
	default:
		dev_err(dev->mt76.dev, "Unknown BSS event tag: %d\n",
			le16_to_cpu(tlv->tag));
		return;
	}
}

static void
mt7999_mcu_twt_sync_event(struct mt7999_dev *dev, struct sk_buff *skb)
{
	struct mt7999_mcu_twt_sync_event *event;
	struct mt7999_sta_link *msta_link;
	struct mt76_wcid *wcid_entry;
	struct mt7999_twt_flow *flow;
	u16 wcid_idx;
	u8 flow_id;

	event = (struct mt7999_mcu_twt_sync_event *)skb->data;

	if (le16_to_cpu(event->tag) != UNI_EVENT_TWT_INFO)
		return;

	wcid_idx = le16_to_cpu(event->wcid);
	flow_id = event->flow_id;

	rcu_read_lock();
	wcid_entry = mt76_wcid_ptr(dev, wcid_idx);
	if (!wcid_entry || !wcid_entry->sta)
		goto unlock;

	msta_link = container_of(wcid_entry, struct mt7999_sta_link, wcid);

	if (flow_id >= MT7999_MAX_STA_TWT_AGRT ||
	    !(msta_link->twt.flowid_mask & BIT(flow_id)))
		goto unlock;

	flow = &msta_link->twt.flow[flow_id];
	flow->suspended = event->idle != 0;

unlock:
	rcu_read_unlock();
}

static void
mt7999_mcu_rx_nf_event(struct mt7999_dev *dev, struct sk_buff *skb)
{
#define UNI_EVENT_NOISE_FLOOR_CTRL	0
#define NF_MODE_IDLE_POWER		0
#define NF_MODE_IPI_HIST		1
	static const u8 nf_power[] = { 92, 89, 86, 83, 80, 75, 70, 65, 60, 55, 52 };
	struct mt7999_mcu_nf_event *event;
	int band;

	skb_pull(skb, sizeof(struct mt7999_mcu_rxd));
	event = (struct mt7999_mcu_nf_event *)skb->data;

	/* idle power is not supported */
	if (le16_to_cpu(event->tag) != UNI_EVENT_NOISE_FLOOR_CTRL ||
	    event->mode != NF_MODE_IPI_HIST || dev->nf_state != NF_STATE_WAIT)
		return;

	dev->nf_state = NF_STATE_RECEIVED;
	for (band = MT_BAND0; band < __MT_MAX_BAND; band++) {
		struct mt7999_phy *phy = __mt7999_phy(dev, band);
		struct mt76_channel_state *state;
		u32 val, sum = 0, n = 0;
		int i, ant, ant_num;
		u16 nf;

		if (!phy)
			continue;

		ant_num = min(hweight8(phy->mt76->chainmask),
			      dev->enable_1rpd ? 1 : MT_NF_MAX_ANT_NUM);
		for (ant = 0; ant < ant_num; ant++) {
			for (i = 0; i < ARRAY_SIZE(nf_power); i++) {
				val = le32_to_cpu(event->avg_ipi[band][ant][i]);
				sum += val * nf_power[i];
				n += val;
			}
		}

		nf = n ? sum / n : 0;
		if (!phy->noise)
			phy->noise = nf << 4;
		else if (nf)
			phy->noise += nf - (phy->noise >> 4);

		state = phy->mt76->chan_state;
		state->noise = -(phy->noise >> 4);
	}

	wake_up(&dev->mt76.mcu.wait);
}

static void
mt7999_mcu_uni_rx_unsolicited_event(struct mt7999_dev *dev, struct sk_buff *skb)
{
	struct mt7999_mcu_rxd *rxd = (struct mt7999_mcu_rxd *)skb->data;

	mt7999_packet_log_to_host(dev, skb->data, skb->len, PKT_BIN_DEBUG_MCU_EVENT, 0);
	switch (rxd->eid) {
	case MCU_UNI_EVENT_FW_LOG_2_HOST:
		mt7999_mcu_rx_log_message(dev, skb);
		break;
	case MCU_UNI_EVENT_IE_COUNTDOWN:
		mt7999_mcu_ie_countdown(dev, skb);
		break;
	case MCU_UNI_EVENT_RDD_REPORT:
#ifdef CONFIG_MTK_DEBUG
		if (dev->dbg.ignore_radar_event)
			break;
#endif
		mt7999_mcu_rx_radar_detected(dev, skb);
		break;
	case MCU_UNI_EVENT_ALL_STA_INFO:
		mt7999_mcu_rx_all_sta_info_event(dev, skb);
		break;
	case MCU_UNI_EVENT_WED_RRO:
		mt7999_mcu_wed_rro_event(dev, skb);
		break;
	case MCU_UNI_EVENT_PP:
		mt7999_mcu_pp_event(dev, skb);
		break;
	case MCU_UNI_EVENT_MLD:
		mt7999_mcu_mld_event(dev, skb);
		break;
	case MCU_UNI_EVENT_SDO:
		mt7999_mcu_sdo_event(dev, skb);
		break;
	case MCU_UNI_EVENT_BSS_INFO:
		mt7999_mcu_bss_event(dev, skb);
		break;
#ifdef CONFIG_MTK_DEBUG
	case MCU_UNI_EVENT_SR:
		mt7999_mcu_rx_sr_event(dev, skb);
		break;
#endif
	case MCU_UNI_EVENT_THERMAL:
		mt7999_mcu_rx_thermal_info_query(dev, skb);
		break;
	case MCU_UNI_EVENT_NOISE_FLOOR:
		mt7999_mcu_rx_nf_event(dev, skb);
		break;
#ifdef CONFIG_NL80211_TESTMODE
	case MCU_UNI_EVENT_TESTMODE_CTRL:
		mt7999_tm_rf_test_event(dev, skb);
		break;
#endif
	case MCU_UNI_EVENT_TWT_SYNC:
		mt7999_mcu_twt_sync_event(dev, skb);
		break;
#if defined CONFIG_NL80211_TESTMODE && defined CONFIG_MTK_DEBUG
	case MCU_UNI_EVENT_BF:
		mt7999_mcu_rx_bf_event(dev, skb);
		break;
#endif
#ifdef CONFIG_MTK_VENDOR
	case MCU_UNI_EVENT_CSI_REPORT:
		mt7999_mcu_csi_report_event(dev, skb);
		break;
#endif
#ifdef CONFIG_FW_CANARY_MONITOR
	case MCU_UNI_EVENT_CANARY_MONITOR:
		mt7999_muldf_canary_monitor_event(dev, skb);
		break;
#endif
	default:
		break;
	}
	dev_kfree_skb(skb);
}

void mt7999_mcu_rx_event(struct mt7999_dev *dev, struct sk_buff *skb)
{
	struct mt7999_mcu_rxd *rxd = (struct mt7999_mcu_rxd *)skb->data;

	trace_mt7999_mcu_event(dev, rxd->option & MCU_UNI_CMD_UNSOLICITED_EVENT,
			      rxd->eid, rxd->ext_eid, skb->data, skb->len);
	if (rxd->option & MCU_UNI_CMD_UNSOLICITED_EVENT) {
		mt7999_mcu_uni_rx_unsolicited_event(dev, skb);
		return;
	}

	mt76_mcu_rx_event(&dev->mt76, skb);
}

static struct tlv *
mt7999_mcu_add_uni_tlv(struct sk_buff *skb, u16 tag, u16 len)
{
	struct tlv *ptlv = skb_put_zero(skb, len);

	ptlv->tag = cpu_to_le16(tag);
	ptlv->len = cpu_to_le16(len);

	return ptlv;
}

static void
mt7999_mcu_bss_rfch_tlv(struct sk_buff *skb, struct mt7999_phy *phy)
{
	static const u8 rlm_ch_band[] = {
		[NL80211_BAND_2GHZ] = 1,
		[NL80211_BAND_5GHZ] = 2,
		[NL80211_BAND_6GHZ] = 3,
	};
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	struct bss_rlm_tlv *ch;
	struct tlv *tlv;
	int freq1 = chandef->center_freq1;

	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_BSS_INFO_RLM, sizeof(*ch));

	ch = (struct bss_rlm_tlv *)tlv;
	ch->control_channel = chandef->chan->hw_value;
	ch->center_chan = ieee80211_frequency_to_channel(freq1);
	ch->bw = mt76_connac_chan_bw(chandef);
	ch->tx_streams = hweight8(phy->mt76->antenna_mask);
	ch->rx_streams = hweight8(phy->mt76->antenna_mask);
	ch->band = rlm_ch_band[chandef->chan->band];

	if (chandef->width == NL80211_CHAN_WIDTH_80P80) {
		int freq2 = chandef->center_freq2;

		ch->center_chan2 = ieee80211_frequency_to_channel(freq2);
	}
}

static void
mt7999_mcu_bss_ra_tlv(struct sk_buff *skb, struct mt7999_phy *phy)
{
	struct bss_ra_tlv *ra;
	struct tlv *tlv;

	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_BSS_INFO_RA, sizeof(*ra));

	ra = (struct bss_ra_tlv *)tlv;
	ra->short_preamble = true;
}

static void
mt7999_mcu_bss_he_tlv(struct sk_buff *skb, struct ieee80211_vif *vif,
		      struct ieee80211_bss_conf *link_conf,
		      struct mt7999_phy *phy)
{
#define DEFAULT_HE_PE_DURATION		4
#define DEFAULT_HE_DURATION_RTS_THRES	1023
	const struct ieee80211_sta_he_cap *cap;
	struct bss_info_uni_he *he;
	struct tlv *tlv;

	cap = mt76_connac_get_he_phy_cap(phy->mt76, vif);

	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_BSS_INFO_HE_BASIC, sizeof(*he));

	he = (struct bss_info_uni_he *)tlv;
	he->he_pe_duration = link_conf->htc_trig_based_pkt_ext;
	if (!he->he_pe_duration)
		he->he_pe_duration = DEFAULT_HE_PE_DURATION;

	he->he_rts_thres = cpu_to_le16(link_conf->frame_time_rts_th);
	if (!he->he_rts_thres)
		he->he_rts_thres = cpu_to_le16(DEFAULT_HE_DURATION_RTS_THRES);

	he->max_nss_mcs[CMD_HE_MCS_BW80] = cap->he_mcs_nss_supp.tx_mcs_80;
	he->max_nss_mcs[CMD_HE_MCS_BW160] = cap->he_mcs_nss_supp.tx_mcs_160;
	he->max_nss_mcs[CMD_HE_MCS_BW8080] = cap->he_mcs_nss_supp.tx_mcs_80p80;
}

static void
mt7999_mcu_bss_mbssid_tlv(struct sk_buff *skb, struct ieee80211_bss_conf *link_conf,
			  bool enable)
{
	struct bss_info_uni_mbssid *mbssid;
	struct tlv *tlv;

	if (link_conf && !link_conf->bssid_indicator && enable)
		return;

	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_BSS_INFO_11V_MBSSID, sizeof(*mbssid));

	mbssid = (struct bss_info_uni_mbssid *)tlv;

	if (enable && link_conf) {
		mbssid->max_indicator = link_conf->bssid_indicator;
		mbssid->mbss_idx = link_conf->bssid_index;
		mbssid->tx_bss_omac_idx = 0;
	}
}

static void
mt7999_mcu_bss_bmc_tlv(struct sk_buff *skb, struct mt76_vif_link *mlink,
		       struct mt7999_phy *phy)
{
	struct bss_rate_tlv *bmc;
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	enum nl80211_band band = chandef->chan->band;
	struct tlv *tlv;
	u8 idx = mlink->mcast_rates_idx ?
		 mlink->mcast_rates_idx : mlink->basic_rates_idx;

	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_BSS_INFO_RATE, sizeof(*bmc));

	bmc = (struct bss_rate_tlv *)tlv;

	bmc->short_preamble = (band == NL80211_BAND_2GHZ);
	bmc->bc_fixed_rate = idx;
	bmc->mc_fixed_rate = idx;
}

static void
mt7999_mcu_bss_txcmd_tlv(struct sk_buff *skb, bool en)
{
	struct bss_txcmd_tlv *txcmd;
	struct tlv *tlv;

	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_BSS_INFO_TXCMD, sizeof(*txcmd));

	txcmd = (struct bss_txcmd_tlv *)tlv;
	txcmd->txcmd_mode = en;
}

static void
mt7999_mcu_bss_mld_tlv(struct sk_buff *skb,
		       struct ieee80211_bss_conf *link_conf,
		       struct mt7999_vif_link *link)
{
	struct ieee80211_vif *vif = link_conf->vif;
	struct mt7999_vif *mvif = (struct mt7999_vif *)vif->drv_priv;
	struct bss_mld_tlv *mld;
	struct tlv *tlv;

	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_BSS_INFO_MLD, sizeof(*mld));
	mld = (struct bss_mld_tlv *)tlv;
	mld->own_mld_id = link->mld_idx;
	mld->link_id = link_conf->link_id;

	if (ieee80211_vif_is_mld(vif)) {
		mld->group_mld_id = mvif->mld_group_idx;
		mld->remap_idx = mvif->mld_remap_idx;
		memcpy(mld->mac_addr, vif->addr, ETH_ALEN);
	} else {
		mld->group_mld_id = 0xff;
		mld->remap_idx = 0xff;
	}

	mt76_bss_dbg(vif, "group_mld_id=%d, own_mld_id=%d, remap_idx=%d, mld->addr[%pM]",
		     mld->group_mld_id,  mld->own_mld_id, mld->remap_idx, mld->mac_addr);
}

static void
mt7999_mcu_bss_sec_tlv(struct sk_buff *skb, struct mt76_vif_link *mlink)
{
	struct bss_sec_tlv *sec;
	struct tlv *tlv;

	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_BSS_INFO_SEC, sizeof(*sec));

	sec = (struct bss_sec_tlv *)tlv;
	sec->cipher = mlink->cipher;
}

static void
mt7999_mcu_bss_ifs_timing_tlv(struct sk_buff *skb, struct mt7999_phy *phy)
{
#define SIFS_TIME_24G		10
#define RIFS_TIME		2
#define EIFS_TIME_OFDM		84
#define EIFS_TIME_CCK		320
	struct bss_ifs_time_tlv *ifs_time;
	struct tlv *tlv;

	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_BSS_INFO_IFS_TIME, sizeof(*ifs_time));

	ifs_time = (struct bss_ifs_time_tlv *)tlv;
	ifs_time->slot_valid = true;
	ifs_time->sifs_valid = true;
	ifs_time->rifs_valid = true;
	ifs_time->eifs_valid = true;

	ifs_time->slot_time = cpu_to_le16(phy->slottime);
	ifs_time->sifs_time = cpu_to_le16(SIFS_TIME_24G);
	ifs_time->rifs_time = cpu_to_le16(RIFS_TIME);
	ifs_time->eifs_time = cpu_to_le16(EIFS_TIME_OFDM);

	if (phy->mt76->chandef.chan->band == NL80211_BAND_2GHZ) {
		ifs_time->eifs_cck_valid = true;
		ifs_time->eifs_cck_time = cpu_to_le16(EIFS_TIME_CCK);
	}
}

static int
mt7999_mcu_bss_basic_tlv(struct sk_buff *skb,
			 struct ieee80211_vif *vif,
			 struct ieee80211_bss_conf *link_conf,
			 struct mt76_vif_link *mvif,
			 struct mt76_phy *phy, u16 wlan_idx,
			 bool enable)
{
	struct cfg80211_chan_def *chandef = &phy->chandef;
	struct mt76_connac_bss_basic_tlv *bss;
	u32 type = CONNECTION_INFRA_AP;
	u16 sta_wlan_idx = 0;
	struct tlv *tlv;
	int idx;

	switch (vif->type) {
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MONITOR:
		break;
	case NL80211_IFTYPE_STATION:
		if (enable) {
			struct ieee80211_sta *sta;

			rcu_read_lock();
			sta = ieee80211_find_sta(vif, link_conf->bssid);
			if (sta) {
				struct mt7999_sta *msta = (void *)sta->drv_priv;
				struct mt7999_sta_link *msta_link;
				int link_id = link_conf->link_id;

				msta_link = mt7999_sta_link(msta, link_id);
				if (msta_link)
					sta_wlan_idx = msta_link->wcid.idx;
			}
			rcu_read_unlock();
		}
		type = CONNECTION_INFRA_STA;
		break;
	case NL80211_IFTYPE_ADHOC:
		type = CONNECTION_IBSS_ADHOC;
		break;
	default:
		WARN_ON(1);
		break;
	}

	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_BSS_INFO_BASIC, sizeof(*bss));

	bss = (struct mt76_connac_bss_basic_tlv *)tlv;
	bss->bmc_tx_wlan_idx = cpu_to_le16(wlan_idx);
	bss->sta_idx = cpu_to_le16(sta_wlan_idx);
	bss->conn_type = cpu_to_le32(type);
	bss->omac_idx = mvif->omac_idx;
	bss->band_idx = mvif->band_idx;
	bss->wmm_idx = mvif->wmm_idx;
	bss->conn_state = !enable;
	bss->active = enable;

	idx = mvif->omac_idx > EXT_BSSID_START ? HW_BSSID_0 : mvif->omac_idx;
	bss->hw_bss_idx = idx;

	if (vif->type == NL80211_IFTYPE_MONITOR) {
		memcpy(bss->bssid, phy->macaddr, ETH_ALEN);
#ifdef CONFIG_NL80211_TESTMODE
		if (mt76_testmode_bf_enabled(phy))
			memcpy(bss->bssid, phy->test.addr[2], ETH_ALEN);
#endif
		return 0;
	}

	if (!link_conf)
		return 0;

	memcpy(bss->bssid, link_conf->bssid, ETH_ALEN);

	mt76_bss_dbg(vif, "band=%d, omac=%d, wmm_idx=%d, bssid=%pM, link=%d, en=%d",
		     bss->band_idx, bss->omac_idx, bss->wmm_idx, bss->bssid,
		     link_conf->link_id, enable);

	bss->bcn_interval = cpu_to_le16(link_conf->beacon_int);
	bss->dtim_period = link_conf->dtim_period;
	bss->phymode = mt76_connac_get_phy_mode(phy, vif,
						chandef->chan->band, NULL);
	bss->phymode_ext = mt76_connac_get_phy_mode_ext(phy, link_conf,
							chandef->chan->band);

	return 0;
}

static struct sk_buff *
__mt7999_mcu_alloc_bss_req(struct mt76_dev *dev, struct mt76_vif_link *mvif, int len)
{
	struct bss_req_hdr hdr = {
		.bss_idx = mvif->idx,
	};
	struct sk_buff *skb;

	skb = mt76_mcu_msg_alloc(dev, NULL, len);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	skb_put_data(skb, &hdr, sizeof(hdr));

	return skb;
}

int mt7999_mcu_add_bss_info(struct mt7999_phy *phy, struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf,
			    struct mt76_vif_link *mlink,
			    struct mt7999_sta_link *msta_link, int enable)
{
	struct mt7999_dev *dev = phy->dev;
	struct sk_buff *skb;

	skb = __mt7999_mcu_alloc_bss_req(&dev->mt76, mlink,
					 MT7999_BSS_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	/* bss_basic must be first */
	mt7999_mcu_bss_basic_tlv(skb, vif, link_conf, mlink, phy->mt76,
				 msta_link->wcid.idx, enable);
	mt7999_mcu_bss_sec_tlv(skb, mlink);

	if (vif->type == NL80211_IFTYPE_MONITOR)
		goto out;

	if (enable) {
		struct mt7999_vif_link *link;

		mt7999_mcu_bss_rfch_tlv(skb, phy);
		mt7999_mcu_bss_bmc_tlv(skb, mlink, phy);
		mt7999_mcu_bss_ra_tlv(skb, phy);
		mt7999_mcu_bss_txcmd_tlv(skb, true);

		if (link_conf->he_support)
			mt7999_mcu_bss_he_tlv(skb, vif, link_conf, phy);

		/* this tag is necessary no matter if the vif is MLD */
		link = container_of(mlink, struct mt7999_vif_link, mt76);
		mt7999_mcu_bss_mld_tlv(skb, link_conf, link);
	}

	mt7999_mcu_bss_mbssid_tlv(skb, link_conf, enable);

out:
	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(BSS_INFO_UPDATE), true);
}

int mt7999_mcu_update_bss_rfch(struct mt7999_phy *phy, struct mt7999_vif_link *link)
{
	struct mt7999_dev *dev = phy->dev;
	struct sk_buff *skb;

	skb = __mt7999_mcu_alloc_bss_req(&dev->mt76, &link->mt76,
					 MT7999_BSS_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt7999_mcu_bss_rfch_tlv(skb, phy);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(BSS_INFO_UPDATE), true);
}

int mt7999_mcu_set_protection(struct mt7999_phy *phy, struct mt7999_vif_link *link,
			      u8 ht_mode, bool use_cts_prot)
{
	struct mt7999_dev *dev = phy->dev;
	struct bss_prot_tlv *prot;
	struct sk_buff *skb;
	struct tlv *tlv;
	enum {
		PROT_NONMEMBER	 = BIT(1),
		PROT_20MHZ	 = BIT(2),
		PROT_NONHT_MIXED = BIT(3),
		PROT_LEGACY_ERP	 = BIT(5),
		PROT_NONGF_STA	 = BIT(7),
	};

	skb = __mt7999_mcu_alloc_bss_req(&dev->mt76, &link->mt76,
					 MT7999_BSS_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_BSS_INFO_PROTECT_INFO,
				     sizeof(*prot));
	prot = (struct bss_prot_tlv *)tlv;

	switch (ht_mode & IEEE80211_HT_OP_MODE_PROTECTION) {
	case IEEE80211_HT_OP_MODE_PROTECTION_NONMEMBER:
		prot->prot_mode = cpu_to_le32(PROT_NONMEMBER);
		break;
	case IEEE80211_HT_OP_MODE_PROTECTION_20MHZ:
		prot->prot_mode = cpu_to_le32(PROT_20MHZ);
		break;
	case IEEE80211_HT_OP_MODE_PROTECTION_NONHT_MIXED:
		prot->prot_mode = cpu_to_le32(PROT_NONHT_MIXED);
		break;
	}

	if (ht_mode & IEEE80211_HT_OP_MODE_NON_GF_STA_PRSNT)
		prot->prot_mode |= cpu_to_le32(PROT_NONGF_STA);

	if (use_cts_prot)
		prot->prot_mode |= cpu_to_le32(PROT_LEGACY_ERP);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WM_UNI_CMD(BSS_INFO_UPDATE), true);
}

int mt7999_mcu_set_emlsr_mode(struct mt7999_dev *dev,
			      struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta,
			      struct ieee80211_eml_params *eml_params)
{
	struct mt7999_sta *msta = (struct mt7999_sta *)sta->drv_priv;
	struct mt7999_sta_link *msta_link;
	struct sta_rec_eml_op *eml_op;
	struct mt7999_vif_link *link;
	struct sk_buff *skb;
	struct tlv *tlv;

	msta_link = mt7999_sta_link_protected(dev, msta, eml_params->link_id);
	if (!msta_link)
		return -EINVAL;

	link = mt7999_vif_link(dev, vif, eml_params->link_id);
	if (!link)
		return -EINVAL;

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, &link->mt76,
					      &msta_link->wcid,
					      MT7999_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_EML_OP, sizeof(*eml_op));
	eml_op = (struct sta_rec_eml_op *)tlv;
	eml_op->link_bitmap = 0;

	if (eml_params->control & IEEE80211_EML_CTRL_EMLSR_MODE) {
		unsigned long link_bitmap = eml_params->link_bitmap;
		unsigned int link_id;

		for_each_set_bit(link_id, &link_bitmap,
				 IEEE80211_MLD_MAX_NUM_LINKS) {
			struct mt76_phy *mphy;

			link = mt7999_vif_link(dev, vif, link_id);
			if (!link)
				continue;

			mphy = mt76_vif_link_phy(&link->mt76);
			if (!mphy)
				continue;

			eml_op->link_bitmap |= BIT(mphy->band_idx);
		}
	}

	mt76_mlo_dbg(vif, "link:%u, wcid:%d, control:%x, mode:%d, bmp:%x",
		     msta_link->wcid.link_id, msta_link->wcid.idx, eml_params->control,
		     !!(eml_params->control & IEEE80211_EML_CTRL_EMLSR_MODE),
		     eml_op->link_bitmap);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(STA_REC_UPDATE), true);
}

int mt7999_mcu_get_tsf(struct mt7999_phy *phy, struct mt7999_vif_link *link,
		       u64 *tsf)
{
	struct mt7999_mcu_mac_info_tsf_resp *resp;
	struct mt7999_mcu_mac_info_tsf *req;
	struct mt7999_dev *dev = phy->dev;
	struct sk_buff *skb, *rskb;
	struct uni_header hdr;
	struct tlv *tlv;
	int len = sizeof(hdr) + sizeof(*req), ret;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));
	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_MAC_INFO_TSF, sizeof(*req));

	req = (struct mt7999_mcu_mac_info_tsf *)tlv;
	req->band_idx = phy->mt76->band_idx;
	req->bss_idx = link->mt76.omac_idx;

	ret = mt76_mcu_skb_send_and_get_msg(&dev->mt76, skb,
					    MCU_WM_UNI_CMD(GET_MAC_INFO),
					    true, &rskb);
	if (ret)
		return ret;

	skb_pull(rskb, sizeof(hdr));
	resp = (struct mt7999_mcu_mac_info_tsf_resp *)rskb->data;

	switch (le16_to_cpu(resp->tag)) {
	case UNI_EVENT_MAC_INFO_TSF:
		*tsf = (u64)le32_to_cpu(resp->tsf_bit32_63) << 32 |
		       le32_to_cpu(resp->tsf_bit0_31);
		break;
	default:
		ret = -ENODATA;
		break;
	}

	dev_kfree_skb(rskb);
	return ret;
}

int mt7999_mcu_get_tsf_offset(struct mt7999_phy *phy,
			      struct mt7999_vif *mvif,
			      int rpting_link_id,
			      int rpted_link_id)
{
	struct ieee80211_vif *vif = container_of((void *)mvif, struct ieee80211_vif,
						 drv_priv);
	struct mt7999_dev *dev = phy->dev;
	struct mt7999_vif_link *rpting_conf, *rpted_conf;
	struct mt7999_mcu_mac_info_tsf_diff *req;
	struct mt7999_mcu_mac_info_tsf_diff_resp *resp;
	struct sk_buff *skb, *rskb;
	struct tlv *tlv;
	struct uni_header hdr;
	int len = sizeof(hdr) + sizeof(*req), ret;
	int32_t tsf0_31, tsf32_63;
	int64_t tsf_rpted, tsf_rpting, tsf_offset;

	rpted_conf = mt7999_vif_link(dev, vif, rpted_link_id);
	rpting_conf = mt7999_vif_link(dev, vif, rpting_link_id);
	if (!rpted_conf || !rpting_conf)
		return -EINVAL;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));
	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_MAC_INFO_TSF_DIFF, sizeof(*req));

	req = (struct mt7999_mcu_mac_info_tsf_diff *)tlv;
	req->bss_idx0 = rpted_link_id;
	req->bss_idx1 = rpting_link_id;

	ret = mt76_mcu_skb_send_and_get_msg(&dev->mt76, skb,
					    MCU_WM_UNI_CMD(GET_MAC_INFO),
					    true, &rskb);
	if (ret)
		return ret;

	skb_pull(rskb, sizeof(hdr));
	resp = (struct mt7999_mcu_mac_info_tsf_diff_resp *)rskb->data;

	switch(le16_to_cpu(resp->tag)) {
	case UNI_EVENT_MAC_INFO_TSF_DIFF:
		tsf0_31 = le32_to_cpu(resp->tsf0_bit0_31);
		tsf32_63 = le32_to_cpu(resp->tsf0_bit32_63);
		tsf_rpted = (int64_t)tsf0_31 + ((int64_t)tsf32_63 << 32);
		tsf0_31 = le32_to_cpu(resp->tsf1_bit0_31);
		tsf32_63 = le32_to_cpu(resp->tsf1_bit32_63);
		tsf_rpting = (int64_t)tsf0_31 + ((int64_t)tsf32_63 << 32);
		tsf_offset = (tsf_rpted - tsf_rpting) / 2;
		rpted_conf->tsf_offset[rpting_link_id] = tsf_offset;
		break;
	default:
		break;
	}

	dev_kfree_skb(rskb);
	return ret;
}

int mt7999_mcu_set_timing(struct mt7999_phy *phy, struct ieee80211_vif *vif,
			  struct ieee80211_bss_conf *link_conf)
{
	struct mt7999_dev *dev = phy->dev;
	struct mt76_vif_link *mlink = mt76_vif_conf_link(&dev->mt76, vif, link_conf);
	struct sk_buff *skb;

	skb = __mt7999_mcu_alloc_bss_req(&dev->mt76, mlink,
					 MT7999_BSS_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt7999_mcu_bss_ifs_timing_tlv(skb, phy);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WM_UNI_CMD(BSS_INFO_UPDATE), true);
}

static int
mt7999_mcu_sta_ba(struct mt7999_dev *dev, struct mt76_vif_link *mvif,
		  struct ieee80211_ampdu_params *params,
		  struct mt76_wcid *wcid, bool enable, bool tx)
{
	struct sta_rec_ba_uni *ba;
	struct sk_buff *skb;
	struct tlv *tlv;

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, mvif, wcid,
					      MT7999_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_BA, sizeof(*ba));

	ba = (struct sta_rec_ba_uni *)tlv;
	ba->ba_type = tx ? MT_BA_TYPE_ORIGINATOR : MT_BA_TYPE_RECIPIENT;
	ba->winsize = cpu_to_le16(params->buf_size);
	ba->ssn = cpu_to_le16(params->ssn);
	ba->ba_en = enable << params->tid;
	ba->amsdu = params->amsdu;
	ba->tid = params->tid;
	ba->ba_rdd_rro = !tx && enable && mt7999_has_hwrro(dev);
	ba->ba_mode = true;

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(STA_REC_UPDATE), true);
}

static int
mt7999_mcu_peer_mld_ba(struct mt7999_dev *dev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta, struct ieee80211_ampdu_params *params,
		       bool enable, bool tx)
{
	struct mt7999_sta *msta = (struct mt7999_sta *)sta->drv_priv;
	struct mt7999_sta_link *msta_link;
	struct peer_mld_req_hdr *hdr;
	struct peer_mld_ba *ba;
	struct sk_buff *skb;
	struct tlv *tlv;

	msta_link = mt7999_sta_link_protected(dev, msta, msta->deflink_id);
	if (!msta_link)
		return -EINVAL;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, sizeof(*hdr) + sizeof(*ba));
	if (!skb)
		return -ENOMEM;

	hdr = skb_put_zero(skb, sizeof(*hdr));
	hdr->fw_mld_idx = cpu_to_le16(msta_link->wcid.idx);
	memcpy(hdr->peer_mld_addr, sta->addr, ETH_ALEN);

	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_PEER_MLD_BA, sizeof(*ba));
	ba = (struct peer_mld_ba *)tlv;
	ba->ba_type = tx ? MT_BA_TYPE_ORIGINATOR : MT_BA_TYPE_RECIPIENT;
	ba->winsize = cpu_to_le16(params->buf_size);
	ba->ssn = cpu_to_le16(params->ssn);
	ba->ba_en = enable << params->tid;
	ba->amsdu = params->amsdu;
	ba->tid = params->tid;
	ba->ba_mode = true;
	ba->ba_add_rro = !tx && enable && mt7999_has_hwrro(dev);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(PEER_MLD), true);
}

/** starec & wtbl **/
int mt7999_mcu_add_tx_ba(struct mt7999_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 struct ieee80211_vif *vif, bool enable)
{
	struct ieee80211_sta *sta = params->sta;
	struct mt7999_sta *msta = (struct mt7999_sta *)sta->drv_priv;
	unsigned int link_id;
	unsigned long valid_links = sta->valid_links ?: BIT(0);

	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt7999_sta_link *msta_link;

		msta_link = mt7999_sta_link_protected(dev, msta, link_id);
		if (!msta_link)
			continue;

		if (enable && !params->amsdu)
			msta_link->wcid.amsdu = false;
	}

	if (!sta->mlo) {
		struct mt7999_vif *mvif = (struct mt7999_vif *)vif->drv_priv;

		return mt7999_mcu_sta_ba(dev, &mvif->deflink.mt76, params,
					 &msta->deflink.wcid, enable, true);
	}

	return mt7999_mcu_peer_mld_ba(dev, vif, sta, params, enable, true);
}

int mt7999_mcu_add_rx_ba(struct mt7999_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 struct ieee80211_vif *vif, bool enable)
{
	struct ieee80211_sta *sta = params->sta;

	if (!sta->mlo) {
		struct mt7999_vif *mvif = (struct mt7999_vif *)vif->drv_priv;
		struct mt7999_sta *msta = (struct mt7999_sta *)sta->drv_priv;

		return mt7999_mcu_sta_ba(dev, &mvif->deflink.mt76, params,
					 &msta->deflink.wcid, enable, false);
	}

	return mt7999_mcu_peer_mld_ba(dev, vif, sta, params, enable, false);
}

static void
mt7999_mcu_sta_he_tlv(struct sk_buff *skb, struct ieee80211_bss_conf *conf,
		      struct ieee80211_link_sta *link_sta,
		      struct mt7999_vif_link *link)
{
	struct ieee80211_he_cap_elem *elem = &link_sta->he_cap.he_cap_elem;
	struct ieee80211_he_mcs_nss_supp mcs_map;
	struct sta_rec_he_v2 *he;
	struct tlv *tlv;
	int i = 0;

	if (!link_sta->he_cap.has_he)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HE_V2, sizeof(*he));

	he = (struct sta_rec_he_v2 *)tlv;
	for (i = 0; i < ARRAY_SIZE(he->he_phy_cap); i++) {
		if (i < ARRAY_SIZE(he->he_mac_cap))
			he->he_mac_cap[i] = elem->mac_cap_info[i];
		he->he_phy_cap[i] = elem->phy_cap_info[i];
	}

	if (conf->vif->type == NL80211_IFTYPE_AP &&
	    (elem->phy_cap_info[1] & IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD))
		u8p_replace_bits(&he->he_phy_cap[1], conf->he_ldpc,
				 IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD);

	mcs_map = link_sta->he_cap.he_mcs_nss_supp;
	switch (link_sta->bandwidth) {
	case IEEE80211_STA_RX_BW_160:
		if (elem->phy_cap_info[0] &
		    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G)
			mt7999_mcu_set_sta_he_mcs(link_sta, link,
						  &he->max_nss_mcs[CMD_HE_MCS_BW8080],
						  le16_to_cpu(mcs_map.rx_mcs_80p80));

		mt7999_mcu_set_sta_he_mcs(link_sta, link,
					  &he->max_nss_mcs[CMD_HE_MCS_BW160],
					  le16_to_cpu(mcs_map.rx_mcs_160));
		fallthrough;
	default:
		mt7999_mcu_set_sta_he_mcs(link_sta, link,
					  &he->max_nss_mcs[CMD_HE_MCS_BW80],
					  le16_to_cpu(mcs_map.rx_mcs_80));
		break;
	}

	he->pkt_ext = 2;
}

static void
mt7999_mcu_sta_he_6g_tlv(struct sk_buff *skb,
			 struct ieee80211_link_sta *link_sta)
{
	struct sta_rec_he_6g_capa *he_6g;
	struct tlv *tlv;

	if (!link_sta->he_6ghz_capa.capa)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HE_6G, sizeof(*he_6g));

	he_6g = (struct sta_rec_he_6g_capa *)tlv;
	he_6g->capa = link_sta->he_6ghz_capa.capa;
}

static void
mt7999_mcu_sta_eht_tlv(struct sk_buff *skb,
		       struct ieee80211_link_sta *link_sta)
{
	struct mt7999_sta *msta = (struct mt7999_sta *)link_sta->sta->drv_priv;
	struct ieee80211_vif *vif = container_of((void *)msta->vif,
						 struct ieee80211_vif, drv_priv);
	struct ieee80211_eht_mcs_nss_supp *mcs_map;
	struct ieee80211_eht_cap_elem_fixed *elem;
	struct sta_rec_eht *eht;
	struct tlv *tlv;

	if (!link_sta->eht_cap.has_eht)
		return;

	mcs_map = &link_sta->eht_cap.eht_mcs_nss_supp;
	elem = &link_sta->eht_cap.eht_cap_elem;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_EHT, sizeof(*eht));

	eht = (struct sta_rec_eht *)tlv;
	eht->tid_bitmap = 0xff;
	eht->mac_cap = cpu_to_le16(*(u16 *)elem->mac_cap_info);
	eht->phy_cap = cpu_to_le64(*(u64 *)elem->phy_cap_info);
	eht->phy_cap_ext = cpu_to_le64(elem->phy_cap_info[8]);

	if (vif->type != NL80211_IFTYPE_STATION &&
	    (link_sta->he_cap.he_cap_elem.phy_cap_info[0] &
	     (IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G |
	      IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G |
	      IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G |
	      IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G)) == 0) {
		memcpy(eht->mcs_map_bw20, &mcs_map->only_20mhz,
		       sizeof(eht->mcs_map_bw20));
		return;
	}

	memcpy(eht->mcs_map_bw80, &mcs_map->bw._80, sizeof(eht->mcs_map_bw80));
	memcpy(eht->mcs_map_bw160, &mcs_map->bw._160, sizeof(eht->mcs_map_bw160));
	memcpy(eht->mcs_map_bw320, &mcs_map->bw._320, sizeof(eht->mcs_map_bw320));
}

static void
mt7999_mcu_sta_uhr_tlv(struct sk_buff *skb,
		       struct ieee80211_link_sta *link_sta)
{
	struct sta_rec_uhr *uhr;
	struct tlv *tlv;

	if (!link_sta->uhr_cap.has_uhr)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_UHR, sizeof(*uhr));
	uhr = (struct sta_rec_uhr *)tlv;

	memcpy(uhr->mac_cap, &link_sta->uhr_cap.mac, sizeof(uhr->mac_cap));
	memcpy(uhr->phy_cap, &link_sta->uhr_cap.phy, sizeof(uhr->phy_cap));
}

static void
mt7999_mcu_sta_ht_tlv(struct sk_buff *skb, struct ieee80211_link_sta *link_sta)
{
	struct sta_rec_ht_uni *ht;
	struct tlv *tlv;

	if (!link_sta->ht_cap.ht_supported)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HT, sizeof(*ht));

	ht = (struct sta_rec_ht_uni *)tlv;
	ht->ht_cap = cpu_to_le16(link_sta->ht_cap.cap);
	ht->ampdu_param = u8_encode_bits(link_sta->ht_cap.ampdu_factor,
					 IEEE80211_HT_AMPDU_PARM_FACTOR) |
			  u8_encode_bits(link_sta->ht_cap.ampdu_density,
					 IEEE80211_HT_AMPDU_PARM_DENSITY);
}

static void
mt7999_mcu_sta_vht_tlv(struct sk_buff *skb, struct ieee80211_link_sta *link_sta)
{
	struct sta_rec_vht *vht;
	struct tlv *tlv;
#ifdef CONFIG_MTK_VENDOR
	struct mt7999_sta *msta = (struct mt7999_sta *)link_sta->sta->drv_priv;
	struct mt7999_phy *phy = (struct mt7999_phy *)msta->vif->deflink.phy;
#endif

	/* For 6G band, this tlv is necessary to let hw work normally */
	if (!link_sta->he_6ghz_capa.capa && !link_sta->vht_cap.vht_supported)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_VHT, sizeof(*vht));

	vht = (struct sta_rec_vht *)tlv;
	vht->vht_cap = cpu_to_le32(link_sta->vht_cap.cap);
	vht->vht_rx_mcs_map = link_sta->vht_cap.vht_mcs.rx_mcs_map;
	vht->vht_tx_mcs_map = link_sta->vht_cap.vht_mcs.tx_mcs_map;
#ifdef CONFIG_MTK_VENDOR
	vht->rts_bw_sig = phy->rts_bw_sig;
#endif
}

static void
mt7999_mcu_sta_amsdu_tlv(struct mt7999_dev *dev, struct sk_buff *skb,
			 struct ieee80211_vif *vif,
			 struct ieee80211_link_sta *link_sta,
			 struct mt7999_sta_link *msta_link)
{
	struct sta_rec_amsdu *amsdu;
	struct tlv *tlv;

	if (vif->type != NL80211_IFTYPE_STATION &&
	    vif->type != NL80211_IFTYPE_MESH_POINT &&
	    vif->type != NL80211_IFTYPE_AP)
		return;

	if (!link_sta->agg.max_amsdu_len)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HW_AMSDU, sizeof(*amsdu));
	amsdu = (struct sta_rec_amsdu *)tlv;
	amsdu->max_amsdu_num = 31;
	amsdu->amsdu_en = true;
	msta_link->wcid.amsdu = true;

	switch (link_sta->agg.max_amsdu_len) {
	case IEEE80211_MAX_MPDU_LEN_VHT_11454:
		amsdu->max_mpdu_size =
			(msta_link->wcid.phy_idx == MT_BAND0 && !link_sta->sta->mlo) ?
			IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991:
			IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454;
		return;
	case IEEE80211_MAX_MPDU_LEN_HT_7935:
	case IEEE80211_MAX_MPDU_LEN_VHT_7991:
		amsdu->max_mpdu_size = IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991;
		return;
	default:
		amsdu->max_mpdu_size = IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_3895;
		return;
	}
}

static void
mt7999_mcu_sta_muru_tlv(struct mt7999_dev *dev, struct sk_buff *skb,
			struct ieee80211_bss_conf *link_conf,
			struct mt7999_vif_link *mconf,
			struct ieee80211_link_sta *link_sta)
{
	struct mt7999_phy *phy = mconf->phy;
	struct ieee80211_he_cap_elem *elem = &link_sta->he_cap.he_cap_elem;
	struct sta_rec_muru *muru;
	struct tlv *tlv;

	if (link_conf->vif->type != NL80211_IFTYPE_STATION &&
	    link_conf->vif->type != NL80211_IFTYPE_AP)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_MURU, sizeof(*muru));

	muru = (struct sta_rec_muru *)tlv;
	muru->cfg.mimo_dl_en = (link_conf->eht_mu_beamformer ||
				link_conf->he_mu_beamformer ||
				link_conf->vht_mu_beamformer ||
				link_conf->vht_mu_beamformee) &&
			       !!(phy->muru_onoff & MUMIMO_DL);
	muru->cfg.mimo_ul_en = !!(phy->muru_onoff & MUMIMO_UL);
	muru->cfg.ofdma_dl_en = !!(phy->muru_onoff & OFDMA_DL);
	muru->cfg.ofdma_ul_en = !!(phy->muru_onoff & OFDMA_UL);

	if (link_sta->vht_cap.vht_supported)
		muru->mimo_dl.vht_mu_bfee =
			!!(link_sta->vht_cap.cap & IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE);

	if (!link_sta->he_cap.has_he)
		return;

	muru->mimo_dl.partial_bw_dl_mimo =
		HE_PHY(CAP6_PARTIAL_BANDWIDTH_DL_MUMIMO, elem->phy_cap_info[6]);

	muru->mimo_ul.full_ul_mimo =
		HE_PHY(CAP2_UL_MU_FULL_MU_MIMO, elem->phy_cap_info[2]);
	muru->mimo_ul.partial_ul_mimo =
		HE_PHY(CAP2_UL_MU_PARTIAL_MU_MIMO, elem->phy_cap_info[2]);

	muru->ofdma_dl.punc_pream_rx =
		HE_PHY(CAP1_PREAMBLE_PUNC_RX_MASK, elem->phy_cap_info[1]);
	muru->ofdma_dl.he_20m_in_40m_2g =
		HE_PHY(CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G, elem->phy_cap_info[8]);
	muru->ofdma_dl.he_20m_in_160m =
		HE_PHY(CAP8_20MHZ_IN_160MHZ_HE_PPDU, elem->phy_cap_info[8]);
	muru->ofdma_dl.he_80m_in_160m =
		HE_PHY(CAP8_80MHZ_IN_160MHZ_HE_PPDU, elem->phy_cap_info[8]);

	muru->ofdma_ul.t_frame_dur =
		HE_MAC(CAP1_TF_MAC_PAD_DUR_MASK, elem->mac_cap_info[1]);
	muru->ofdma_ul.mu_cascading =
		HE_MAC(CAP2_MU_CASCADING, elem->mac_cap_info[2]);
	muru->ofdma_ul.uo_ra =
		HE_MAC(CAP3_OFDMA_RA, elem->mac_cap_info[3]);
	muru->ofdma_ul.rx_ctrl_frame_to_mbss =
		HE_MAC(CAP3_RX_CTRL_FRAME_TO_MULTIBSS, elem->mac_cap_info[3]);
}

static inline bool
mt7999_is_ebf_supported(struct mt7999_phy *phy,
			struct ieee80211_bss_conf *link_conf,
			struct ieee80211_link_sta *link_sta, bool bfee)
{
	int sts = hweight16(phy->mt76->chainmask);

	if (link_conf->vif->type != NL80211_IFTYPE_STATION &&
	    link_conf->vif->type != NL80211_IFTYPE_AP)
		return false;

	if (!bfee && sts < 2)
		return false;

	if (link_sta->eht_cap.has_eht) {
		struct ieee80211_sta_eht_cap *pc = &link_sta->eht_cap;
		struct ieee80211_eht_cap_elem_fixed *pe = &pc->eht_cap_elem;

		if (bfee)
			return link_conf->eht_su_beamformee &&
			       EHT_PHY(CAP0_SU_BEAMFORMER, pe->phy_cap_info[0]);
		else
			return link_conf->eht_su_beamformer &&
			       EHT_PHY(CAP0_SU_BEAMFORMEE, pe->phy_cap_info[0]);
	}

	if (link_sta->he_cap.has_he) {
		struct ieee80211_he_cap_elem *pe = &link_sta->he_cap.he_cap_elem;

		if (bfee)
			return link_conf->he_su_beamformee &&
			       HE_PHY(CAP3_SU_BEAMFORMER, pe->phy_cap_info[3]);
		else
			return link_conf->he_su_beamformer &&
			       HE_PHY(CAP4_SU_BEAMFORMEE, pe->phy_cap_info[4]);
	}

	if (link_sta->vht_cap.vht_supported) {
		u32 cap = link_sta->vht_cap.cap;

		if (bfee)
			return link_conf->vht_su_beamformee &&
			       (cap & IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE);
		else
			return link_conf->vht_su_beamformer &&
			       (cap & IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE);
	}

	return false;
}

static void
mt7999_mcu_sta_sounding_rate(struct sta_rec_bf_uni *bf, struct mt7999_phy *phy)
{
#define CFEND_RATE_OFDM_6M	0x4b
#define CFEND_RATE_OFDM_24M	0x49
	bf->sounding_phy = MT_PHY_TYPE_OFDM;
	bf->ndp_rate = 0;			/* mcs0 */
	bf->ndpa_rate = CFEND_RATE_OFDM_6M;
	bf->rept_poll_rate = CFEND_RATE_OFDM_24M;
}

static void
mt7999_mcu_sta_bfer_ht(struct ieee80211_link_sta *link_sta,
		       struct mt7999_phy *phy, struct sta_rec_bf_uni *bf,
		       bool explicit)
{
	struct ieee80211_mcs_info *mcs = &link_sta->ht_cap.mcs;
	u8 n = 0;

	bf->tx_mode = MT_PHY_TYPE_HT;

	if ((mcs->tx_params & IEEE80211_HT_MCS_TX_RX_DIFF) &&
	    (mcs->tx_params & IEEE80211_HT_MCS_TX_DEFINED))
		n = FIELD_GET(IEEE80211_HT_MCS_TX_MAX_STREAMS_MASK,
			      mcs->tx_params);
	else if (mcs->rx_mask[3])
		n = 3;
	else if (mcs->rx_mask[2])
		n = 2;
	else if (mcs->rx_mask[1])
		n = 1;

	bf->nrow = hweight8(phy->mt76->antenna_mask) - 1;
	bf->ncol = min_t(u8, bf->nrow, n);
	bf->ibf_ncol = min_t(u8, MT7999_IBF_MAX_NC, explicit ? bf->ncol : n);
}

static void
mt7999_mcu_sta_bfer_vht(struct ieee80211_link_sta *link_sta,
			struct mt7999_phy *phy, struct sta_rec_bf_uni *bf,
			bool explicit)
{
	struct ieee80211_sta_vht_cap *pc = &link_sta->vht_cap;
	struct ieee80211_sta_vht_cap *vc = &phy->mt76->sband_5g.sband.vht_cap;
	u16 mcs_map = le16_to_cpu(pc->vht_mcs.rx_mcs_map);
	u8 nss_mcs = mt7999_mcu_get_sta_nss(mcs_map);
	u8 tx_ant = hweight8(phy->mt76->antenna_mask) - 1;

	bf->tx_mode = MT_PHY_TYPE_VHT;

	if (explicit) {
		u8 sts, snd_dim;

		mt7999_mcu_sta_sounding_rate(bf, phy);

		sts = FIELD_GET(IEEE80211_VHT_CAP_BEAMFORMEE_STS_MASK,
				pc->cap);
		snd_dim = FIELD_GET(IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK,
				    vc->cap);
		bf->nrow = min_t(u8, min_t(u8, snd_dim, sts), tx_ant);
		bf->ncol = min_t(u8, nss_mcs, bf->nrow);
		bf->ibf_ncol = min_t(u8, MT7999_IBF_MAX_NC, bf->ncol);

		if (link_sta->bandwidth == IEEE80211_STA_RX_BW_160)
			bf->nrow = 1;
	} else {
		bf->nrow = tx_ant;
		bf->ncol = min_t(u8, nss_mcs, bf->nrow);
		bf->ibf_ncol = min_t(u8, MT7999_IBF_MAX_NC, nss_mcs);

		if (link_sta->bandwidth == IEEE80211_STA_RX_BW_160)
			bf->ibf_nrow = 1;
	}
}

static void
mt7999_mcu_sta_bfer_he(struct ieee80211_link_sta *link_sta,
		       struct ieee80211_vif *vif, struct mt7999_phy *phy,
		       struct sta_rec_bf_uni *bf, bool explicit)
{
	struct ieee80211_sta_he_cap *pc = &link_sta->he_cap;
	struct ieee80211_he_cap_elem *pe = &pc->he_cap_elem;
	const struct ieee80211_sta_he_cap *vc =
		mt76_connac_get_he_phy_cap(phy->mt76, vif);
	const struct ieee80211_he_cap_elem *ve = &vc->he_cap_elem;
	u16 mcs_map = le16_to_cpu(pc->he_mcs_nss_supp.rx_mcs_80);
	u8 nss_mcs = mt7999_mcu_get_sta_nss(mcs_map);
	u8 snd_dim, sts;

	if (!vc)
		return;

	bf->tx_mode = MT_PHY_TYPE_HE_SU;

	mt7999_mcu_sta_sounding_rate(bf, phy);

	bf->trigger_su = HE_PHY(CAP6_TRIG_SU_BEAMFORMING_FB,
				pe->phy_cap_info[6]);
	bf->trigger_mu = HE_PHY(CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB,
				pe->phy_cap_info[6]);
	snd_dim = HE_PHY(CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK,
			 ve->phy_cap_info[5]);
	sts = HE_PHY(CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_MASK,
		     pe->phy_cap_info[4]);
	bf->nrow = min_t(u8, snd_dim, sts);
	bf->ncol = min_t(u8, nss_mcs, bf->nrow);
	bf->ibf_ncol = explicit ? min_t(u8, MT7999_IBF_MAX_NC, bf->ncol) :
				  min_t(u8, MT7999_IBF_MAX_NC, nss_mcs);

	if (link_sta->bandwidth != IEEE80211_STA_RX_BW_160)
		return;

	/* go over for 160MHz and 80p80 */
	if (pe->phy_cap_info[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G) {
		mcs_map = le16_to_cpu(pc->he_mcs_nss_supp.rx_mcs_160);
		nss_mcs = mt7999_mcu_get_sta_nss(mcs_map);

		bf->ncol_gt_bw80 = nss_mcs;
	}

	if (pe->phy_cap_info[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G) {
		mcs_map = le16_to_cpu(pc->he_mcs_nss_supp.rx_mcs_80p80);
		nss_mcs = mt7999_mcu_get_sta_nss(mcs_map);

		if (bf->ncol_gt_bw80)
			bf->ncol_gt_bw80 = min_t(u8, bf->ncol_gt_bw80, nss_mcs);
		else
			bf->ncol_gt_bw80 = nss_mcs;
	}

	snd_dim = HE_PHY(CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_MASK,
			 ve->phy_cap_info[5]);
	sts = HE_PHY(CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_MASK,
		     pe->phy_cap_info[4]);

	bf->nrow_gt_bw80 = min_t(int, snd_dim, sts);
}

static void
mt7999_mcu_sta_bfer_eht(struct ieee80211_link_sta *link_sta,
			struct ieee80211_vif *vif, struct mt7999_phy *phy,
			struct sta_rec_bf_uni *bf, bool explicit)
{
	struct ieee80211_sta_eht_cap *pc = &link_sta->eht_cap;
	struct ieee80211_eht_cap_elem_fixed *pe = &pc->eht_cap_elem;
	struct ieee80211_eht_mcs_nss_supp *eht_nss = &pc->eht_mcs_nss_supp;
	const struct ieee80211_sta_eht_cap *vc =
		mt76_connac_get_eht_phy_cap(phy->mt76, vif);
	const struct ieee80211_eht_cap_elem_fixed *ve = &vc->eht_cap_elem;
	u8 nss_mcs = u8_get_bits(eht_nss->bw._80.rx_tx_mcs9_max_nss,
				 IEEE80211_EHT_MCS_NSS_RX) - 1;
	u8 snd_dim, sts;

	bf->tx_mode = MT_PHY_TYPE_EHT_MU;

	mt7999_mcu_sta_sounding_rate(bf, phy);

	bf->trigger_su = EHT_PHY(CAP3_TRIG_SU_BF_FDBK, pe->phy_cap_info[3]);
	bf->trigger_mu = EHT_PHY(CAP3_TRIG_MU_BF_PART_BW_FDBK, pe->phy_cap_info[3]);
	snd_dim = EHT_PHY(CAP2_SOUNDING_DIM_80MHZ_MASK, ve->phy_cap_info[2]);
	sts = EHT_PHY(CAP0_BEAMFORMEE_SS_80MHZ_MASK, pe->phy_cap_info[0]) +
	      (EHT_PHY(CAP1_BEAMFORMEE_SS_80MHZ_MASK, pe->phy_cap_info[1]) << 1);
	bf->nrow = min_t(u8, snd_dim, sts);
	bf->ncol = min_t(u8, nss_mcs, bf->nrow);
	bf->ibf_ncol = explicit ? min_t(u8, MT7999_IBF_MAX_NC, bf->ncol) :
				  min_t(u8, MT7999_IBF_MAX_NC, nss_mcs);

	if (link_sta->bandwidth < IEEE80211_STA_RX_BW_160)
		return;

	switch (link_sta->bandwidth) {
	case IEEE80211_STA_RX_BW_160:
		snd_dim = EHT_PHY(CAP2_SOUNDING_DIM_160MHZ_MASK, ve->phy_cap_info[2]);
		sts = EHT_PHY(CAP1_BEAMFORMEE_SS_160MHZ_MASK, pe->phy_cap_info[1]);
		nss_mcs = u8_get_bits(eht_nss->bw._160.rx_tx_mcs9_max_nss,
				      IEEE80211_EHT_MCS_NSS_RX) - 1;

		bf->nrow_gt_bw80 = min_t(u8, snd_dim, sts);
		bf->ncol_gt_bw80 = nss_mcs;
		break;
	case IEEE80211_STA_RX_BW_320:
		snd_dim = EHT_PHY(CAP2_SOUNDING_DIM_320MHZ_MASK, ve->phy_cap_info[2]) +
			  (EHT_PHY(CAP3_SOUNDING_DIM_320MHZ_MASK,
				   ve->phy_cap_info[3]) << 1);
		sts = EHT_PHY(CAP1_BEAMFORMEE_SS_320MHZ_MASK, pe->phy_cap_info[1]);
		nss_mcs = u8_get_bits(eht_nss->bw._320.rx_tx_mcs9_max_nss,
				      IEEE80211_EHT_MCS_NSS_RX) - 1;

		bf->nrow_gt_bw80 = min_t(u8, snd_dim, sts) << 4;
		bf->ncol_gt_bw80 = nss_mcs << 4;
		break;
	default:
		break;
	}
}

static void
mt7999_mcu_sta_bfer_tlv(struct mt7999_dev *dev, struct sk_buff *skb,
			struct ieee80211_bss_conf *link_conf,
			struct ieee80211_link_sta *link_sta,
			struct mt7999_vif_link *link)
{
#define EBF_MODE	BIT(0)
#define IBF_MODE	BIT(1)
#define BF_MAT_ORDER	4
	struct mt7999_phy *phy = mt7999_vif_link_phy(link);
	struct ieee80211_vif *vif = link_conf->vif;
	struct sta_rec_bf_uni *bf;
	struct tlv *tlv;
	static const u8 matrix[BF_MAT_ORDER][BF_MAT_ORDER] = {
		{0, 0, 0, 0},
		{1, 1, 0, 0},	/* 2x1, 2x2, 2x3, 2x4 */
		{2, 4, 4, 0},	/* 3x1, 3x2, 3x3, 3x4 */
		{3, 5, 6, 0}	/* 4x1, 4x2, 4x3, 4x4 */
	};
	int tx_ant;
	bool ebf;

	if (!phy)
		return;

	if (!(link_sta->ht_cap.ht_supported || link_sta->he_cap.has_he))
		return;

	ebf = mt7999_is_ebf_supported(phy, link_conf, link_sta, false);
	if (!ebf && !dev->ibf)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_BF, sizeof(*bf));
	bf = (struct sta_rec_bf_uni *)tlv;

	/* he/eht: eBF only
	 * vht: support eBF and iBF
	 * ht: iBF only, since mac80211 lacks of eBF support
	 */
	if (link_sta->eht_cap.has_eht)
		mt7999_mcu_sta_bfer_eht(link_sta, vif, phy, bf, ebf);
	else if (link_sta->he_cap.has_he)
		mt7999_mcu_sta_bfer_he(link_sta, vif, phy, bf, ebf);
	else if (link_sta->vht_cap.vht_supported)
		mt7999_mcu_sta_bfer_vht(link_sta, phy, bf, ebf);
	else if (link_sta->ht_cap.ht_supported)
		mt7999_mcu_sta_bfer_ht(link_sta, phy, bf, ebf);
	else
		return;

	bf->bf_cap = ebf ? EBF_MODE : (dev->ibf ? IBF_MODE : 0);
	tx_ant = hweight16(phy->mt76->chainmask) - 1;
	bf->bw = link_sta->bandwidth;
	bf->ibf_dbw = link_sta->bandwidth;
	bf->ibf_nrow = tx_ant;

	if (!ebf && link_sta->bandwidth <= IEEE80211_STA_RX_BW_40 && !bf->ncol)
		bf->ibf_timeout = MT7999_IBF_TIMEOUT_LEGACY;
	else
		bf->ibf_timeout = MT7999_IBF_TIMEOUT;

	if (bf->ncol < BF_MAT_ORDER) {
		if (ebf)
			bf->mem_20m = tx_ant < BF_MAT_ORDER ?
				      matrix[tx_ant][bf->ncol] : 0;
		else
			bf->mem_20m = bf->nrow < BF_MAT_ORDER ?
				      matrix[bf->nrow][bf->ncol] : 0;
	}
}

static void
mt7999_mcu_sta_bfee_tlv(struct mt7999_dev *dev, struct sk_buff *skb,
			struct ieee80211_bss_conf *link_conf,
			struct ieee80211_link_sta *link_sta,
			struct mt7999_vif_link *link)
{
	struct mt7999_phy *phy = mt7999_vif_link_phy(link);
	struct sta_rec_bfee *bfee;
	struct tlv *tlv;
	u8 nrow = 0;
	int tx_ant;

	if (!phy)
		return;

	if (!(link_sta->vht_cap.vht_supported || link_sta->he_cap.has_he))
		return;

	if (!mt7999_is_ebf_supported(phy, link_conf, link_sta, true))
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_BFEE, sizeof(*bfee));
	bfee = (struct sta_rec_bfee *)tlv;

	if (link_sta->he_cap.has_he) {
		struct ieee80211_he_cap_elem *pe = &link_sta->he_cap.he_cap_elem;

		nrow = HE_PHY(CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK,
			      pe->phy_cap_info[5]);
	} else if (link_sta->vht_cap.vht_supported) {
		struct ieee80211_sta_vht_cap *pc = &link_sta->vht_cap;

		nrow = FIELD_GET(IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK,
				 pc->cap);
	}

	/* reply with identity matrix to avoid 2x2 BF negative gain */
	tx_ant = hweight8(phy->mt76->antenna_mask) - 1;
	bfee->fb_identity_matrix = (nrow == 1 && tx_ant == 2);
}

static void
mt7999_mcu_sta_tx_proc_tlv(struct sk_buff *skb)
{
	struct sta_rec_tx_proc *tx_proc;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_TX_PROC, sizeof(*tx_proc));

	tx_proc = (struct sta_rec_tx_proc *)tlv;
	tx_proc->flag = cpu_to_le32(0);
}

static void
mt7999_mcu_sta_hdrt_tlv(struct mt7999_dev *dev, struct sk_buff *skb)
{
	struct sta_rec_hdrt *hdrt;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HDRT, sizeof(*hdrt));

	hdrt = (struct sta_rec_hdrt *)tlv;
	hdrt->hdrt_mode = 1;
}

static void
mt7999_mcu_sta_hdr_trans_tlv(struct mt7999_dev *dev, struct sk_buff *skb,
			     struct ieee80211_vif *vif, struct mt76_wcid *wcid)
{
	struct sta_rec_hdr_trans *hdr_trans;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HDR_TRANS, sizeof(*hdr_trans));
	hdr_trans = (struct sta_rec_hdr_trans *)tlv;
	hdr_trans->dis_rx_hdr_tran = true;

	if (!wcid->sta)
		return;

	if (vif->type == NL80211_IFTYPE_STATION)
		hdr_trans->to_ds = true;
	else
		hdr_trans->from_ds = true;

	hdr_trans->dis_rx_hdr_tran = !test_bit(MT_WCID_FLAG_HDR_TRANS, &wcid->flags);
	if (test_bit(MT_WCID_FLAG_4ADDR, &wcid->flags)) {
		hdr_trans->to_ds = true;
		hdr_trans->from_ds = true;
	}

	if (vif->type == NL80211_IFTYPE_MESH_POINT) {
		hdr_trans->to_ds = true;
		hdr_trans->from_ds = true;
		hdr_trans->mesh = true;
	}
}

static enum mcu_mmps_mode
mt7999_mcu_get_mmps_mode(enum ieee80211_smps_mode smps)
{
	switch (smps) {
	case IEEE80211_SMPS_OFF:
		return MCU_MMPS_DISABLE;
	case IEEE80211_SMPS_STATIC:
		return MCU_MMPS_STATIC;
	case IEEE80211_SMPS_DYNAMIC:
		return MCU_MMPS_DYNAMIC;
	default:
		return MCU_MMPS_DISABLE;
	}
}

int mt7999_mcu_set_ra_option_ctrl(struct mt7999_dev *dev, u8 option, u8 val)
{
	struct {
		u8 __rsv1[4];

		__le16 tag;
		__le16 len;
		__le16 version;

		u8 option;
		u8 val;
	} __packed req = {
		.tag = cpu_to_le16(UNI_RA_OPTION_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),
		.option = option,
		.val = val,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(RA), &req,
				 sizeof(req), false);
}

int mt7999_mcu_set_fixed_rate_ctrl(struct mt7999_dev *dev, void *data,
				   u16 tag, u16 version)
{
	struct uni_header hdr = {};
	struct ra_fixed_rate *req;
	struct sk_buff *skb;
	struct tlv *tlv;
	int len;

	len = sizeof(hdr) + sizeof(*req);

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));

	tlv = mt7999_mcu_add_uni_tlv(skb, tag, sizeof(*req));
	req = (struct ra_fixed_rate *)tlv;
	req->version = cpu_to_le16(version);

	switch (tag) {
	case UNI_RA_FIXED_RATE_SET:
		memcpy(&req->set, data, sizeof(struct ra_fixed_rate_set));
		break;
	case UNI_RA_FIXED_RATE_RESET:
		memcpy(&req->reset, data, sizeof(struct ra_fixed_rate_reset));
		break;
	case UNI_RA_FIXED_RATE_OPT_PARAM_SET:
		memcpy(&req->opt_param_set, data,
		       sizeof(struct ra_fixed_rate_opt_param_set));
		break;
	default:
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, MCU_WM_UNI_CMD(RA), true);
}

int mt7999_mcu_sta_rate_update(struct mt7999_dev *dev, struct mt7999_sta *msta,
			       u8 link_id, u32 field)
{
	struct mt7999_vif *mvif = msta->vif;
	struct sta_rec_ra_fixed_uni *ra;
	struct mt7999_sta_link *msta_link;
	struct ieee80211_link_sta *link_sta;
	struct mt76_vif_link *mlink;
	struct ieee80211_sta *sta;
	struct sk_buff *skb;
	struct tlv *tlv;
	int err = -ENODEV;

	rcu_read_lock();

	mlink = rcu_dereference(mvif->mt76.link[link_id]);
	if (!mlink)
		goto error_unlock;

	msta_link = mt7999_sta_link(msta, link_id);
	if (!msta_link)
		goto error_unlock;

	sta = wcid_to_sta(&msta_link->wcid);
	link_sta = rcu_dereference(sta->link[link_id]);
	if (!link_sta)
		goto error_unlock;

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, mlink,
					      &msta_link->wcid,
					      MT7999_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		goto error_unlock;
	}

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_RA_UPDATE, sizeof(*ra));
	ra = (struct sta_rec_ra_fixed_uni *)tlv;

	switch (field) {
	case RATE_PARAM_AUTO:
		break;
	case RATE_PARAM_MMPS_UPDATE:
		ra->mmps_mode = mt7999_mcu_get_mmps_mode(link_sta->smps_mode);
		break;
	case RATE_PARAM_VHT_OMN_UPDATE:
		ra->op_mode = true;
		switch (link_sta->bandwidth) {
		case IEEE80211_STA_RX_BW_20:
			ra->op_vht_chan_width =
				IEEE80211_OPMODE_NOTIF_CHANWIDTH_20MHZ;
			break;
		case IEEE80211_STA_RX_BW_40:
			ra->op_vht_chan_width =
				IEEE80211_OPMODE_NOTIF_CHANWIDTH_40MHZ;
			break;
		case IEEE80211_STA_RX_BW_80:
			ra->op_vht_chan_width =
				IEEE80211_OPMODE_NOTIF_CHANWIDTH_80MHZ;
			break;
		case IEEE80211_STA_RX_BW_160:
			ra->op_vht_chan_width =
				IEEE80211_OPMODE_NOTIF_CHANWIDTH_160MHZ;
			break;
		default:
			err = -EINVAL;
			goto error_unlock;
		}
		ra->op_vht_rx_nss = link_sta->rx_nss ? link_sta->rx_nss - 1 : 0;
		break;
	default:
		break;
	}
	ra->field = cpu_to_le32(field);

	rcu_read_unlock();

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(STA_REC_UPDATE), true);
error_unlock:
	rcu_read_unlock();

	return err;
}

static int
mt7999_count_mcs_rates(const u16 *mcs_mask, int nss_max,
		       u8 *out_mcs, u8 *out_nss)
{
	int i, nrates = 0;

	for (i = 0; i < nss_max; i++) {
		if (!mcs_mask[i])
			continue;
		nrates += hweight16(mcs_mask[i]);
		if (nrates > 1)
			return nrates;
		*out_mcs = ffs(mcs_mask[i]) - 1;
		*out_nss = i;
	}

	return nrates;
}

static int
mt7999_count_ht_rates(const u8 *mcs_mask, u8 *out_mcs, u8 *out_nss)
{
	int i, nrates = 0;

	for (i = 0; i < IEEE80211_HT_MCS_MASK_LEN; i++) {
		if (!mcs_mask[i])
			continue;
		nrates += hweight8(mcs_mask[i]);
		if (nrates > 1)
			return nrates;
		*out_mcs = ffs(mcs_mask[i]) - 1;
		*out_nss = i;
	}

	return nrates;
}

/* n > 1 means a rate range (e.g. MCS 0-7), which firmware does not support. */
static bool
mt7999_find_fixed_rate(struct ieee80211_link_sta *link_sta,
		       const struct cfg80211_bitrate_mask *mask,
		       enum nl80211_band band,
		       struct ra_rate_params *params)
{
	typeof(mask->control[0]) *ctrl = &mask->control[band];
	u8 mcs = 0, nss = 0;
	int n;

	if (link_sta->eht_cap.has_eht) {
		n = mt7999_count_mcs_rates(ctrl->eht_mcs, NL80211_EHT_NSS_MAX,
					   &mcs, &nss);
		if (n) {
			params->phy_mode = RA_PHY_MODE_EHT;
			params->mcs = mcs;
			params->nss = nss;
			return n == 1;
		}
	}

	if (link_sta->he_cap.has_he) {
		n = mt7999_count_mcs_rates(ctrl->he_mcs, NL80211_HE_NSS_MAX,
					   &mcs, &nss);
		if (n) {
			params->phy_mode = RA_PHY_MODE_HE;
			params->mcs = mcs;
			params->nss = nss;
			return n == 1;
		}
	}

	if (link_sta->vht_cap.vht_supported) {
		n = mt7999_count_mcs_rates(ctrl->vht_mcs, NL80211_VHT_NSS_MAX,
					   &mcs, &nss);
		if (n) {
			params->phy_mode = RA_PHY_MODE_VHT;
			params->mcs = mcs;
			params->nss = nss;
			return n == 1;
		}
	}

	if (link_sta->ht_cap.ht_supported) {
		n = mt7999_count_ht_rates(ctrl->ht_mcs, &mcs, &nss);
		if (n) {
			params->phy_mode = RA_PHY_MODE_HT;
			params->mcs = mcs;
			params->nss = nss;
			return n == 1;
		}
	}

	n = hweight32(ctrl->legacy);
	if (n == 1) {
		int idx = ffs(ctrl->legacy) - 1;

		if (idx < 4 && band == NL80211_BAND_2GHZ) {
			params->phy_mode = RA_PHY_MODE_CCK;
			params->mcs = idx;
		} else {
			params->phy_mode = RA_PHY_MODE_OFDM;
			params->mcs = (band == NL80211_BAND_2GHZ) ? idx - 4 : idx;
		}
		return true;
	}

	return false;
}

int mt7999_mcu_set_fixed_rate_param(struct mt7999_dev *dev,
				    struct mt7999_sta *msta,
				    struct ra_rate_params *params,
				    u8 link_id, u16 bitmap)
{
	struct ra_fixed_rate_opt_param_set rate = {};
	struct mt7999_sta_link *msta_link;

	lockdep_assert_held(&dev->mt76.mutex);

	msta_link = mt7999_sta_link_protected(dev, msta, link_id);
	if (!msta_link)
		return -ENODEV;

	rate.hdr.wlan_idx[0] = cpu_to_le16(msta_link->wcid.idx);
	rate.hdr.target_wlan_idx = cpu_to_le16(msta_link->wcid.idx);
	rate.hdr.wlan_idx_num = 1;
	rate.hdr.ac_idx_bitmap = GENMASK(IEEE80211_NUM_ACS - 1, 0);
	rate.opt_param_bitmap = cpu_to_le16(bitmap);
	rate.params = *params;

	return mt7999_mcu_set_fixed_rate_ctrl(dev, &rate,
					      UNI_RA_FIXED_RATE_OPT_PARAM_SET, 0);
}

static int
mt7999_mcu_add_rate_ctrl_fixed(struct mt7999_dev *dev, struct mt7999_sta *msta,
			       struct ieee80211_vif *vif, u8 link_id)
{
	struct cfg80211_bitrate_mask mask;
	struct ieee80211_link_sta *link_sta;
	struct mt7999_sta_link *msta_link;
	struct mt7999_vif_link *link;
	struct ra_rate_params params = {};
	typeof(mask.control[0]) *ctrl;
	struct ieee80211_sta *sta;
	enum nl80211_band band;
	struct mt76_phy *mphy;
	bool fixed_rate;
	u16 bitmap = 0;

	rcu_read_lock();

	link = mt7999_vif_link(dev, vif, link_id);
	if (!link)
		goto error_unlock;

	msta_link = mt7999_sta_link(msta, link_id);
	if (!msta_link)
		goto error_unlock;

	sta = wcid_to_sta(&msta_link->wcid);
	link_sta = rcu_dereference(sta->link[link_id]);
	if (!link_sta)
		goto error_unlock;

	mphy = mt76_vif_link_phy(&link->mt76);
	if (!mphy)
		goto error_unlock;

	band = mphy->chandef.chan->band;
	mask = link->bitrate_mask;

	fixed_rate = mt7999_find_fixed_rate(link_sta, &mask, band, &params);

	rcu_read_unlock();

	ctrl = &mask.control[band];

	/* HE/EHT GI 0 is 0.8us, but firmware GI 0 is 0.4us (legacy SGI) */
	if (ctrl->eht_gi != 0xff)
		params.gi = ctrl->eht_gi + 1;
	else if (ctrl->he_gi != 0xff)
		params.gi = ctrl->he_gi + 1;
	else if (ctrl->gi == NL80211_TXRATE_FORCE_SGI)
		params.gi = 0;
	else if (ctrl->gi == NL80211_TXRATE_FORCE_LGI)
		params.gi = 1;

	if (ctrl->eht_ltf != 0xff)
		params.ltf = ctrl->eht_ltf;
	else if (ctrl->he_ltf != 0xff)
		params.ltf = ctrl->he_ltf;

	if (fixed_rate) {
		bitmap |= RA_OPT_PARAM_PHYMODE | RA_OPT_PARAM_MCS;
		if (params.phy_mode >= RA_PHY_MODE_HT)
			bitmap |= RA_OPT_PARAM_NSS;
	}

	if (ctrl->gi != NL80211_TXRATE_DEFAULT_GI ||
	    ctrl->he_gi != 0xff || ctrl->eht_gi != 0xff)
		bitmap |= RA_OPT_PARAM_GI;

	if (ctrl->he_ltf != 0xff || ctrl->eht_ltf != 0xff)
		bitmap |= RA_OPT_PARAM_LTF;

	if (!bitmap)
		return 0;

	return mt7999_mcu_set_fixed_rate_param(dev, msta, &params, link_id, bitmap);

error_unlock:
	rcu_read_unlock();

	return -ENODEV;
}

static void
mt7999_mcu_sta_rate_ctrl_tlv(struct sk_buff *skb, struct mt7999_dev *dev,
			     struct ieee80211_vif *vif,
			     struct ieee80211_bss_conf *link_conf,
			     struct ieee80211_link_sta *link_sta,
			     struct mt7999_vif_link *link)
{
#define INIT_RCPI	180
	enum ieee80211_sta_rx_bandwidth cap_bw = ieee80211_link_sta_cap_bw(link_sta);
	struct mt76_phy *mphy = mt76_vif_link_phy(&link->mt76);
	struct cfg80211_bitrate_mask *mask = &link->bitrate_mask;
	u32 cap = link_sta->sta->wme ? STA_CAP_WMM : 0;
	u8 cap_nss = ieee80211_link_sta_cap_nss(link_sta);
	struct cfg80211_chan_def *chandef;
	struct sta_rec_ra_uni *ra;
	enum nl80211_band band;
	struct tlv *tlv;
	u32 supp_rate;

	if (!mphy)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_RA, sizeof(*ra));
	ra = (struct sta_rec_ra_uni *)tlv;

	chandef = &mphy->chandef;
	band = chandef->chan->band;
	supp_rate = link_sta->supp_rates[band];

	ra->valid = true;
	ra->auto_rate = true;
	ra->phy_mode = mt76_connac_get_phy_mode(mphy, vif, band, link_sta);
	ra->channel = chandef->chan->hw_value;
	ra->bw = mt76_connac_chan_bw(chandef);
	ra->phy.bw = (cap_bw == IEEE80211_STA_RX_BW_320) ? CMD_CBW_320MHZ : cap_bw;
	ra->mmps_mode = mt7999_mcu_get_mmps_mode(link_sta->smps_mode);
	ra->op_mode = cap_bw != link_sta->bandwidth || cap_nss != link_sta->rx_nss;
	ra->op_vht_chan_width = link_sta->bandwidth;
	ra->op_vht_rx_nss = link_sta->rx_nss - 1;
	ra->op_vht_rx_nss_type = 0;

	if (supp_rate) {
		supp_rate &= mask->control[band].legacy;
		ra->rate_len = hweight32(supp_rate);

		if (band == NL80211_BAND_2GHZ) {
			ra->supp_mode = MODE_CCK;
			ra->supp_cck_rate = supp_rate & GENMASK(3, 0);

			if (ra->rate_len > 4) {
				ra->supp_mode |= MODE_OFDM;
				ra->supp_ofdm_rate = supp_rate >> 4;
			}
		} else {
			ra->supp_mode = MODE_OFDM;
			ra->supp_ofdm_rate = supp_rate;
		}
	}

	if (link_sta->ht_cap.ht_supported) {
		ra->supp_mode |= MODE_HT;
		ra->af = link_sta->ht_cap.ampdu_factor;
		ra->ht_gf = !!(link_sta->ht_cap.cap & IEEE80211_HT_CAP_GRN_FLD);

		cap |= STA_CAP_HT;
		if (link_sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20)
			cap |= STA_CAP_SGI_20;
		if (link_sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40)
			cap |= STA_CAP_SGI_40;
		if (link_sta->ht_cap.cap & IEEE80211_HT_CAP_TX_STBC)
			cap |= STA_CAP_TX_STBC;
		if (link_sta->ht_cap.cap & IEEE80211_HT_CAP_RX_STBC)
			cap |= STA_CAP_RX_STBC;
		if (link_conf->ht_ldpc &&
		    (link_sta->ht_cap.cap & IEEE80211_HT_CAP_LDPC_CODING))
			cap |= STA_CAP_LDPC;

		mt7999_mcu_set_sta_ht_mcs(link_sta, ra->ht_mcs,
					  mask->control[band].ht_mcs);
		ra->supp_ht_mcs = *(__le32 *)ra->ht_mcs;
	}

	if (link_sta->vht_cap.vht_supported) {
		u8 af;

		ra->supp_mode |= MODE_VHT;
		af = FIELD_GET(IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK,
			       link_sta->vht_cap.cap);
		ra->af = max_t(u8, ra->af, af);

		cap |= STA_CAP_VHT;
		if (link_sta->vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_80)
			cap |= STA_CAP_VHT_SGI_80;
		if (link_sta->vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_160)
			cap |= STA_CAP_VHT_SGI_160;
		if (link_sta->vht_cap.cap & IEEE80211_VHT_CAP_TXSTBC)
			cap |= STA_CAP_VHT_TX_STBC;
		if (link_sta->vht_cap.cap & IEEE80211_VHT_CAP_RXSTBC_1)
			cap |= STA_CAP_VHT_RX_STBC;
		if ((vif->type != NL80211_IFTYPE_AP || link_conf->vht_ldpc) &&
		    (link_sta->vht_cap.cap & IEEE80211_VHT_CAP_RXLDPC))
			cap |= STA_CAP_VHT_LDPC;

		mt7999_mcu_set_sta_vht_mcs(link_sta, ra->supp_vht_mcs,
					   mask->control[band].vht_mcs);
	}

	if (link_sta->he_cap.has_he) {
		ra->supp_mode |= MODE_HE;
		cap |= STA_CAP_HE;

		if (link_sta->he_6ghz_capa.capa)
			ra->af = le16_get_bits(link_sta->he_6ghz_capa.capa,
					       IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP);
	}
	ra->sta_cap = cpu_to_le32(cap);

	memset(ra->rx_rcpi, INIT_RCPI, sizeof(ra->rx_rcpi));
}

int mt7999_mcu_add_rate_ctrl(struct mt7999_dev *dev, struct mt7999_sta *msta,
			     struct ieee80211_vif *vif, u8 link_id,
			     bool changed)
{
	struct ieee80211_bss_conf *link_conf;
	struct ieee80211_link_sta *link_sta;
	struct mt7999_sta_link *msta_link;
	struct mt7999_vif_link *link;
	struct ieee80211_sta *sta;
	struct sk_buff *skb;
	int ret = -ENODEV;

#ifdef CONFIG_MTK_VENDOR
	if (changed && dev->cert_mode == 2)
		return mt7999_mcu_add_rate_ctrl_fixed(dev, msta, vif, link_id);
#endif

	rcu_read_lock();

	link = mt7999_vif_link(dev, vif, link_id);
	if (!link)
		goto error_unlock;

	msta_link = mt7999_sta_link(msta, link_id);
	if (!msta_link)
		goto error_unlock;

	sta = wcid_to_sta(&msta_link->wcid);
	link_sta = rcu_dereference(sta->link[link_id]);
	if (!link_sta)
		goto error_unlock;

	link_conf = rcu_dereference(vif->link_conf[link_id]);
	if (!link_conf)
		goto error_unlock;

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, &link->mt76,
					      &msta_link->wcid,
					      MT7999_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb)) {
		ret = PTR_ERR(skb);
		goto error_unlock;
	}

	/* firmware rc algorithm refers to sta_rec_he for HE control.
	 * once dev->rc_work changes the settings driver should also
	 * update sta_rec_he here.
	 */
	if (changed)
		mt7999_mcu_sta_he_tlv(skb, link_conf, link_sta, link);

	/* sta_rec_ra accommodates BW, NSS and only MCS range format
	 * i.e 0-{7,8,9} for VHT.
	 */
	mt7999_mcu_sta_rate_ctrl_tlv(skb, dev, vif, link_conf, link_sta, link);

	rcu_read_unlock();

	ret = mt76_mcu_skb_send_msg(&dev->mt76, skb,
				    MCU_WMWA_UNI_CMD(STA_REC_UPDATE), true);
	if (ret)
		return ret;

	return mt7999_mcu_add_rate_ctrl_fixed(dev, msta, vif, link_id);

error_unlock:
	rcu_read_unlock();

	return ret;
}

int mt7999_mcu_mld_reconf_stop_link(struct mt7999_dev *dev,
				    struct ieee80211_vif *vif,
				    u16 removed_links)
{
	unsigned long rem_links = removed_links;
	struct mld_reconf_stop_link *sl;
	struct mld_req_hdr hdr = {};
	unsigned int link_id;
	struct sk_buff *skb;
	struct tlv *tlv;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, sizeof(hdr) + sizeof(*sl));
	if (!skb)
		return -ENOMEM;

	memcpy(hdr.mld_addr, vif->addr, ETH_ALEN);
	skb_put_data(skb, &hdr, sizeof(hdr));

	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_MLD_RECONF_STOP_LINK, sizeof(*sl));
	sl = (struct mld_reconf_stop_link *)tlv;
	sl->link_bitmap = cpu_to_le16(removed_links);

	for_each_set_bit(link_id, &rem_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt7999_vif_link *link;

		link = mt7999_vif_link(dev, vif, link_id);
		if (!link)
			continue;

		sl->bss_idx[link_id] = link->mt76.idx;
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, MCU_WM_UNI_CMD(MLD),
				     true);
}

int mt7999_mcu_mld_link_oper(struct mt7999_dev *dev,
			     struct ieee80211_bss_conf *link_conf,
			     struct mt7999_vif_link *link, bool add)
{
	struct ieee80211_vif *vif = link_conf->vif;
	struct mt7999_vif *mvif = (struct mt7999_vif *)vif->drv_priv;
	struct bss_mld_link_op_tlv *mld_op;
	struct sk_buff *skb;
	struct tlv *tlv;

	skb = __mt7999_mcu_alloc_bss_req(&dev->mt76, &link->mt76,
					 MT7999_BSS_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_BSS_INFO_MLD_LINK_OP,
				     sizeof(*mld_op));
	mld_op = (struct bss_mld_link_op_tlv *)tlv;
	mld_op->link_operation = add;
	mld_op->own_mld_id = link->mld_idx;
	mld_op->link_id = link_conf->link_id;
	mld_op->group_mld_id = add ? mvif->mld_group_idx : 0xff;
	mld_op->remap_idx = add ? mvif->mld_remap_idx : 0xff;
	memcpy(mld_op->mac_addr, vif->addr, ETH_ALEN);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(BSS_INFO_UPDATE), true);
}

static void
mt7999_mcu_sta_mld_setup_tlv(struct mt7999_dev *dev, struct sk_buff *skb,
			     struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta)
{
	struct mt7999_sta *msta = (struct mt7999_sta *)sta->drv_priv;
	unsigned int nlinks = hweight16(sta->valid_links);
	struct mld_setup_link *mld_setup_link;
	struct ieee80211_link_sta *link_sta;
	struct sta_rec_mld_setup *mld_setup;
	struct mt7999_sta_link *msta_link;
	unsigned int link_id;
	struct tlv *tlv;

	msta_link = mt7999_sta_link_protected(dev, msta, msta->deflink_id);
	if (!msta_link)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_MLD,
				      sizeof(struct sta_rec_mld_setup) +
				      sizeof(struct mld_setup_link) * nlinks);

	mld_setup = (struct sta_rec_mld_setup *)tlv;
	memcpy(mld_setup->mld_addr, sta->addr, ETH_ALEN);
	mld_setup->setup_wcid = cpu_to_le16(msta_link->wcid.idx);
	mld_setup->primary_id = cpu_to_le16(msta_link->wcid.idx);

	if (nlinks > 1) {
		msta_link = mt7999_sta_link_protected(dev, msta,
						      msta->seclink_id);
		if (!msta_link)
			return;
	}
	mld_setup->second_id = cpu_to_le16(msta_link->wcid.idx);
	mld_setup->link_num = nlinks;
	/* the flag is used for informing FW to use PEER_MLD for MLD setup, and
	 * the MLD_SETUP command is now used mainly for link addition
	 */
	mld_setup->extra_info = EXTRA_INFO_MLD_SKIP;

	mld_setup_link = (struct mld_setup_link *)mld_setup->link_info;
	mt76_sta_dbg(vif, "STA %pM pri_link=%u, pri_wcid=%u, sec_link=%u, sec_wcid=%u",
		     sta->addr, msta->deflink_id, le16_to_cpu(mld_setup->primary_id),
		     msta->seclink_id, le16_to_cpu(mld_setup->second_id));
	for_each_sta_active_link(vif, sta, link_sta, link_id) {
		struct mt7999_vif_link *link;

		msta_link = mt7999_sta_link_protected(dev, msta, link_id);
		if (!msta_link)
			continue;

		link = mt7999_vif_link(dev, vif, link_id);
		if (!link)
			continue;

		mld_setup_link->wcid = cpu_to_le16(msta_link->wcid.idx);
		mld_setup_link->bss_idx = link->mt76.idx;

		mt76_sta_dbg(vif, "link_id(%d) wcid(%d) bss_idx(%d)",
			     link_id, mld_setup_link->wcid, mld_setup_link->bss_idx);

		mld_setup_link++;
	}
}

static void
mt7999_mcu_sta_eht_mld_tlv(struct mt7999_dev *dev, struct sk_buff *skb,
			   struct ieee80211_sta *sta)
{
	struct sta_rec_eht_mld *eht_mld;
	struct tlv *tlv;
	int i;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_EHT_MLD, sizeof(*eht_mld));
	eht_mld = (struct sta_rec_eht_mld *)tlv;

	for (i = 0; i < ARRAY_SIZE(eht_mld->str_cap); i++)
		eht_mld->str_cap[i] = 0x7;

	eht_mld->eml_cap = cpu_to_le16(sta->eml_cap);
}

static void
mt7999_mcu_sta_mesh_tlv(struct mt7999_dev *dev, struct sk_buff *skb,
			struct ieee80211_vif *vif)
{
	struct sta_rec_link_mesh *link_mesh;
	struct sta_rec_peer_mesh *peer_mesh;
	struct tlv *tlv;
	u32 config_bitmap = 0;

	if (vif->type != NL80211_IFTYPE_MESH_POINT)
		return;

	/* FIXME Do not support 11s + MLD */
	if (ieee80211_vif_is_mld(vif))
		return;

	/* Set UWTBL first */
	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_SET_PEER_MESH, sizeof(*peer_mesh));
	peer_mesh = (struct sta_rec_peer_mesh *)tlv;
	config_bitmap |= u32_encode_bits(dev->mesh_hw_amsdu_ctrl.de_amsdu,
					 UWTBL_MESH_RX_HW_DE_AMSDU) |
			 u32_encode_bits(dev->mesh_hw_amsdu_ctrl.subfrm_len_type,
					 UWTBL_MESH_AMSDU_SUBFRM_LEN_TYPE) |
			 u32_encode_bits(dev->mesh_hw_amsdu_ctrl.subfrm_len_endian,
					 UWTBL_MESH_AMSDU_SUBFRM_LEN_ENDIAN);
	peer_mesh->config_bitmap = cpu_to_le32(config_bitmap);
	peer_mesh->update_mask = cpu_to_le32(UWTBL_MESH_RX_HW_DE_AMSDU |
					     UWTBL_MESH_AMSDU_SUBFRM_LEN_TYPE |
					     UWTBL_MESH_AMSDU_SUBFRM_LEN_ENDIAN);

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_SET_LINK_MESH, sizeof(*link_mesh));
	link_mesh = (struct sta_rec_link_mesh *)tlv;
	config_bitmap = u32_encode_bits(LWTBL_MESH_ENABLE, LWTBL_MESH);
	link_mesh->config_bitmap = cpu_to_le32(config_bitmap);
	link_mesh->update_mask = cpu_to_le32(LWTBL_MESH);
}

int mt7999_mcu_add_sta(struct mt7999_dev *dev,
		       struct ieee80211_bss_conf *link_conf,
		       struct ieee80211_link_sta *link_sta,
		       struct mt7999_vif_link *link,
		       struct mt7999_sta_link *msta_link,
		       int conn_state, bool newly)
{
	struct mt76_wcid *wcid = msta_link ? &msta_link->wcid : link->mt76.wcid;
	struct ieee80211_sta *sta = link_sta ? link_sta->sta : NULL;
	struct sk_buff *skb;

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, &link->mt76, wcid,
					      MT7999_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	/* starec basic */
	mt76_connac_mcu_sta_basic_tlv(&dev->mt76, skb, link_conf, link_sta,
				      conn_state, newly);

	mt76_sta_dbg(link_conf->vif, "link=%u, wcid=%u, newly=%d, conn_state=%d",
		     wcid->link_id, wcid->idx, newly, conn_state);

	if (conn_state == CONN_STATE_DISCONNECT)
		goto out;

	/* starec hdr trans */
	mt7999_mcu_sta_hdr_trans_tlv(dev, skb, link_conf->vif, wcid);
	/* starec tx proc */
	mt7999_mcu_sta_tx_proc_tlv(skb);

	/* tag order is in accordance with firmware dependency. */
	if (link_sta) {
		/* starec hdrt mode */
		mt7999_mcu_sta_hdrt_tlv(dev, skb);
		if (conn_state == CONN_STATE_CONNECT) {
			/* starec bfer */
			mt7999_mcu_sta_bfer_tlv(dev, skb, link_conf, link_sta,
						link);
			/* starec bfee */
			mt7999_mcu_sta_bfee_tlv(dev, skb, link_conf, link_sta,
						link);
		}
		/* starec ht */
		mt7999_mcu_sta_ht_tlv(skb, link_sta);
		/* starec vht */
		mt7999_mcu_sta_vht_tlv(skb, link_sta);
		/* starec uapsd */
		mt76_connac_mcu_sta_uapsd(skb, link_conf->vif, sta);
		/* starec amsdu */
		mt7999_mcu_sta_amsdu_tlv(dev, skb, link_conf->vif, link_sta,
					 msta_link);
		/* starec he */
		mt7999_mcu_sta_he_tlv(skb, link_conf, link_sta, link);
		/* starec he 6g*/
		mt7999_mcu_sta_he_6g_tlv(skb, link_sta);
		/* starec eht */
		mt7999_mcu_sta_eht_tlv(skb, link_sta);
		/* starec muru */
		mt7999_mcu_sta_muru_tlv(dev, skb, link_conf, link, link_sta);

		if (sta->mlo) {
			mt7999_mcu_sta_mld_setup_tlv(dev, skb, link_conf->vif,
						     sta);
			mt7999_mcu_sta_eht_mld_tlv(dev, skb, sta);
		}

		/* starec uhr */
		mt7999_mcu_sta_uhr_tlv(skb, link_sta);

		/* starec mesh */
		mt7999_mcu_sta_mesh_tlv(dev, skb, link_conf->vif);
	}
out:
	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(STA_REC_UPDATE), true);
}

int mt7999_mcu_teardown_mld_sta(struct mt7999_dev *dev,
				struct mt7999_vif_link *link,
				struct mt7999_sta_link *msta_link)
{
	struct sk_buff *skb;

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, &link->mt76,
					      &msta_link->wcid,
					      MT7999_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	mt76_connac_mcu_add_tlv(skb, STA_REC_MLD_OFF, sizeof(struct tlv));

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(STA_REC_UPDATE), true);
}

void mt7999_mcu_update_sta_rec_bw(void *data, struct ieee80211_sta *sta)
{
	struct mt7999_vif_link *link = (struct mt7999_vif_link *)data;
	struct mt7999_sta *msta = (struct mt7999_sta *)sta->drv_priv;
	struct mt7999_phy *phy = mt7999_vif_link_phy(link);
	struct mt7999_sta_link *msta_link;
	struct mt7999_dev *dev;
	struct ieee80211_bss_conf *link_conf;
	struct ieee80211_link_sta *link_sta;
	struct ieee80211_vif *vif;
	struct sk_buff *skb;
	int link_id;

	if (!phy)
		return;

	if (link->mt76.mvif != &msta->vif->mt76)
		return;

	dev = phy->dev;
	link_id = link->msta_link.wcid.link_id;
	link_sta = link_sta_dereference_protected(sta, link_id);
	if (!link_sta)
		return;

	msta_link = mt7999_sta_link_protected(dev, msta, link_id);
	if (!msta_link)
		return;

	vif = container_of((void *)msta->vif, struct ieee80211_vif, drv_priv);
	link_conf = link_conf_dereference_protected(vif, link_id);
	if (!link_conf)
		return;

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, &link->mt76,
					      &msta_link->wcid,
					      MT7999_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return;

	mt7999_mcu_sta_bfer_tlv(dev, skb, link_conf, link_sta, link);
	mt7999_mcu_sta_rate_ctrl_tlv(skb, dev, vif, link_conf, link_sta, link);

	mt76_mcu_skb_send_msg(&dev->mt76, skb,
			      MCU_WMWA_UNI_CMD(STA_REC_UPDATE), true);
}

static int
mt7999_mcu_sta_key_tlv(struct mt76_dev *dev, struct mt76_wcid *wcid,
		       struct sk_buff *skb,
		       struct ieee80211_key_conf *key,
		       enum set_key_cmd cmd)
{
	struct sta_rec_sec_uni *sec;
	struct sec_key_uni *sec_key;
	struct tlv *tlv;
	u8 cipher;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_KEY_V2, sizeof(*sec));
	sec = (struct sta_rec_sec_uni *)tlv;
	sec->add = cmd;
	sec->n_cipher = 1;
	sec_key = &sec->key;
	sec_key->wlan_idx = cpu_to_le16(wcid->idx);
	sec_key->key_id = key->keyidx;

	cipher = mt76_connac_mcu_get_cipher(key->cipher);
	if (cipher == MCU_CIPHER_NONE)
		return -EOPNOTSUPP;

#define REMOVE_SPECIFIC_KEY 1
	if (cmd == DISABLE_KEY)
		sec_key->rm_key_policy = REMOVE_SPECIFIC_KEY;
	sec_key->cipher_id = cipher;
	sec_key->cipher_len = sizeof(*sec_key);
	sec_key->key_len = key->keylen;
	sec_key->need_resp = 0;
	memcpy(sec_key->key, key->key, key->keylen);

	if (cipher == MCU_CIPHER_TKIP) {
		/* Rx/Tx MIC keys are swapped */
		memcpy(sec_key->key + 16, key->key + 24, 8);
		memcpy(sec_key->key + 24, key->key + 16, 8);
		return 0;
	}

	if (sec_key->key_id != 6 && sec_key->key_id != 7)
		return 0;

	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_AES_CMAC:
		sec_key->cipher_id = MCU_CIPHER_BCN_PROT_CMAC_128;
		break;
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
		sec_key->cipher_id = MCU_CIPHER_BCN_PROT_GMAC_128;
		break;
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		sec_key->cipher_id = MCU_CIPHER_BCN_PROT_GMAC_256;
		break;
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
		sec_key->cipher_id = MCU_CIPHER_BCN_PROT_CMAC_256;
		break;
	default:
		return -EOPNOTSUPP;
	}

	sec_key->bcn_mode = BP_HW_MODE;

	return 0;
}

int mt7999_mcu_add_key(struct mt76_dev *dev, struct mt7999_vif_link *link,
		       struct ieee80211_key_conf *key, int mcu_cmd,
		       struct mt76_wcid *wcid, enum set_key_cmd cmd)
{
	struct sk_buff *skb;
	int ret;

	skb = __mt76_connac_mcu_alloc_sta_req(dev, (struct mt76_vif_link *)link,
					      wcid, MT7999_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	ret = mt7999_mcu_sta_key_tlv(dev, wcid, skb, key, cmd);
	if (ret) {
		dev_kfree_skb(skb);
		return ret;
	}

	return mt76_mcu_skb_send_msg(dev, skb, mcu_cmd, true);
}

int mt7999_mcu_add_dev_info(struct mt7999_phy *phy, struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf,
			    struct mt76_vif_link *mlink, bool enable)
{
	struct mt7999_dev *dev = phy->dev;
	struct {
		struct req_hdr {
			u8 omac_idx;
			u8 band_idx;
			u8 __rsv[2];
		} __packed hdr;
		struct req_tlv {
			__le16 tag;
			__le16 len;
			u8 active;
			u8 __rsv;
			u8 omac_addr[ETH_ALEN];
		} __packed tlv;
	} data = {
		.hdr = {
			.omac_idx = mlink->omac_idx,
			.band_idx = mlink->band_idx,
		},
		.tlv = {
			.tag = cpu_to_le16(DEV_INFO_ACTIVE),
			.len = cpu_to_le16(sizeof(struct req_tlv)),
			.active = enable,
		},
	};

	memcpy(data.tlv.omac_addr, link_conf->addr, ETH_ALEN);
	mt76_bss_dbg(vif, "band=%u, omac=%u, addr=%pM, en=%d",
		     data.hdr.band_idx, data.hdr.omac_idx,
		     data.tlv.omac_addr, enable);

	return mt76_mcu_send_msg(&dev->mt76, MCU_WMWA_UNI_CMD(DEV_INFO_UPDATE),
				 &data, sizeof(data), true);
}

static int
mt7999_mcu_mld_reconf(struct mt7999_dev *dev, struct ieee80211_vif *vif,
		      u16 removed_links, u16 *removal_count)
{
	struct mld_req_hdr hdr = { .mld_idx = 0xff };
	struct mld_reconf_timer *rt;
	struct sk_buff *skb;
	struct tlv *tlv;
	int len = sizeof(hdr) + sizeof(*rt);
	unsigned long rem = removed_links;
	u8 link_id;

	memcpy(hdr.mld_addr, vif->addr, ETH_ALEN);

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));

	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_MLD_RECONF_AP_REM_TIMER, sizeof(*rt));
	rt = (struct mld_reconf_timer *)tlv;
	rt->link_bitmap = cpu_to_le16(removed_links);

	for_each_set_bit(link_id, &rem, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct ieee80211_bss_conf *conf =
			link_conf_dereference_protected(vif, link_id);
		struct mt7999_vif_link *mconf =	mt7999_vif_link(dev, vif, link_id);
		u8 band_idx;
		u16 to_sec;

		if (!conf || !mconf)
			continue;

		band_idx = mconf->phy->mt76->band_idx;
		to_sec = conf->beacon_int * removal_count[link_id] / 1000;
		rt->to_sec[band_idx] = cpu_to_le16(to_sec);
		rt->bss_idx[band_idx] = mconf->mt76.idx;
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, MCU_WM_UNI_CMD(MLD), true);
}

int mt7999_mcu_mld_set_attlm(struct mt7999_dev *dev, struct ieee80211_vif *vif,
			     u16 disabled_links, u16 switch_time, u32 duration)
{
	struct mld_req_hdr hdr = { .mld_idx = 0xff };
	struct mld_attlm_req *req;
	struct mt7999_mcu_mld_attlm_resp_event *resp;
	struct sk_buff *skb, *rskb;
	struct tlv *tlv;
	int len = sizeof(hdr) + sizeof(*req), ret;
	unsigned long valid_disabled_links =
			(unsigned long) vif->valid_links & disabled_links;
	u8 link_id;
	bool bss_idx_set = false;

	memcpy(hdr.mld_addr, vif->addr, ETH_ALEN);

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));
	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_MLD_ATTLM_RES_REQ, sizeof(*req));
	req = (struct mld_attlm_req *)tlv;

	req->attlm_idx = 0;
	req->mst_timer = 1;
	req->e_timer = 1;
	req->mst_timer_adv_time = cpu_to_le16(50);
	req->e_timer_adv_time = cpu_to_le16(0);
	req->mst_duration = cpu_to_le32(switch_time * USEC_PER_MSEC);
	req->e_duration = cpu_to_le32(duration * USEC_PER_MSEC);
	req->disabled_link_bitmap = cpu_to_le16(valid_disabled_links);
	for_each_set_bit(link_id, &valid_disabled_links,
			 IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt7999_vif_link *mconf = mt7999_vif_link(dev, vif, link_id);

		if (!mconf)
			continue;

		if (!bss_idx_set) {
			req->bss_idx = mconf->mt76.idx;
			bss_idx_set = true;
		}

		req->disabled_bss_idx[link_id] = mconf->mt76.idx;
	}

	if (!bss_idx_set) {
		dev_kfree_skb(skb);
		return -ENOLINK;
	}

	ret = mt76_mcu_skb_send_and_get_msg(&dev->mt76, skb, MCU_WM_UNI_CMD(MLD),
					    true, &rskb);

	if (ret)
		return ret;

	skb_pull(rskb, sizeof(struct mt7999_mcu_mld_event) - sizeof(struct mt7999_mcu_rxd));
	resp = (struct mt7999_mcu_mld_attlm_resp_event *)rskb->data;
	switch(le16_to_cpu(resp->tag)) {
	case UNI_EVENT_MLD_ATTLM_RES_RSP: {
		u32 tsf_0, tsf_1;
		u64 switch_time_tsf;
		u16 switch_time_tsf_tu;

		tsf_0 = le32_to_cpu(resp->switch_time_tsf[0]);
		tsf_1 = le32_to_cpu(resp->switch_time_tsf[1]);
		switch_time_tsf = (u64)tsf_0 + ((u64)tsf_1 << 32);
		switch_time_tsf_tu = (u16)u64_get_bits(switch_time_tsf,
						       GENMASK_ULL(25, 10));
		ieee80211_attlm_notify(vif, switch_time_tsf_tu,
				       NL80211_ATTLM_STARTED, GFP_KERNEL);
		break;
	}
	default:
		break;
	}

	dev_kfree_skb(rskb);
	return ret;
}

int mt7999_mcu_peer_mld_ttlm_req(struct mt7999_dev *dev, struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta,
				 struct ieee80211_neg_ttlm *neg_ttlm)
{
	struct mt7999_sta_link *msta_link;
	struct mt7999_sta *msta;
	struct peer_mld_req_hdr hdr = { .fw_mld_idx = cpu_to_le16(0xff) };
	struct peer_mld_ttlm_req *req;
	struct sk_buff *skb;
	struct tlv *tlv;
	int len = sizeof(hdr) + sizeof(*req);
	unsigned long valid_links = (unsigned long)vif->valid_links;
	u8 link_id, tid;

	if (vif->type != NL80211_IFTYPE_STATION &&
	    vif->type != NL80211_IFTYPE_AP)
		return -EOPNOTSUPP;

	if (!sta || !neg_ttlm)
		return -EINVAL;

	memcpy(hdr.peer_mld_addr, sta->addr, ETH_ALEN);
	msta = (struct mt7999_sta *)sta->drv_priv;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	mt76_mlo_dbg(vif, "Setup TTLM for %pM\n", sta->addr);
	skb_put_data(skb, &hdr, sizeof(hdr));
	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_PEER_MLD_TTLM_REQ, sizeof(*req));
	req = (struct peer_mld_ttlm_req *)tlv;

	memcpy(req->mld_addr, vif->addr, ETH_ALEN);
	req->enabled_link_bitmap = cpu_to_le16(vif->valid_links);

	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		msta_link = mt7999_sta_link_protected(dev, msta, link_id);
		if (!msta_link)
			continue;

		req->link_to_wcid[link_id] = cpu_to_le16(msta_link->wcid.idx);

		/* Skip all tid for tx paused link */
		if (msta->vif->tx_paused_links & BIT(link_id))
			continue;

		for (tid = 0; tid < IEEE80211_TTLM_NUM_TIDS; tid++) {
			if (neg_ttlm->downlink[tid] & BIT(link_id))
				req->dl_tid_map[link_id] |= BIT(tid);
			if (neg_ttlm->uplink[tid] & BIT(link_id))
				req->ul_tid_map[link_id] |= BIT(tid);
		}

		dev_dbg(dev->mt76.dev, "link_id=%u, dl_bitmap=%u, ul_bitmap=%u\n",
			link_id, req->dl_tid_map[link_id],
			req->ul_tid_map[link_id]);
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, MCU_WM_UNI_CMD(PEER_MLD),
				     true);
}

static struct sk_buff *
mt7999_mcu_alloc_peer_mld_req(struct mt7999_dev *dev,
			      struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta,
			      struct mt7999_sta_link *msta_link, u16 len)
{
	struct peer_mld_req_hdr *hdr;
	struct sk_buff *skb;

	if (sta) {
		struct mt7999_sta *msta = (struct mt7999_sta *)sta->drv_priv;

		msta_link = mt7999_sta_link_protected(dev, msta, msta->deflink_id);
	}
	if (!msta_link)
		return ERR_PTR(-EINVAL);

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return ERR_PTR(-ENOMEM);

	hdr = skb_put_zero(skb, sizeof(*hdr));
	hdr->fw_mld_idx = cpu_to_le16(msta_link->wcid.idx);
	if (sta)
		memcpy(hdr->peer_mld_addr, sta->addr, ETH_ALEN);
	else {
		struct ieee80211_bss_conf *link_conf =
			link_conf_dereference_protected(vif, msta_link->wcid.link_id);

		if (!link_conf)
			link_conf = &vif->bss_conf;

		if (vif->type == NL80211_IFTYPE_STATION &&
		    !is_zero_ether_addr(link_conf->bssid))
			memcpy(hdr->peer_mld_addr, link_conf->bssid, ETH_ALEN);
		else
			eth_broadcast_addr(hdr->peer_mld_addr);
	}

	return skb;
}

static int
mt7999_mcu_peer_mld_info_tlv(struct mt7999_dev *dev, struct sk_buff *skb,
			     struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta,
			     struct mt7999_sta_link *msta_link, bool assoc)
{
	enum {
		STAGE_PRE_AUTH = 1,
		STAGE_ASSOC_DONE,
	};
	struct peer_mld_link_info *link_info;
	struct peer_mld_info *info;
	struct mt7999_sta_link *msta_link_sec;
	struct mt7999_sta *msta;
	struct tlv *tlv;
	unsigned long valid_links;
	u8 link_id, link_num = 1;
	int len;

	if (sta) {
		msta = (struct mt7999_sta *)sta->drv_priv;

		msta_link = mt7999_sta_link_protected(dev, msta, msta->deflink_id);
	}
	if (!msta_link)
		return -EINVAL;

	if (sta && sta->mlo) {
		link_num = hweight16(sta->valid_links);
		valid_links = sta->valid_links;
		msta_link_sec = mt7999_sta_link_protected(dev, msta, msta->seclink_id);
		if (!msta_link_sec)
			return -EINVAL;
	} else
		msta_link_sec = msta_link;

	len = sizeof(*info) + link_num * sizeof(*link_info);
	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_PEER_MLD_INFO, len);

	info = (struct peer_mld_info *)tlv;
	info->link_num = link_num;
	info->stage = assoc ? STAGE_ASSOC_DONE : STAGE_PRE_AUTH;
	info->hw_info.primary_id = cpu_to_le16(msta_link->wcid.idx);
	info->hw_info.setup_wcid = cpu_to_le16(msta_link->wcid.idx);
	info->hw_info.second_id = cpu_to_le16(msta_link_sec->wcid.idx);

	link_info = (struct peer_mld_link_info *)info->link_info;
	if (!sta || !sta->mlo) {
		struct mt7999_vif_link *link;

		link = mt7999_vif_link(dev, vif, msta_link->wcid.link_id);
		if (!link)
			return -EINVAL;

		link_info->fw_link_id = cpu_to_le16(msta_link->wcid.idx);
		link_info->link_id = 0xf;
		link_info->bss_idx = link->mt76.idx;
		link_info->hw_info.wlan_idx = cpu_to_le16(msta_link->wcid.idx);
		link_info->hw_info.band_idx = link->mt76.band_idx;

		return 0;
	}

	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt7999_vif_link *link;

		msta_link = mt7999_sta_link_protected(dev, msta, link_id);
		if (!msta_link)
			return -EINVAL;

		link = mt7999_vif_link(dev, vif, link_id);
		if (!link)
			return -EINVAL;

		link_info->fw_link_id = cpu_to_le16(msta_link->wcid.idx);
		link_info->link_id = msta_link->wcid.link_id;
		link_info->bss_idx = link->mt76.idx;

		link_info->hw_info.wlan_idx = cpu_to_le16(msta_link->wcid.idx);
		link_info->hw_info.band_idx = link->mt76.band_idx;

		link_info++;
	}

	return 0;
}

static int
mt7999_mcu_peer_mld_cap_tlv(struct mt7999_dev *dev, struct sk_buff *skb,
			    struct ieee80211_sta *sta)
{
	struct peer_mld_cap *cap;
	struct peer_mld_cap_entry *cap_entry;
	struct tlv *tlv;
	int len = sizeof(*cap) + 2 * sizeof(*cap_entry);
	int min_amsdu_size = INT_MAX;
	unsigned long valid_links;
	u8 link_id;

	if (!sta)
		return -EINVAL;

	valid_links = sta->valid_links;
	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct ieee80211_link_sta *link_sta;

		link_sta = link_sta_dereference_protected(sta, link_id);
		if (link_sta && link_sta->agg.max_amsdu_len < min_amsdu_size)
			min_amsdu_size = link_sta->agg.max_amsdu_len;
	}

	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_PEER_MLD_CAP, len);
	cap = (struct peer_mld_cap *)tlv;

	switch (min_amsdu_size) {
	case IEEE80211_MAX_MPDU_LEN_VHT_11454:
		min_amsdu_size = IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454;
		break;
	case IEEE80211_MAX_MPDU_LEN_HT_7935:
	case IEEE80211_MAX_MPDU_LEN_VHT_7991:
		min_amsdu_size = IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991;
		break;
	default:
		min_amsdu_size = IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_3895;
		break;
	}

	cap_entry = (struct peer_mld_cap_entry *)cap->entry;
	cap_entry->type = cpu_to_le32(PEER_MLD_CAP_TYPE_AMSDU_MAX_LEN);
	cap_entry->val = cpu_to_le32(min_amsdu_size);
	cap_entry++;

	cap_entry->type = cpu_to_le32(PEER_MLD_CAP_TYPE_AMSDU_MAX_CNT);
	cap_entry->val = cpu_to_le32(31);

	return 0;
}

int mt7999_mcu_set_peer_mld(struct mt7999_dev *dev, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta,
			    struct mt7999_sta_link *msta_link, bool assoc)
{
	struct sk_buff *skb;
	int ret, len;
	u8 link_num = (sta && sta->mlo) ? hweight16(sta->valid_links) : 1;

	len = sizeof(struct peer_mld_req_hdr) + sizeof(struct peer_mld_info) +
	      link_num * sizeof(struct peer_mld_link_info);
	len += assoc ? sizeof(struct peer_mld_cap) + 2 * sizeof(struct peer_mld_cap_entry) : 0;

	skb = mt7999_mcu_alloc_peer_mld_req(dev, vif, sta, msta_link, len);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	ret = mt7999_mcu_peer_mld_info_tlv(dev, skb, vif, sta, msta_link, assoc);
	if (ret)
		goto out;

	if (assoc) {
		ret = mt7999_mcu_peer_mld_cap_tlv(dev, skb, sta);
		if (ret)
			goto out;
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(PEER_MLD), true);
out:
	dev_kfree_skb(skb);
	return -EINVAL;
}

static void
mt7999_mcu_beacon_cntdwn(struct sk_buff *rskb, struct sk_buff *skb,
			 struct ieee80211_mutable_offsets *offs,
			 bool csa)
{
	struct bss_bcn_cntdwn_tlv *info;
	struct tlv *tlv;
	u16 tag;

	if (!offs->cntdwn_counter_offs[0])
		return;

	tag = csa ? UNI_BSS_INFO_BCN_CSA : UNI_BSS_INFO_BCN_BCC;

	tlv = mt7999_mcu_add_uni_tlv(rskb, tag, sizeof(*info));

	info = (struct bss_bcn_cntdwn_tlv *)tlv;
	info->cnt = skb->data[offs->cntdwn_counter_offs[0]];

	/* abort the CCA countdown when starting CSA countdown */
	if (csa) {
		struct bss_bcn_cntdwn_tlv *cca_info;

		tlv = mt7999_mcu_add_uni_tlv(rskb, UNI_BSS_INFO_BCN_BCC,
					     sizeof(*cca_info));
		cca_info = (struct bss_bcn_cntdwn_tlv *)tlv;
		cca_info->cca.abort = true;
	}
}

static void
mt7999_mcu_beacon_mbss(struct sk_buff *rskb, struct sk_buff *skb,
		       struct bss_bcn_content_tlv *bcn,
		       struct ieee80211_mutable_offsets *offs,
		       struct mt7999_mbssid_data *mbssid_data)
{
	struct bss_bcn_mbss_tlv *mbss;
	struct tlv *tlv;
	int i;

	tlv = mt7999_mcu_add_uni_tlv(rskb, UNI_BSS_INFO_BCN_MBSSID, sizeof(*mbss));

	mbss = (struct bss_bcn_mbss_tlv *)tlv;
	mbss->offset[0] = cpu_to_le16(offs->tim_offset);
	mbss->bitmap = cpu_to_le32(1);

	for (i = 0; i < MAX_BEACON_NUM; i++) {
		if (!mbssid_data[i].mbssid_idx.valid)
			continue;

		mbss->offset[i] = cpu_to_le16(mbssid_data[i].mbssid_idx.offset);
		mbss->bitmap |= cpu_to_le32(BIT(i));
	}
}

static bool
mt7999_mcu_beacon_is_cu_link(struct sk_buff *skb, struct mt7999_vif_link *mconf,
			     u16 tail_offset)
{
	const struct element *elem;
	u8 *beacon_tail = skb->data + tail_offset;
	bool has_ml_ie = false;
	int bpcc;

	for_each_element_extid(elem, WLAN_EID_EXT_EHT_MULTI_LINK,
			       beacon_tail, skb->len - tail_offset)
		if (ieee80211_mle_type_ok(elem->data + 1,
					  IEEE80211_ML_CONTROL_TYPE_BASIC,
					  elem->datalen - 1)) {
			has_ml_ie = true;
			break;
		}

	if (!has_ml_ie)
		return false;

	bpcc = ieee80211_mle_get_bss_param_ch_cnt(elem->data + 1);
	if (bpcc < 0 || bpcc == mconf->bpcc)
		return false;

	mconf->bpcc = bpcc;

	return true;
}

static void
mt7999_mcu_beacon_crit_update(struct sk_buff *rskb, struct sk_buff *skb,
			      struct ieee80211_bss_conf *conf,
			      struct mt7999_vif_link *mconf,
			      struct ieee80211_mutable_offsets *offs,
			      struct mt7999_mbssid_data *mbssid_data)
{
	struct ieee80211_mgmt *mgmt = (void *)skb->data;
	struct bss_bcn_crit_update_tlv *crit;
	struct tlv *tlv;
	u16 capab_info = le16_to_cpu(mgmt->u.beacon.capab_info);
	int i;

	if (!ieee80211_vif_is_mld(conf->vif) ||
	    !(capab_info & (WLAN_CAPABILITY_PBCC |
			    WLAN_CAPABILITY_NON_TX_BSSID_CU)))
		return;

	tlv = mt7999_mcu_add_uni_tlv(rskb, UNI_BSS_INFO_BCN_CRIT_UPDATE, sizeof(*crit));

	crit = (struct bss_bcn_crit_update_tlv *)tlv;
	if (capab_info & WLAN_CAPABILITY_PBCC)
		crit->bss_bitmap = cpu_to_le32(BIT(0));
	/* The beacon of the CU link should be set in sequence
	 * to ensure it appears in the air before the beacon of
	 * the non-CU link.
	 */
	if (!mt7999_mcu_beacon_is_cu_link(skb, mconf, offs->tim_offset))
		crit->bypass_seq_bitmap = cpu_to_le32(BIT(0));
	crit->tim_ie_pos[0] = cpu_to_le16(offs->tim_offset);
	crit->cap_info_ie_pos[0] = cpu_to_le16(offsetof(struct ieee80211_mgmt,
							u.beacon.capab_info));

	for (i = 1; i < MAX_BEACON_NUM; i++) {
		u16 ntx_bss_capab_info, offset;

		if (!mbssid_data[i].ntx_bss_cap.valid)
			continue;

		offset = mbssid_data[i].ntx_bss_cap.offset;
		ntx_bss_capab_info = le16_to_cpu(*(skb->data + offset + 2));
		if (!(ntx_bss_capab_info & WLAN_CAPABILITY_PBCC))
			continue;

		crit->cap_info_ie_pos[i] = cpu_to_le16(offset);
		crit->bss_bitmap |= cpu_to_le32(BIT(i));
		if (!mbssid_data[i].is_cu_link)
			crit->bypass_seq_bitmap |= cpu_to_le32(BIT(i));
	}

	crit->require_event = true;
}

static void
mt7999_mcu_beacon_sta_prof_csa(struct sk_buff *rskb,
			       struct ieee80211_bss_conf *conf,
			       struct ieee80211_mutable_offsets *offs)
{
	struct ieee80211_vif *vif = conf->vif;
	struct mt7999_vif *mvif = (struct mt7999_vif *)vif->drv_priv;
	struct mt7999_dev *dev = mvif->deflink.phy->dev;
	struct mt7999_vif_link *cs_mconf;
	struct bss_bcn_sta_prof_cntdwn_tlv *sta_prof;
	struct tlv *tlv;
	u8 cs_band;

	if (!ieee80211_vif_is_mld(vif) || !offs->sta_prof_cntdwn_offs[0])
		return;

	cs_mconf = mt7999_vif_link(dev, conf->vif, mvif->cs_links[0]);
	if (!cs_mconf)
		return;

	tlv = mt7999_mcu_add_uni_tlv(rskb, UNI_BSS_INFO_BCN_STA_PROF_CSA, sizeof(*sta_prof));

	cs_band = cs_mconf->phy->mt76->band_idx;
	sta_prof = (struct bss_bcn_sta_prof_cntdwn_tlv *)tlv;
	sta_prof->sta_prof_csa_offs[cs_band] = cpu_to_le16(offs->sta_prof_cntdwn_offs[0] - 4);
	sta_prof->cs_bss_idx[cs_band] = cs_mconf->mt76.idx;
}

static void
mt7999_mcu_beacon_cont(struct mt7999_dev *dev,
		       struct ieee80211_bss_conf *link_conf,
		       struct mt7999_vif_link *link,
		       struct sk_buff *rskb, struct sk_buff *skb,
		       struct bss_bcn_content_tlv *bcn,
		       struct ieee80211_mutable_offsets *offs)
{
	struct mt76_wcid *wcid = &link->msta_link.wcid;
	u8 *buf;

	bcn->pkt_len = cpu_to_le16(MT_TXD_SIZE + skb->len);
	bcn->tim_ie_pos = cpu_to_le16(offs->tim_offset);

	if (offs->cntdwn_counter_offs[0]) {
		u16 offset = offs->cntdwn_counter_offs[0];

		if (link_conf->csa_active)
			bcn->csa_ie_pos = cpu_to_le16(offset - 4);
		if (link_conf->color_change_active)
			bcn->bcc_ie_pos = cpu_to_le16(offset - 3);
	}

	buf = (u8 *)bcn + sizeof(*bcn);
	mt7999_mac_write_txwi(dev, (__le32 *)buf, skb, wcid, NULL, 0, 0,
			      BSS_CHANGED_BEACON);

	memcpy(buf + MT_TXD_SIZE, skb->data, skb->len);

	mt7999_packet_log_to_host(dev, (__le32 *)buf, MT_TXD_SIZE, PKT_BIN_DEBUG_TXD, 0);
	mt7999_packet_log_to_host(dev, skb->data, skb->len, PKT_BIN_DEBUG_TX, 0);
}

static void
mt7999_mcu_beacon_ml_reconf(struct mt7999_dev *dev,
			    struct ieee80211_bss_conf *conf,
			    struct sk_buff *rskb, struct sk_buff *skb,
			    struct ieee80211_mutable_offsets *offs)
{
	struct bss_bcn_ml_reconf_tlv *reconf;
	struct bss_bcn_ml_reconf_offset *reconf_offs;
	const struct element *elem, *sub;
	struct tlv *tlv;
	u16 removal_offs[IEEE80211_MLD_MAX_NUM_LINKS] = {};
	u16 removal_count[IEEE80211_MLD_MAX_NUM_LINKS] = {};
	u16 tail_offset = offs->tim_offset + offs->tim_length;
	unsigned long removed_links = 0;
	bool has_reconf = false;
	u8 link_id, *beacon_tail = skb->data + tail_offset;

	if (!ieee80211_vif_is_mld(conf->vif))
		return;

	/* TODO: currently manually parse reconf info directly from the IE, it
	 * is expected to be passed from upper layer in the future.
	 */
	for_each_element_extid(elem, WLAN_EID_EXT_EHT_MULTI_LINK,
			       beacon_tail, skb->len - tail_offset) {
		if (ieee80211_mle_type_ok(elem->data + 1,
					  IEEE80211_ML_CONTROL_TYPE_RECONF,
					  elem->datalen - 1)) {
			has_reconf = true;
			break;
		}
	}

	if (!has_reconf)
		return;

	for_each_mle_subelement(sub, elem->data + 1, elem->datalen - 1) {
		struct ieee80211_mle_per_sta_profile *prof = (void *)sub->data;
		u8 *pos = prof->variable;
		u16 control;

		if (sub->id != IEEE80211_MLE_SUBELEM_PER_STA_PROFILE)
			continue;

		if (!ieee80211_mle_reconf_sta_prof_size_ok(sub->data,
							   sub->datalen))
			return;

		control = le16_to_cpu(prof->control);
		link_id = control & IEEE80211_MLE_STA_RECONF_CONTROL_LINK_ID;

		removed_links |= BIT(link_id);

		if (control & IEEE80211_MLE_STA_RECONF_CONTROL_STA_MAC_ADDR_PRESENT)
			pos += 6;

		if (control & IEEE80211_MLE_STA_RECONF_CONTROL_AP_REM_TIMER_PRESENT) {
			removal_offs[link_id] = pos - skb->data;
			removal_count[link_id] = le16_to_cpu(*(__le16 *)pos);
		}
	}

	if (!removed_links)
		return;

	/* the first link to be removed */
	if (conf->link_id == ffs(removed_links) - 1)
		mt7999_mcu_mld_reconf(dev, conf->vif, removed_links, removal_count);

	tlv = mt7999_mcu_add_uni_tlv(rskb, UNI_BSS_INFO_BCN_ML_RECONF,
				     sizeof(*reconf) +
				     sizeof(*reconf_offs) * hweight16(removed_links));
	reconf = (struct bss_bcn_ml_reconf_tlv *)tlv;
	reconf->reconf_count = hweight16(removed_links);

	reconf_offs = (struct bss_bcn_ml_reconf_offset *)reconf->offset;
	for_each_set_bit(link_id, &removed_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt7999_vif_link *mconf = mt7999_vif_link(dev, conf->vif, link_id);

		reconf_offs->ap_removal_timer_offs =
			cpu_to_le16(removal_offs[link_id]);
		reconf_offs->bss_idx = mconf->mt76.idx;
		reconf_offs++;
	}
}

static u16
mt7999_get_attlm_offset(const struct element *elem, struct sk_buff *skb)
{
	if (!elem)
		return 0;

	if (ieee80211_tid_to_link_map_size_ok(elem->data + 1,
					      elem->datalen - 1)) {
		struct ieee80211_ttlm_elem *ttlm =
			(struct ieee80211_ttlm_elem *)elem->data + 1;

		if (!(ttlm->control & IEEE80211_TTLM_CONTROL_SWITCH_TIME_PRESENT) &&
		    (ttlm->control & IEEE80211_TTLM_CONTROL_EXPECTED_DUR_PRESENT))
			return (u16)((u8 *)elem - skb->data);
	}

	return 0;
}

static void
mt7999_mcu_beacon_ttlm(struct mt7999_dev *dev, struct ieee80211_bss_conf *conf,
		       struct sk_buff *rskb, struct sk_buff *skb,
		       struct ieee80211_mutable_offsets *offs,
		       struct mt7999_mbssid_data *mbssid_data)
{
	u16 offset = 0, tail_offset = offs->tim_offset + offs->tim_length;
	struct bss_bcn_attlm_offset_tlv *attlm_offset;
	u8 *beacon_tail = skb->data + tail_offset;
	const struct element *elem;
	bool cntdown_ttlm = false;
	struct tlv *tlv;
	int i;

	if (!ieee80211_vif_is_mld(conf->vif))
		return;

	for_each_element_extid(elem, WLAN_EID_EXT_TID_TO_LINK_MAPPING,
			       beacon_tail, skb->len - tail_offset) {
		if ((offset = mt7999_get_attlm_offset(elem, skb)) > 0) {
			cntdown_ttlm = true;
			break;
		}
	}

	for (i = 1; i < MAX_BEACON_NUM; i++) {
		if (mbssid_data[i].attlm.valid) {
			cntdown_ttlm = true;
			break;
		}
	}

	if (!cntdown_ttlm)
		return;

	tlv = mt7999_mcu_add_uni_tlv(rskb, UNI_BSS_INFO_BCN_ATTLM,
				     sizeof(*attlm_offset));
	attlm_offset = (struct bss_bcn_attlm_offset_tlv *)tlv;
	attlm_offset->valid_id_bitmap = BIT(0);
	attlm_offset->offset[0] = cpu_to_le16(offset);

	for (i = 1; i < MAX_BEACON_NUM; i++) {
		if (!mbssid_data[i].attlm.valid)
			continue;

		attlm_offset->non_tx_bitmap |= BIT(i);
		attlm_offset->non_tx_valid_id_bitmap[i] |= BIT(0);
		attlm_offset->non_tx_offsets[i][0] = mbssid_data[i].attlm.offset;
	}
}

static bool
mt7999_is_nontx_cu_link(struct mt7999_phy *phy,
			   const struct element *sub_elem, u8 bssid_idx)
{
	const struct element *ml_elem;
	struct mt7999_vif_link *mconf;
	int bpcc;

	ml_elem = cfg80211_find_ext_elem(WLAN_EID_EXT_EHT_MULTI_LINK,
					 sub_elem->data, sub_elem->datalen);
	if (!ml_elem || !ieee80211_mle_type_ok(ml_elem->data + 1,
					       IEEE80211_ML_CONTROL_TYPE_BASIC,
					       ml_elem->datalen - 1))
		return false;

	bpcc = ieee80211_mle_get_bss_param_ch_cnt(ml_elem->data + 1);
	if (bpcc < 0)
		return false;

	rcu_read_lock();
	mconf =	rcu_dereference(phy->mbssid_conf[bssid_idx]);
	if (!mconf || bpcc == mconf->bpcc) {
		rcu_read_unlock();
		return false;
	}

	mconf->bpcc = bpcc;

	rcu_read_unlock();
	return true;
}

static void
mt7999_parse_mbssid_elems(struct mt7999_phy *phy, struct sk_buff *skb,
			  u16 mbssid_off, struct mt7999_mbssid_data *mbssid_data)
{
	const struct element *elem;

	for_each_element_id(elem, WLAN_EID_MULTIPLE_BSSID,
			    &skb->data[mbssid_off],
			    skb->len - mbssid_off) {
		const struct element *sub_elem;

		if (elem->datalen < 2)
			continue;

		for_each_element(sub_elem, elem->data + 1, elem->datalen - 1) {
			const struct ieee80211_bssid_index *idx;
			const u8 *idx_ie, *ntx_bss_cap_ie;
			const struct element *attlm_elem;
			u16 offset;
			u8 bssid_idx;
			bool cu_flag = false;

			/* not a valid BSS profile */
			if (sub_elem->id || sub_elem->datalen < 4)
				continue;

			/* Find WLAN_EID_MULTI_BSSID_IDX
			 * in the merged nontransmitted profile
			 */
			idx_ie = cfg80211_find_ie(WLAN_EID_MULTI_BSSID_IDX,
						  sub_elem->data, sub_elem->datalen);

			/* At leat the BSSID idx should be preset and valid.
			 * Otherwise we do not know the idx.
			 * FIXME: Handle split subelements if other
			 * subelements need parsing
			 */
			if (!idx_ie || idx_ie[1] < sizeof(*idx))
				continue;

			idx = (void *)(idx_ie + 2);
			bssid_idx = idx->bssid_index;
			if (!bssid_idx || bssid_idx > MT7999_MAX_MBSSID - 1)
				continue;

			mbssid_data[bssid_idx].mbssid_idx.offset = idx_ie - skb->data;
			mbssid_data[bssid_idx].mbssid_idx.valid = true;

			/* Find WLAN_EID_NON_TX_BSSID_CAP
			 * in the merged nontransmitted profile
			 */
			ntx_bss_cap_ie = cfg80211_find_ie(WLAN_EID_NON_TX_BSSID_CAP,
							  sub_elem->data,
							  sub_elem->datalen);

			if (ntx_bss_cap_ie && ntx_bss_cap_ie[1] == sizeof(u16)) {
				mbssid_data[bssid_idx].ntx_bss_cap.offset =
					ntx_bss_cap_ie - skb->data;
				mbssid_data[bssid_idx].ntx_bss_cap.valid =
					true;
				cu_flag = le16_to_cpu(*(u16 *)(ntx_bss_cap_ie + 2)) &
					  WLAN_CAPABILITY_PBCC;
			}

			/* Find WLAN_EID_EXT_EHT_MULTI_LINK
			 * in the merged nontransmitted profile
			 */
			if (cu_flag)
				mbssid_data[bssid_idx].is_cu_link =
					mt7999_is_nontx_cu_link(phy, sub_elem,
								bssid_idx);

			/* Find WLAN_EID_EXT_TID_TO_LINK_MAPPING
			 * in the merged nontransmitted profile
			 */
			attlm_elem = cfg80211_find_ext_elem(
						WLAN_EID_EXT_TID_TO_LINK_MAPPING,
						sub_elem->data, sub_elem->datalen);

			if ((offset = mt7999_get_attlm_offset(attlm_elem, skb)) > 0) {
				mbssid_data[bssid_idx].attlm.offset = offset;
				mbssid_data[bssid_idx].attlm.valid = true;
			}
		}
	}
}

static void
mt7999_mcu_beacon_btwt(struct mt7999_dev *dev,
		       struct sk_buff *rskb, struct sk_buff *skb,
		       struct ieee80211_mutable_offsets *offs)
{
	u16 offset = 0, tail_offset = offs->tim_offset + offs->tim_length;
	u8 *beacon_tail = skb->data + tail_offset;
	const struct element *elem;
	struct bss_bcn_btwt_tlv *btwt;
	struct tlv *tlv;

	if (dev->twt.n_agrt_bc == 0)
		return;

	for_each_element_id(elem, WLAN_EID_S1G_TWT, beacon_tail, skb->len - tail_offset) {
		offset = (u16)((u8 *)elem - skb->data);
		break;
	}

	if (offset == 0)
		return;

	tlv = mt7999_mcu_add_uni_tlv(rskb, UNI_BSS_INFO_BCN_BTWT, sizeof(*btwt));
	btwt = (struct bss_bcn_btwt_tlv *) tlv;
	btwt->btwt_ie_pos = cpu_to_le16(offset);
}

int mt7999_mcu_add_beacon(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			  struct ieee80211_bss_conf *link_conf, bool enabled)
{
	struct mt7999_dev *dev = mt7999_hw_dev(hw);
	struct mt7999_vif_link *link = mt7999_vif_conf_link(dev, vif, link_conf);
	struct mt76_phy *mphy = link ? mt76_vif_link_phy(&link->mt76) : NULL;
	struct mt76_vif_link *mlink = link ? &link->mt76 : NULL;
	struct ieee80211_mutable_offsets offs;
	struct ieee80211_tx_info *info;
	struct sk_buff *skb, *rskb;
	struct tlv *tlv;
	struct bss_bcn_content_tlv *bcn;
	struct mt7999_mbssid_data *mbssid_data;
	int len, extra_len = 0;

	if (link_conf->nontransmitted)
		return 0;

	if (!mlink)
		return -EINVAL;

	rskb = __mt7999_mcu_alloc_bss_req(&dev->mt76, mlink,
					  MT7999_MAX_BSS_OFFLOAD_SIZE);
	if (IS_ERR(rskb))
		return PTR_ERR(rskb);

	skb = ieee80211_beacon_get_template(hw, vif, &offs, link_conf->link_id);
	if (enabled && !skb) {
		dev_kfree_skb(rskb);
		return -EINVAL;
	}

	if (skb) {
		if (skb->len > MT7999_MAX_BEACON_SIZE) {
			dev_err(dev->mt76.dev, "Bcn size limit exceed\n");
			dev_kfree_skb(rskb);
			dev_kfree_skb(skb);
			return -EINVAL;
		}

		extra_len = skb->len;
	}

	len = ALIGN(sizeof(*bcn) + MT_TXD_SIZE + extra_len, 4);
	tlv = mt7999_mcu_add_uni_tlv(rskb, UNI_BSS_INFO_BCN_CONTENT, len);
	bcn = (struct bss_bcn_content_tlv *)tlv;
	bcn->enable = enabled;
	if (!bcn->enable)
		goto out;

	info = IEEE80211_SKB_CB(skb);
	info->hw_queue |= FIELD_PREP(MT_TX_HW_QUEUE_PHY, mlink->band_idx);

	mbssid_data = kzalloc(sizeof(struct mt7999_mbssid_data) * MAX_BEACON_NUM, GFP_KERNEL);
	if (!mbssid_data) {
		dev_kfree_skb(rskb);
		dev_kfree_skb(skb);
		return -ENOMEM;
	}

	mt7999_parse_mbssid_elems(mphy->priv, skb, offs.mbssid_off, mbssid_data);
	mt7999_mcu_beacon_cont(dev, link_conf, link, rskb, skb, bcn, &offs);
	if (link_conf->bssid_indicator)
		mt7999_mcu_beacon_mbss(rskb, skb, bcn, &offs, mbssid_data);
	mt7999_mcu_beacon_cntdwn(rskb, skb, &offs, link_conf->csa_active);
	mt7999_mcu_beacon_sta_prof_csa(rskb, link_conf, &offs);
	mt7999_mcu_beacon_crit_update(rskb, skb, link_conf, link, &offs, mbssid_data);
	mt7999_mcu_beacon_ml_reconf(dev, link_conf, rskb, skb, &offs);
	mt7999_mcu_beacon_ttlm(dev, link_conf, rskb, skb, &offs, mbssid_data);
	mt7999_mcu_beacon_btwt(dev, rskb, skb, &offs);

	kfree(mbssid_data);
out:
	dev_kfree_skb(skb);
	return mt76_mcu_skb_send_msg(&dev->mt76, rskb,
				     MCU_WMWA_UNI_CMD(BSS_INFO_UPDATE), true);
}

int mt7999_mcu_beacon_inband_discov(struct mt7999_dev *dev,
				    struct ieee80211_bss_conf *link_conf,
				    struct mt7999_vif_link *link, u32 changed)
{
#define OFFLOAD_TX_MODE_SU	BIT(0)
	struct mt76_phy *mphy = mt76_vif_link_phy(&link->mt76);
	struct ieee80211_vif *vif = link_conf->vif;
	struct ieee80211_hw *hw = mt76_hw(dev);
	struct mt76_wcid *wcid = &dev->mt76.global_wcid;
	struct bss_inband_discovery_tlv *discov;
	struct ieee80211_tx_info *info;
	struct sk_buff *rskb, *skb = NULL;
	struct cfg80211_chan_def *chandef;
	enum nl80211_band band;
	struct tlv *tlv;
	u8 *buf, interval = 0;
	int len;

	if (!mphy)
		return -EINVAL;

	chandef = &mphy->chandef;
	band = chandef->chan->band;

	if (link_conf->nontransmitted)
		return 0;

	rskb = __mt7999_mcu_alloc_bss_req(&dev->mt76, &link->mt76,
					  MT7999_MAX_BSS_OFFLOAD_SIZE);
	if (IS_ERR(rskb))
		return PTR_ERR(rskb);

	if (changed & BSS_CHANGED_FILS_DISCOVERY) {
		interval = link_conf->fils_discovery.max_interval;
		skb = ieee80211_get_fils_discovery_tmpl(hw, vif, link_conf->link_id);
	} else if (changed & BSS_CHANGED_UNSOL_BCAST_PROBE_RESP) {
		interval = link_conf->unsol_bcast_probe_resp_interval;
		skb = ieee80211_get_unsol_bcast_probe_resp_tmpl(hw, vif, link_conf->link_id);
	}

	if (!skb) {
		dev_kfree_skb(rskb);
		return -EINVAL;
	}

	if (skb->len > MT7999_MAX_BEACON_SIZE) {
		dev_err(dev->mt76.dev, "inband discovery size limit exceed\n");
		dev_kfree_skb(rskb);
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	info = IEEE80211_SKB_CB(skb);
	info->control.vif = vif;
	info->band = band;
	info->hw_queue |= FIELD_PREP(MT_TX_HW_QUEUE_PHY, mphy->band_idx);

	len = ALIGN(sizeof(*discov) + MT_TXD_SIZE + skb->len, 4);
	tlv = mt7999_mcu_add_uni_tlv(rskb, UNI_BSS_INFO_OFFLOAD, len);

	discov = (struct bss_inband_discovery_tlv *)tlv;
	discov->tx_mode = OFFLOAD_TX_MODE_SU;
	/* 0: UNSOL PROBE RESP, 1: FILS DISCOV */
	discov->tx_type = !!(changed & BSS_CHANGED_FILS_DISCOVERY);
	discov->tx_interval = interval;
	discov->prob_rsp_len = cpu_to_le16(MT_TXD_SIZE + skb->len);
	discov->enable = !!(interval);
	discov->wcid = cpu_to_le16(MT7999_WTBL_RESERVED);

	buf = (u8 *)tlv + sizeof(*discov);

	mt7999_mac_write_txwi(dev, (__le32 *)buf, skb, wcid, NULL, 0, 0, changed);

	memcpy(buf + MT_TXD_SIZE, skb->data, skb->len);

	dev_kfree_skb(skb);

	return mt76_mcu_skb_send_msg(&dev->mt76, rskb,
				     MCU_WM_UNI_CMD(BSS_INFO_UPDATE), true);
}

static int mt7999_driver_own(struct mt7999_dev *dev, u8 band)
{
	mt76_wr(dev, MT_TOP_LPCR_HOST_BAND(band), MT_TOP_LPCR_HOST_DRV_OWN);
	if (!mt76_poll_msec(dev, MT_TOP_LPCR_HOST_BAND(band),
			    MT_TOP_LPCR_HOST_FW_OWN_STAT, 0, 500)) {
		dev_err(dev->mt76.dev, "Timeout for driver own\n");
		return -EIO;
	}

	return 0;
}

static int
mt7999_firmware_state(struct mt7999_dev *dev, u8 fw_state)
{
	u32 state = FIELD_PREP(MT_TOP_MISC_FW_STATE, fw_state);

	if (!mt76_poll_msec(dev, MT_TOP_MISC, MT_TOP_MISC_FW_STATE,
			    state, 1000)) {
		dev_err(dev->mt76.dev, "Timeout for initializing firmware\n");
		return -EIO;
	}
	return 0;
}

static int
mt7999_mcu_restart(struct mt76_dev *dev)
{
	struct {
		u8 __rsv1[4];

		__le16 tag;
		__le16 len;
		u8 power_mode;
		u8 __rsv2[3];
	} __packed req = {
		.tag = cpu_to_le16(UNI_POWER_OFF),
		.len = cpu_to_le16(sizeof(req) - 4),
		.power_mode = 1,
	};

	return mt76_mcu_send_msg(dev, MCU_WM_UNI_CMD(POWER_CTRL), &req,
				 sizeof(req), false);
}

static void mt7999_slot_destroy(struct mt7999_dev *dev)
{
	struct mt7999_fwdl_queue *q = &dev->fwdl->q;
	int i;

	for (i = 0; i < SLOT_NUM; i++) {
		struct mt7999_fwdl_entry *entry = &q->entry[i];

		if (!entry->buf)
			continue;

		dma_free_coherent(dev->mt76.dma_dev, SLOT_SIZE,
				  entry->buf, entry->dma_addr);
		entry->buf = NULL;
	}
	dma_free_coherent(dev->mt76.dma_dev, sizeof(*q->desc.buf) * SLOT_NUM,
			  q->desc.buf, q->desc.dma_addr);
	q->desc.buf = NULL;

}

static int mt7999_slot_init(struct mt7999_dev *dev)
{
	struct mt7999_fwdl_queue *q = &dev->fwdl->q;
	int i;

	q->desc.buf = dma_alloc_coherent(dev->mt76.dma_dev,
					 sizeof(*q->desc.buf) * SLOT_NUM,
					 &q->desc.dma_addr, GFP_KERNEL);
	if (!q->desc.buf)
		return -ENOMEM;

	for (i = 0; i < SLOT_NUM; i++) {
		struct mt7999_fwdl_entry *entry = &q->entry[i];

		entry->buf = dma_alloc_coherent(dev->mt76.dma_dev, SLOT_SIZE,
						&entry->dma_addr, GFP_KERNEL);
		if (!entry->buf) {
			mt7999_slot_destroy(dev);
			return -ENOMEM;
		}

		q->desc.buf[i] = cpu_to_le64(entry->dma_addr);
	}

	mt76_wr(dev, MT_FWDL_SLOT_TBL_ADDR_31_0, q->desc.dma_addr);
	mt76_wr(dev, MT_FWDL_SLOT_TBL_ADDR_63_32, q->desc.dma_addr >> 32);
	mt76_wr(dev, MT_FWDL_SLOT_TBL_SIZE, sizeof(*q->desc.buf) * SLOT_NUM);
	mt76_wr(dev, MT_FWDL_SLOT_ADDR,
		u32_encode_bits(SLOT_NUM, MT_FWDL_SLOT_NUM) |
		u32_encode_bits(SLOT_SIZE, MT_FWDL_SLOT_SIZE));

	return 0;
}

static void mt7999_start_fwdl(struct mt7999_dev *dev, const struct firmware *fw)
{
	const struct mt7999_firmware_hdr *hdr = (const struct mt7999_firmware_hdr *)(fw->data);
	const struct mt7999_firmware_section *section_map;
	struct mt7999_firmware_download *fwdl = dev->fwdl;
	u32 size = sizeof(struct mt7999_firmware_hdr);
	u8 *fw_ptr = (u8 *)fw->data;

	section_map = (const struct mt7999_firmware_section *)
		      (fw_ptr + sizeof(struct mt7999_firmware_hdr));

	size += le32_to_cpu(hdr->section_num) * sizeof(struct mt7999_firmware_section);
	/* Section 0 (cert section) is part of the FW header */
	size += le32_to_cpu(section_map[0].size);

	fwdl->msg_sn++;

	mt76_wr(dev, MT_FWDL_SYNC_INFO, MT_FWDL_SYNC_INFO_ALL);
	mt76_wr(dev, MT_FWDL_TIMEOUT_ADDR, MT_FWDL_TIMEOUT);
	mt76_wr(dev, MT_FWDL_MSG,
		(u32_encode_bits(MT_FWDL_MSG_ID_START_DL, MT_FWDL_MSG_ID_MASK) |
		 u32_encode_bits(fwdl->msg_sn, MT_FWDL_MSG_SN_MASK)));
	mt76_rmw(dev, MT_FWDL_RADIO_TYPE,
		 MT_FWDL_RADIO_TYPE_MASK, MT_FWDL_RADIO_TYPE_WIFI);
	mt76_wr(dev, MT_FWDL_HEADER_SIZE, size);

	fwdl->size = size;
	fwdl->wait_ack = true;
	mt76_wr(dev, MT_MAC_IREG_FWDL_DOORBELL, MT_MAC_IREG_FWDL_DOORBELL_START_DL);
}

static int
mt7999_send_fw_binary(struct mt7999_dev *dev, const struct firmware *fw)
{
	const struct mt7999_firmware_hdr *hdr;
	struct mt7999_firmware_download *fwdl = dev->fwdl;
	struct mt7999_firmware_section *section;
	u16 slot_ready = fwdl->q.slot_ready;
	u16 block_id = fwdl->block_id;
	u8 *fw_ptr = (u8 *)fw->data;
	u8 *data;
	u32 len;

	hdr = (const struct mt7999_firmware_hdr *)(fw->data);
	if (block_id == 1 || block_id > le32_to_cpu(hdr->section_num))
		return -EINVAL;

	if (!mt76_poll(dev, MT_FWDL_COMM_STATE, MT_FWDL_COMM_STATE_MASK,
		       MT_FWDL_COMM_STATE_SLOT, 70000))
		return -EIO;

	fwdl->slot_done = mt76_rr(dev, MT_FWDL_SLOT_DONE);
	if (slot_ready != fwdl->slot_done) /* Slot is not empty*/
		return -EINVAL;

	section = (struct mt7999_firmware_section *)(fw_ptr +
		  (sizeof(struct mt7999_firmware_section) * (block_id - 1)) +
		  sizeof(struct mt7999_firmware_hdr));
	data = block_id ? fw_ptr + le32_to_cpu(section->offset) : fw_ptr;
	len = block_id ? le32_to_cpu(section->size): fwdl->size;

	while (len > 0) {
		struct mt7999_fwdl_entry *entry;
		u32 cur_len = min_t(u32, len, SLOT_SIZE);

		if (((slot_ready + 1) % SLOT_NUM) == fwdl->slot_done) {
			dev_err(dev->mt76.dev, "FWDL ring is full\n");
			return -ENOMEM;
		}

		entry = &fwdl->q.entry[slot_ready];
		memset(entry->buf, 0, SLOT_SIZE);
		memcpy(entry->buf, data, cur_len);

		slot_ready = (slot_ready + 1) % SLOT_NUM;
		data += cur_len;
		len -= cur_len;
	}
	fwdl->q.slot_ready = slot_ready;
	fwdl->msg_sn++;

	mt76_wr(dev, MT_FWDL_MSG,
		(u32_encode_bits(MT_FWDL_MSG_ID_SLOT_DONE, MT_FWDL_MSG_ID_MASK) |
		 u32_encode_bits(fwdl->msg_sn, MT_FWDL_MSG_SN_MASK)));
	mt76_wr(dev, MT_FWDL_SLOT_READY, slot_ready);

	mt76_wr(dev, MT_MAC_IREG_FWDL_DOORBELL, MT_MAC_IREG_FWDL_DOORBELL_SLOT_RDY);

	return 0;
}

static int
mt7999_handle_fwdl_notification(struct mt7999_dev *dev, const struct firmware *fw)
{
	struct mt7999_firmware_download *fwdl = dev->fwdl;
	unsigned long ret;

	while (true) {
		ret = wait_for_completion_timeout(&fwdl->wait, 20 * HZ);
		if (!ret)
			return -EIO;

		if (test_and_clear_bit(FWDL_REQUEST_DL, &fwdl->action)) {
			ret = mt7999_send_fw_binary(dev, fw);
			if (ret)
				return ret;
		}

		if (test_and_clear_bit(FWDL_DONE, &fwdl->action))
			return 0;

		if (test_and_clear_bit(FWDL_FAILED, &fwdl->action))
			return -fwdl->errno;
	}

	return 0;
}

static void mt7999_emi_config(struct mt7999_dev *dev)
{
	mt76_wr(dev, MT_CONN_HOST_CSR_EMI_BASE, 0);
}

static int mt7999_poll_sw_init_done(struct mt7999_dev *dev)
{
	/* make sure fw is download state */
	if (!mt76_poll_msec(dev, MT_FWDL_SW_INIT_DONE, MT_FWDL_SW_INIT_DONE_MASK,
			    MT_FWDL_SW_INIT_DONE_MASK, 10000)) {
		dev_err(dev->mt76.dev, "Firmware is not ready for download\n");
		return -EIO;
	}

	if (!mt76_poll_msec(dev, MT_FWDL_FW_STATE, MT_FWDL_FW_STATE_MASK, 0, 5000)) {
		dev_err(dev->mt76.dev,
			"Timeout for polling FW state, current state=0x%x\n",
			mt76_rr(dev, MT_FWDL_FW_STATE));
		return -EIO;
	}

	return 0;
}

static int mt7999_load_firmware(struct mt7999_dev *dev)
{
	const struct mt7999_firmware_hdr *hdr;
	struct mt7999_firmware_download *fwdl;
	const struct firmware *fw = NULL;
	int ret;

	fwdl = kzalloc(sizeof(struct mt7999_firmware_download), GFP_KERNEL);
	if (!fwdl)
		return -ENOMEM;

	tasklet_setup(&fwdl->irq_tasklet, mt7999_irq_tasklet_fwdl);
	init_completion(&fwdl->wait);
	spin_lock_init(&fwdl->irq_lock);
	fwdl->dev = dev;
	dev->fwdl = fwdl;

	if (dev->testmode_enable)
		ret = request_firmware(&fw, fw_name(dev, FIRMWARE_TM), dev->mt76.dev);
	else
		ret = request_firmware(&fw, fw_name(dev, FIRMWARE), dev->mt76.dev);
	if (ret)
		goto out;

	hdr = (const struct mt7999_firmware_hdr *)(fw->data);
	memcpy(dev->fw_build_date, hdr->build_date, sizeof(dev->fw_build_date));
	dev->fw_build_date[MT7999_BUILD_TIME_LEN - 1] = '\0';
	dev_info(dev->mt76.dev, "Firmware Version: 0x%x, Build Time: %.16s, Mode: %s\n",
		 le32_to_cpu(hdr->version), dev->fw_build_date,
		 dev->testmode_enable ? "Testmode" : "Normal mode");

	mt7999_emi_config(dev);

	set_bit(MT76_STATE_MCU_FWDL, &dev->mphy.state);
	ret = mt7999_poll_sw_init_done(dev);
	if (ret)
		goto release_fw;

	ret = mt7999_slot_init(dev);
	if (ret)
		goto release_fw;

	mt76_wr(dev, MT_MAC_IREG_AUTO_CLR, 0);
	mt7999_irq_fwdl_enable(dev, MT_MAC_IREG_IRQ_ALL);

	mt7999_start_fwdl(dev, fw);

	ret = mt7999_handle_fwdl_notification(dev, fw);
	if (ret)
		goto free_slot;

	dev_dbg(dev->mt76.dev, "Firmware init done\n");

free_slot:
	mt7999_irq_fwdl_disable(dev, MT_MAC_IREG_IRQ_ALL);
	mt76_wr(dev, MT_MAC_IREG_AUTO_CLR, ~0);
	mt7999_slot_destroy(dev);
release_fw:
	release_firmware(fw);
	clear_bit(MT76_STATE_MCU_FWDL, &dev->mphy.state);
out:
	tasklet_disable(&fwdl->irq_tasklet);
	kfree(dev->fwdl);
	dev->fwdl = NULL;

	return ret;
}

int mt7999_mcu_fw_log_2_host(struct mt7999_dev *dev, u8 ctrl)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
		u8 ctrl;
		u8 interval;
		u8 _rsv2[2];
	} __packed data = {
		.tag = cpu_to_le16(UNI_WSYS_CONFIG_FW_LOG_CTRL),
		.len = cpu_to_le16(sizeof(data) - 4),
		.ctrl = ctrl,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(WSYS_CONFIG), &data,
				 sizeof(data), true);
}

int mt7999_mcu_fw_dbg_ctrl(struct mt7999_dev *dev, u32 module, u8 level)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
		__le32 module_idx;
		u8 level;
		u8 _rsv2[3];
	} data = {
		.tag = cpu_to_le16(UNI_WSYS_CONFIG_FW_DBG_CTRL),
		.len = cpu_to_le16(sizeof(data) - 4),
		.module_idx = cpu_to_le32(module),
		.level = level,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(WSYS_CONFIG), &data,
				 sizeof(data), false);
}

int mt7999_mcu_fw_time_sync(struct mt76_dev *dev)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
		__le32 sec;
		__le32 usec;
	} data = {
		.tag = cpu_to_le16(UNI_WSYS_CONFIG_FW_TIME_SYNC),
		.len = cpu_to_le16(sizeof(data) - 4),
	};
	struct timespec64 ts;
	struct tm tm;

	ktime_get_real_ts64(&ts);
	data.sec = cpu_to_le32((u32)ts.tv_sec);
	data.usec = cpu_to_le32((u32)(ts.tv_nsec / 1000));

	/* Dump synchronized time for ConsysPlanet to parse. */
	time64_to_tm(ts.tv_sec, 0, &tm);
	dev_info(dev->dev, "%ld-%02d-%02d %02d:%02d:%02d.%ld UTC\n",
	        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
	        tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000);

	return mt76_mcu_send_msg(dev, MCU_WM_UNI_CMD(WSYS_CONFIG), &data,
	                         sizeof(data), true);
}

/*static int mt7999_mcu_set_mwds(struct mt7999_dev *dev, bool enabled)
{
	struct {
		u8 enable;
		u8 _rsv[3];
	} __packed req = {
		.enable = enabled
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WA_EXT_CMD(MWDS_SUPPORT), &req,
				 sizeof(req), false);
}*/

static void mt7999_add_rx_airtime_tlv(struct sk_buff *skb, u8 band_idx)
{
	struct vow_rx_airtime_clr *req_airtime_clr;
	struct vow_rx_early_end *req_early_end;
	struct vow_rx_airtime *req_airtime;
	struct vow_rx_ed_offs *req_ed_offs;
	struct tlv *tlv;

	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_VOW_RX_AT_AIRTIME_CLR_EN,
				     sizeof(*req_airtime_clr));
	req_airtime_clr = (struct vow_rx_airtime_clr *)tlv;
	req_airtime_clr->enable = true;
	req_airtime_clr->band = band_idx;

	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_VOW_RX_AT_AIRTIME_EN,
				     sizeof(*req_airtime));
	req_airtime = (struct vow_rx_airtime *)tlv;
	req_airtime->enable = true;
	req_airtime->band = band_idx;

	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_VOW_RX_AT_EARLY_END_EN,
				     sizeof(*req_early_end));
	req_early_end = (struct vow_rx_early_end *)tlv;
	req_early_end->enable = true;

	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_VOW_RX_AT_ED_OFFS,
				     sizeof(*req_ed_offs));
	req_ed_offs = (struct vow_rx_ed_offs *)tlv;
	req_ed_offs->compensation = 4;
	req_ed_offs->band = band_idx;
}

int mt7999_mcu_init_rx_airtime(struct mt7999_dev *dev)
{
	struct uni_header hdr = {};
	struct sk_buff *skb;
	int len, n_bands, i;

	n_bands = 1 + mt7999_band_valid(dev, MT_BAND1) +
		  mt7999_band_valid(dev, MT_BAND2);
	len = sizeof(hdr) + n_bands * (sizeof(struct vow_rx_airtime) +
				       sizeof(struct vow_rx_airtime_clr) +
				       sizeof(struct vow_rx_early_end) +
				       sizeof(struct vow_rx_ed_offs));
	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));

	for (i = MT_BAND0; i < __MT_MAX_BAND; i++) {
		if (mt7999_band_valid(dev, i))
			mt7999_add_rx_airtime_tlv(skb, i);
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WM_UNI_CMD(VOW), true);
}

int mt7999_mcu_init_firmware(struct mt7999_dev *dev)
{
	int ret;

	/* force firmware operation mode into normal state,
	 * which should be set before firmware download stage.
	 */
	mt76_wr(dev, MT_SWDEF_CONFIG_MODE, MT_SWDEF_CONFIG_NORMAL_MODE);

	ret = mt7999_load_firmware(dev);
	if (ret)
		return ret;

	ret = mt7999_driver_own(dev, 0);
	if (ret)
		return ret;
	/* set driver own for band1 when two hif exist */
	if (dev->hif2)
		ret = mt7999_driver_own(dev, 1);

	return ret;
}

int mt7999_mcu_init(struct mt7999_dev *dev)
{
	static const struct mt76_mcu_ops mt7999_mcu_ops = {
		.max_retry = 3,
		.headroom = sizeof(struct mt76_connac2_mcu_txd), /* reuse */
		.mcu_skb_send_msg = mt7999_mcu_send_message,
		.mcu_parse_response = mt7999_mcu_parse_response,
	};

	dev->mt76.mcu_ops = &mt7999_mcu_ops;

	return mt7999_mcu_init_firmware(dev);
}

void mt7999_mcu_exit(struct mt7999_dev *dev)
{
	mt7999_mcu_restart(&dev->mt76);
	if (mt7999_firmware_state(dev, FW_STATE_INITIAL)) {
		dev_err(dev->mt76.dev, "Failed to exit mcu\n");
		goto out;
	}

	/* Todo: check if driver own is necessary */
#if 0
	mt76_wr(dev, MT_TOP_LPCR_HOST_BAND(0), MT_TOP_LPCR_HOST_FW_OWN);
	if (dev->hif2)
		mt76_wr(dev, MT_TOP_LPCR_HOST_BAND(1),
			MT_TOP_LPCR_HOST_FW_OWN);
#endif
out:
	skb_queue_purge(&dev->mt76.mcu.res_q);
}

static int mt7999_mcu_set_red_config(struct mt7999_dev *dev)
{
#define RED_TOKEN_CONFIG	2
#define RED_TOKEN_SRC_CNT	4
#define RED_RSV_SIZE		1120
	struct mt7999_red_config {
		u8 rsv[4];

		__le16 tag;
		__le16 len;

		u8 mode;
		u8 version;
		u8 _rsv[4];
		__le16 cmd_len;

		__le16 tcp_offset;
		__le16 priority_offset;
		__le16 token_per_src[RED_TOKEN_SRC_CNT];
		__le16 token_thr_per_src[RED_TOKEN_SRC_CNT];
	} __packed req = {
		.tag = cpu_to_le16(UNI_SDO_RED_SETTING),
		.len = cpu_to_le16(sizeof(req) - 4),
		.mode = RED_TOKEN_CONFIG,
		.cmd_len = cpu_to_le16(sizeof(req) - 8),
		.tcp_offset = cpu_to_le16(200),
		.priority_offset = cpu_to_le16(255),
	};
	int len = sizeof(req) + RED_RSV_SIZE;
	struct sk_buff *skb;
	u8 i;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	for (i = 0; i < RED_TOKEN_SRC_CNT; i++) {
		req.token_per_src[i] = cpu_to_le16(MT7999_WED_TOKEN_SIZE);
		req.token_thr_per_src[i] = cpu_to_le16(MT7999_WED_TOKEN_SIZE);
	}
	req.token_per_src[RED_TOKEN_SRC_CNT - 1] = dev->mt76.token_size;

	skb_put_data(skb, &req, sizeof(req));
	skb_put_zero(skb, RED_RSV_SIZE);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WM_UNI_CMD(SDO), false);
}

int mt7999_mcu_red_config(struct mt7999_dev *dev, bool enable)
{
#define RED_DISABLE		0
#define RED_BY_MCU_ENABLE	2
	struct {
		u8 __rsv1[4];

		__le16 tag;
		__le16 len;
		u8 enable;
		u8 __rsv2[3];
	} __packed req = {
		.tag = cpu_to_le16(UNI_VOW_RED_ENABLE),
		.len = cpu_to_le16(sizeof(req) - 4),
		.enable = enable ? RED_BY_MCU_ENABLE : RED_DISABLE,
	};
	int ret;

	ret = mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(VOW), &req,
				 sizeof(req), true);

	if (ret)
		return ret;

#ifdef CONFIG_MTK_DEBUG
	dev->red_enable = enable;
#endif

	ret = mt7999_mcu_sdo_cmd(dev, false, SDO_PARAM_RED_EN, enable, 0);

	if (ret || !enable)
		return ret;

	return mt7999_mcu_set_red_config(dev);
}

int mt7999_mcu_set_hdr_trans(struct mt7999_dev *dev, bool hdr_trans)
{
	struct uni_header hdr = {};
	struct hdr_trans_blacklist *req_blacklist;
	struct hdr_trans_en *req_en;
	struct sk_buff *skb;
	struct tlv *tlv;
	int len = MT7999_HDR_TRANS_MAX_SIZE + sizeof(hdr);

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));

	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_HDR_TRANS_EN, sizeof(*req_en));
	req_en = (struct hdr_trans_en *)tlv;
	req_en->enable = hdr_trans;

	tlv = mt7999_mcu_add_uni_tlv(skb, UNI_HDR_TRANS_VLAN,
				     sizeof(struct hdr_trans_vlan));

	if (hdr_trans) {
		tlv = mt7999_mcu_add_uni_tlv(skb, UNI_HDR_TRANS_BLACKLIST,
					     sizeof(*req_blacklist));
		req_blacklist = (struct hdr_trans_blacklist *)tlv;
		req_blacklist->enable = 1;
		req_blacklist->type = cpu_to_le16(ETH_P_PAE);
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WM_UNI_CMD(RX_HDR_TRANS), true);
}

int mt7999_mcu_set_tx(struct mt7999_dev *dev, struct ieee80211_vif *vif,
		      struct ieee80211_bss_conf *link_conf)
{
#define UNI_EDCA_AC_PARAM	0
#define WMM_AIFS_SET		BIT(0)
#define WMM_CW_MIN_SET		BIT(1)
#define WMM_CW_MAX_SET		BIT(2)
#define WMM_TXOP_SET		BIT(3)
#define WMM_PARAM_SET		(WMM_AIFS_SET | WMM_CW_MIN_SET | \
				 WMM_CW_MAX_SET | WMM_TXOP_SET)
	struct mt7999_vif_link *link = mt7999_vif_conf_link(dev, vif, link_conf);
	struct mt7999_vif *mvif = (struct mt7999_vif *)vif->drv_priv;
	unsigned int link_id = link_conf->link_id;
	struct mt7999_vif_link_info *link_info = &mvif->link_info[link_id];
	struct {
		u8 bss_idx;
		u8 __rsv[3];
	} __packed hdr = {
		.bss_idx = link->mt76.idx,
	};
	struct sk_buff *skb;
	int len = sizeof(hdr) + IEEE80211_NUM_ACS * sizeof(struct edca);
	int ac;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		struct ieee80211_tx_queue_params *q = &link_info->queue_params[ac];
		struct edca *e;
		struct tlv *tlv;

		tlv = mt7999_mcu_add_uni_tlv(skb, UNI_EDCA_AC_PARAM, sizeof(*e));

		e = (struct edca *)tlv;
		e->set = WMM_PARAM_SET;
		e->queue = ac;
		e->aifs = q->aifs;
		e->txop = cpu_to_le16(q->txop);
		if (!ac)
			link->phy->txop[link->mt76.wmm_idx] = q->txop;

		if (q->cw_min)
			e->cw_min = fls(q->cw_min);
		else
			e->cw_min = 5;

		if (q->cw_max)
			e->cw_max = fls(q->cw_max);
		else
			e->cw_max = 10;
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WM_UNI_CMD(EDCA_UPDATE), true);
}

int mt7999_mcu_set_fcc5_lpn(struct mt7999_dev *dev, int val)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		__le32 ctrl;
		__le16 min_lpn;
		u8 rsv[2];
	} __packed req = {
		.tag = cpu_to_le16(UNI_RDD_CTRL_SET_TH),
		.len = cpu_to_le16(sizeof(req) - 4),
		.ctrl = cpu_to_le32(RDD_FCC5_LPN_UPDATE),
		.min_lpn = cpu_to_le16(val),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(RDD_CTRL),
				 &req, sizeof(req), true);
}

int mt7999_mcu_set_pulse_th(struct mt7999_dev *dev,
			    const struct mt7999_dfs_pulse *pulse)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		__le32 ctrl;
		__le32 max_width;		/* us */
		__le32 max_pwr;			/* dbm */
		__le32 min_pwr;			/* dbm */
		__le32 min_stgr_pri;		/* us */
		__le32 max_stgr_pri;		/* us */
		__le32 min_cr_pri;		/* us */
		__le32 max_cr_pri;		/* us */
	} __packed req = {
		.tag = cpu_to_le16(UNI_RDD_CTRL_SET_TH),
		.len = cpu_to_le16(sizeof(req) - 4),
		.ctrl = cpu_to_le32(RDD_PULSE_THRESHOLD_UPDATE),

#define __req_field(field) .field = cpu_to_le32(pulse->field)
		__req_field(max_width),
		__req_field(max_pwr),
		__req_field(min_pwr),
		__req_field(min_stgr_pri),
		__req_field(max_stgr_pri),
		__req_field(min_cr_pri),
		__req_field(max_cr_pri),
#undef __req_field
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(RDD_CTRL),
				 &req, sizeof(req), true);
}

int mt7999_mcu_set_radar_th(struct mt7999_dev *dev, int index,
			    const struct mt7999_dfs_pattern *pattern)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		__le32 ctrl;
		__le16 radar_type;
		u8 enable;
		u8 stgr;
		u8 min_crpn;
		u8 max_crpn;
		u8 min_crpr;
		u8 min_pw;
		__le32 min_pri;
		__le32 max_pri;
		u8 max_pw;
		u8 min_crbn;
		u8 max_crbn;
		u8 min_stgpn;
		u8 max_stgpn;
		u8 min_stgpr;
		u8 rsv[2];
		__le32 min_stgpr_diff;
	} __packed req = {
		.tag = cpu_to_le16(UNI_RDD_CTRL_SET_TH),
		.len = cpu_to_le16(sizeof(req) - 4),

		.ctrl = cpu_to_le32(RDD_RADAR_THRESHOLD_UPDATE),
		.radar_type = cpu_to_le16(index),

#define __req_field_u8(field) .field = pattern->field
#define __req_field_u32(field) .field = cpu_to_le32(pattern->field)
		__req_field_u8(enable),
		__req_field_u8(stgr),
		__req_field_u8(min_crpn),
		__req_field_u8(max_crpn),
		__req_field_u8(min_crpr),
		__req_field_u8(min_pw),
		__req_field_u32(min_pri),
		__req_field_u32(max_pri),
		__req_field_u8(max_pw),
		__req_field_u8(min_crbn),
		__req_field_u8(max_crbn),
		__req_field_u8(min_stgpn),
		__req_field_u8(max_stgpn),
		__req_field_u8(min_stgpr),
		__req_field_u32(min_stgpr_diff),
#undef __req_field_u8
#undef __req_field_u32
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(RDD_CTRL),
				 &req, sizeof(req), true);
}

static int
mt7999_mcu_background_chain_ctrl(struct mt7999_phy *phy,
				 struct cfg80211_chan_def *chandef,
				 int cmd)
{
#define UNI_OFFCH_SCAN_CTRL	0
	struct mt7999_dev *dev = phy->dev;
	struct mt76_phy *mphy = phy->mt76;
	struct ieee80211_channel *chan = mphy->chandef.chan;
	int freq = mphy->chandef.center_freq1;
	struct mt7999_mcu_background_chain_ctrl req = {
		.tag = cpu_to_le16(UNI_OFFCH_SCAN_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),
		.monitor_scan_type = 2, /* simple rx */
		.band_idx = phy->mt76->band_idx,
		.band = 1,
	};

	if (!chandef && cmd != CH_SWITCH_BACKGROUND_SCAN_STOP)
		return -EINVAL;

	if (!cfg80211_chandef_valid(&mphy->chandef))
		return -EINVAL;

	switch (cmd) {
	case CH_SWITCH_BACKGROUND_SCAN_START:
		req.chan = chan->hw_value;
		req.central_chan = ieee80211_frequency_to_channel(freq);
		req.bw = mt76_connac_chan_bw(&mphy->chandef);
		req.monitor_chan = chandef->chan->hw_value;
		req.monitor_central_chan =
			ieee80211_frequency_to_channel(chandef->center_freq1);
		req.monitor_bw = mt76_connac_chan_bw(chandef);
		req.scan_mode = 1;
		break;
	case CH_SWITCH_BACKGROUND_SCAN_RUNNING:
		req.monitor_chan = chandef->chan->hw_value;
		req.monitor_central_chan =
			ieee80211_frequency_to_channel(chandef->center_freq1);
		req.monitor_bw = mt76_connac_chan_bw(chandef);
		req.scan_mode = 2;
		break;
	case CH_SWITCH_BACKGROUND_SCAN_STOP:
		req.chan = chan->hw_value;
		req.central_chan = ieee80211_frequency_to_channel(freq);
		req.bw = mt76_connac_chan_bw(&mphy->chandef);
		req.tx_stream = hweight8(mphy->antenna_mask);
		req.rx_stream = mphy->antenna_mask;
		break;
	default:
		return -EINVAL;
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(OFFCH_SCAN_CTRL),
				 &req, sizeof(req), false);
}

int mt7999_mcu_rdd_background_enable(struct mt7999_phy *phy,
				     struct cfg80211_chan_def *chandef)
{
	struct mt7999_dev *dev = phy->dev;
	int err, region, rdd_idx = mt7999_get_rdd_idx(phy, true);

	if (rdd_idx < 0)
		return 0;

	if (!chandef) { /* disable offchain */
		err = mt7999_mcu_rdd_cmd(dev, RDD_STOP, rdd_idx, 0);
		if (err)
			return err;

		return mt7999_mcu_background_chain_ctrl(phy, NULL,
				CH_SWITCH_BACKGROUND_SCAN_STOP);
	}

	err = mt7999_mcu_background_chain_ctrl(phy, chandef,
					       CH_SWITCH_BACKGROUND_SCAN_START);
	if (err)
		return err;

	switch (dev->mt76.region) {
	case NL80211_DFS_ETSI:
		region = 0;
		break;
	case NL80211_DFS_JP:
		region = 2;
		break;
	case NL80211_DFS_FCC:
	default:
		region = 1;
		break;
	}

	return mt7999_mcu_rdd_cmd(dev, RDD_START, rdd_idx, region);
}

int mt7999_mcu_set_chan_info(struct mt7999_phy *phy, u16 tag, bool sta)
{
	static const u8 ch_band[] = {
		[NL80211_BAND_2GHZ] = 0,
		[NL80211_BAND_5GHZ] = 1,
		[NL80211_BAND_6GHZ] = 2,
	};
	struct mt7999_dev *dev = phy->dev;
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	int freq1 = chandef->center_freq1;
	u8 band_idx = phy->mt76->band_idx;
	enum nl80211_iftype iftype = sta ? NL80211_IFTYPE_STATION :
					   NL80211_IFTYPE_AP;
	struct {
		/* fixed field */
		u8 __rsv[4];

		__le16 tag;
		__le16 len;
		u8 control_ch;
		u8 center_ch;
		u8 bw;
		u8 tx_path_num;
		u8 rx_path;	/* mask or num */
		u8 switch_reason;
		u8 band_idx;
		u8 center_ch2;	/* for 80+80 only */
		__le16 cac_case;
		u8 channel_band;
		u8 rsv0;
		__le32 outband_freq;
		u8 txpower_drop;
		u8 ap_bw;
		u8 ap_center_ch;
		u8 rsv1[53];
	} __packed req = {
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(sizeof(req) - 4),
		.control_ch = chandef->chan->hw_value,
		.center_ch = ieee80211_frequency_to_channel(freq1),
		.bw = mt76_connac_chan_bw(chandef),
		.tx_path_num = hweight16(phy->mt76->chainmask),
		.rx_path = mt7999_rx_chainmask(phy),
		.band_idx = band_idx,
		.channel_band = ch_band[chandef->chan->band],
	};

	if (phy->mt76->offchannel ||
	    !test_bit(MT76_STATE_RUNNING, &phy->mt76->state))
		req.switch_reason = CH_SWITCH_SCAN_BYPASS_DPD;
	else if (!cfg80211_reg_can_beacon(phy->mt76->hw->wiphy, chandef, iftype))
		req.switch_reason = CH_SWITCH_DFS;
	else
		req.switch_reason = CH_SWITCH_NORMAL;

	if (tag == UNI_CHANNEL_SWITCH)
		req.rx_path = hweight8(req.rx_path);

	if (chandef->width == NL80211_CHAN_WIDTH_80P80) {
		int freq2 = chandef->center_freq2;

		req.center_ch2 = ieee80211_frequency_to_channel(freq2);
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_WMWA_UNI_CMD(CHANNEL_SWITCH),
				 &req, sizeof(req), true);
}

static int
mt7999_mcu_get_cal_free_data(struct mt7999_dev *dev)
{
#define MT_EE_BAND2_OFFSET		(0x1200 - 0x1100)
	struct cal_free_data {
		u16 adie_offs;
		u16 eep_offs;
	};
	static const struct cal_free_data cal_7975[] = {
		{ 0x5cd, 0x1051 }, { 0x5cf, 0x1053 }, { 0x5d1, 0x1055 },
		{ 0x5d3, 0x1057 }, { 0x6c0, 0x104c }, { 0x6c1, 0x104d },
		{ 0x6c2, 0x104e }, { 0x6c3, 0x104f }, { 0x7a1, 0x106b },
		{ 0x7a6, 0x107a }, { 0x7a8, 0x107c }, { 0x7aa, 0x107e },
	};
	static const struct cal_free_data cal_7976[] = {
		{  0x4c, 0x104c }, {  0x4d, 0x104d }, {  0x4e, 0x104e },
		{  0x4f, 0x104f }, {  0x50, 0x1050 }, {  0x51, 0x1051 },
		{  0x53, 0x1053 }, {  0x55, 0x1055 }, {  0x57, 0x1057 },
		{  0x59, 0x1059 }, { 0x790, 0x1069 }, { 0x791, 0x106b },
		{ 0x794, 0x106a }, { 0x795, 0x106c }, { 0x7a6, 0x107a },
		{ 0x7a8, 0x107c }, { 0x7aa, 0x107e },
	};
	static const struct cal_free_data cal_7977[] = {
		{ 0x4c, 0x114c }, { 0x4d, 0x114d }, { 0x4e, 0x114e },
		{ 0x4f, 0x114f }, { 0x50, 0x1150 }, { 0x51, 0x1151 },
		{ 0x53, 0x1153 }, { 0x55, 0x1155 }, { 0x57, 0x1157 },
		{ 0x59, 0x1159 }, { 0x69, 0x1169 }, { 0x6a, 0x116a },
		{ 0x7a, 0x117a }, { 0x7c, 0x117c }, { 0x7e, 0x117e },
	};
	static const struct cal_free_data cal_7979[] = {
		{ 0x4c, 0x114c}, { 0x4d, 0x114d}, { 0x4e, 0x114e},
		{ 0x4f, 0x114f}, { 0x50, 0x1150}, { 0x51, 0x1151},
		{ 0x53, 0x1153}, { 0x55, 0x1155}, { 0x57, 0x1157},
		{ 0x59, 0x1159}, { 0x69, 0x1169}, { 0x6a, 0x116a},
		{ 0x7a, 0x117a}, { 0x7c, 0x117c}, { 0x7e, 0x117e},
	};
#if 0
	static const struct cal_free_data cal_7978[] = {
		{ 0x4c, 0x114c }, { 0x4d, 0x114d }, { 0x4e, 0x114e },
		{ 0x4f, 0x114f }, { 0x50, 0x1150 }, { 0x51, 0x1151 },
		{ 0x53, 0x1153 }, { 0x55, 0x1155 }, { 0x57, 0x1157 },
		{ 0x59, 0x1159 }, { 0x90, 0x1169 }, { 0x91, 0x116b },
		{ 0x94, 0x116a }, { 0x95, 0x116c }, { 0xa6, 0x117a },
		{ 0xa8, 0x117c }, { 0xaa, 0x117e },
	};
	static const struct cal_free_data cal_7777[] = {
		{ 0x47, 0x1147 }, { 0x48, 0x1148 }, { 0x49, 0x1149 },
		{ 0x4a, 0x114a }, { 0x4b, 0x114b }, { 0x51, 0x1151 },
		{ 0x53, 0x1153 }, { 0x55, 0x1155 }, { 0x57, 0x1157 },
		{ 0x59, 0x1159 }, { 0x69, 0x1169 }, { 0x6a, 0x116a },
		{ 0x6d, 0x116d }, { 0x6e, 0x116e }, { 0x7a, 0x117a },
		{ 0x7e, 0x117e }, { 0x80, 0x1180 }, { 0x81, 0x1181 },
		{ 0x82, 0x1182 },
	};
#endif
	const struct cal_free_data *cal_arr[__MT_MAX_BAND];
	u16 cal_arr_len[__MT_MAX_BAND] = {};
	u8 *eeprom = (u8 *)dev->mt76.eeprom.data;
	int band, i, ret;

#define CAL_ARR(_band, _adie) do {				\
		cal_arr[_band] = cal_##_adie;			\
		cal_arr_len[_band] = ARRAY_SIZE(cal_##_adie);	\
	} while (0)

	switch (mt76_chip(&dev->mt76)) {
	case MT7999_DEVICE_ID:
		if (dev->var.type == MT7999_VAR_TYPE_233)
			return 0;

		if (dev->var.fem == MT7999_FEM_INT) {
			CAL_ARR(0, 7975);
			CAL_ARR(1, 7979);
			CAL_ARR(2, 7979);
		} else {
			CAL_ARR(0, 7976);
			CAL_ARR(1, 7977);
			CAL_ARR(2, 7977);
		}
		break;
	default:
		return -EINVAL;
	}

	for (band = 0; band < __MT_MAX_BAND; band++) {
		u8 buf[MT7999_EEPROM_BLOCK_SIZE];
		const struct cal_free_data *cal;
		u16 prev_block_idx = -1;
		u16 adie_base;

		if (!cal_arr_len[band])
			continue;

		if (band == MT_BAND0)
			adie_base = MT7999_EFUSE_BASE_OFFS_ADIE0;
		else if (band == MT_BAND1)
			adie_base = MT7999_EFUSE_BASE_OFFS_ADIE1;
		else if (band == MT_BAND2)
			adie_base = MT7999_EFUSE_BASE_OFFS_ADIE2;

		cal = cal_arr[band];
		for (i = 0; i < cal_arr_len[band]; i++) {
			u16 adie_offset = cal[i].adie_offs + adie_base;
			u16 eep_offset = cal[i].eep_offs;
			u16 block_idx = adie_offset / MT7999_EEPROM_BLOCK_SIZE;
			u16 offset = adie_offset % MT7999_EEPROM_BLOCK_SIZE;

			if (band == MT_BAND2)
				eep_offset += MT_EE_BAND2_OFFSET;

			if (prev_block_idx != block_idx) {
				memset(buf, 0, sizeof(buf));
				ret = mt7999_mcu_get_eeprom(dev, adie_offset, buf,
							    MT7999_EEPROM_BLOCK_SIZE,
							    EEPROM_MODE_EFUSE);
				if (ret) {
					if (ret != -EINVAL)
						return ret;
					prev_block_idx = -1;
					continue;
				}
			}
			eeprom[eep_offset] = buf[offset];
			prev_block_idx = block_idx;
		}
	}

	return 0;
}

int mt7999_mcu_set_eeprom(struct mt7999_dev *dev)
{
#define PER_PAGE_SIZE		0x400
	struct mt7999_mcu_eeprom_update req = {
		.tag = cpu_to_le16(UNI_EFUSE_BUFFER_MODE),
		.buffer_mode = EE_MODE_BUFFER,
		.format = EE_FORMAT_WHOLE,
	};
	u16 eeprom_size = MT7999_EEPROM_SIZE;
	u8 *eep = (u8 *)dev->mt76.eeprom.data;
	int ret, eep_len, i;

	ret = mt7999_mcu_get_cal_free_data(dev);
	if (ret)
		return ret;

	req.max_page = DIV_ROUND_UP(eeprom_size, PER_PAGE_SIZE) - 1;
	for (i = 0; i <= req.max_page; i++, eep += eep_len) {
		struct sk_buff *skb;
		int msg_len;

		if (i == req.max_page && !!(eeprom_size % PER_PAGE_SIZE))
			eep_len = eeprom_size % PER_PAGE_SIZE;
		else
			eep_len = PER_PAGE_SIZE;

		msg_len = sizeof(req) + eep_len;
		skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, msg_len);
		if (!skb)
			return -ENOMEM;

		req.len = cpu_to_le16(msg_len - 4);
		req.idx = i;
		req.buf_len = cpu_to_le16(eep_len);

		skb_put_data(skb, &req, sizeof(req));
		skb_put_data(skb, eep, eep_len);

		ret = mt76_mcu_skb_send_msg(&dev->mt76, skb,
					    MCU_WM_UNI_CMD(EFUSE_CTRL), true);
		if (ret)
			return ret;
	}

	return 0;
}

int mt7999_mcu_get_eeprom(struct mt7999_dev *dev, u32 offset, u8 *buf, u32 buf_len,
			  enum mt7999_eeprom_mode mode)
{
	struct mt7999_mcu_eeprom_access req = {
		.info.len = cpu_to_le16(sizeof(req) - 4),
	};
	struct mt7999_mcu_eeprom_access_event *event;
	struct sk_buff *skb;
	int ret, cmd;
	u32 addr;
	u32 ret_len;

	switch (mode) {
	case EEPROM_MODE_EFUSE:
		addr = round_down(offset, MT7999_EEPROM_BLOCK_SIZE);
		cmd = MCU_WM_UNI_CMD_QUERY(EFUSE_CTRL);
		req.info.tag = cpu_to_le16(UNI_EFUSE_ACCESS);
		break;
	case EEPROM_MODE_EXT:
		addr = round_down(offset, MT7999_EXT_EEPROM_BLOCK_SIZE);
		cmd = MCU_WM_UNI_CMD_QUERY(EXT_EEPROM_CTRL);
		req.info.tag = cpu_to_le16(UNI_EXT_EEPROM_ACCESS);
		req.eeprom.ext_eeprom.data_len = cpu_to_le32(buf_len);
		break;
	default:
		return -EINVAL;
	}

	req.info.addr = cpu_to_le32(addr);
	ret = mt76_mcu_send_and_get_msg(&dev->mt76, cmd, &req, sizeof(req),
					true, &skb);
	if (ret)
		return ret;

	event = (struct mt7999_mcu_eeprom_access_event *)skb->data;
	if (!event->valid) {
		ret = -EINVAL;
		goto out;
	}

	switch (mode) {
	case EEPROM_MODE_EFUSE:
		if (!buf_len || buf_len > MT7999_EEPROM_BLOCK_SIZE)
			buf_len = MT7999_EEPROM_BLOCK_SIZE;

		memcpy(buf, event->eeprom.efuse, buf_len);
		break;
	case EEPROM_MODE_EXT:
		ret_len = le32_to_cpu(event->eeprom.ext_eeprom.data_len);
		if (!buf_len || buf_len > MT7999_EXT_EEPROM_BLOCK_SIZE)
			buf_len = MT7999_EXT_EEPROM_BLOCK_SIZE;

		memcpy(buf, event->eeprom.ext_eeprom.data,
		       ret_len < buf_len ? ret_len : buf_len);
		break;
	default:
		ret = -EINVAL;
		break;
	}

out:
	dev_kfree_skb(skb);

	return ret;
}

int
mt7999_mcu_write_ext_eeprom(struct mt7999_dev *dev, u32 offset,
			    u32 data_len, u8 *write_buf)
{
	struct mt7999_mcu_eeprom_access req = {
		.info.tag = cpu_to_le16(UNI_EXT_EEPROM_ACCESS),
		.info.len = cpu_to_le16(sizeof(req) - 4 +
					MT7999_EXT_EEPROM_BLOCK_SIZE),
	};
	u32 block_num, block_size = MT7999_EXT_EEPROM_BLOCK_SIZE;
	u8 *buf = write_buf;
	int i, ret = -EINVAL;
	int msg_len = sizeof(req) + block_size;

	if (!mt7999_has_ext_eeprom(dev))
		return ret;

	if (!buf)
		buf = (u8 *)dev->mt76.eeprom.data + offset;

	block_num = DIV_ROUND_UP(data_len, block_size);
	for (i = 0; i < block_num; i++) {
		struct sk_buff *skb;
		u32 buf_len = block_size;
		u32 block_offs = i * block_size;

		if (block_offs + block_size > data_len)
			buf_len = data_len % block_size;

		req.info.addr = cpu_to_le32(offset + block_offs);
		req.eeprom.ext_eeprom.data_len = cpu_to_le32(buf_len);

		skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, msg_len);
		if (!skb)
			return -ENOMEM;

		skb_put_data(skb, &req, sizeof(req));
		skb_put_data(skb, buf, buf_len);

		ret = mt76_mcu_skb_send_msg(&dev->mt76, skb,
					    MCU_WM_UNI_CMD(EXT_EEPROM_CTRL), false);
		if (ret)
			return ret;

		buf += buf_len;
	}

	return 0;
}

int mt7999_mcu_get_efuse_free_block(struct mt7999_dev *dev, void *info)
{
#define UNI_EVENT_EFUSE_FREE_BLOCK	1
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
		u8 num;
		u8 version;
		u8 die_idx;
		u8 _rsv2;
	} __packed req = {
		.tag = cpu_to_le16(UNI_EFUSE_FREE_BLOCK),
		.len = cpu_to_le16(sizeof(req) - 4),
		.version = 2,
	};
	struct mt7999_mcu_eeprom_free_block_event {
		u8 _rsv[4];

		struct {
			__le16 tag;
			__le16 len;
			struct efuse_bank_info info;
		} efuse_bank[MT_EFUSE_TOTAL_BANK];
	} __packed * res;
	struct efuse_bank_info *bank_info = (struct efuse_bank_info *)info;
	struct sk_buff *skb;
	int ret, i;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_WM_UNI_CMD_QUERY(EFUSE_CTRL), &req,
					sizeof(req), true, &skb);
	if (ret)
		return ret;

	res = (struct mt7999_mcu_eeprom_free_block_event *)skb->data;
	if (le16_to_cpu(res->efuse_bank[0].tag) != UNI_EVENT_EFUSE_FREE_BLOCK) {
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	for (i = 0; i < MT_EFUSE_TOTAL_BANK; i++)
		memcpy(bank_info + i, &res->efuse_bank[i].info, sizeof(*bank_info));

	dev_kfree_skb(skb);

	return 0;
}

static int mt7999_mcu_set_pre_cal(struct mt7999_dev *dev, u32 idx,
				  u8 *cal, u32 len, u32 cal_id)
{
#define PRECAL_CMD_PRE_CAL_RESULT	0x0
	struct {
		/* fixed field */
		u8 action;
		u8 dest;
		u8 attribute;
		u8 tag_num;

		__le16 tag;
		__le16 len;

		__le32 cal_id;
		s8 precal;
		u8 band;
		u8 rsv[2];
		__le32 idx;
		__le32 cal_len;
	} req = {
		.tag = cpu_to_le16(PRECAL_CMD_PRE_CAL_RESULT),
		.len = cpu_to_le16(sizeof(req) - 4 + len),
		.cal_id = cpu_to_le32(cal_id),
		.idx = cpu_to_le32(idx),
		.cal_len = cpu_to_le32(len),
	};
	struct sk_buff *skb;

	if (!len)
		return 0;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, sizeof(req) + len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &req, sizeof(req));
	skb_put_data(skb, cal, len);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, MCU_WM_UNI_CMD(PRECAL_RESULT), false);
}

int mt7999_mcu_apply_group_cal(struct mt7999_dev *dev)
{
	u8 *cal = dev->cal.data, *eeprom = dev->mt76.eeprom.data;
	u32 idx = 0, total_idx = MT_EE_CAL_GROUP_SIZE / MT_EE_CAL_UNIT;
	u32 offs = MT_EE_DO_PRE_CAL;
	int ret = 0;

	if (!(eeprom[offs] & MT_EE_WIFI_CAL_GROUP))
		return 0;

	for (idx = 0; idx < total_idx; idx++, cal += MT_EE_CAL_UNIT) {
		ret = mt7999_mcu_set_pre_cal(dev, idx, cal, MT_EE_CAL_UNIT, RF_PRE_CAL);
		if (ret)
			goto out;
	}

	ret = mt7999_mcu_set_pre_cal(dev, total_idx, cal,
				     MT_EE_CAL_GROUP_SIZE % MT_EE_CAL_UNIT, RF_PRE_CAL);

out:
	return ret;
}

int mt7999_mcu_apply_tx_dpd(struct mt7999_phy *phy)
{
	struct mt7999_dev *dev = phy->dev;
	struct mt76_phy *mphy = phy->mt76;
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	enum nl80211_band band = chandef->chan->band;
	enum nl80211_chan_width bw = chandef->width;
	const struct ieee80211_channel *chan_list;
	u32 cal_id, chan_list_size, base_offset = 0, offs = MT_EE_DO_PRE_CAL;
	u32 per_chan_size = DPD_PER_CH_BW20_SIZE;
	u16 channel = ieee80211_frequency_to_channel(chandef->center_freq1);
	u8 dpd_mask, *cal = dev->cal.data, *eeprom = dev->mt76.eeprom.data;
	int idx, i, ret;
	bool has_skip_ch = (band == NL80211_BAND_5GHZ);

	switch (band) {
	case NL80211_BAND_2GHZ:
		dpd_mask = MT_EE_WIFI_CAL_DPD_2G;
		/* channel 14 don't need DPD cal */
		if (channel >= 1 && channel <= 4)
			channel = 3;
		else if (channel >= 5 && channel <= 9)
			channel = 7;
		else if (channel >= 10 && channel <= 13)
			channel = 11;
		else
			return 0;
		cal_id = RF_DPD_FLAT_CAL;
		chan_list = dpd_2g_ch_list_bw20;
		chan_list_size = DPD_CH_NUM(BW20_2G);
		break;
	case NL80211_BAND_5GHZ:
		dpd_mask = MT_EE_WIFI_CAL_DPD_5G;
		cal_id = RF_DPD_FLAT_5G_CAL;
		chan_list = mphy->sband_5g.sband.channels;
		chan_list_size = mphy->sband_5g.sband.n_channels;
		base_offset += MT_EE_CAL_DPD_SIZE_2G;
		if (bw == NL80211_CHAN_WIDTH_160) {
			base_offset += DPD_CH_NUM(BW20_5G) * DPD_PER_CH_BW20_SIZE +
				       DPD_CH_NUM(BW80_5G) * DPD_PER_CH_GT_BW20_SIZE;
			per_chan_size = DPD_PER_CH_GT_BW20_SIZE;
			cal_id = RF_DPD_FLAT_5G_MEM_CAL;
			chan_list = dpd_5g_ch_list_bw160;
			chan_list_size = DPD_CH_NUM(BW160_5G);
			has_skip_ch = false;
		} else if (bw == NL80211_CHAN_WIDTH_80) {
			base_offset += DPD_CH_NUM(BW20_5G) * DPD_PER_CH_BW20_SIZE;
			per_chan_size = DPD_PER_CH_GT_BW20_SIZE;
			cal_id = RF_DPD_FLAT_5G_MEM_CAL;
			chan_list = dpd_5g_ch_list_bw80;
			chan_list_size = DPD_CH_NUM(BW80_5G);
			has_skip_ch = false;
		} else if (bw > NL80211_CHAN_WIDTH_20) {
			/* apply (center channel - 2)'s dpd cal data for bw 40/80 channels */
			channel -= 2;
		}
		if (channel >= dpd_5g_skip_ch_list[0].hw_value &&
		    channel <= dpd_5g_skip_ch_list[DPD_CH_NUM(BW20_5G_SKIP) - 1].hw_value)
			return 0;
		break;
	case NL80211_BAND_6GHZ:
		dpd_mask = MT_EE_WIFI_CAL_DPD_6G;
		cal_id = RF_DPD_FLAT_6G_CAL;
		chan_list = mphy->sband_6g.sband.channels;
		chan_list_size = mphy->sband_6g.sband.n_channels;
		base_offset += MT_EE_CAL_DPD_SIZE_2G + MT_EE_CAL_DPD_SIZE_5G;
		if (bw == NL80211_CHAN_WIDTH_160) {
			base_offset += mphy->sband_6g.sband.n_channels * DPD_PER_CH_BW20_SIZE;
			per_chan_size = DPD_PER_CH_GT_BW20_SIZE;
			cal_id = RF_DPD_FLAT_6G_MEM_CAL;
			chan_list = dpd_6g_ch_list_bw160;
			chan_list_size = DPD_CH_NUM(BW160_6G);
		} else if (bw == NL80211_CHAN_WIDTH_320) {
			base_offset += mphy->sband_6g.sband.n_channels * DPD_PER_CH_BW20_SIZE +
				       DPD_CH_NUM(BW80_6G) * DPD_PER_CH_GT_BW20_SIZE +
				       DPD_CH_NUM(BW160_6G) * DPD_PER_CH_GT_BW20_SIZE;
			per_chan_size = DPD_PER_CH_GT_BW20_SIZE;
			cal_id = RF_DPD_FLAT_6G_MEM_CAL;
			chan_list = dpd_6g_ch_list_bw320;
			chan_list_size = DPD_CH_NUM(BW320_6G);
		} else if (bw == NL80211_CHAN_WIDTH_80) {
			base_offset += mphy->sband_6g.sband.n_channels * DPD_PER_CH_BW20_SIZE;
			per_chan_size = DPD_PER_CH_GT_BW20_SIZE;
			cal_id = RF_DPD_FLAT_6G_MEM_CAL;
			chan_list = dpd_6g_ch_list_bw80;
			chan_list_size = DPD_CH_NUM(BW80_6G);
		} else if (bw > NL80211_CHAN_WIDTH_20) {
			/* apply (center channel - 2)'s dpd cal data for bw 40/80 channels */
			channel -= 2;
		}
		break;
	default:
		dpd_mask = 0;
		break;
	}

	if (!(eeprom[offs] & dpd_mask))
		return 0;

	for (idx = 0; idx < chan_list_size; idx++)
		if (channel == chan_list[idx].hw_value)
			break;
	if (idx == chan_list_size)
		return -EINVAL;

	if (has_skip_ch && channel > dpd_5g_skip_ch_list[DPD_CH_NUM(BW20_5G_SKIP) - 1].hw_value)
		idx -= DPD_CH_NUM(BW20_5G_SKIP);

	cal += MT_EE_CAL_GROUP_SIZE + base_offset + idx * per_chan_size;

	for (i = 0; i < per_chan_size / MT_EE_CAL_UNIT; i++) {
		ret = mt7999_mcu_set_pre_cal(dev, i, cal, MT_EE_CAL_UNIT, cal_id);
		if (ret)
			return ret;

		cal += MT_EE_CAL_UNIT;
	}

	return ret;
}

int mt7999_mcu_get_chip_config(struct mt7999_dev *dev, u32 *cap)
{
#define UNI_EVENT_CHIP_CONFIG_EFUSE_VERSION	0x21
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
	} __packed req = {
		.tag = cpu_to_le16(UNI_CHIP_CONFIG_NIC_CAPA),
		.len = cpu_to_le16(sizeof(req) - 4),
	};
	struct sk_buff *skb;
	u8 *buf;
	int ret;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76,
					MCU_WM_UNI_CMD_QUERY(CHIP_CONFIG), &req,
					sizeof(req), true, &skb);
	if (ret)
		return ret;

	/* fixed field */
	skb_pull(skb, 4);

	buf = skb->data;
	while (buf - skb->data < skb->len) {
		struct tlv *tlv = (struct tlv *)buf;

		switch (le16_to_cpu(tlv->tag)) {
		case UNI_EVENT_CHIP_CONFIG_EFUSE_VERSION:
			*cap = le32_to_cpu(*(__le32 *)(buf + sizeof(*tlv)));
			break;
		default:
			break;
		}

		buf += le16_to_cpu(tlv->len);
	}

	dev_kfree_skb(skb);

	return 0;
}

int mt7999_mcu_get_chan_mib_info(struct mt7999_phy *phy, bool chan_switch)
{
	enum {
		IDX_TX_TIME,
		IDX_RX_TIME,
		IDX_OBSS_AIRTIME,
		IDX_NON_WIFI_TIME,
		IDX_NUM
	};
	struct {
		struct {
			u8 band;
			u8 __rsv[3];
		} hdr;
		struct {
			__le16 tag;
			__le16 len;
			__le32 offs;
		} data[IDX_NUM];
	} __packed req = {
		.hdr.band = phy->mt76->band_idx,
	};
	static const u32 offs[] = {
		[IDX_TX_TIME] = MIB_TX_TIME,
		[IDX_RX_TIME] = MIB_RX_TIME,
		[IDX_OBSS_AIRTIME] = MIB_OBSS_AIRTIME,
		[IDX_NON_WIFI_TIME] = MIB_NON_WIFI_TIME,
	};
	struct mt76_channel_state *state = phy->mt76->chan_state;
	struct mt76_channel_state *state_ts = &phy->state_ts;
	struct mt7999_dev *dev = phy->dev;
	struct mt7999_mcu_mib *res;
	struct sk_buff *skb;
	int i, ret;

	for (i = 0; i < IDX_NUM; i++) {
		req.data[i].tag = cpu_to_le16(UNI_MIB_DATA);
		req.data[i].len = cpu_to_le16(sizeof(req.data[i]));
		req.data[i].offs = cpu_to_le32(offs[i]);
	}

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_WM_UNI_CMD_QUERY(GET_MIB_INFO),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	skb_pull(skb, sizeof(req.hdr));

	res = (struct mt7999_mcu_mib *)(skb->data);

	if (chan_switch)
		goto out;

#define __res_u64(s) le64_to_cpu(res[s].data)
	state->cc_tx += __res_u64(IDX_TX_TIME) - state_ts->cc_tx;
	state->cc_bss_rx += __res_u64(IDX_RX_TIME) - state_ts->cc_bss_rx;
	state->cc_rx += __res_u64(IDX_RX_TIME) +
			__res_u64(IDX_OBSS_AIRTIME) -
			state_ts->cc_rx;
	state->cc_busy += __res_u64(IDX_TX_TIME) +
			  __res_u64(IDX_RX_TIME) +
			  __res_u64(IDX_OBSS_AIRTIME) +
			  __res_u64(IDX_NON_WIFI_TIME) -
			  state_ts->cc_busy;
out:
	state_ts->cc_tx = __res_u64(IDX_TX_TIME);
	state_ts->cc_bss_rx = __res_u64(IDX_RX_TIME);
	state_ts->cc_rx = __res_u64(IDX_RX_TIME) + __res_u64(IDX_OBSS_AIRTIME);
	state_ts->cc_busy = __res_u64(IDX_TX_TIME) +
			    __res_u64(IDX_RX_TIME) +
			    __res_u64(IDX_OBSS_AIRTIME) +
			    __res_u64(IDX_NON_WIFI_TIME);
#undef __res_u64

	dev_kfree_skb(skb);

	return 0;
}

int mt7999_mcu_get_temperature(struct mt7999_phy *phy)
{
#define GET_TEMPERATURE 0
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		u8 rsv1;
		u8 action;
		u8 band_idx;
		u8 rsv2;
	} req = {
		.tag = cpu_to_le16(UNI_THERMAL_TEMP_QUERY),
		.len = cpu_to_le16(sizeof(req) - 4),
		.action = GET_TEMPERATURE,
		.band_idx = phy->mt76->band_idx,
	};
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		__le32 rsv;
		__le32 temperature;
	} __packed * res;
	struct sk_buff *skb;
	int ret;
	u32 temp;

	ret = mt76_mcu_send_and_get_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(THERMAL),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	res = (void *)skb->data;
	temp = le32_to_cpu(res->temperature);
	dev_kfree_skb(skb);

	return temp;
}

int mt7999_mcu_set_thermal_throttling(struct mt7999_phy *phy, u8 state)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		struct mt7999_mcu_thermal_ctrl ctrl;
	} __packed req = {
		.tag = cpu_to_le16(UNI_THERMAL_PROTECT_DUTY_CONFIG),
		.len = cpu_to_le16(sizeof(req) - 4),
		.ctrl = {
			.band_idx = phy->mt76->band_idx,
		},
	};
	int level, ret;

	/* set duty cycle and level */
	for (level = 0; level < 4; level++) {
		req.ctrl.duty.duty_level = level;
		req.ctrl.duty.duty_cycle = state;

		ret = mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(THERMAL),
					&req, sizeof(req), false);
		if (ret)
			return ret;

		phy->throttle_level[level] = state;
		state /= 2;
	}

	return 0;
}

int mt7999_mcu_set_thermal_protect(struct mt7999_phy *phy, bool enable)
{
#define SUSTAIN_PERIOD		10
	const struct mt7999_thermal_protect_cfg {
		u8 protect_type;
		u8 trigger_type;
		u32 trigger_temp;
		u32 restore_temp;
		u16 sustain_time;
	} cfgs[] = {
		{ .protect_type = 1,
		  .trigger_type = 1,
		  .trigger_temp = phy->throttle_temp[TTI_MAX],
		  .restore_temp = phy->throttle_temp[TTI_CRIT],
		  .sustain_time = SUSTAIN_PERIOD },
		{ .protect_type = 2,
		  .trigger_type = 1,
		  .trigger_temp = phy->throttle_temp[TTI_RADIO_OFF],
		  .restore_temp = 0,
		  .sustain_time = 0 },
	};
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(cfgs); i++) {
		const struct mt7999_thermal_protect_cfg *cfg = &cfgs[i];
		struct {
			u8 _rsv[4];

			__le16 tag;
			__le16 len;

			struct mt7999_mcu_thermal_ctrl ctrl;
			struct mt7999_mcu_thermal_enable enable;
		} __packed req = {
			.tag = cpu_to_le16(UNI_THERMAL_PROTECT_DISABLE),
			.len = cpu_to_le16(sizeof(req) - 4 - sizeof(req.enable)),
			.ctrl = {
				.band_idx = phy->mt76->band_idx,
				.type.protect_type = cfg->protect_type,
				.type.trigger_type = cfg->trigger_type,
			},
		};

		ret = mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(THERMAL),
					&req, sizeof(req) - sizeof(req.enable), false);
		if (ret)
			return ret;

		if (!enable)
			continue;

		req.tag = cpu_to_le16(UNI_THERMAL_PROTECT_ENABLE);
		req.len = cpu_to_le16(sizeof(req) - 4);
		req.enable.trigger_temp = cpu_to_le32(cfg->trigger_temp);
		req.enable.restore_temp = cpu_to_le32(cfg->restore_temp);
		req.enable.sustain_time = cpu_to_le16(cfg->sustain_time);

		ret = mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(THERMAL),
					&req, sizeof(req), false);
		if (ret)
			return ret;
	}

	return 0;
}

int mt7999_mcu_set_thermal_info_query(struct mt7999_phy *phy)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		u8 action;
		u8 band_idx;
		u8 __rsv[2];
	} __packed req = {
		.tag = cpu_to_le16(UNI_THERMAL_PROTECT_INFO),
		.len = cpu_to_le16(sizeof(req) - 4),
		.action = UNI_THERMAL_PROTECT_INFO,
		.band_idx = phy->mt76->band_idx,
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(THERMAL),
			&req, sizeof(req), false);
}

int mt7999_mcu_set_ser(struct mt7999_dev *dev, u8 action, u8 val, u8 band)
{
	struct {
		u8 rsv[4];

		__le16 tag;
		__le16 len;

		union {
			struct {
				__le32 mask;
			} __packed set;

			struct {
				u8 method;
				u8 band;
				u8 rsv2[2];
			} __packed trigger;
		};
	} __packed req = {
		.tag = cpu_to_le16(action),
		.len = cpu_to_le16(sizeof(req) - 4),
	};

	switch (action) {
	case UNI_SER_QUERY:
		break;
	case UNI_SER_SET:
		req.set.mask = cpu_to_le32(val);
		break;
	case UNI_SER_TRIGGER:
		req.trigger.method = val;
		req.trigger.band = band;
		break;
	default:
		return -EINVAL;
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(SER),
				 &req, sizeof(req), false);
}

int mt7999_mcu_set_txbf(struct mt7999_dev *dev, u8 action)
{
#define MT7999_BF_MAX_SIZE	sizeof(union bf_tag_tlv)
	struct uni_header hdr;
	struct sk_buff *skb;
	struct tlv *tlv;
	int len = sizeof(hdr) + MT7999_BF_MAX_SIZE;

	memset(&hdr, 0, sizeof(hdr));

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));

	switch (action) {
	case UNI_BF_SOUNDING_ON: {
		struct bf_sounding_on *req_snd_on;

		tlv = mt7999_mcu_add_uni_tlv(skb, action, sizeof(*req_snd_on));
		req_snd_on = (struct bf_sounding_on *)tlv;
		req_snd_on->snd_mode = BF_PROCESSING;
		break;
	}
	case UNI_BF_HW_EN_UPDATE: {
		struct bf_hw_en_status_update *req_hw_en;

		tlv = mt7999_mcu_add_uni_tlv(skb, action, sizeof(*req_hw_en));
		req_hw_en = (struct bf_hw_en_status_update *)tlv;
		req_hw_en->ebf = true;
		req_hw_en->ibf = dev->ibf;
		break;
	}
	case UNI_BF_MOD_EN_CTRL: {
		struct bf_mod_en_ctrl *req_mod_en;

		tlv = mt7999_mcu_add_uni_tlv(skb, action, sizeof(*req_mod_en));
		req_mod_en = (struct bf_mod_en_ctrl *)tlv;
		req_mod_en->bf_num = mt7999_band_valid(dev, MT_BAND2) ? 3 : 2;
		req_mod_en->bf_bitmap = mt7999_band_valid(dev, MT_BAND2) ?
					GENMASK(2, 0) : GENMASK(1, 0);
		break;
	}
	default:
		return -EINVAL;
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, MCU_WM_UNI_CMD(BF), true);
}

static int
mt7999_mcu_enable_obss_spr(struct mt7999_phy *phy, u16 action, u8 val)
{
	struct mt7999_dev *dev = phy->dev;
	struct {
		u8 band_idx;
		u8 __rsv[3];

		__le16 tag;
		__le16 len;

		__le32 val;
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(action),
		.len = cpu_to_le16(sizeof(req) - 4),
		.val = cpu_to_le32(val),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(SR),
				 &req, sizeof(req), true);
}

static int
mt7999_mcu_set_obss_spr_pd(struct mt7999_phy *phy,
			   struct ieee80211_he_obss_pd *he_obss_pd)
{
#define SR_PD_MAX				-62
#define SR_PD_MIN				-82
#define SR_TXPOWER_RESTRICT_FOLLOW_MTK		2
#define SR_TXPOWER_REF				21
	struct mt7999_dev *dev = phy->dev;
	struct {
		u8 band_idx;
		u8 __rsv[3];

		__le16 tag;
		__le16 len;

		u8 pd_th_non_srg;		/* dBm */
		u8 pd_th_srg;			/* dBm */
		u8 period_offs;
		u8 rcpi_src;
		__le16 obss_pd_min;		/* dBm */
		__le16 obss_pd_min_srg;		/* dBm */
		__le32 resp_txpwr_mode;
		__le32 txpwr_restrict_mode;
		u8 txpwr_ref;
		u8 __rsv2[3];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_SR_SET_PARAM),
		.len = cpu_to_le16(sizeof(req) - 4),
		.obss_pd_min = cpu_to_le16(SR_PD_MIN),
		.obss_pd_min_srg = cpu_to_le16(SR_PD_MIN),
		.txpwr_restrict_mode = cpu_to_le32(SR_TXPOWER_RESTRICT_FOLLOW_MTK),
		.txpwr_ref = SR_TXPOWER_REF,
	};
	int ret;

	/* disable firmware dynamic PD adjustment */
	ret = mt7999_mcu_enable_obss_spr(phy, UNI_SR_ENABLE_DPD, false);
	if (ret)
		return ret;

	if (he_obss_pd->sr_ctrl &
	    IEEE80211_HE_SPR_NON_SRG_OBSS_PD_SR_DISALLOWED)
		req.pd_th_non_srg = SR_PD_MIN;
	else if (he_obss_pd->sr_ctrl & IEEE80211_HE_SPR_NON_SRG_OFFSET_PRESENT)
		req.pd_th_non_srg  = SR_PD_MIN + he_obss_pd->non_srg_max_offset;
	else
		req.pd_th_non_srg  = SR_PD_MAX;

	if (he_obss_pd->sr_ctrl & IEEE80211_HE_SPR_SRG_INFORMATION_PRESENT)
		req.pd_th_srg = SR_PD_MIN + he_obss_pd->max_offset;

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(SR),
				 &req, sizeof(req), false);
}

static int
mt7999_mcu_set_obss_spr_siga(struct mt7999_phy *phy,
			     struct mt7999_vif_link *link,
			     struct ieee80211_he_obss_pd *he_obss_pd)
{
#define SR_MODE_SINGLE_AP	0
#define SR_BSSID_NUM		20
#define SR_BSSID_OMAC_OFFSET	12
	struct mt7999_dev *dev = phy->dev;
	u8 omac = link->mt76.omac_idx;
	struct {
		u8 band_idx;
		u8 __rsv[3];

		__le16 tag;
		__le16 len;

		u8 omac;
		u8 __rsv2[3];
		u8 flag[SR_BSSID_NUM];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_SR_SET_SIGA),
		.len = cpu_to_le16(sizeof(req) - 4),
	};
	int ret;

	if (!(he_obss_pd->sr_ctrl & IEEE80211_HE_SPR_HESIGA_SR_VAL15_ALLOWED))
		return 0;

	/* switch to normal AP mode */
	ret = mt7999_mcu_enable_obss_spr(phy, UNI_SR_ENABLE_MODE,
					 SR_MODE_SINGLE_AP);
	if (ret)
		return ret;

	if (omac >= SR_BSSID_OMAC_OFFSET)
		req.omac = omac - SR_BSSID_OMAC_OFFSET;
	else
		req.omac = omac;

	req.flag[req.omac] = 0xf;

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(SR),
				 &req, sizeof(req), false);
}

static int
mt7999_mcu_set_obss_spr_bitmap(struct mt7999_phy *phy,
			       struct ieee80211_he_obss_pd *he_obss_pd)
{
	struct mt7999_dev *dev = phy->dev;
	struct {
		u8 band_idx;
		u8 __rsv[3];

		__le16 tag;
		__le16 len;

		struct {
			__le32 color_l;
			__le32 color_h;
			__le32 bssid_l;
			__le32 bssid_h;
		} spr_bitmap[__MT_MAX_BAND];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_SR_SET_SRG_BITMAP),
		.len = cpu_to_le16(sizeof(req) - 4),
	};
	u32 bitmap;

	memcpy(&bitmap, he_obss_pd->bss_color_bitmap, sizeof(bitmap));
	req.spr_bitmap[req.band_idx].color_l = cpu_to_le32(bitmap);

	memcpy(&bitmap, he_obss_pd->bss_color_bitmap + 4, sizeof(bitmap));
	req.spr_bitmap[req.band_idx].color_h = cpu_to_le32(bitmap);

	memcpy(&bitmap, he_obss_pd->partial_bssid_bitmap, sizeof(bitmap));
	req.spr_bitmap[req.band_idx].bssid_l = cpu_to_le32(bitmap);

	memcpy(&bitmap, he_obss_pd->partial_bssid_bitmap + 4, sizeof(bitmap));
	req.spr_bitmap[req.band_idx].bssid_h = cpu_to_le32(bitmap);

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(SR), &req,
				 sizeof(req), true);
}

int mt7999_mcu_add_obss_spr(struct mt7999_phy *phy,
			    struct mt7999_vif_link *link,
			    struct ieee80211_he_obss_pd *he_obss_pd)
{
	int ret;

	/* enable firmware scene detection algorithms */
	ret = mt7999_mcu_enable_obss_spr(phy, UNI_SR_ENABLE_SD, sr_scene_detect);
	if (ret)
		return ret;

	/* firmware dynamically adjusts PD threshold so skip manual control */
	if (sr_scene_detect && !he_obss_pd->enable)
		return 0;

	/* enable spatial reuse */
	ret = mt7999_mcu_enable_obss_spr(phy, UNI_SR_ENABLE, he_obss_pd->enable);
	if (ret)
		return ret;

	if (sr_scene_detect || !he_obss_pd->enable)
		return 0;

	ret = mt7999_mcu_enable_obss_spr(phy, UNI_SR_ENABLE_TX, true);
	if (ret)
		return ret;

	/* set SRG/non-SRG OBSS PD threshold */
	ret = mt7999_mcu_set_obss_spr_pd(phy, he_obss_pd);
	if (ret)
		return ret;

	/* Set SR prohibit */
	ret = mt7999_mcu_set_obss_spr_siga(phy, link, he_obss_pd);
	if (ret)
		return ret;

	/* set SRG BSS color/BSSID bitmap */
	return mt7999_mcu_set_obss_spr_bitmap(phy, he_obss_pd);
}

int mt7999_mcu_update_bss_color(struct mt7999_dev *dev,
				struct mt76_vif_link *mlink,
				struct cfg80211_he_bss_color *he_bss_color)
{
	int len = sizeof(struct bss_req_hdr) + sizeof(struct bss_color_tlv);
	struct bss_color_tlv *bss_color;
	struct sk_buff *skb;
	struct tlv *tlv;

	skb = __mt7999_mcu_alloc_bss_req(&dev->mt76, mlink, len);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	tlv = mt76_connac_mcu_add_tlv(skb, UNI_BSS_INFO_BSS_COLOR,
				      sizeof(*bss_color));
	bss_color = (struct bss_color_tlv *)tlv;
	bss_color->enable = he_bss_color->enabled;
	bss_color->color = he_bss_color->color;

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WMWA_UNI_CMD(BSS_INFO_UPDATE), true);
}

int mt7999_mcu_twt_agrt_update(struct mt7999_dev *dev,
			       struct mt7999_vif_link *link,
			       struct mt7999_twt_flow *flow,
			       int cmd)
{
#define UNI_TWT_ARGT_UPDATE	0
#define TWT_AGRT_TRIGGER	BIT(0)
#define TWT_AGRT_ANNOUNCE	BIT(1)
#define TWT_AGRT_PROTECT	BIT(2)
#define TWT_AGRT_NEXT_TWT_32	BIT(4)
#define TWT_AGRT_NEXT_TWT_48	BIT(5)
#define TWT_AGRT_NEXT_TWT_64	BIT(6)
	struct {
		/* fixed field */
		u8 bss;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;
		u8 tbl_idx;
		u8 cmd;
		u8 own_mac_idx;
		u8 flowid; /* 0xff for group id */
		__le16 peer_id_grp_id; /* BIT15=0/1, this will be peer_id/grp_id
					* BIT14=1, this will be btwt_id
					* BIT13=1, this will be rtwt_id
					*/
		u8 duration; /* 256 us */
		u8 bss_idx;
		__le64 start_tsf;
		__le16 mantissa;
		u8 exponent;
		u8 is_ap;
		u8 agrt_params;
		u8 persist;
		__le16 num_reject;
		u8 member_cnt;
		u8 tbl_idx_h;
		u8 tid_dl_bmp;
		u8 tid_ul_bmp;
		__le16 sta_list[MT7999_MAX_BTWT_MEMBER_CNT];
	} __packed req = {
		.tag = cpu_to_le16(UNI_TWT_ARGT_UPDATE),
		.len = cpu_to_le16(sizeof(req) - 4),
		.tbl_idx = flow->table_id,
		.cmd = cmd,
		.own_mac_idx = link->mt76.omac_idx,
		.flowid = flow->id,
		.peer_id_grp_id= cpu_to_le16(flow->peer_id_grp_id),
		.duration = flow->duration,
		.bss = link->mt76.idx,
		.bss_idx = link->mt76.idx,
		.start_tsf = cpu_to_le64(flow->tsf),
		.mantissa = flow->mantissa,
		.exponent = flow->exp,
		.is_ap = true,
		.persist = flow->persist,
		.num_reject = cpu_to_le16(MT7999_NTBBT_BEFORE_REJECT),
		.member_cnt = flow->member_cnt,
		.tbl_idx_h = 0,
		.tid_dl_bmp = flow->tid_dl_bmp,
		.tid_ul_bmp = flow->tid_ul_bmp,
	};

	if (flow->protection)
		req.agrt_params |= TWT_AGRT_PROTECT;
	if (!flow->flowtype)
		req.agrt_params |= TWT_AGRT_ANNOUNCE;
	if (flow->trigger)
		req.agrt_params |= TWT_AGRT_TRIGGER;

	if (cmd == MCU_TWT_AGRT_SUSPEND_RESUME) {
		switch (flow->next_twt_size) {
		case IEEE80211_TWT_INFO_NEXT_TWT_SIZE_32:
			req.agrt_params |= TWT_AGRT_NEXT_TWT_32;
			break;
		case IEEE80211_TWT_INFO_NEXT_TWT_SIZE_48:
			req.agrt_params |= TWT_AGRT_NEXT_TWT_48;
			break;
		case IEEE80211_TWT_INFO_NEXT_TWT_SIZE_64:
			req.agrt_params |= TWT_AGRT_NEXT_TWT_64;
			break;
		}
	}

	if (req.member_cnt) {
		u8 i;
		for (i = 0; i < MT7999_MAX_BTWT_MEMBER_CNT; i++)
			req.sta_list[i] = cpu_to_le16(flow->sta_list[i]);
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(TWT),
				 &req, sizeof(req), true);
}

int mt7999_mcu_set_bssid_mapping_addr(struct mt76_dev *dev, u8 band_idx)
{
	enum {
		BSSID_MAPPING_ADDR1,
		BSSID_MAPPING_ADDR2,
		BSSID_MAPPING_ADDR3,
	};
	struct {
		u8 band_idx;
		u8 _rsv1[3];

		__le16 tag;
		__le16 len;
		u8 addr;
		u8 _rsv2[3];
	} __packed req = {
		.band_idx = band_idx,
		.tag = cpu_to_le16(UNI_BAND_CONFIG_BSSID_MAPPING_ADDR),
		.len = cpu_to_le16(sizeof(req) - 4),
		.addr = BSSID_MAPPING_ADDR1,
	};

	return mt76_mcu_send_msg(dev, MCU_WM_UNI_CMD(BAND_CONFIG),
				 &req, sizeof(req), true);
}

int mt7999_mcu_set_rts_thresh(struct mt7999_phy *phy, u32 val)
{
	struct {
		u8 band_idx;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;
		__le32 len_thresh;
		__le32 pkt_thresh;
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_BAND_CONFIG_RTS_THRESHOLD),
		.len = cpu_to_le16(sizeof(req) - 4),
		.len_thresh = cpu_to_le32(val),
		.pkt_thresh = cpu_to_le32(0x2),
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(BAND_CONFIG),
				 &req, sizeof(req), true);
}

int mt7999_mcu_set_band_confg(struct mt7999_phy *phy, u16 option, bool enable)
{
	struct {
		u8 band_idx;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;
		bool enable;
		u8 _rsv2[3];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(option),
		.len = cpu_to_le16(sizeof(req) - 4),
		.enable = enable,
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(BAND_CONFIG),
				 &req, sizeof(req), true);
}

int mt7999_mcu_set_radio_ultra_save_en(struct mt7999_phy *phy, bool enable)
{
	struct {
		u8 band_idx;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;
		u8 enable;
		u8 _rsv2[3];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_BAND_CONFIG_ULTRA_SAVE),
		.len = cpu_to_le16(sizeof(req) - 4),
		.enable = enable,
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(BAND_CONFIG),
				 &req, sizeof(req), true);
}

int mt7999_mcu_set_radio_en(struct mt7999_phy *phy, bool enable)
{
	struct {
		u8 band_idx;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;
		u8 enable;
		u8 _rsv2[3];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_BAND_CONFIG_RADIO_ENABLE),
		.len = cpu_to_le16(sizeof(req) - 4),
		.enable = enable,
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(BAND_CONFIG),
				 &req, sizeof(req), true);
}

int mt7999_mcu_rdd_resume_tx(struct mt7999_phy *phy)
{
	struct {
		u8 band_idx;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;
		u8 mac_enable;
		u8 _rsv2[3];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_BAND_CONFIG_MAC_ENABLE_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),
		.mac_enable = 2,
	};
	int ret;

	if (!phy->rdd_tx_paused)
		return 0;

	ret = mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(BAND_CONFIG),
				&req, sizeof(req), true);
	if (!ret)
		phy->rdd_tx_paused = false;

	return ret;
}

int mt7999_mcu_set_ack_timeout(struct mt7999_phy *phy, u8 type, u32 timeout)
{
	struct {
		u8 band_idx;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;
		u8 type;
		u8 _rsv2[3];
		__le32 timeout;
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_BAND_CONFIG_ACK_TIMEOUT),
		.len = cpu_to_le16(sizeof(req) - 4),
		.type = type,
		.timeout = cpu_to_le32(timeout),
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(BAND_CONFIG),
				 &req, sizeof(req), true);
}

int mt7999_mcu_rdd_cmd(struct mt7999_dev *dev, int cmd, u8 rdd_idx, u8 val)
{
	struct mt7999_rdd_ctrl req = {
		.tag = cpu_to_le16(UNI_RDD_CTRL_PARM),
		.len = cpu_to_le16(sizeof(req) - 4),
		.ctrl = cmd,
		.rdd_idx = rdd_idx,
		.val = val,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(RDD_CTRL),
				 &req, sizeof(req), false);
}

int mt7999_mcu_rdd_background_disable_timer(struct mt7999_dev *dev, bool disable_timer)
{
	struct mt7999_rdd_ctrl req = {
		.tag = cpu_to_le16(UNI_RDD_CTRL_PARM),
		.len = cpu_to_le16(sizeof(req) - 4),
		.ctrl = RDD_DISABLE_ZW_TIMER,
		.rdd_idx = MT_RDD_IDX_BACKGROUND,
		.disable_timer = disable_timer,
	};

	if (dev->var.fem == MT7999_FEM_INT)
		return 0;

	switch (dev->mt76.region) {
	case NL80211_DFS_ETSI:
		req.val = 0;
		break;
	case NL80211_DFS_JP:
		req.val = 2;
		break;
	case NL80211_DFS_FCC:
	default:
		req.val = 1;
		break;
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(RDD_CTRL),
				 &req, sizeof(req), false);
}

int mt7999_mcu_wtbl_update_hdr_trans(struct mt7999_dev *dev,
				     struct ieee80211_vif *vif,
				     struct mt7999_vif_link *link,
				     struct mt7999_sta_link *msta_link)
{
	struct sk_buff *skb;

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, &link->mt76,
					      &msta_link->wcid,
					      MT7999_STA_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	/* starec hdr trans */
	mt7999_mcu_sta_hdr_trans_tlv(dev, skb, vif, &msta_link->wcid);
	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_WM_UNI_CMD(STA_REC_UPDATE), true);
}

int mt7999_mcu_set_fixed_rate_table(struct mt7999_phy *phy, u8 table_idx,
				    u16 rate_idx, bool beacon)
{
#define UNI_FIXED_RATE_TABLE_SET	0
#define SPE_IDX_SELECT_TXD		0
#define SPE_IDX_SELECT_BMC_WTBL		1
#define SPE_IDX_BAND(_band)		(24 + (_band))
	struct mt7999_dev *dev = phy->dev;
	struct fixed_rate_table_ctrl req = {
		.tag = cpu_to_le16(UNI_FIXED_RATE_TABLE_SET),
		.len = cpu_to_le16(sizeof(req) - 4),
		.table_idx = table_idx,
		.rate_idx = cpu_to_le16(rate_idx),
		.gi = 1,
		.he_ltf = 1,
	};
	u8 band_idx = phy->mt76->band_idx;

	if (beacon) {
		req.spe_idx_sel = SPE_IDX_SELECT_TXD;
		req.spe_idx = dev->mt76.lpi_mode && dev->mt76.mgmt_pwr_enhance ?
			0 : SPE_IDX_BAND(band_idx);
		phy->beacon_rate = rate_idx;
	} else {
		req.spe_idx_sel = SPE_IDX_SELECT_BMC_WTBL;
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(FIXED_RATE_TABLE),
				 &req, sizeof(req), false);
}

int mt7999_mcu_rf_regval(struct mt7999_dev *dev, u32 regidx,
			 u32 *val, bool set, bool efem)
{
	struct {
		u8 __rsv1[4];

		__le16 tag;
		__le16 len;
		__le16 idx;
		u8 __rsv2[2];
		__le32 ofs;
		__le32 data;
	} __packed *res, req = {
		.len = cpu_to_le16(sizeof(req) - 4),

		.idx = cpu_to_le16(u32_get_bits(regidx, GENMASK(31, 24))),
		.ofs = cpu_to_le32(u32_get_bits(regidx, GENMASK(23, 0))),
		.data = set ? cpu_to_le32(*val) : 0,
	};
	struct sk_buff *skb;
	int ret;

	req.tag = cpu_to_le16(efem ? UNI_ACCESS_EFEM_REG_BASIC : UNI_ACCESS_RF_REG_BASIC);

	if (set)
		return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(REG_ACCESS),
					 &req, sizeof(req), true);

	ret = mt76_mcu_send_and_get_msg(&dev->mt76,
					MCU_WM_UNI_CMD_QUERY(REG_ACCESS),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	res = (void *)skb->data;
	*val = le32_to_cpu(res->data);
	dev_kfree_skb(skb);

	return 0;
}

int mt7999_mcu_set_rro(struct mt7999_dev *dev, u16 tag, u16 val)
{
	struct {
		u8 __rsv1[4];
		__le16 tag;
		__le16 len;
		union {
			struct {
				u8 type;
				u8 __rsv2[3];
			} __packed platform_type;
			struct {
				u8 type;
				u8 dest;
				u8 __rsv2[2];
			} __packed bypass_mode;
			struct {
				u8 path;
				u8 __rsv2[3];
			} __packed txfree_path;
			struct {
				__le16 session_id;
				u8 __rsv2[2];
			} __packed reset_session;
		};
	} __packed req = {
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(sizeof(req) - 4),
	};

	switch (tag) {
	case UNI_RRO_SET_PLATFORM_TYPE:
		req.platform_type.type = val;
		break;
	case UNI_RRO_SET_BYPASS_MODE:
		req.bypass_mode.type = val;
		break;
	case UNI_RRO_SET_TXFREE_PATH:
		req.txfree_path.path = val;
		break;
	case UNI_RRO_DEL_BA_SESSION:
		req.reset_session.session_id = cpu_to_le16(val);
		break;
	default:
		return -EINVAL;
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(RRO), &req,
				 sizeof(req), true);
}

int mt7999_mcu_set_rro_flush_timeout(struct mt7999_dev *dev)
{
	u16 timeout = mt76_rr(dev, MT_HW_REV) == MT_HW_REV1 ? 512 : 128;
	struct {
		u8 __rsv1[4];
		__le16 tag;
		__le16 len;

		__le16 flush_one;
		__le16 flush_all;
		u8 __rsv2[4];
	} __packed req = {
		.tag = cpu_to_le16(UNI_RRO_SET_FLUSH_TIMEOUT),
		.len = cpu_to_le16(sizeof(req) - 4),
		.flush_one = cpu_to_le16(timeout),
		.flush_all = cpu_to_le16(2 * timeout),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(RRO), &req,
				 sizeof(req), true);
}

int mt7999_mcu_get_per_sta_info(struct mt76_dev *dev, u16 tag,
				u16 sta_num, u16 *sta_list)
{
	struct mt7999_mcu_per_sta_info_event *res;
	struct mt7999_sta_link *msta_link;
	struct mt76_wcid *wcid;
	struct sk_buff *skb;
	int i, j, ret;
	u16 wlan_idx;
	struct {
		u8 __rsv1;
		u8 unsolicit;
		u8 __rsv2[2];

		__le16 tag;
		__le16 len;
		__le16 sta_num;
		u8 __rsv3[2];
		__le16 sta_list[PER_STA_INFO_MAX_NUM];
	} __packed req = {
		.unsolicit = 0,
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(sizeof(req) - 4),
		.sta_num = cpu_to_le16(sta_num)
	};

	if (sta_num > PER_STA_INFO_MAX_NUM)
		return -EINVAL;

	for (i = 0; i < sta_num; ++i)
		req.sta_list[i] = cpu_to_le16(sta_list[i]);

	ret = mt76_mcu_send_and_get_msg(dev, MCU_WM_UNI_CMD(PER_STA_INFO),
	                                &req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	res = (struct mt7999_mcu_per_sta_info_event *)skb->data;
	if (le16_to_cpu(res->tag) != tag) {
		ret = -EINVAL;
		goto out;
	}

	rcu_read_lock();
	switch (tag) {
	case UNI_PER_STA_RSSI:
		for (i = 0; i < sta_num; ++i) {
			struct per_sta_rssi *rssi = &res->rssi[i];
			struct mt76_phy *mphy;
			struct mt7999_phy *phy;
			u8 *rcpi = rssi->rcpi;
			u16 rx_mask;

			wlan_idx = le16_to_cpu(rssi->wlan_idx);
			wcid = __mt76_wcid_ptr(dev, wlan_idx);
			if (!wcid || !wcid->sta)
				continue;

			msta_link = container_of(wcid, struct mt7999_sta_link, wcid);
			mphy = dev->phys[wcid->phy_idx];
			phy = mphy->priv;
			rx_mask = mt7999_rx_chainmask(phy);

			for (j = 0; j < ARRAY_SIZE(rssi->rcpi); ++j)
				msta_link->chain_ack_signal[j] = to_rssi(MT_PRXV4_RCPI0, rcpi[j]);
			msta_link->chain_ack_signal[4] = to_rssi(MT_PRXV4_RCPI0, rssi->rcpi_wf4);

			msta_link->ack_signal = mt76_rx_signal(rx_mask,
							       msta_link->chain_ack_signal);
			ewma_avg_signal_add(&msta_link->avg_ack_signal, -msta_link->ack_signal);
		}
		break;
	case UNI_PER_STA_SNR:
		for (i = 0; i < sta_num; ++i) {
			wlan_idx = le16_to_cpu(res->snr[i].wlan_idx);
			wcid = __mt76_wcid_ptr(dev, wlan_idx);
			if (!wcid || !wcid->sta)
				continue;

			msta_link = container_of(wcid, struct mt7999_sta_link, wcid);
			memcpy(msta_link->chain_ack_snr, res->snr[i].val,
			       sizeof(res->snr[i].val));
			msta_link->chain_ack_snr[4] = res->snr[i].snr_wf4;
		}
		break;
	case UNI_PER_STA_PKT_CNT:
		for (i = 0; i < sta_num; ++i) {
			wlan_idx = le16_to_cpu(res->msdu_cnt[i].wlan_idx);
			wcid = __mt76_wcid_ptr(dev, wlan_idx);
			if (!wcid || !wcid->sta)
				continue;

			/* Retry counter is increased by 1 when an MSDU is
			 * successfully transmitted after one or more retries
			 */
			wcid->stats.tx_packets_retried +=
				le32_to_cpu(res->msdu_cnt[i].tx_retries);

			/* Drop counter is increased by 1 when an MSDU is
			 * dropped due to retry limit or exceeded TXOP
			 */
			wcid->stats.tx_packets_failed +=
				le32_to_cpu(res->msdu_cnt[i].tx_drops);
		}
		break;
	default:
		ret = -EINVAL;
		dev_err(dev->dev, "Unknown UNI_PER_STA_INFO_TAG: %d\n", tag);
	}
	rcu_read_unlock();
out:
	dev_kfree_skb(skb);
	return ret;
}

int mt7999_mcu_get_all_sta_info(struct mt76_dev *dev, u16 tag)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
	} __packed req = {
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(sizeof(req) - 4),
	};

	return mt76_mcu_send_msg(dev, MCU_WM_UNI_CMD(ALL_STA_INFO),
				 &req, sizeof(req), false);
}

int mt7999_mcu_get_bss_acq_pkt_cnt(struct mt7999_dev *dev)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		__le32 bitmap[SDO_CFG_BSS_MAP_WORDLEN];
	} __packed req = {
		.tag = cpu_to_le16(UNI_SDO_GET_BSS_ACQ_PKT_NUM),
		.len = cpu_to_le16(sizeof(req) - 4),
	};
	int i;

	for (i = 0; i < SDO_CFG_BSS_MAP_WORDLEN; i++)
		req.bitmap[i] = cpu_to_le32(~0);

	return mt76_mcu_send_msg(&dev->mt76, MCU_WA_UNI_CMD(SDO), &req,
				 sizeof(req), false);
}

int mt7999_mcu_set_sniffer_mode(struct mt7999_phy *phy, bool enabled)
{
#define UNI_SNIFFER_MODE_ENABLE		0
	struct mt7999_dev *dev = phy->dev;
	struct {
		u8 band_idx;
		u8 _rsv[3];
		__le16 tag;
		__le16 len;
		u8 enable;
		u8 _pad[3];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_SNIFFER_MODE_ENABLE),
		.len = cpu_to_le16(sizeof(req) - 4),
		.enable = enabled,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(SNIFFER), &req,
				 sizeof(req), true);
}

static void
mt7999_update_max_txpower_cur(struct mt7999_phy *phy, int tx_power)
{
	struct mt76_phy *mphy = phy->mt76;
	struct ieee80211_channel *chan = mphy->main_chandef.chan;
	int e2p_power_limit = 0;

	if (chan == NULL) {
		mphy->txpower_cur = tx_power;
		return;
	}

	e2p_power_limit = mt7999_eeprom_get_target_power(phy->dev, chan);
	e2p_power_limit += mt7999_eeprom_get_power_delta(phy->dev, chan->band);

	if (phy->sku_limit_en)
		mphy->txpower_cur = min_t(int, e2p_power_limit, tx_power);
	else
		mphy->txpower_cur = e2p_power_limit;
}

static int mt7999_afc_update_power_limit(struct mt7999_dev *dev,
					 struct ieee80211_channel *chan,
					 struct mt76_power_limits *la,
					 s8 *tx_power,
					 struct cfg80211_chan_def *chandef)
{
	s8 *power_list, bw320_offset, target_power;
	int table_idx, i, bw, mcs, ru, wide_bw, path;
	s8 bf_offset_ofdm[] = {6, 10, 12, 14};
	s8 bf_offset[] = {0, 6, 10, 12, 14, 0, 4, 6, 8, 0, 3, 5, 0, 2, 0};

	table_idx = chan->hw_value / 4;
	if (table_idx < 0 || table_idx > MAX_CHANNEL_NUM_6G ||
	    !dev->mt76.afc_power_table[table_idx])
		return -EINVAL;

	power_list = dev->mt76.afc_power_table[table_idx];

	switch (chandef->center_freq1) {
	case 6105:
	case 6425:
	case 6745:
		bw320_offset = 1;
		break;
	case 6265:
	case 6585:
	case 6905:
		bw320_offset = 2;
		break;
	default:
		bw320_offset = 0;
		break;
	}

	if (chandef) {
		switch (chandef->width) {
		case NL80211_CHAN_WIDTH_20:
			target_power = power_list[afc_power_bw20];
			break;
		case NL80211_CHAN_WIDTH_40:
			target_power = power_list[afc_power_bw40];
			break;
		case NL80211_CHAN_WIDTH_80:
			target_power = power_list[afc_power_bw80];
			break;
		case NL80211_CHAN_WIDTH_160:
			target_power = power_list[afc_power_bw160];
			break;
		case NL80211_CHAN_WIDTH_320:
			if (bw320_offset == 1)
				target_power = power_list[afc_power_bw320_1];
			else
				target_power = power_list[afc_power_bw320_2];
			break;
		default:
			break;
		}
		*tx_power = min_t(int, *tx_power, target_power);
	}

	target_power = min_t(s8, (s8)*tx_power, power_list[afc_power_bw20]);
	for (i = 0; i < sizeof(la->cck); i++)
		la->cck[i] = min_t(s8, la->cck[i], power_list[afc_power_bw20]);
	for (i = 0; i < sizeof(la->ofdm); i++)
		la->ofdm[i] = min_t(s8, la->ofdm[i], target_power);

	for (i = 0; i < sizeof(la->path.cck); i++)
		la->path.cck[i] = min_t(s8, la->path.cck[i], power_list[afc_power_bw20]);
	for (i = 0; i < sizeof(la->path.ofdm); i++)
		la->path.ofdm[i] = min_t(s8, la->path.ofdm[i], target_power);
	for (i = 0; i < sizeof(la->path.ofdm_bf); i++) {
		la->path.ofdm_bf[i] =
			min_t(s8, la->path.ofdm_bf[i],
			      target_power - bf_offset_ofdm[i]);
	}

	for (bw = afc_power_bw20; bw < afc_power_table_num; bw++) {
		if ((bw == afc_power_bw320_1 && bw320_offset == 2) ||
		    (bw == afc_power_bw320_2 && bw320_offset == 1))
			continue;

		if (power_list[bw] == AFC_INVALID_POWER)
			continue;

		/* Negative index means doesn't need to update powers of the type. */
		if (mt7999_get_bw_power_table_idx(bw, &mcs, &ru, &wide_bw, &path))
			return -EINVAL;

		if (mcs >= 0) {
			for (i = 0; i < sizeof(la->mcs[0]); i++)
				la->mcs[mcs][i] =
					min_t(s8, la->mcs[mcs][i], power_list[bw]);
		}

		if (ru >= 0) {
			for (i = 0; i < sizeof(la->ru[0]); i++)
				la->ru[ru][i] = min_t(s8, la->ru[ru][i], power_list[bw]);
		}

		if (wide_bw >= 0) {
			for (i = 0; i < sizeof(la->eht[0]); i++)
				la->eht[wide_bw][i] =
					min_t(s8, la->eht[wide_bw][i], power_list[bw]);
			for (i = 0; i < sizeof(la->uhr[0]); i++)
				la->uhr[wide_bw][i] =
					min_t(s8, la->uhr[wide_bw][i], power_list[bw]);
		}

		if (path >= 0) {
			for (i = 0; i < sizeof(la->path.ru[0]); i++) {
				la->path.ru[path][i] =
					min_t(s8, la->path.ru[path][i], power_list[bw]);
				la->path.ru_bf[path][i] =
					min_t(s8, la->path.ru_bf[path][i],
					      power_list[bw] - bf_offset[i]);
			}
		}
	}

	return 0;
}

static s8 mt7999_get_power_bound(struct mt7999_phy *phy, s8 txpower)
{
       struct mt76_phy *mphy = phy->mt76;
       int n_chains = hweight16(mphy->chainmask);

       txpower = mt76_get_sar_power(mphy, mphy->chandef.chan, txpower * 2);
       txpower -= mt76_tx_power_path_delta(n_chains);

       return txpower;
}

int mt7999_mcu_set_txpower_sku(struct mt7999_phy *phy,
			       int txpower_setting)
{
#define TX_POWER_LIMIT_TABLE_RATE	0
#define TX_POWER_LIMIT_TABLE_PATH	1
	struct mt7999_dev *dev = phy->dev;
	struct mt76_phy *mphy = phy->mt76;
	u16 data_len = max(mt7999_get_sku_rate_num(), MT7999_SKU_PATH_NUM);
	struct tx_power_limit_table_ctrl {
		u8 __rsv1[4];

		__le16 tag;
		__le16 len;
		u8 power_ctrl_id;
		u8 power_limit_type;
		u8 band_idx;
	} __packed req = {
		.tag = cpu_to_le16(UNI_TXPOWER_POWER_LIMIT_TABLE_CTRL),
		.len = cpu_to_le16(sizeof(req) + data_len - 4),
		.power_ctrl_id = UNI_TXPOWER_POWER_LIMIT_TABLE_CTRL,
		.power_limit_type = TX_POWER_LIMIT_TABLE_RATE,
		.band_idx = phy->mt76->band_idx,
	};
	struct mt76_power_limits *la;
	struct sk_buff *skb;
	struct ieee80211_channel *chan = mphy->main_chandef.chan;
	int i, ret;
	s8 txpower_limit;

	if (txpower_setting == INT_MIN || txpower_setting > 127)
		txpower_setting = 127;
	txpower_limit = mt7999_get_power_bound(phy, txpower_setting);

	if (!phy->sku_limit_en) {
		mt7999_update_max_txpower_cur(phy, txpower_limit);
		return 0;
	}

	la = kzalloc(sizeof(*la), GFP_KERNEL);
	if (!la)
		return -ENOMEM;

	mt76_get_rate_power_limits(mphy, chan, la, &txpower_limit);

	if (!la->path.ofdm[0])
		phy->sku_path_en = false;

	if(phy->mt76->cap.has_6ghz && dev->mt76.afc_power_table) {
		ret = mt7999_afc_update_power_limit(dev, chan, la, &txpower_limit,
						    &mphy->main_chandef);
		if (ret)
			goto out;
	}

	mt7999_update_max_txpower_cur(phy, txpower_limit);

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, sizeof(req) + data_len);
	if (!skb) {
		ret = -ENOMEM;
		goto out;
	}

	skb_put_data(skb, &req, sizeof(req));
	skb_put_data(skb, &la->cck, 4);

	/* FW would compensate for PSD countries
	 * driver doesn't need to do it
	 */
	if (phy->mt76->cap.has_6ghz && mphy->dev->lpi_mode && mphy->dev->lpi_psd &&
	    !mt76_is_psd_country(dev->mt76.alpha2)) {
		switch (mphy->main_chandef.width) {
		case NL80211_CHAN_WIDTH_20:
			skb_put_data(skb, &la->eht[3], 8);
			break;
		case NL80211_CHAN_WIDTH_40:
			skb_put_data(skb, &la->eht[4], 8);
			break;
		case NL80211_CHAN_WIDTH_80:
			skb_put_data(skb, &la->eht[5], 8);
			break;
		case NL80211_CHAN_WIDTH_160:
			skb_put_data(skb, &la->eht[6], 8);
			break;
		case NL80211_CHAN_WIDTH_320:
			skb_put_data(skb, &la->eht[7], 8);
			break;
		default:
			skb_put_data(skb, &la->ofdm, sizeof(la->ofdm));
			break;
		}
	} else {
		skb_put_data(skb, &la->ofdm, 8);
	}

	skb_put_data(skb, &la->mcs[0], 8); /* HT20 */
	skb_put_data(skb, &la->mcs[1], 9); /* HT40 */

	/* VHT20, VHT40, VHT80, and VHT160 */
	for (i = 0; i < 4; i++) {
		skb_put_data(skb, &la->mcs[i], 10);
		skb_put_zero(skb, 2);  /* padding */
	}

	/* HE RU26, RU52, RU106, RU242, RU484, RU996, and RU2x996 */
	skb_put_data(skb, &la->ru[0], sizeof(la->ru));
	/* EHT/UHR RU26, RU52, RU78, RU106, RU132, RU242, RU484, RU726, RU996, RU1480,
	 * RU1722, RU1922, RU2476, RU2988, RU3472, and RU3984
	 */
	skb_put_data(skb, &la->eht[0], sizeof(la->eht));
	skb_put_data(skb, &la->uhr[0], sizeof(la->uhr));

	ret = mt76_mcu_skb_send_msg(&dev->mt76, skb,
				    MCU_WM_UNI_CMD(TXPOWER), true);
	if (ret)
		goto out;

	/* only set per-path power table when it's configured */
	if (!phy->sku_path_en)
		goto out;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, sizeof(req) + data_len);
	if (!skb) {
		ret = -ENOMEM;
		goto out;
	}
	req.power_limit_type = TX_POWER_LIMIT_TABLE_PATH;

	skb_put_data(skb, &req, sizeof(req));
	skb_put_data(skb, &la->path.cck, 5);

	/* FW would NOT compensate in the case of BF backoff table
	 * driver needs to compensate for LPI PSD
	 */
	if (phy->mt76->cap.has_6ghz && mphy->dev->lpi_mode && mphy->dev->lpi_psd) {
		switch (mphy->main_chandef.width) {
		case NL80211_CHAN_WIDTH_20:
			skb_put_data(skb, &la->path.ru[5], 5);
			skb_put_data(skb, &la->path.ru_bf[5], 4);
			break;
		case NL80211_CHAN_WIDTH_40:
			skb_put_data(skb, &la->path.ru[6], 5);
			skb_put_data(skb, &la->path.ru_bf[6], 4);
			break;
		case NL80211_CHAN_WIDTH_80:
			skb_put_data(skb, &la->path.ru[8], 5);
			skb_put_data(skb, &la->path.ru_bf[8], 4);
			break;
		case NL80211_CHAN_WIDTH_160:
			skb_put_data(skb, &la->path.ru[11], 5);
			skb_put_data(skb, &la->path.ru_bf[11], 4);
			break;
		case NL80211_CHAN_WIDTH_320:
			skb_put_data(skb, &la->path.ru[15], 5);
			skb_put_data(skb, &la->path.ru_bf[15], 4);
			break;
		default:
			skb_put_data(skb, &la->path.ofdm, 5);
			skb_put_data(skb, &la->path.ofdm_bf, 4);
			break;
		}
	} else {
		skb_put_data(skb, &la->path.ofdm, 5);
		skb_put_data(skb, &la->path.ofdm_bf, 4);
	}

	for (i = 0; i < 32; i++) {
		bool bf = i % 2;
		u8 idx = i / 2;
		s8 *buf = bf ? la->path.ru_bf[idx] : la->path.ru[idx];

		skb_put_data(skb, buf, 15);
	}

	/* padding */
	skb_put_zero(skb, data_len - MT7999_SKU_PATH_NUM);

	ret = mt76_mcu_skb_send_msg(&dev->mt76, skb,
				    MCU_WM_UNI_CMD(TXPOWER), true);
out:
	kfree(la);
	return ret;
}

int mt7999_mcu_set_lpi_psd(struct mt7999_phy *phy, u8 enable)
{
	struct mt7999_dev *dev = phy->dev;

	struct {
		u8 band_idx;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;
		u8 lpi_enable;
		u8 psd_limit;
		u8 _rsv2[2];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_BAND_CONFIG_LPI_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),
		.lpi_enable = enable,
		.psd_limit = enable && dev->mt76.lpi_mode &&
			     mt76_is_psd_country(dev->mt76.alpha2),
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(BAND_CONFIG),
				 &req, sizeof(req), true);
}

int mt7999_alloc_afc_table(struct mt7999_dev *dev)
{
	struct mt76_dev *mdev = &dev->mt76;
	int i;

	mdev->afc_power_table =
		(s8**)devm_kzalloc(dev->mt76.dev,
				   MAX_CHANNEL_NUM_6G * sizeof(s8*),
				   GFP_KERNEL);

	if (!mdev->afc_power_table)
		return -ENOMEM;

	for (i = 0; i < MAX_CHANNEL_NUM_6G; i++) {
		mdev->afc_power_table[i] =
			(s8*)devm_kzalloc(dev->mt76.dev,
					  afc_power_table_num * sizeof(s8),
					  GFP_KERNEL);
		if (!mdev->afc_power_table[i]) {
			mt7999_free_afc_table(dev);
			return -ENOMEM;
		}
	}
	return 0;
}

void mt7999_free_afc_table(struct mt7999_dev *dev)
{
	struct mt76_dev *mdev = &dev->mt76;
	int i;

	if (mdev->afc_power_table) {
		for (i = 0; i < MAX_CHANNEL_NUM_6G; i++)
			devm_kfree(mdev->dev, mdev->afc_power_table[i]);
		devm_kfree(mdev->dev, mdev->afc_power_table);
	}
	mdev->afc_power_table = NULL;
}

int mt7999_mcu_cp_support(struct mt7999_dev *dev, u8 mode)
{
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;
		u8 cp_mode;
		u8 rsv[3];
	} __packed req = {
		.tag = cpu_to_le16(UNI_SDO_CP_MODE),
		.len = cpu_to_le16(sizeof(req) - 4),
		.cp_mode = mode,
	};

	if (mode < mt76_connac_lmac_mapping(IEEE80211_AC_BE) ||
	    mode > mt76_connac_lmac_mapping(IEEE80211_AC_VO))
		return -EINVAL;

	return mt76_mcu_send_msg(&dev->mt76, MCU_WA_UNI_CMD(SDO),
				 &req, sizeof(req), false);
}

int mt7999_mcu_set_pp_en(struct mt7999_phy *phy, u8 mode, u16 bitmap)
{
	struct mt7999_dev *dev = phy->dev;
	bool pp_auto = (mode == PP_FW_MODE);
	struct mt7999_mcu_pp_en_ctrl req = {
		.tag = cpu_to_le16(UNI_PP_EN_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),

		.mgmt_mode = !pp_auto,
		.band_idx = phy->mt76->band_idx,
		.force_bitmap_ctrl = (mode == PP_USR_MODE) ? 2 : 0,
		.auto_mode = pp_auto,
		.bitmap = cpu_to_le16(bitmap),
		.csa_enable = false,
	};

	if (phy->mt76->chandef.chan->band == NL80211_BAND_2GHZ ||
	    mode > PP_USR_MODE)
		return 0;

	if (bitmap && phy->punct_bitmap == bitmap)
		return 0;

	phy->punct_bitmap = bitmap;
	phy->pp_mode = mode;

#ifdef CONFIG_MTK_DEBUG
	/* Configuring PP would cause FW to disable MRU Probe by default. */
	phy->mru_probe_enable = false;
#endif

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(PP),
				 &req, sizeof(req), false);
}

int mt7999_mcu_set_pp_sta_dscb(struct mt7999_phy *phy,
			       struct cfg80211_chan_def *chandef,
			       u8 omac_idx)
{
	struct mt7999_dev *dev = phy->dev;
	struct {
		u8 _rsv1[4];

		__le16 tag;
		__le16 len;
		u8 band_idx;
		u8 omac_idx;
		u8 eht_op_present;
		u8 dscb_present;
		__le16 dscb;
		u8 ctrl;
		u8 ccfs0;
		u8 ccfs1;
		u8 rsv2[3];
	} __packed req = {
		.tag = cpu_to_le16(UNI_PP_DSCB_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),

		.band_idx = phy->mt76->band_idx,
		.omac_idx = omac_idx,
		.eht_op_present = true,
		.dscb_present = !!chandef->punctured,
		.dscb = cpu_to_le16(chandef->punctured),
		.ccfs0 = ieee80211_frequency_to_channel(chandef->center_freq1),
		.ccfs1 = ieee80211_frequency_to_channel(chandef->center_freq1),
	};

	if (phy->mt76->chandef.chan->band == NL80211_BAND_2GHZ ||
	    phy->punct_bitmap == chandef->punctured)
		return 0;

	switch (chandef->width) {
	case NL80211_CHAN_WIDTH_320:
		req.ctrl = IEEE80211_EHT_OPER_CHAN_WIDTH_320MHZ;
		if (chandef->chan->hw_value < req.ccfs1)
			req.ccfs0 -= 16;
		else
			req.ccfs0 += 16;
		break;
	case NL80211_CHAN_WIDTH_160:
		req.ctrl = IEEE80211_EHT_OPER_CHAN_WIDTH_160MHZ;
		if (chandef->chan->hw_value < req.ccfs1)
			req.ccfs0 -= 8;
		else
			req.ccfs0 += 8;
		break;
	case NL80211_CHAN_WIDTH_80:
		req.ctrl = IEEE80211_EHT_OPER_CHAN_WIDTH_80MHZ;
		req.ccfs0 = 0;
		break;
	default:
		return 0;
		break;
	}

	phy->punct_bitmap = cpu_to_le16(chandef->punctured);

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(PP),
				 &req, sizeof(req), false);
}

int mt7999_mcu_set_pp_alg_ctrl(struct mt7999_phy *phy, u8 action)
{
	struct mt7999_dev *dev = phy->dev;
	struct {
		u8 _rsv1[4];

		__le16 tag;
		__le16 len;

		__le32 pp_timer_intv;
		__le32 thr_x2_value;
		__le32 thr_x2_shift;
		__le32 thr_x3_value;
		__le32 thr_x3_shift;
		__le32 thr_x4_value;
		__le32 thr_x4_shift;
		__le32 thr_x5_value;
		__le32 thr_x5_shift;
		__le32 thr_x6_value;
		__le32 thr_x6_shift;
		__le32 thr_x7_value;
		__le32 thr_x7_shift;
		__le32 thr_x8_value;
		__le32 thr_x8_shift;
		u8 band_idx;
		u8 pp_action;
		u8 reset;
		u8 _rsv2;
	} __packed req = {
		.tag = cpu_to_le16(UNI_PP_ALG_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),
		.band_idx = phy->mt76->band_idx,
		.pp_action = action,
	};

	switch (action)
	{
	case PP_ALG_SET_TIMER:
		req.pp_timer_intv = 5000;
		break;
	case PP_ALG_SET_THR:
		req.thr_x2_value = 1;
		req.thr_x2_shift = 0;
		req.thr_x3_value = 5000000;
		req.thr_x3_shift = 3;
		req.thr_x4_value = 1;
		req.thr_x4_shift = 1;
		req.thr_x5_value = 1;
		req.thr_x5_shift = 0;
		req.thr_x6_value = 1;
		req.thr_x6_shift = 3;
		req.thr_x7_value = 1;
		req.thr_x7_shift = 0;
		req.thr_x8_value = 5000000;
		req.thr_x8_shift = 2;
		break;
	case PP_ALG_GET_STATISTICS:
		break;
	default:
		return 0;
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(PP),
				 &req, sizeof(req), false);
}

int mt7999_mcu_set_tx_power_ctrl(struct mt7999_phy *phy, u8 power_ctrl_id, u8 data)
{
	struct mt7999_dev *dev = phy->dev;
	struct tx_power_ctrl req = {
		.tag = cpu_to_le16(power_ctrl_id),
		.len = cpu_to_le16(sizeof(req) - 4),
		.power_ctrl_id = power_ctrl_id,
		.band_idx = phy->mt76->band_idx,
	};

	switch (power_ctrl_id) {
	case UNI_TXPOWER_SKU_POWER_LIMIT_CTRL:
		req.sku_enable = !!data;
		break;
	case UNI_TXPOWER_PERCENTAGE_CTRL:
		req.percentage_ctrl_enable = !!data;
		break;
	case UNI_TXPOWER_PERCENTAGE_DROP_CTRL:
		req.power_drop_level = data;
		break;
	case UNI_TXPOWER_BACKOFF_POWER_LIMIT_CTRL:
		req.bf_backoff_enable = !!data;
		break;
	case UNI_TXPOWER_ATE_MODE_CTRL:
		req.ate_mode_enable = !!data;
		break;
	default:
		req.sku_enable = !!data;
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(TXPOWER),
				 &req, sizeof(req), false);
}

static int
mt7999_mcu_set_scs_stats(struct mt7999_phy *phy)
{
	struct mt7999_scs_ctrl ctrl = phy->scs_ctrl;
	struct {
		u8 band_idx;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;

		u8 _rsv2[6];
		s8 min_rssi;
		u8 _rsv3;
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_SCS_SEND_DATA),
		.len = cpu_to_le16(sizeof(req) - 4),

		.min_rssi = ctrl.sta_min_rssi,
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(SCS),
				 &req, sizeof(req), false);
}

static void
mt7999_sta_rssi_work(void *data, struct ieee80211_sta *sta)
{
	struct mt7999_sta *msta = (struct mt7999_sta *)sta->drv_priv;
	struct mt7999_phy *poll_phy = (struct mt7999_phy *)data;
	struct mt7999_vif *mvif = msta->vif;
	struct ieee80211_link_sta *link_sta;
	struct ieee80211_vif *vif;
	unsigned int link_id;

	vif = container_of((void *)mvif, struct ieee80211_vif, drv_priv);

	rcu_read_lock();
	for_each_sta_active_link(vif, sta, link_sta, link_id) {
		struct mt7999_sta_link *msta_link;

		msta_link = mt7999_sta_link(msta, link_id);
		if (!msta_link)
			continue;

		if (msta_link->wcid.phy_idx != poll_phy->mt76->band_idx)
			continue;

		if (poll_phy->scs_ctrl.sta_min_rssi > msta_link->ack_signal)
			poll_phy->scs_ctrl.sta_min_rssi = msta_link->ack_signal;

		break;
	}
	rcu_read_unlock();
}

void mt7999_mcu_scs_sta_poll(struct work_struct *work)
{
	struct mt7999_dev *dev = container_of(work, struct mt7999_dev,
				 scs_work.work);
	bool scs_enable_flag = false;
	u8 i;

	for (i = 0; i < __MT_MAX_BAND; i++) {
		struct mt7999_phy *phy;

		switch (i) {
		case MT_BAND0:
			phy = dev->mphy.priv;
			break;
		case MT_BAND1:
			phy = mt7999_phy2(dev);
			break;
		case MT_BAND2:
			phy = mt7999_phy3(dev);
			break;
		default:
			phy = NULL;
			break;
		}

		if (!phy || !test_bit(MT76_STATE_RUNNING, &phy->mt76->state) ||
		    !phy->scs_ctrl.scs_enable)
			continue;

		ieee80211_iterate_stations_atomic(phy->mt76->hw,
						  mt7999_sta_rssi_work, phy);

		scs_enable_flag = true;
		if (mt7999_mcu_set_scs_stats(phy))
			dev_err(dev->mt76.dev, "Failed to send scs mcu cmd\n");
		phy->scs_ctrl.sta_min_rssi = 0;
	}

	if (scs_enable_flag)
		ieee80211_queue_delayed_work(mt76_hw(dev), &dev->scs_work, HZ);
}


int mt7999_mcu_set_scs(struct mt7999_phy *phy, u8 enable)
{
	struct mt7999_dev *dev = phy->dev;
	struct {
		u8 band_idx;
		u8 _rsv[3];

		__le16 tag;
		__le16 len;

		u8 scs_enable;
		u8 _rsv2[3];
	} __packed req = {
		.band_idx = phy->mt76->band_idx,
		.tag = cpu_to_le16(UNI_SCS_ENABLE),
		.len = cpu_to_le16(sizeof(req) - 4),
		.scs_enable = enable,
	};

	phy->scs_ctrl.scs_enable = enable;

	if (enable == SCS_ENABLE)
		ieee80211_queue_delayed_work(mt76_hw(dev), &dev->scs_work, HZ);

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(SCS),
				 &req, sizeof(req), false);
}

int mt7999_mcu_set_vow_drr_ctrl(struct mt7999_phy *phy,
				struct mt7999_vif_link *mconf,
				struct mt7999_sta_link *msta_link,
				enum vow_drr_ctrl_id id)
{
	struct mt7999_vow_sta_ctrl *vow = msta_link ? &msta_link->vow : NULL;
	u32 i, val = 0;
	struct mt7999_mcu_vow_drr_ctrl req = {
		.tag = cpu_to_le16(UNI_VOW_DRR_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),
		.wlan_idx = cpu_to_le16(msta_link ? msta_link->wcid.idx : 0),
		.band_idx = phy->mt76->band_idx,
		.wmm_idx = msta_link ? mconf->mt76.wmm_idx : 0,
		.ctrl_id = cpu_to_le32(id),
		.omac_idx = msta_link ? mconf->mt76.omac_idx : 0
	};

	switch (id) {
	case VOW_DRR_CTRL_STA_ALL:
		val |= FIELD_PREP(MT7999_DRR_STA_BSS_GRP_MASK, vow->bss_grp_idx);
		val |= FIELD_PREP(MT7999_DRR_STA_WMM_MASK, req.wmm_idx);
		val |= FIELD_PREP(MT7999_DRR_STA_AC0_QNTM_MASK, vow->drr_quantum[IEEE80211_AC_BK]);
		val |= FIELD_PREP(MT7999_DRR_STA_AC1_QNTM_MASK, vow->drr_quantum[IEEE80211_AC_BE]);
		val |= FIELD_PREP(MT7999_DRR_STA_AC2_QNTM_MASK, vow->drr_quantum[IEEE80211_AC_VI]);
		val |= FIELD_PREP(MT7999_DRR_STA_AC3_QNTM_MASK, vow->drr_quantum[IEEE80211_AC_VO]);
		req.val = cpu_to_le32(val);
		break;
	case VOW_DRR_CTRL_STA_BSS_GROUP:
		req.val = cpu_to_le32(vow->bss_grp_idx);
		break;
	case VOW_DRR_CTRL_AIRTIME_DEFICIT_BOUND:
		req.val = cpu_to_le32(phy->dev->vow.max_deficit);
		break;
	case VOW_DRR_CTRL_AIRTIME_QUANTUM_ALL:
		for (i = 0; i < VOW_DRR_QUANTUM_NUM; ++i)
			req.drr_quantum[i] = phy->dev->vow.drr_quantum[i] - 1;
		break;
	case VOW_DRR_CTRL_STA_PAUSE:
		req.val = cpu_to_le32(vow->paused);
		break;
	default:
		dev_err(phy->dev->mt76.dev, "Unknown VoW DRR Control ID: %u\n", id);
		return -EINVAL;
	}

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(VOW),
	                         &req, sizeof(req), true);
}

int mt7999_mcu_set_vow_feature_ctrl(struct mt7999_phy *phy, unsigned long flags)
{
	struct mt7999_vow_ctrl *vow = &phy->dev->vow;
	struct {
		u8 __rsv1[4];

		__le16 tag;
		__le16 len;

		/* DW0 */
		__le16 apply_bwc_enable_per_grp;
		__le16 apply_bwc_refill_period		: 1;
		__le16 __rsv2				: 3;
		__le16 apply_band1_search_rule		: 1;
		__le16 apply_band0_search_rule		: 1;
		__le16 __rsv3				: 3;
		__le16 apply_watf_enable		: 1;
		__le16 __rsv4				: 2;
		__le16 apply_grp_no_change_in_txop	: 1;
		__le16 apply_atf_enable			: 1;
		__le16 apply_bwc_token_refill_enable	: 1;
		__le16 apply_bwc_enable			: 1;

		/* DW1 */
		__le16 apply_bwc_check_time_token_per_grp;
		__le16 __rsv5;

		/* DW2 */
		__le16 apply_bwc_check_len_token_per_grp;
		__le16 __rsv6;

		/* DW3 */
		u8 band_idx;
		u8 __rsv7[3];

		/* DW4 */
		__le32 __rsv8;

		/* DW5 */
		__le16 bwc_enable_per_grp;
		__le16 bwc_refill_period	: 3;
		__le16 __rsv9			: 1;
		__le16 band1_search_rule	: 1;
		__le16 band0_search_rule	: 1;
		__le16 __rsv10			: 3;
		__le16 watf_enable		: 1;
		__le16 __rsv11			: 2;
		__le16 grp_no_change_in_txop	: 1;
		__le16 atf_enable		: 1;
		__le16 bwc_token_refill_enable	: 1;
		__le16 bwc_enable		: 1;

		/* DW6 */
		__le16 bwc_check_time_token_per_grp;
		__le16 __rsv12;

		/* DW7 */
		__le16 bwc_check_len_token_per_grp;
		__le16 __rsv13;

		/* DW8 */
		__le32 apply_atf_rts_sta_lock		: 1;
		__le32 atf_rts_sta_lock			: 1;
		__le32 apply_atf_keep_quantum		: 1;
		__le32 atf_keep_quantum			: 1;
		__le32 apply_tx_cnt_mode_ctrl		: 1;
		__le32 tx_cnt_mode_ctrl			: 4;
		__le32 apply_tx_measure_mode_enable	: 1;
		__le32 tx_measure_mode_enable		: 1;
		__le32 apply_backoff_ctrl		: 1;
		__le32 backoff_bound_enable		: 1;
		__le32 backoff_bound			: 5;
		__le32 apply_atf_rts_fail_charge	: 1;
		__le32 atf_rts_fail_charge		: 1;
		__le32 apply_zero_eifs			: 1;
		__le32 zero_eifs			: 1;
		__le32 apply_rx_rifs_enable		: 1;
		__le32 rx_rifs_enable			: 1;
		__le32 apply_vow_ctrl			: 1;
		__le32 vow_ctrl_val			: 1;
		__le32 vow_ctrl_bit			: 5;
		__le32 __rsv14				: 1;

		/* DW9 */
		__le32 apply_spl_sta_num	: 1;
		__le32 spl_sta_num		: 3;
		__le32 dbg_lvl			: 2;
		__le32 apply_atf_sch_ctrl	: 1;
		__le32 atf_sch_type		: 2;
		__le32 atf_sch_policy		: 2;
		__le32 __rsv15			: 21;
	} __packed req = {
		.tag = cpu_to_le16(UNI_VOW_FEATURE_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),
		/* DW3 */
		/* band_idx is not used by per-dev config */
		.band_idx = phy->mt76->band_idx,
		/* DW5 */
		.bwc_enable_per_grp = cpu_to_le16(0xffff),
		.bwc_refill_period = VOW_REFILL_PERIOD_32US,
		.band1_search_rule = VOW_SEARCH_WMM_FIRST,
		.band0_search_rule = VOW_SEARCH_WMM_FIRST,
		.watf_enable = vow->watf_enable,
		.grp_no_change_in_txop = true,
		.atf_enable = vow->atf_enable,
		.bwc_token_refill_enable = true,
		.bwc_enable = false,
		/* DW6 */
		.bwc_check_time_token_per_grp = cpu_to_le16(0x0),
		/* DW7 */
		.bwc_check_len_token_per_grp = cpu_to_le16(0x0),
		/* DW8 */
		.atf_rts_sta_lock = true,
		.atf_keep_quantum = true,
		.tx_cnt_mode_ctrl = 0,
		.tx_measure_mode_enable = true,
		.backoff_bound_enable = true,
		.backoff_bound = 6,
		.atf_rts_fail_charge = false,
		.zero_eifs = true,
		.rx_rifs_enable = true,
		.vow_ctrl_val = true,
		/* Reset DRR table when SER occurs. */
		.vow_ctrl_bit = 26,
		/* DW9 */
		.dbg_lvl = 0,
		.atf_sch_type = vow->sch_type,
		.atf_sch_policy = vow->sch_policy
	};

	/* DW0 */
	req.apply_bwc_enable_per_grp =
		!!test_bit(VOW_FF_BWC_ENABLE, &flags) ? cpu_to_le16(0xffff)
						      : cpu_to_le16(0x0);
	req.apply_bwc_refill_period =
		!!test_bit(VOW_FF_BWC_REFILL_PERIOD, &flags);
	req.apply_band1_search_rule =
		!!test_bit(VOW_FF_BAND1_SEARCH_RULE, &flags);
	req.apply_band0_search_rule =
		!!test_bit(VOW_FF_BAND0_SEARCH_RULE, &flags);
	req.apply_watf_enable =
		!!test_bit(VOW_FF_WATF_ENABLE, &flags);
	req.apply_grp_no_change_in_txop =
		!!test_bit(VOW_FF_GRP_NO_CHANGE_IN_TXOP, &flags);
	req.apply_atf_enable =
		!!test_bit(VOW_FF_ATF_ENABLE, &flags);
	req.apply_bwc_token_refill_enable =
		!!test_bit(VOW_FF_BWC_TOKEN_REFILL_ENABLE, &flags);
	req.apply_bwc_enable =
		!!test_bit(VOW_FF_BWC_ENABLE, &flags);
	/* DW1 */
	req.apply_bwc_check_time_token_per_grp =
		!!test_bit(VOW_FF_BWC_ENABLE, &flags) ? cpu_to_le16(0xffff)
						      : cpu_to_le16(0x0);
	/* DW2 */
	req.apply_bwc_check_len_token_per_grp =
		!!test_bit(VOW_FF_BWC_ENABLE, &flags) ? cpu_to_le16(0xffff)
						      : cpu_to_le16(0x0);
	/* DW8 */
	req.apply_atf_rts_sta_lock =
		!!test_bit(VOW_FF_ATF_RTS_STA_LOCK, &flags);
	req.apply_atf_keep_quantum =
		!!test_bit(VOW_FF_ATF_KEEP_QUANTUM, &flags);
	req.apply_tx_cnt_mode_ctrl =
		!!test_bit(VOW_FF_TX_CNT_MODE_CTRL, &flags);
	req.apply_tx_measure_mode_enable =
		!!test_bit(VOW_FF_TX_MEASURE_MODE_ENABLE, &flags);
	req.apply_backoff_ctrl =
		!!test_bit(VOW_FF_BACKOFF_CTRL, &flags);
	req.apply_atf_rts_fail_charge =
		!!test_bit(VOW_FF_ATF_RTS_FAIL_CHARGE, &flags);
	req.apply_zero_eifs =
		!!test_bit(VOW_FF_ZERO_EIFS, &flags);
	req.apply_rx_rifs_enable =
		!!test_bit(VOW_FF_RX_RIFS_ENABLE, &flags);
	req.apply_vow_ctrl =
		!!test_bit(VOW_FF_VOW_CTRL, &flags);
	/* DW9 */
	req.apply_spl_sta_num =
		!!test_bit(VOW_FF_SPL_STA_NUM, &flags);
	req.apply_atf_sch_ctrl =
		!!test_bit(VOW_FF_ATF_SCH_CTRL, &flags);

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(VOW),
	                         &req, sizeof(req), true);
}

int mt7999_mcu_epcs_ctrl(struct mt7999_dev *dev, struct ieee80211_sta *sta,
			 u8 link_id, bool enable, u16 wmm_idx,
			 struct mt7999_wmm_params *params)
{
	struct {
		u8 rsv1[4];

		__le16 tag;
		__le16 len;
		__le32 cmd;

		__le16 wlan_idx;
		__le16 wmm_idx;
		u8 enable;
		u8 rsv2[3];
		struct {
			struct {
				__le16 txop_limit;
				u8 ecwmin;
				u8 ecwmax;
				u8 aifsn;
				u8 rsv[3];
				u8 mu_ecwmin;
				u8 mu_ecwmax;
				u8 mu_aifsn;
				u8 mu_timer;
			} params[IEEE80211_NUM_ACS];
			__le16 flags;
			u8 rsv[2];
		} wmm;
	} req = {
		.tag = cpu_to_le16(UNI_EPCS_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),
		.cmd = cpu_to_le32(EPCS_CTRL_ENABLE),
		.wmm_idx = cpu_to_le16(wmm_idx),
		.enable = enable,
		.wmm.flags = enable ? cpu_to_le16(EPCS_CTRL_WMM_FLAG_VALID) : 0,
	};
	struct mt7999_sta *msta = (struct mt7999_sta *)sta->drv_priv;
	struct mt7999_sta_link *msta_link;
	int i;

	msta_link = mt7999_sta_link_protected(dev, msta, link_id);
	if (!msta_link)
		return -EINVAL;
	req.wlan_idx = cpu_to_le16(msta_link->wcid.idx);

	for (i = IEEE80211_AC_VO; i < IEEE80211_NUM_ACS; ++i) {
		u8 usr_ac = mt76_ac_to_hwq(i),
		   lmac_ac = mt76_connac_lmac_mapping(i);

		req.wmm.params[lmac_ac].txop_limit = cpu_to_le16(params[usr_ac].txop_limit);
		req.wmm.params[lmac_ac].ecwmin = params[usr_ac].ecwmin;
		req.wmm.params[lmac_ac].ecwmax = params[usr_ac].ecwmax;
		req.wmm.params[lmac_ac].aifsn = params[usr_ac].aifsn;
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(EPCS), &req,
				 sizeof(req), false);
}

int mt7999_mcu_set_tpo(struct mt7999_dev *dev, u8 type, u8 val)
{
	struct {
		u8 __rsv[4];

		__le16 tag;
		__le16 len;

		u8 type;
		u8 val;
		u8 __rsv2[2];
	} __packed req = {
		.tag = cpu_to_le16(UNI_TPO_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),

		.type = type,
		.val = val,
	};

#ifdef CONFIG_MTK_DEBUG
	switch (type) {
	case MT7999_LP_TPO_ALL:
		dev->dbg.lp.tpo = val;
		break;
	case MT7999_LP_TPO_PB:
		dev->dbg.lp.pb_tpo = val;
		break;
	case MT7999_LP_TPO_PR:
		dev->dbg.lp.lp_tpo = val;
		break;
	case MT7999_LP_TPO_MIN_TX:
		dev->dbg.lp.min_tx_tpo = val;
		break;
	default:
		break;
	}
#endif

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(TPO),
				 &req, sizeof(req), true);
}

int mt7999_mcu_set_lp_option(struct mt7999_dev *dev, u8 *arg)
{
	struct {
		u8 __rsv[4];

		__le16 tag;
		__le16 len;

		u8 arg[4];
	} __packed req = {
		.tag = cpu_to_le16(UNI_POWER_LOW_POWER),
		.len = cpu_to_le16(sizeof(req) - 4),
	};

	memcpy(&req.arg, arg, sizeof(req.arg));

#ifdef CONFIG_MTK_DEBUG
#define ULTRA_SAVE_FEATURE	1
#define ULTRA_SAVE_FEATURE_ALL	2
#define ULTRA_SAVE_FEATURE_DCM	3
#define ULTRA_SAVE_FEATURE_1RPD	4
#define ULTRA_SAVE_FEATURE_MMPS	5
#define ULTRA_SAVE_FEATURE_MDPC	6
	if (arg[0] == ULTRA_SAVE_FEATURE_ALL) {
		dev->dbg.lp.ultra_save = !!arg[1];
		dev->dbg.lp.one_rpd = !!arg[1];
		dev->dbg.lp.mmps = !!arg[1];
		dev->dbg.lp.mdpc = !!arg[1];
		dev->dbg.lp.dcm = !!arg[1];
		dev->dbg.lp.alpl = !!arg[1];
	} else if (arg[0] == ULTRA_SAVE_FEATURE) {
		switch (arg[1]) {
		case ULTRA_SAVE_FEATURE_DCM:
			dev->dbg.lp.dcm = !!arg[2];
			break;
		case ULTRA_SAVE_FEATURE_1RPD:
			dev->dbg.lp.one_rpd = !!arg[2];
			break;
		case ULTRA_SAVE_FEATURE_MMPS:
			dev->dbg.lp.mmps = !!arg[2];
			break;
		case ULTRA_SAVE_FEATURE_MDPC:
			dev->dbg.lp.mdpc = !!arg[2];
			break;
		default:
			break;
		}
	}
#endif

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(POWER_CTRL), &req,
				 sizeof(req), false);
}

void mt7999_mcu_enable_1rpd(struct mt7999_dev *dev, bool enable)
{
#define MT7999_ULTRA_SAVE_CONFIG_FEATURES      1
#define MT7999_ULTRA_SAVE_1RPD                 4
	u8 arg[4], i;
	if (enable == dev->enable_1rpd)
		return;

	dev->enable_1rpd = enable;
	memset(arg, 0, sizeof(arg));
	arg[0] = MT7999_ULTRA_SAVE_CONFIG_FEATURES;
	arg[1] = MT7999_ULTRA_SAVE_1RPD;
	arg[2] = enable;

	for (i = 0; i < __MT_MAX_BAND; i++) {
		arg[3] = i;
		mt7999_mcu_set_lp_option(dev, arg);
	}
}

int mt7999_mcu_set_pst(struct mt7999_dev *dev, u32 band, u32 cmd, u32 val)
{
	struct {
		u8 __rsv[4];

		__le16 tag;
		__le16 len;

		__le32 band;
		__le32 cmd;
		__le32 val;
	} __packed req = {
		.tag = cpu_to_le16(UNI_MURU_PST_LOW_POWER),
		.len = cpu_to_le16(sizeof(req) - 4),

		.band = cpu_to_le32(band),
		.cmd = cpu_to_le32(cmd),
		.val = cpu_to_le32(val),
	};

#ifdef CONFIG_MTK_DEBUG
	if (cmd == MURU_PST_CMD_ALGO_MODE) {
		if (band == 0xff) {
			dev->dbg.lp.pst = val ? GENMASK(2, 0) : 0;
		} else if (band < __MT_MAX_BAND) {
			dev->dbg.lp.pst &= ~BIT(band);
			dev->dbg.lp.pst |= val ? BIT(band) : 0;
		}
	}
#endif
	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(MURU), &req,
				 sizeof(req), false);
}

int mt7999_mcu_flush_ple(struct mt7999_dev *dev, u16 threshold,
			 u8 *sta_bitmap, u16 sta_cnt)
{
	/* TODO: check if it is required for connac 5 */
	/*
	struct {
		u8 _rsv1[4];

		__le16 tag;
		__le16 len;

		__le16 threshold;
		__le16 sta_cnt;
		u8 band;
		u8 _rsv2[3];
		u8 sta_bitmap[MT7999_WTBL_GROUP_SIZE];
		u8 _rsv3[3];
	} __packed req = {
		.tag = cpu_to_le16(UNI_WSYS_CONFIG_PLE_FLUSH_PEER),
		.len = sizeof(req) - 4,

		.threshold = cpu_to_le16(threshold),
		.sta_cnt = cpu_to_le16(sta_cnt),
		.band = 0xff,
	};
	int len = sizeof(req);

	req.len = cpu_to_le16(req.len);
	memcpy(&req.sta_bitmap, sta_bitmap, sizeof(req.sta_bitmap));

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(WSYS_CONFIG), &req,
				 len, false);
	*/
	return 0;
}

int mt7999_mcu_set_nf_ctrl(struct mt7999_dev *dev, bool enable, u8 timeout)
{
#define UNI_NOISE_FLOOR_CTRL	0
#define NF_EVENT_TIMEOUT	(5 * HZ)
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		u8 enable;
		u8 timeout;
		u8 count;
		u8 event_num;
	} __packed req = {
		.tag = cpu_to_le16(UNI_NOISE_FLOOR_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),
		.enable = enable,
		.timeout = timeout,
		.count = 1,
		.event_num = 1,
	};
	int ret;

	dev->nf_state = NF_STATE_WAIT;
	ret = mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(NOISE_FLOOR),
				&req, sizeof(req), false);
	if (ret)
		goto out;

	ret = wait_event_timeout(dev->mt76.mcu.wait,
				 (dev->nf_state == NF_STATE_RECEIVED ||
				  test_bit(MT76_MCU_RESET, &dev->mphy.state)),
				 msecs_to_jiffies(timeout) + NF_EVENT_TIMEOUT);
	if (!ret) {
		ret = -ETIMEDOUT;
		goto out;
	}

out:
	dev->nf_state = NF_STATE_IDLE;
	if (ret < 0) {
		dev_err(dev->mt76.dev,
			"Failed to update noise floor (%pe)\n",
			ERR_PTR(ret));
		return ret;
	}

	return 0;
};

#ifdef CONFIG_MTK_VENDOR
void mt7999_set_wireless_vif(void *data, u8 *mac, struct ieee80211_vif *vif)
{
	u8 mode, val, band_idx;
	struct mt7999_vif *mvif = (struct mt7999_vif *)vif->drv_priv;
	struct mt7999_dev *dev = mvif->deflink.phy->dev;
	struct mt7999_phy *phy;
	struct mt76_phy *mphy;

	mode = FIELD_GET(RATE_CFG_MODE, *((u32 *)data));
	val = FIELD_GET(RATE_CFG_VAL, *((u32 *)data));
	band_idx = FIELD_GET(RATE_CFG_BAND_IDX, *((u32 *)data));

	if (!mt7999_band_valid(dev, band_idx))
		goto error;

	mphy = dev->mt76.phys[band_idx];
	if (!mphy)
		goto error;

	phy = (struct mt7999_phy *)mphy->priv;

	switch (mode) {
	case RATE_PARAM_FIXED_OFDMA:
		if (val == 3)
			phy->muru_onoff |= OFDMA_DL;
		else
			phy->muru_onoff |= val;
		break;
	case RATE_PARAM_FIXED_MIMO:
		if (val == 0)
			phy->muru_onoff |= MUMIMO_DL_CERT | MUMIMO_DL;
		else
			phy->muru_onoff |= MUMIMO_UL;
		break;
	case RATE_PARAM_AUTO_MU:
		phy->muru_onoff = val & GENMASK(3, 0);
		break;
	}

	return;
error:
	dev_err(dev->mt76.dev, "Invalid band_idx to config\n");
	return;
}

void mt7999_set_beacon_vif(struct ieee80211_vif *vif, u8 val)
{
	struct mt7999_vif *mvif = (struct mt7999_vif *)vif->drv_priv;
	struct ieee80211_hw *hw = mvif->deflink.phy->mt76->hw;

	vif->bss_conf.enable_beacon = val;

	mt7999_mcu_add_beacon(hw, vif, &vif->bss_conf, val);
}

static int mt7999_mcu_set_csi_enable(struct mt7999_phy *phy, u16 tag)
{
	struct {
		u8 band;
		u8 rsv1[3];

		__le16 tag;
		__le16 len;
	} __packed req = {
		.band = phy->mt76->band_idx,
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(sizeof(req) - 4),
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(CSI_CTRL), &req,
				sizeof(req), false);
}

static int mt7999_mcu_set_csi_frame_type(struct mt7999_phy *phy, u16 tag, u8 type_idx, u32 type)
{
	struct {
		u8 band;
		u8 rsv1[3];

		__le16 tag;
		__le16 len;
		u8 frame_type_idx;
		u8 frame_type;
		u8 rsv2[2];
	} __packed req = {
		.band = phy->mt76->band_idx,
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(sizeof(req) - 4),
		.frame_type_idx = type_idx,
		.frame_type = type,
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(CSI_CTRL), &req,
				sizeof(req), false);
}

static int mt7999_mcu_set_csi_chain_filter(struct mt7999_phy *phy, u16 tag, u8 func, u32 value)
{
	struct {
		u8 band;
		u8 rsv1[3];

		__le16 tag;
		__le16 len;
		u8 function;
		u8 chain_value;
		u8 rsv2[2];
	} __packed req = {
		.band = phy->mt76->band_idx,
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(sizeof(req) - 4),
		.function = func,
		.chain_value = value,
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(CSI_CTRL), &req,
				sizeof(req), false);
}

static int mt7999_mcu_set_csi_sta_filter(struct mt7999_phy *phy, u16 tag, u32 op, u8 *sta_mac)
{
	struct {
		u8 band;
		u8 rsv1[3];

		__le16 tag;
		__le16 len;
		u8 operation;
		u8 rsv2[1];
		u8 mac[6];
	} __packed req = {
		.band = phy->mt76->band_idx,
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(sizeof(req) - 4),
		.operation = op,
	};

	memcpy(req.mac, sta_mac, ETH_ALEN);

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(CSI_CTRL), &req,
				sizeof(req), false);
}

static int mt7999_mcu_set_csi_active_mode(struct mt7999_phy *phy, u16 tag,
					  u32 interval, u8 frame_idx, u8 subframe_idx, u32 bitmap)
{
	struct {
		u8 band;
		u8 rsv1[3];

		__le16 tag;
		__le16 len;
		__le16 interval; /* uint: ms */
		u8 frame_type_idx;
		u8 subframe_type_idx;
		__le32 bitmap; /* sta wcid bitmap */
		u8 rsv2[4];
	} __packed req = {
		.band = phy->mt76->band_idx,
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(sizeof(req) - 4),
		.interval = cpu_to_le16(interval),
		.frame_type_idx = frame_idx,
		.subframe_type_idx = subframe_idx,
		.bitmap = cpu_to_le32(bitmap),
	};

	return mt76_mcu_send_msg(&phy->dev->mt76, MCU_WM_UNI_CMD(CSI_CTRL), &req,
				sizeof(req), false);
}

static void
mt7999_csi_wcid_bitmap_update(void *data, struct ieee80211_sta *sta)
{
	struct mt7999_sta *msta = (struct mt7999_sta *)sta->drv_priv;
	struct mt7999_phy *phy = msta->vif->deflink.phy;
	struct csi_bitmap_info_update *sta_info = (struct csi_bitmap_info_update *)data;
	u16 wcid = 0;

#define CSI_ACTIVE_MODE_ADD 1
#define CSI_ACTIVE_MODE_REMOVE 0

	if (!memcmp(sta_info->addr, sta->addr, ETH_ALEN)) {
		wcid = msta->deflink.wcid.idx;

		/* active mode: only support station with wcid less than 32 */
		if (wcid > 32)
			return;

		if (sta_info->action == CSI_ACTIVE_MODE_ADD)
			phy->csi.active_bitmap |= BIT(wcid);
		else if (sta_info->action == CSI_ACTIVE_MODE_REMOVE)
			phy->csi.active_bitmap &= ~(BIT(wcid));
	}
}

int mt7999_mcu_set_csi(struct mt7999_phy *phy, u8 mode,
			u8 cfg, u8 v1, u32 v2, u8 *mac_addr)
{
	switch (mode) {
	case CSI_CONTROL_MODE_STOP:
		return mt7999_mcu_set_csi_enable(phy, UNI_CSI_STOP);
	case CSI_CONTROL_MODE_START:
		return mt7999_mcu_set_csi_enable(phy, UNI_CSI_START);
	case CSI_CONTROL_MODE_SET:
		switch (cfg) {
		case CSI_CONFIG_FRAME_TYPE:
			if (v2 > 255)
				return -EINVAL;

			return mt7999_mcu_set_csi_frame_type(phy,
					UNI_CSI_SET_FRAME_TYPE, v1, v2);
		case CSI_CONFIG_CHAIN_FILTER:
			if (v2 > 255)
				return -EINVAL;

			return mt7999_mcu_set_csi_chain_filter(phy,
					UNI_CSI_SET_CHAIN_FILTER, v1, v2);
		case CSI_CONFIG_STA_FILTER:
			if (!is_valid_ether_addr(mac_addr))
				return -EINVAL;

			if (v2 > 255)
				return -EINVAL;

			return mt7999_mcu_set_csi_sta_filter(phy,
					UNI_CSI_SET_STA_FILTER, v2, mac_addr);
		case CSI_CONFIG_ACTIVE_MODE:
			if (is_valid_ether_addr(mac_addr)) {
				struct csi_bitmap_info_update sta_info;

				if (v2 > 255)
					return -EINVAL;

				memcpy(sta_info.addr, mac_addr, ETH_ALEN);
				sta_info.action = v2;

				ieee80211_iterate_stations_atomic(phy->mt76->hw,
								mt7999_csi_wcid_bitmap_update, &sta_info);
				return 0;
			} else {
				u8 frame_type = v1 & 0x3;
				u8 frame_subtype = (v1 & 0x3c) >> 2;

					/* active mode: max interval is 3000ms */
					if (v2 > 3000)
						return -EINVAL;

				return mt7999_mcu_set_csi_active_mode(phy, UNI_CSI_SET_ACTIVE_MODE,
						v2, frame_type, frame_subtype, phy->csi.active_bitmap);
			}
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

int mt7999_mcu_set_qos_map(struct mt7999_dev *dev, struct ieee80211_vif *vif,
			   struct mt7999_vif_link *link,
			   struct cfg80211_qos_map *usr_qos_map)
{
	struct mt7999_vif *mvif = (struct mt7999_vif *)vif->drv_priv;
	struct {
		u8 _rsv[4];

		__le16 tag;
		__le16 len;

		u8 bss_idx;
		u8 qos_map_enable;
		u8 __rsv[2];
		s8 qos_map[IP_DSCP_NUM];
	} __packed req = {
		.tag = cpu_to_le16(UNI_SDO_SET_QOS_MAP),
		.len = cpu_to_le16(sizeof(req) - 4),
		.bss_idx = link->mt76.idx,
		.qos_map_enable = true,
	};
	s8 i;

	/* Default QoS map, defined in section 2.3 of RFC8325.
	 * Three most significant bits of DSCP are used as UP.
	 */
	for (i = 0; i < IP_DSCP_NUM; ++i)
		req.qos_map[i] = i >> 3;

	/* Recommended QoS map, defined in section 4 of RFC8325.
	 * Used in cfg80211_classify8021d since kernel v6.8.
	 */
	req.qos_map[10] = req.qos_map[12] = req.qos_map[14] = req.qos_map[16] = 0;
	req.qos_map[18] = req.qos_map[20] = req.qos_map[22] = 3;
	req.qos_map[24] = 4;
	req.qos_map[40] = 5;
	req.qos_map[44] = req.qos_map[46] = 6;
	req.qos_map[48] = 7;

	/* User-defined QoS map */
	if (usr_qos_map) {
		for (i = 0; i < IEEE80211_NUM_UPS; ++i) {
			u8 low = usr_qos_map->up[i].low;
			u8 high = usr_qos_map->up[i].high;

			if (low < IP_DSCP_NUM && high < IP_DSCP_NUM && low <= high)
				memset(req.qos_map + low, i, high - low + 1);
		}

		for (i = 0; i < usr_qos_map->num_des; ++i) {
			u8 dscp = usr_qos_map->dscp_exception[i].dscp;
			u8 up = usr_qos_map->dscp_exception[i].up;

			if (dscp < IP_DSCP_NUM && up < IEEE80211_NUM_UPS)
				req.qos_map[dscp] = up;
		}
	}

	memcpy(mvif->qos_map, req.qos_map, IP_DSCP_NUM);

	return mt76_mcu_send_msg(&dev->mt76, MCU_WA_UNI_CMD(SDO),
				 &req, sizeof(req), false);
}
#endif
