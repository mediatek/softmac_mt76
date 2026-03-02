// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#include "mt7996.h"
#include "mcu.h"
#include "mac.h"
#ifdef CONFIG_MTK_DEBUG
#include "mtk_mcu.h"
#endif

static bool bkg_max80;
module_param(bkg_max80, bool, 0644);
MODULE_PARM_DESC(bkg_max80, "Restrict the max bandwidth of background radar to 80MHz");

int mt7996_run(struct mt7996_phy *phy)
{
	struct mt7996_dev *dev = phy->dev;
	int ret;

	mt7996_mac_enable_nf(dev, phy->mt76->band_idx);

	ret = mt7996_mcu_set_rts_thresh(phy, 0x92b);
	if (ret)
		return ret;

	ret = mt7996_mcu_set_radio_en(phy, true);
	if (ret)
		return ret;

	ret = mt7996_mcu_set_chan_info(phy, UNI_CHANNEL_RX_PATH, false);
	if (ret)
		return ret;

	/* set a parking channel */
	ret = mt7996_mcu_set_chan_info(phy, UNI_CHANNEL_SWITCH, false);
	if (ret)
		return ret;

	ret = mt7996_mcu_set_thermal_throttling(phy, MT7996_THERMAL_THROTTLE_MAX);
	if (ret)
		return ret;

	ret = mt7996_mcu_set_thermal_protect(phy, true);
	if (ret)
		return ret;

	ret = mt7996_mcu_set_scs(phy, SCS_ENABLE);
	if (ret)
		return ret;

#ifdef CONFIG_MTK_DEBUG
	phy->sr_enable = true;
	phy->enhanced_sr_enable = true;
	phy->thermal_protection_enable = true;
	ret = mt7996_mcu_set_tx_power_ctrl(phy, UNI_TXPOWER_SKU_POWER_LIMIT_CTRL,
					   dev->dbg.sku_disable ? 0 : phy->sku_limit_en);

	ret = mt7996_mcu_set_tx_power_ctrl(phy, UNI_TXPOWER_BACKOFF_POWER_LIMIT_CTRL,
					   dev->dbg.sku_disable ? 0 : phy->sku_path_en);
#else
	ret = mt7996_mcu_set_tx_power_ctrl(phy, UNI_TXPOWER_SKU_POWER_LIMIT_CTRL,
					   phy->sku_limit_en);
	ret = mt7996_mcu_set_tx_power_ctrl(phy, UNI_TXPOWER_BACKOFF_POWER_LIMIT_CTRL,
					   phy->sku_path_en);
#endif
	if (ret)
		return ret;

	memset(phy->user_edcca_threshold, MT7996_EDCCA_DEFAULT_THRESHOLD,
	       EDCCA_MAX_BW_NUM);

	set_bit(MT76_STATE_RUNNING, &phy->mt76->state);

	ieee80211_queue_delayed_work(dev->mphy.hw, &phy->mt76->mac_work,
				     MT7996_WATCHDOG_TIME);

	if (!phy->counter_reset) {
		mt7996_mac_reset_counters(phy);
		phy->counter_reset = true;
	}

	return 0;
}

static int mt7996_start(struct ieee80211_hw *hw)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	int ret;

	if (dev->testmode_enable)
		return 0;

	flush_work(&dev->init_work);

	mutex_lock(&dev->mt76.mutex);
	ret = mt7996_mcu_set_hdr_trans(dev, true);
	if (!ret && !is_mt7996(&dev->mt76)) {
		u8 queue = mt76_connac_lmac_mapping(IEEE80211_AC_VI);

		ret = mt7996_mcu_cp_support(dev, queue);
#ifdef CONFIG_MTK_DEBUG
		dev->sr_pp_enable = is_mt7990(&dev->mt76) ? dev->pwr_boost_cap : true;
		dev->uba_enable = true;
#endif
	}
	mutex_unlock(&dev->mt76.mutex);

	ieee80211_queue_delayed_work(hw, &dev->scs_work, HZ);

	return ret;
}

static void mt7996_stop_phy(struct mt7996_phy *phy)
{
	struct mt7996_dev *dev = phy->dev;

	if (dev->testmode_enable ||
	    !test_bit(MT76_STATE_RUNNING, &phy->mt76->state))
		return;

	cancel_delayed_work_sync(&phy->mt76->mac_work);

	mutex_lock(&dev->mt76.mutex);

	mt7996_mcu_rdd_resume_tx(phy);
	mt7996_mcu_set_radio_en(phy, false);

	clear_bit(MT76_STATE_RUNNING, &phy->mt76->state);

	mutex_unlock(&dev->mt76.mutex);
}

static void mt7996_stop(struct ieee80211_hw *hw, bool suspend)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);

	cancel_delayed_work_sync(&dev->scs_work);
}

static inline int get_free_idx(u64 mask, u8 start, u8 end)
{
	if (~mask & GENMASK_ULL(end, start))
		return __ffs64(~mask & GENMASK_ULL(end, start)) + 1;
	return 0;
}

static int get_omac_idx(enum nl80211_iftype type, u64 mask)
{
	int i;

	switch (type) {
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_STATION:
		/* prefer hw bssid slot 1-3 */
		i = get_free_idx(mask, HW_BSSID_1, HW_BSSID_3);
		if (i)
			return i - 1;

		if (type != NL80211_IFTYPE_STATION)
			break;

		i = get_free_idx(mask, EXT_BSSID_1, EXT_BSSID_MAX);
		if (i)
			return i - 1;

		if (~mask & BIT(HW_BSSID_0))
			return HW_BSSID_0;

		break;
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_AP:
		/* ap uses hw bssid 0 and ext bssid */
		if (~mask & BIT(HW_BSSID_0))
			return HW_BSSID_0;

		i = get_free_idx(mask, EXT_BSSID_1, EXT_BSSID_MAX);
		if (i)
			return i - 1;

		break;
	default:
		WARN_ON(1);
		break;
	}

	return -1;
}

static int get_own_mld_idx(u64 mask, bool group_mld)
{
	u8 start = group_mld ? 0 : 16;
	u8 end = group_mld ? 15 : 63;
	int idx;

	idx = get_free_idx(mask, start, end);
	if (idx)
		return idx - 1;

	/* If the 16-63 range is not available, perform another lookup in the
	 * range 0-15
	 */
	if (!group_mld) {
		idx = get_free_idx(mask, 0, 15);
		if (idx)
			return idx - 1;
	}

	return -EINVAL;
}

static void
mt7996_init_bitrate_mask(struct ieee80211_vif *vif, struct mt7996_vif_link *mlink)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mlink->bitrate_mask.control); i++) {
		mlink->bitrate_mask.control[i].gi = NL80211_TXRATE_DEFAULT_GI;
		mlink->bitrate_mask.control[i].he_gi = 0xff;
		mlink->bitrate_mask.control[i].he_ltf = 0xff;
		mlink->bitrate_mask.control[i].legacy = GENMASK(31, 0);
		memset(mlink->bitrate_mask.control[i].ht_mcs, 0xff,
		       sizeof(mlink->bitrate_mask.control[i].ht_mcs));
		memset(mlink->bitrate_mask.control[i].vht_mcs, 0xff,
		       sizeof(mlink->bitrate_mask.control[i].vht_mcs));
		memset(mlink->bitrate_mask.control[i].he_mcs, 0xff,
		       sizeof(mlink->bitrate_mask.control[i].he_mcs));
	}
}

static int
mt7996_set_hw_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
		  struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		  unsigned int link_id, struct ieee80211_key_conf *key)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct ieee80211_bss_conf *link_conf;
	struct mt7996_sta_link *msta_link;
	struct mt7996_vif_link *link;
	int idx = key->keyidx;
	u8 *wcid_keyidx;
	bool is_bigtk;
	int err;

	link = mt7996_vif_link(dev, vif, link_id);
	if (!link)
		return 0;

	if (!mt7996_vif_link_phy(dev, link))
		return 0;

	if (sta) {
		struct mt7996_sta *msta;

		msta = (struct mt7996_sta *)sta->drv_priv;
		msta_link = mt7996_sta_link_protected(dev, msta, link_id);
		if (!msta_link)
			return 0;

		if (!msta_link->wcid.sta)
			return -EOPNOTSUPP;
	} else {
		msta_link = &link->msta_link;
	}
	wcid_keyidx = &msta_link->wcid.hw_key_idx;

	is_bigtk = key->keyidx == 6 || key->keyidx == 7;
	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_AES_CMAC:
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		if (is_bigtk) {
			wcid_keyidx = &msta_link->wcid.hw_key_idx2;
			key->flags |= IEEE80211_KEY_FLAG_GENERATE_MMIE;
		}
		break;
	default:
		break;
	}

	link_conf = link_conf_dereference_protected(vif, link_id);
	if (!link_conf)
		link_conf = &vif->bss_conf;

	if (cmd == SET_KEY && !sta && !link->mt76.cipher) {
		link->mt76.cipher =
			mt76_connac_mcu_get_cipher(key->cipher);
		mt7996_mcu_add_bss_info(link->phy, vif, link_conf,
					&link->mt76, msta_link, true);
	}

	if (cmd == SET_KEY)
		*wcid_keyidx = idx;
	else if (idx == *wcid_keyidx)
		*wcid_keyidx = -1;

	/* only do remove key for BIGTK */
	if (cmd != SET_KEY && !is_bigtk)
		return 0;

	mt76_wcid_key_setup(&dev->mt76, &msta_link->wcid, key);

	err = mt7996_mcu_add_key(&dev->mt76, link, key,
				 MCU_WMWA_UNI_CMD(STA_REC_UPDATE),
				 &msta_link->wcid, cmd);

	/* remove and add beacon in order to enable beacon protection */
	if (cmd == SET_KEY && is_bigtk && link_conf->enable_beacon) {
		mt7996_mcu_add_beacon(hw, vif, link_conf, false);
		mt7996_mcu_add_beacon(hw, vif, link_conf, true);
	}

	return err;
}

struct mt7996_key_iter_data {
    enum set_key_cmd cmd;
    unsigned int link_id;
};

static void
mt7996_key_iter(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		struct ieee80211_sta *sta, struct ieee80211_key_conf *key,
		void *data)
{
	struct mt7996_key_iter_data *it = data;

	if (sta)
		return;

	WARN_ON(mt7996_set_hw_key(hw, it->cmd, vif, NULL, it->link_id, key));
}

int mt7996_vif_link_add(struct mt76_phy *mphy, struct ieee80211_vif *vif,
			struct ieee80211_bss_conf *link_conf,
			struct mt76_vif_link *mlink)
{
	struct mt7996_vif_link *link = container_of(mlink, struct mt7996_vif_link, mt76);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_sta_link *msta_link = &link->msta_link;
	struct mt7996_phy *phy = mphy->priv;
	struct mt7996_dev *dev = phy->dev;
	u8 band_idx = phy->mt76->band_idx;
	struct mt7996_key_iter_data it = {
		.cmd = SET_KEY,
		.link_id = link_conf->link_id,
	};
	int mld_idx, idx, ret;

	if (mvif->mt76.valid_links & BIT(link_conf->link_id))
		return 0;

	if (!dev->testmode_enable) {
		mlink->idx = __ffs64(~dev->mt76.vif_mask);
		if (mlink->idx >= mt7996_max_interface_num(dev))
			return -ENOSPC;

		idx = get_omac_idx(vif->type, phy->omac_mask);
		if (idx < 0)
			return -ENOSPC;
	} else {
		/* bss idx & omac idx should be set to band idx for ibf cal */
		if (dev->mt76.vif_mask & BIT_ULL(band_idx) ||
		    phy->omac_mask & BIT_ULL(band_idx))
			return -ENOSPC;

		mlink->idx = band_idx;
		idx = band_idx;
	}

	mld_idx = get_own_mld_idx(dev->mld_idx_mask, false);
	if (mld_idx < 0)
		return -ENOSPC;

	link->mld_idx = mld_idx;
	link->phy = phy;
	mlink->omac_idx = idx;
	mlink->band_idx = band_idx;
	mlink->wmm_idx = vif->type == NL80211_IFTYPE_AP ? 0 : 3;
	mlink->wcid = &msta_link->wcid;
	mlink->wcid->offchannel = mlink->offchannel;
	mvif->mt76.valid_links |= BIT(link_conf->link_id);

	ret = mt7996_mcu_add_dev_info(phy, vif, link_conf, mlink, true);
	if (ret)
		return ret;

	dev->mt76.vif_mask |= BIT_ULL(mlink->idx);
	dev->mld_idx_mask |= BIT_ULL(link->mld_idx);
	phy->omac_mask |= BIT_ULL(mlink->omac_idx);

	idx = MT7996_WTBL_RESERVED - mlink->idx;

	INIT_LIST_HEAD(&msta_link->rc_list);
	msta_link->wcid.idx = idx;
	msta_link->wcid.link_id = link_conf->link_id;
	msta_link->wcid.link_valid = ieee80211_vif_is_mld(vif);
	msta_link->wcid.tx_info |= MT_WCID_TX_INFO_SET;
	mt76_wcid_init(&msta_link->wcid, band_idx);

	msta_link->sta = &mvif->sta;

	mt7996_mac_wtbl_update(dev, idx,
			       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	if (vif->type != NL80211_IFTYPE_AP &&
	    (!mlink->omac_idx || mlink->omac_idx > 3))
		vif->offload_flags = 0;

	if (phy->mt76->chandef.chan->band != NL80211_BAND_2GHZ)
		mlink->basic_rates_idx = MT7996_BASIC_RATES_TBL + 4;
	else
		mlink->basic_rates_idx = MT7996_BASIC_RATES_TBL;

	mt7996_init_bitrate_mask(vif, link);

	mt7996_mcu_add_bss_info(phy, vif, link_conf, mlink, msta_link, true);
	/* defer the first STA_REC of BMC entry to BSS_CHANGED_BSSID for STA
	 * interface, since firmware only records BSSID when the entry is new
	 */
	if (vif->type != NL80211_IFTYPE_STATION)
		mt7996_mcu_add_sta(dev, vif, link_conf, NULL, link, msta_link,
				   CONN_STATE_PORT_SECURE, true);
	rcu_assign_pointer(dev->mt76.wcid[idx], &msta_link->wcid);
	rcu_assign_pointer(mvif->sta.link[link_conf->link_id], msta_link);

	ieee80211_iter_keys(mphy->hw, vif, mt7996_key_iter, &it);

	if (vif->txq && !mlink->wcid->offchannel &&
	    mvif->mt76.deflink_id == IEEE80211_LINK_UNSPECIFIED) {
		struct mt76_txq *mtxq = (struct mt76_txq *)vif->txq->drv_priv;

		mvif->mt76.deflink_id = link_conf->link_id;
		mtxq->wcid = idx;
	}

	if (link_conf->nontransmitted && link_conf->bssid_index != 0 &&
	    link_conf->bssid_index < MT7996_MAX_MBSSID) {
		rcu_assign_pointer(phy->mbssid_conf[link_conf->bssid_index], link);
		link->mbssid_idx = link_conf->bssid_index;
	}

	link->bpcc = 0;
	memset(link->tsf_offset, 0, sizeof(link->tsf_offset));
	INIT_DELAYED_WORK(&link->sta_chsw_work, mt7996_sta_chsw_work);

	mt76_bss_dbg(vif, "band=%u, bss_idx=%u, link_id=%u, wcid=%u",
		     phy->mt76->band_idx, mlink->idx, link_conf->link_id,
		     msta_link->wcid.idx);

	return 0;
}

void mt7996_vif_link_remove(struct mt76_phy *mphy, struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf,
			    struct mt76_vif_link *mlink)
{
	struct mt7996_vif_link *link = container_of(mlink, struct mt7996_vif_link, mt76);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_sta_link *msta_link = &link->msta_link;
	struct mt7996_phy *phy = mphy->priv;
	struct mt7996_dev *dev = phy->dev;
	int link_id = msta_link->wcid.link_id;
	struct mt7996_key_iter_data it = {
		.cmd = SET_KEY,
		.link_id = link_id,
	};
	int idx = msta_link->wcid.idx;

	cancel_delayed_work(&link->sta_chsw_work);

	if (dev->testmode_enable) {
		if (!mt76_testmode_enabled(mphy))
			goto cleanup;
		return;
	}

	if (ieee80211_vif_is_mld(vif) && mlink->ctx)
		return;

	if (!mlink->wcid->offchannel)
		ieee80211_iter_keys(mphy->hw, vif, mt7996_key_iter, &it);

	if (link->mbssid_idx != 0 && link->mbssid_idx < MT7996_MAX_MBSSID) {
		rcu_assign_pointer(phy->mbssid_conf[link->mbssid_idx], NULL);
		link->mbssid_idx = 0;
	}

	mt7996_mcu_add_sta(dev, vif, link_conf, NULL, link, msta_link,
			   CONN_STATE_DISCONNECT, false);
	mt7996_mcu_add_bss_info(phy, vif, link_conf, mlink, msta_link, false);

	mt7996_mcu_add_dev_info(phy, vif, link_conf, mlink, false);

	rcu_assign_pointer(dev->mt76.wcid[idx], NULL);
	rcu_assign_pointer(mvif->sta.link[link_id], NULL);
	if (link != &mvif->deflink && !mlink->offchannel)
		rcu_assign_pointer(mvif->mt76.link[link_id], NULL);

	if (vif->txq && !mlink->wcid->offchannel &&
	    mvif->mt76.deflink_id == link_id) {
		struct ieee80211_bss_conf *iter;
		struct mt76_txq *mtxq;
		unsigned int link_id, deflink_id = mvif->mt76.deflink_id;

		mvif->mt76.deflink_id = IEEE80211_LINK_UNSPECIFIED;
		mtxq = (struct mt76_txq *)vif->txq->drv_priv;
		/* Primary link will be removed, look for a new one */
		for_each_vif_active_link(vif, iter, link_id) {
			struct mt7996_vif_link *link;

			if (link_id == deflink_id)
				continue;

			link = mt7996_vif_link(dev, vif, link_id);
			if (!link)
				continue;

			mtxq->wcid = link->msta_link.wcid.idx;
			mvif->mt76.deflink_id = link_id;
			break;
		}
	}

	dev->mt76.vif_mask &= ~BIT_ULL(mlink->idx);
	dev->mld_idx_mask &= ~BIT_ULL(link->mld_idx);
	phy->omac_mask &= ~BIT_ULL(mlink->omac_idx);

	mvif->mt76.valid_links &= ~BIT(link_id);

	spin_lock_bh(&dev->mt76.sta_poll_lock);
	if (!list_empty(&msta_link->wcid.poll_list))
		list_del_init(&msta_link->wcid.poll_list);
	spin_unlock_bh(&dev->mt76.sta_poll_lock);

	mt76_wcid_cleanup(&dev->mt76, &msta_link->wcid);
	mlink->wcid = NULL;

	if (link != &mvif->deflink && !mlink->offchannel)
		kfree_rcu(link, mt76.rcu_head);
}

static void mt7996_phy_set_rxfilter(struct mt7996_phy *phy)
{
	struct mt7996_dev *dev = phy->dev;
	u32 ctl_flags = MT_WF_RFCR1_DROP_ACK |
			MT_WF_RFCR1_DROP_BF_POLL |
			MT_WF_RFCR1_DROP_BA |
			MT_WF_RFCR1_DROP_CFEND |
			MT_WF_RFCR1_DROP_CFACK;
	u32 filter = phy->rxfilter;

	if (filter & MT_WF_RFCR_DROP_OTHER_UC) {
		filter |= MT_WF_RFCR_DROP_CTS |
			  MT_WF_RFCR_DROP_RTS |
			  MT_WF_RFCR_DROP_CTL_RSV |
			  MT_WF_RFCR_DROP_FCSFAIL;
	}

	mt76_wr(dev, MT_WF_RFCR(phy->mt76->band_idx), filter);
	if (filter & MT_WF_RFCR_DROP_CTL_RSV)
		mt76_set(dev, MT_WF_RFCR1(phy->mt76->band_idx), ctl_flags);
	else
		mt76_clear(dev, MT_WF_RFCR1(phy->mt76->band_idx), ctl_flags);
}

static void mt7996_set_monitor(struct mt7996_phy *phy, bool enabled)
{
	struct mt7996_dev *dev;

	if (!phy)
		return;

	dev = phy->dev;

	if (enabled == !(phy->rxfilter & MT_WF_RFCR_DROP_OTHER_UC))
		return;

	if (!enabled)
		phy->rxfilter |= MT_WF_RFCR_DROP_OTHER_UC;
	else
		phy->rxfilter &= ~MT_WF_RFCR_DROP_OTHER_UC;

	mt76_rmw_field(dev, MT_DMA_DCR0(phy->mt76->band_idx),
		       MT_DMA_DCR0_RXD_G5_EN, enabled);
	mt76_rmw_field(dev, MT_MDP_DCR0,
		       MT_MDP_DCR0_RX_HDR_TRANS_EN, !enabled);
	mt7996_phy_set_rxfilter(phy);
	mt7996_mcu_set_sniffer_mode(phy, enabled);
}

static int mt7996_testmode_init(struct mt7996_dev *dev, struct ieee80211_vif *vif)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct wireless_dev *wdev = ieee80211_vif_to_wdev(vif);
	struct mt7996_phy *phy;
	int i, err = 0;

	mutex_lock(&dev->mt76.mutex);

	for (i = 0; i < MT7996_MAX_RADIOS; i++) {
		phy = dev->radio_phy[i];
		if (!phy)
			continue;

		if (wdev->radio_mask & BIT(i)) {
			phy->mt76->monitor_vif = vif;
			mvif->deflink.phy = phy;
		}

		if (test_bit(MT76_STATE_RUNNING, &phy->mt76->state))
			continue;

		err = mt7996_mcu_set_radio_en(phy, true);
		if (err)
			goto out;

		err = mt7996_mcu_set_chan_info(phy, UNI_CHANNEL_RX_PATH, false);
		if (err)
			goto out;

		/* set a parking channel */
		err = mt7996_mcu_set_chan_info(phy, UNI_CHANNEL_SWITCH, false);
		if (err)
			goto out;

		set_bit(MT76_STATE_RUNNING, &phy->mt76->state);
	}

	mt76_vif_init(vif, &mvif->mt76);
	mvif->mt76.deflink_id = IEEE80211_LINK_UNSPECIFIED;

out:
	if (err) {
		dev_err(dev->mt76.dev,
			"testmode setup fail for band %d err=%d\n",
			phy->mt76->band_idx, err);
		phy->mt76->monitor_vif = NULL;
		mvif->deflink.phy = NULL;
	}

	mutex_unlock(&dev->mt76.mutex);

	return err;
}

static int mt7996_add_interface(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct wireless_dev *wdev = ieee80211_vif_to_wdev(vif);
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	int i, err = 0;

	if (dev->testmode_enable) {
		if (vif->type != NL80211_IFTYPE_MONITOR) {
			dev_err(dev->mt76.dev, "Only monitor interface is allowed in testmode\n");
			return -EINVAL;
		}

		return mt7996_testmode_init(dev, vif);
	}

	mutex_lock(&dev->mt76.mutex);

	for (i = 0; i < MT7996_MAX_RADIOS; i++) {
		struct mt7996_phy *phy = dev->radio_phy[i];

		if (!phy || !(wdev->radio_mask & BIT(i)) ||
		    test_bit(MT76_STATE_RUNNING, &phy->mt76->state))
			continue;

		err = mt7996_run(phy);
		if (err)
			goto out;

		if (vif->type == NL80211_IFTYPE_MONITOR) {
			mt7996_set_monitor(phy, true);
			phy->mt76->monitor_vif = vif;
		}

		if (!mvif->deflink.phy)
			mvif->deflink.phy = phy;
	}

	mt76_vif_init(vif, &mvif->mt76);

	vif->offload_flags |= IEEE80211_OFFLOAD_ENCAP_4ADDR;
	mvif->mt76.deflink_id = IEEE80211_LINK_UNSPECIFIED;
	mvif->sta.vif = mvif;

	memset(mvif->cs_links, IEEE80211_LINK_UNSPECIFIED,
	       sizeof(mvif->cs_links));

	INIT_DELAYED_WORK(&mvif->beacon_mon_work, mt7996_beacon_mon_work);

out:
	mutex_unlock(&dev->mt76.mutex);

	return err;
}

struct mt7996_radio_data {
	u32 active_mask;
	u32 monitor_mask;
};

static void mt7996_remove_iter(void *data, u8 *mac, struct ieee80211_vif *vif)
{
	struct wireless_dev *wdev = ieee80211_vif_to_wdev(vif);
	struct mt7996_radio_data *rdata = data;

	rdata->active_mask |= wdev->radio_mask;
	if (vif->type == NL80211_IFTYPE_MONITOR)
		rdata->monitor_mask |= wdev->radio_mask;
}

static void mt7996_remove_interface(struct ieee80211_hw *hw,
				    struct ieee80211_vif *vif)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_radio_data rdata = {};
	int i;
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;

	cancel_delayed_work(&mvif->beacon_mon_work);

	ieee80211_iterate_active_interfaces_mtx(hw, 0, mt7996_remove_iter,
						&rdata);
	mt76_vif_cleanup(&dev->mt76, vif);

	for (i = 0; i < MT7996_MAX_RADIOS; i++) {
		struct mt7996_phy *phy = dev->radio_phy[i];

		if (!phy)
			continue;
		if (vif->type == NL80211_IFTYPE_MONITOR &&
		    !(rdata.monitor_mask & BIT(i)))
			mt7996_set_monitor(phy, false);
		if (!(rdata.active_mask & BIT(i)))
			mt7996_stop_phy(phy);

		if (vif == phy->mt76->monitor_vif) {
			mutex_lock(&dev->mt76.mutex);
			phy->mt76->monitor_vif = NULL;
#ifdef CONFIG_NL80211_TESTMODE
			if (dev->testmode_enable)
				kfree(phy->mt76->lists);
#endif
			mutex_unlock(&dev->mt76.mutex);
		}
	}
}

int mt7996_set_channel(struct mt76_phy *mphy)
{
	struct mt7996_phy *phy = mphy->priv;
	struct mt7996_dev *dev = phy->dev;
	int ret;

	if (dev->cal.data) {
		ret = mt7996_mcu_apply_tx_dpd(phy);
		if (ret)
			goto out;
	}

	if (dev->testmode_enable) {
		if (mt76_testmode_enabled(mphy) || mt76_testmode_bf_enabled(mphy))
			mt7996_tm_update_channel(phy);
		return 0;
	}

	ret = mt7996_mcu_set_chan_info(phy, UNI_CHANNEL_SWITCH, false);
	if (ret)
		goto out;

	ret = mt7996_mcu_set_chan_info(phy, UNI_CHANNEL_RX_PATH, false);
	if (ret)
		goto out;

	ret = mt7996_dfs_init_radar_detector(phy);
	mt7996_mac_cca_stats_reset(phy);

	mt7996_mac_reset_counters(phy);
#ifdef CONFIG_MTK_VENDOR
	if (phy->user_edcca_threshold[EDCCA_BW_20] != MT7996_EDCCA_DEFAULT_THRESHOLD ||
	    phy->user_edcca_threshold[EDCCA_BW_40] != MT7996_EDCCA_DEFAULT_THRESHOLD ||
	    phy->user_edcca_threshold[EDCCA_BW_80] != MT7996_EDCCA_DEFAULT_THRESHOLD ||
	    phy->user_edcca_threshold[EDCCA_BW_160] != MT7996_EDCCA_DEFAULT_THRESHOLD)
		mt7996_mcu_edcca_threshold_ctrl(phy, phy->user_edcca_threshold, true);
#endif
	phy->noise = 0;

out:
	ieee80211_queue_delayed_work(mphy->hw, &mphy->mac_work,
				     MT7996_WATCHDOG_TIME);

	return ret;
}

static int mt7996_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
			  struct ieee80211_vif *vif, struct ieee80211_sta *sta,
			  struct ieee80211_key_conf *key)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	unsigned int link_id;
	unsigned long links;
	int err = 0;

	/* The hardware does not support per-STA RX GTK, fallback
	 * to software mode for these.
	 */
	if ((vif->type == NL80211_IFTYPE_ADHOC ||
	     vif->type == NL80211_IFTYPE_MESH_POINT) &&
	    (key->cipher == WLAN_CIPHER_SUITE_TKIP ||
	     key->cipher == WLAN_CIPHER_SUITE_CCMP) &&
	    !(key->flags & IEEE80211_KEY_FLAG_PAIRWISE))
		return -EOPNOTSUPP;

	/* fall back to sw encryption for unsupported ciphers */
	switch (key->cipher) {
	case WLAN_CIPHER_SUITE_TKIP:
	case WLAN_CIPHER_SUITE_CCMP:
	case WLAN_CIPHER_SUITE_CCMP_256:
	case WLAN_CIPHER_SUITE_GCMP:
	case WLAN_CIPHER_SUITE_GCMP_256:
	case WLAN_CIPHER_SUITE_SMS4:
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
	case WLAN_CIPHER_SUITE_BIP_CMAC_256:
	case WLAN_CIPHER_SUITE_BIP_GMAC_128:
	case WLAN_CIPHER_SUITE_BIP_GMAC_256:
		if (key->keyidx == 6 || key->keyidx == 7)
			break;
		fallthrough;
	case WLAN_CIPHER_SUITE_WEP40:
	case WLAN_CIPHER_SUITE_WEP104:
	default:
		return -EOPNOTSUPP;
	}

	mutex_lock(&dev->mt76.mutex);

	if (key->link_id >= 0)
		links = BIT(key->link_id);
	else if (sta && sta->valid_links)
		links = sta->valid_links;
	else if (vif->valid_links)
		links = vif->valid_links;
	else
		links = BIT(0);

	for_each_set_bit(link_id, &links, IEEE80211_MLD_MAX_NUM_LINKS) {
		err = mt7996_set_hw_key(hw, cmd, vif, sta, link_id, key);
		if (err)
			break;
	}
	mutex_unlock(&dev->mt76.mutex);

	return err;
}

static int mt7996_config(struct ieee80211_hw *hw, int radio_idx, u32 changed)
{
	return 0;
}

static int
mt7996_conf_tx(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       unsigned int link_id, u16 queue,
	       const struct ieee80211_tx_queue_params *params)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_vif_link_info *link_info = &mvif->link_info[link_id];
	static const u8 mq_to_aci[] = {
		[IEEE80211_AC_VO] = 3,
		[IEEE80211_AC_VI] = 2,
		[IEEE80211_AC_BE] = 0,
		[IEEE80211_AC_BK] = 1,
	};

	/* firmware uses access class index */
	link_info->queue_params[mq_to_aci[queue]] = *params;
	/* no need to update right away, we'll get BSS_CHANGED_QOS */

	return 0;
}

static void mt7996_configure_filter(struct ieee80211_hw *hw,
				    unsigned int changed_flags,
				    unsigned int *total_flags,
				    u64 multicast)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy;
	u32 filter_mask = 0, filter_set = 0;
	u32 flags = 0;

#define MT76_FILTER(_flag, _hw) do {				\
		flags |= *total_flags & FIF_##_flag;		\
		filter_mask |= (_hw);				\
		filter_set |= !(flags & FIF_##_flag) * (_hw);	\
	} while (0)

	mutex_lock(&dev->mt76.mutex);

	MT76_FILTER(OTHER_BSS, MT_WF_RFCR_DROP_OTHER_TIM |
			       MT_WF_RFCR_DROP_A3_MAC |
			       MT_WF_RFCR_DROP_A3_BSSID);

	MT76_FILTER(FCSFAIL, MT_WF_RFCR_DROP_FCSFAIL);

	MT76_FILTER(CONTROL, MT_WF_RFCR_DROP_CTS |
			     MT_WF_RFCR_DROP_RTS |
			     MT_WF_RFCR_DROP_CTL_RSV);

	*total_flags = flags;

	mt7996_for_each_phy(dev, phy) {
		phy->rxfilter &= ~(MT_WF_RFCR_DROP_OTHER_BSS |
				   MT_WF_RFCR_DROP_OTHER_BEACON |
				   MT_WF_RFCR_DROP_FRAME_REPORT |
				   MT_WF_RFCR_DROP_PROBEREQ |
				   MT_WF_RFCR_DROP_MCAST_FILTERED |
				   MT_WF_RFCR_DROP_MCAST |
				   MT_WF_RFCR_DROP_BCAST |
				   MT_WF_RFCR_DROP_DUPLICATE |
				   MT_WF_RFCR_DROP_A2_BSSID |
				   MT_WF_RFCR_DROP_UNWANTED_CTL |
				   MT_WF_RFCR_DROP_STBC_MULTI |
				   filter_mask);
		phy->rxfilter |= filter_set;
		mt7996_phy_set_rxfilter(phy);
	}

	mutex_unlock(&dev->mt76.mutex);
}

static u8
mt7996_get_rates_table(struct mt7996_phy *phy, struct ieee80211_bss_conf *conf,
		       bool beacon, bool mcast)
{
#define FR_RATE_IDX_OFDM_6M 0x004b
	struct mt7996_dev *dev = phy->dev;
	struct mt76_vif_link *mvif = mt76_vif_conf_link(&dev->mt76, conf->vif, conf);
	u16 rate;
	u8 i, idx;

	rate = mt76_connac2_mac_tx_rate_val(phy->mt76, conf, beacon, mcast);

	if (beacon) {
		/* For WFA Cert TGax T/C 4.73.1 */
		if (dev->cert_mode && phy->mt76->band_idx == MT_BAND2 &&
		    conf->he_support && !conf->eht_support)
			rate = 0x0200;
		else if (dev->mt76.lpi_mode && dev->mt76.lpi_bcn_enhance)
			rate = FR_RATE_IDX_OFDM_6M;

		/* odd index for driver, even index for firmware */
		idx = MT7996_BEACON_RATES_TBL + 2 * phy->mt76->band_idx;
		if (phy->beacon_rate != rate)
			mt7996_mcu_set_fixed_rate_table(phy, idx, rate, beacon);

		return idx;
	}

	idx = FIELD_GET(MT_TX_RATE_IDX, rate);
	for (i = 0; i < ARRAY_SIZE(mt76_rates); i++)
		if ((mt76_rates[i].hw_value & GENMASK(7, 0)) == idx)
			return MT7996_BASIC_RATES_TBL + 2 * i;

	return mvif->basic_rates_idx;
}

static void
mt7996_update_mu_group(struct ieee80211_hw *hw, struct mt7996_vif_link *link,
		       struct ieee80211_bss_conf *info)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	u8 band = link->mt76.band_idx;
	u32 *mu;

	mu = (u32 *)info->mu_group.membership;
	mt76_wr(dev, MT_WF_PHYRX_BAND_GID_TAB_VLD0(band), mu[0]);
	mt76_wr(dev, MT_WF_PHYRX_BAND_GID_TAB_VLD1(band), mu[1]);

	mu = (u32 *)info->mu_group.position;
	mt76_wr(dev, MT_WF_PHYRX_BAND_GID_TAB_POS0(band), mu[0]);
	mt76_wr(dev, MT_WF_PHYRX_BAND_GID_TAB_POS1(band), mu[1]);
	mt76_wr(dev, MT_WF_PHYRX_BAND_GID_TAB_POS2(band), mu[2]);
	mt76_wr(dev, MT_WF_PHYRX_BAND_GID_TAB_POS3(band), mu[3]);
}

static void
mt7996_vif_cfg_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		       u64 changed)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);

	mutex_lock(&dev->mt76.mutex);

	if ((changed & BSS_CHANGED_ASSOC) && vif->cfg.assoc) {
		struct ieee80211_bss_conf *link_conf;
		unsigned long link_id;

		for_each_vif_active_link(vif, link_conf, link_id) {
			struct mt7996_vif_link *link;

			link = mt7996_vif_link(dev, vif, link_id);
			if (!link)
				continue;

			if (!link->phy)
				continue;

			mt7996_mcu_add_bss_info(link->phy, vif, link_conf,
						&link->mt76, &link->msta_link,
						true);
			mt7996_mcu_add_sta(dev, vif, link_conf, NULL, link, NULL,
					   CONN_STATE_PORT_SECURE, false);
		}
	}

	mutex_unlock(&dev->mt76.mutex);
}

static void mt7996_get_tsf_offset(struct ieee80211_vif *vif,
				  struct mt7996_phy *phy,
				  struct mt7996_dev *dev)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_vif_link *rpted_mconf;
	unsigned long valid_links = vif->valid_links;
	unsigned int rpting_linkid, rpted_linkid;

	for_each_set_bit(rpted_linkid, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {

		rpted_mconf = mt7996_vif_link(dev, vif, rpted_linkid);
		if (!rpted_mconf)
			return;

		for_each_set_bit(rpting_linkid, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {

			if (rpted_linkid == rpting_linkid)
				continue;
			mt7996_mcu_get_tsf_offset(phy, mvif, rpting_linkid, rpted_linkid);
		}

		ieee80211_tsf_offset_notify(vif, rpted_linkid, rpted_mconf->tsf_offset,
					    sizeof(rpted_mconf->tsf_offset), GFP_KERNEL);
	}
}

static void
mt7996_link_info_changed(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			 struct ieee80211_bss_conf *info, u64 changed)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif_link *link;
	struct mt7996_phy *phy;

	mutex_lock(&dev->mt76.mutex);

	link = mt7996_vif_conf_link(dev, vif, info);
	if (!link)
		goto out;

	phy = mt7996_vif_link_phy(dev, link);
	if (!phy)
		goto out;

	/* station mode uses BSSID to map the wlan entry to a peer,
	 * and then peer references bss_info_rfch to set bandwidth cap.
	 */
	if ((changed & BSS_CHANGED_BSSID && !is_zero_ether_addr(info->bssid)) ||
	    (changed & BSS_CHANGED_BEACON_ENABLED)) {
		mt7996_mcu_add_bss_info(phy, vif, info, &link->mt76,
					&link->msta_link, true);
		mt7996_mcu_add_sta(dev, vif, info, NULL, link, &link->msta_link,
				   CONN_STATE_PORT_SECURE,
				   !!(changed & BSS_CHANGED_BSSID));
	}

	if (changed & BSS_CHANGED_BSSID && is_zero_ether_addr(info->bssid))
		mt7996_mcu_add_sta(dev, vif, info, NULL, link, &link->msta_link,
				   CONN_STATE_DISCONNECT, false);

	if (changed & BSS_CHANGED_HT || changed & BSS_CHANGED_ERP_CTS_PROT)
		mt7996_mcu_set_protection(phy, link, info->ht_operation_mode,
					  info->use_cts_prot);

	if (changed & BSS_CHANGED_ERP_SLOT) {
		int slottime = info->use_short_slot ? 9 : 20;

		if (slottime != phy->slottime) {
			phy->slottime = slottime;
			mt7996_mcu_set_timing(phy, vif, info);
		}
	}

	if (changed & BSS_CHANGED_MCAST_RATE)
		link->mt76.mcast_rates_idx =
			mt7996_get_rates_table(phy, info, false, true);

	if (changed & BSS_CHANGED_BASIC_RATES)
		link->mt76.basic_rates_idx =
			mt7996_get_rates_table(phy, info, false, false);

	/* ensure that enable txcmd_mode after bss_info */
	if (changed & (BSS_CHANGED_QOS | BSS_CHANGED_BEACON_ENABLED))
		mt7996_mcu_set_tx(dev, vif, info);

	if (changed & BSS_CHANGED_HE_OBSS_PD)
		mt7996_mcu_add_obss_spr(phy, link, &info->he_obss_pd);

	if (changed & BSS_CHANGED_HE_BSS_COLOR) {
		if ((vif->type == NL80211_IFTYPE_AP &&
		    link->mt76.omac_idx <= HW_BSSID_MAX) ||
		   vif->type == NL80211_IFTYPE_STATION)
			mt7996_mcu_update_bss_color(dev, &link->mt76,
						    &info->he_bss_color);
	}

	if (changed & (BSS_CHANGED_BEACON |
		       BSS_CHANGED_BEACON_ENABLED)) {
		link->mt76.beacon_rates_idx =
			mt7996_get_rates_table(phy, info, true, false);

		/* The CSA beacon will be set in channel_switch_beacon,
		 * but beacon can be disabled during CSA for DFS channel.
		 */
		if (!info->enable_beacon || !info->csa_active)
			mt7996_mcu_add_beacon(hw, vif, info, info->enable_beacon);

		if (!info->enable_beacon && hweight16(vif->valid_links) > 1)
			mt7996_get_tsf_offset(vif, phy, dev);
	}

	if (changed & (BSS_CHANGED_UNSOL_BCAST_PROBE_RESP |
		       BSS_CHANGED_FILS_DISCOVERY))
		mt7996_mcu_beacon_inband_discov(dev, info, link, changed);

	if (changed & BSS_CHANGED_MU_GROUPS)
		mt7996_update_mu_group(hw, link, info);

	if (changed & BSS_CHANGED_TXPOWER &&
	    info->txpower != phy->txpower) {
		phy->txpower = info->txpower;
		mt7996_mcu_set_txpower_sku(phy, info->txpower);
	}

out:
	mutex_unlock(&dev->mt76.mutex);
}

static void
mt7996_channel_switch_beacon(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct cfg80211_chan_def *chandef)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_phy *phy = mt7996_band_phy(dev, chandef->chan->band);
	struct ieee80211_bss_conf *link_conf;
	u16 ready_links = 0;
	unsigned int link_id;
	int i;

	mutex_lock(&dev->mt76.mutex);
	link_id = mvif->mt76.band_to_link[phy->mt76->band_idx];
	if (link_id == IEEE80211_LINK_UNSPECIFIED)
		goto out;

	for (i = 0; i < sizeof(mvif->cs_links); i++) {
		if (mvif->cs_links[i] == IEEE80211_LINK_UNSPECIFIED) {
			mvif->cs_links[i] = link_id;
			ready_links |= BIT(link_id);
			break;
		}

		ready_links |= BIT(mvif->cs_links[i]);
	}

	if (ready_links < vif->valid_links)
		goto out;

	for (i = 0; i < sizeof(mvif->cs_links); i++) {
		link_id = mvif->cs_links[i];
		if (link_id == IEEE80211_LINK_UNSPECIFIED)
			break;

		link_conf = link_conf_dereference_protected(vif, link_id);
		if (!link_conf)
			break;

		/* Reset beacon when channel switch triggered during CAC to let
		 * FW correctly perform CSA countdown
		 */
		if (i == 0 &&
		    !cfg80211_reg_can_beacon(hw->wiphy, &phy->mt76->chandef,
					     vif->type))
			mt7996_mcu_add_beacon(hw, vif, link_conf, false);

		mt7996_mcu_add_beacon(hw, vif, link_conf, true);
	}

out:
	mutex_unlock(&dev->mt76.mutex);
}

static int
mt7996_pre_channel_switch(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			  struct ieee80211_channel_switch *ch_switch)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct cfg80211_chan_def *c = &ch_switch->chandef, *rdd2_chandef;
	struct mt7996_phy *phy = mt7996_band_phy(dev, c->chan->band);

	mutex_lock(&dev->mt76.mutex);

	if (phy != dev->rdd2_phy)
		goto out;

	rdd2_chandef = &dev->rdd2_chandef;
	if (rdd2_chandef->width != c->width ||
	    rdd2_chandef->center_freq1 != c->center_freq1)
		dev->bg_nxt_freq = 0;

out:
	mutex_unlock(&dev->mt76.mutex);

	return 0;
}

static int
mt7996_post_channel_switch(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			   struct ieee80211_bss_conf *link_conf)
{
	struct cfg80211_chan_def *chandef = &link_conf->chanreq.oper;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy = mt7996_band_phy(dev, chandef->chan->band);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_vif_link *link;
	int ret = -EINVAL;

	if (mvif->cs_links[0] != link_conf->link_id)
		return 0;

	mutex_lock(&dev->mt76.mutex);

	link = mt7996_vif_conf_link(dev, vif, link_conf);
	if (!link)
		goto out;

	ret = mt7996_mcu_update_bss_rfch(phy, link);
	if (ret)
		goto out;

	ieee80211_iterate_stations_mtx(hw, mt7996_mcu_update_sta_rec_bw, link);

	ret = mt7996_mcu_rdd_resume_tx(phy);
	if (ret)
		goto out;

out:
	memset(mvif->cs_links, IEEE80211_LINK_UNSPECIFIED,
	       sizeof(mvif->cs_links));

	mutex_unlock(&dev->mt76.mutex);

	if (ret || phy->mt76->dfs_state != MT_DFS_STATE_CAC)
		return ret;

	return mt76_set_channel(phy->mt76, chandef, false);
}

static void
mt7996_sta_init_txq_wcid(struct ieee80211_sta *sta, int idx)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sta->txq); i++) {
		struct mt76_txq *mtxq;

		if (!sta->txq[i])
			continue;

		mtxq = (struct mt76_txq *)sta->txq[i]->drv_priv;
		mtxq->wcid = idx;
	}
}

static int
mt7996_mac_sta_init_link(struct mt7996_dev *dev,
			 struct ieee80211_bss_conf *link_conf,
			 struct ieee80211_link_sta *link_sta,
			 struct mt7996_vif_link *link, unsigned int link_id)
{
	struct ieee80211_sta *sta = link_sta->sta;
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_phy *phy = link->phy;
	struct mt7996_sta_link *msta_link;
	int idx, i;

	idx = mt76_wcid_alloc(dev->mt76.wcid_mask, MT7996_WTBL_STA);
	if (idx < 0)
		return -ENOSPC;

	if (msta->deflink_id == IEEE80211_LINK_UNSPECIFIED) {
		msta_link = &msta->deflink;
		msta->deflink_id = link_id;
		msta->seclink_id = msta->deflink_id;
		mt7996_sta_init_txq_wcid(sta, idx);
	} else {
		msta_link = kzalloc(sizeof(*msta_link), GFP_KERNEL);
		if (!msta_link)
			return -ENOMEM;

		if (msta->seclink_id == msta->deflink_id &&
		    (sta->valid_links & ~BIT(msta->deflink_id)))
			msta->seclink_id = __ffs(sta->valid_links &
						 ~BIT(msta->deflink_id));
	}

	INIT_LIST_HEAD(&msta_link->rc_list);
	INIT_LIST_HEAD(&msta_link->wcid.poll_list);
	msta_link->sta = msta;
	msta_link->wcid.sta = 1;
	msta_link->wcid.idx = idx;
	msta_link->wcid.link_id = link_id;
	msta_link->wcid.link_valid = !!sta->valid_links;
	msta_link->wcid.def_wcid = &msta->deflink.wcid;
	msta->valid_links |= BIT(link_id);

	ewma_avg_signal_init(&msta_link->avg_ack_signal);
	ewma_signal_init(&msta_link->wcid.rssi);
	for (i = 0; i < IEEE80211_MAX_CHAINS; ++i)
		ewma_avg_signal_init(msta_link->chain_signal_avg + i);
	ewma_avg_signal_init(&msta_link->signal_avg);

	rcu_assign_pointer(msta->link[link_id], msta_link);

	mt7996_mac_wtbl_update(dev, idx, MT_WTBL_UPDATE_ADM_COUNT_CLEAR);

	rcu_assign_pointer(dev->mt76.wcid[idx], &msta_link->wcid);
	mt76_wcid_init(&msta_link->wcid, phy->mt76->band_idx);

#ifdef CONFIG_MTK_VENDOR
	mt7996_vendor_amnt_sta_remove(link->phy, link_sta);
#endif

	mt76_sta_dbg(link_conf->vif, "STA %pM, wcid=%u, link_id=%u (%pM), pri_link=%u, sec_link=%u",
		     sta->addr, msta_link->wcid.idx, link_id,
		     link_sta->addr, msta->deflink_id, msta->seclink_id);

	return 0;
}

void mt7996_mac_sta_deinit_link(struct mt7996_dev *dev,
				struct mt7996_sta_link *msta_link)
{
	spin_lock_bh(&dev->mt76.sta_poll_lock);
	if (!list_empty(&msta_link->wcid.poll_list))
		list_del_init(&msta_link->wcid.poll_list);
	if (!list_empty(&msta_link->rc_list))
		list_del_init(&msta_link->rc_list);
	spin_unlock_bh(&dev->mt76.sta_poll_lock);

	rcu_assign_pointer(dev->mt76.wcid[msta_link->wcid.idx], NULL);
	mt76_wcid_cleanup(&dev->mt76, &msta_link->wcid);
	mt76_wcid_mask_clear(dev->mt76.wcid_mask, msta_link->wcid.idx);
}

static void
mt7996_mac_sta_remove_links(struct mt7996_dev *dev, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, unsigned long links)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt76_dev *mdev = &dev->mt76;
	unsigned int link_id;

	mt76_sta_dbg(vif, "removed_links=0x%lx", links);
	for_each_set_bit(link_id, &links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt7996_sta_link *msta_link = NULL;
		struct mt7996_phy *phy;

		msta_link = rcu_replace_pointer(msta->link[link_id], msta_link,
						lockdep_is_held(&mdev->mutex));
		if (!msta_link)
			continue;

		mt7996_mac_wtbl_update(dev, msta_link->wcid.idx,
				       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
		mt7996_mac_sta_deinit_link(dev, msta_link);

		phy = __mt7996_phy(dev, msta_link->wcid.phy_idx);
		if (phy)
			phy->mt76->num_sta--;

		if (msta->deflink_id == link_id) {
			if (msta->seclink_id == msta->deflink_id) {
				/* no secondary link available */
				msta->deflink_id = IEEE80211_LINK_UNSPECIFIED;
				msta->seclink_id = msta->deflink_id;
			} else {
				struct mt7996_sta_link *msta_seclink;

				/* switch to the secondary link */
				msta->deflink_id = msta->seclink_id;
				msta_seclink = mt76_dereference(
						msta->link[msta->seclink_id],
						mdev);
				if (msta_seclink)
					mt7996_sta_init_txq_wcid(sta,
						msta_seclink->wcid.idx);
			}
		} else if (msta->seclink_id == link_id) {
			msta->seclink_id = msta->deflink_id;
		}

		msta_link->sta->valid_links &= ~BIT(link_id);
		if (msta_link != &msta->deflink)
			kfree_rcu(msta_link, rcu_head);
	}
}

static int
mt7996_mac_sta_add_links(struct mt7996_dev *dev, struct ieee80211_vif *vif,
			 struct ieee80211_sta *sta, unsigned long new_links)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	unsigned int link_id;
	int err = 0;

	for_each_set_bit(link_id, &new_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct ieee80211_bss_conf *link_conf;
		struct ieee80211_link_sta *link_sta;
		struct mt7996_vif_link *link;
		struct mt76_phy *mphy;

		if (rcu_access_pointer(msta->link[link_id]))
			continue;

		link_conf = link_conf_dereference_protected(vif, link_id);
		if (!link_conf) {
			err = -EINVAL;
			goto error_unlink;
		}

		link = mt7996_vif_link(dev, vif, link_id);
		if (!link) {
			err = -EINVAL;
			goto error_unlink;
		}

		link_sta = link_sta_dereference_protected(sta, link_id);
		if (!link_sta) {
			err = -EINVAL;
			goto error_unlink;
		}

		mphy = mt76_vif_link_phy(&dev->mt76, &link->mt76);
		if (!mphy) {
			err = -EINVAL;
			goto error_unlink;
		}

		err = mt7996_mac_sta_init_link(dev, link_conf, link_sta, link,
					       link_id);
		if (err)
			goto error_unlink;

		mphy->num_sta++;
	}

	return 0;

error_unlink:
	mt7996_mac_sta_remove_links(dev, vif, sta, new_links);

	return err;
}

static int
mt7996_mac_sta_change_links(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, u16 old_links,
			    u16 new_links)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	unsigned long add = new_links & ~old_links;
	unsigned long rem = old_links & ~new_links;
	int ret, link_id;
	struct mt7996_sta_link *msta_link;
	int i;

	mt76_sta_dbg(vif, "STA %pM old=0x%x, new=0x%x", sta->addr, old_links, new_links);

	mutex_lock(&dev->mt76.mutex);

	for_each_set_bit(link_id, &rem, IEEE80211_MLD_MAX_NUM_LINKS) {
		msta_link = mt76_dereference(msta->link[link_id], &dev->mt76);
		if (!msta_link)
			continue;

		msta_link->wcid.sta_disabled = 1;

		if (link_id != msta->deflink_id)
			continue;

		msta->deflink_id = msta->seclink_id;
		msta_link = mt76_dereference(msta->link[msta->deflink_id], &dev->mt76);

		for (i = 0; i < ARRAY_SIZE(sta->txq); i++) {
			struct mt76_txq *mtxq;
			if (!sta->txq[i])
				continue;
			mtxq = (struct mt76_txq *)sta->txq[i]->drv_priv;
			mtxq->wcid = msta_link->wcid.idx;
		}
	}

	msta_link = mt76_dereference(msta->link[msta->deflink_id], &dev->mt76);
	for (i = 0; i < ARRAY_SIZE(sta->txq); i++) {
		struct mt76_txq *mtxq;

		if (!sta->txq[i])
			continue;
		mtxq = (struct mt76_txq *)sta->txq[i]->drv_priv;
		mtxq->wcid = msta_link->wcid.idx;
	}

	ret = mt7996_mac_sta_add_links(dev, vif, sta, add);

	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static int
mt7996_mac_sta_add(struct mt7996_dev *dev, struct ieee80211_vif *vif,
		   struct ieee80211_sta *sta)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	unsigned long links = sta->valid_links ? sta->valid_links : BIT(0);
	int err;

	mutex_lock(&dev->mt76.mutex);

	msta->deflink_id = IEEE80211_LINK_UNSPECIFIED;
	msta->seclink_id = IEEE80211_LINK_UNSPECIFIED;
	msta->vif = mvif;
	err = mt7996_mac_sta_add_links(dev, vif, sta, links);

	mutex_unlock(&dev->mt76.mutex);

	return err;
}

static void mt7996_check_mld_sta(void *data, struct ieee80211_sta *sta)
{
	bool *has_mld_sta = data;

	if (*has_mld_sta)
		return;
	if (sta->mlo)
		*has_mld_sta = true;
}

static int
mt7996_mac_sta_event(struct mt7996_dev *dev, struct ieee80211_vif *vif,
		     struct ieee80211_sta *sta, enum mt76_sta_event ev)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	unsigned long links = sta->valid_links | msta->valid_links;
	unsigned long valid_links = msta->valid_links;
	unsigned int link_id;
	int err = 0;
	bool has_mld_sta = false;

	mutex_lock(&dev->mt76.mutex);

	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct ieee80211_link_sta *link_sta;
		struct ieee80211_bss_conf *link_conf;
		struct mt7996_sta_link *msta_link;
		struct mt7996_vif_link *link;
		int i;

		link_conf = link_conf_dereference_protected(vif, link_id);
		if (!link_conf && ev != MT76_STA_EVENT_DISASSOC)
			continue;

		link_sta = link_sta_dereference_protected(sta, link_id);
		if (!link_sta && ev != MT76_STA_EVENT_DISASSOC)
			continue;

		link = mt7996_vif_link(dev, vif, link_id);
		if (!link)
			continue;

		msta_link = mt7996_sta_link_protected(dev, msta, link_id);
		if (!msta_link)
			continue;

		switch (ev) {
		case MT76_STA_EVENT_ASSOC:
			err = mt7996_mcu_add_sta(dev, vif, link_conf, link_sta,
						 link, msta_link,
						 CONN_STATE_CONNECT, true);
			if (err)
				goto unlock;

			err = mt7996_mcu_add_rate_ctrl(dev, msta_link->sta, vif,
						       link_id, false);
			if (err)
				goto unlock;

			if (link_sta->eht_cap.has_eht &&
			    vif->type == NL80211_IFTYPE_STATION) {
				err = mt7996_mcu_set_pp_sta_dscb(link->phy, &link_conf->chanreq.oper, link->mt76.omac_idx);
				if (err)
					return err;
			}
			if (sta->mlo)
				mt7996_set_pse_drop(dev, 0);

#ifdef CONFIG_MTK_VENDOR
			if (hweight16(msta->valid_links) == 1 && link->phy &&
			    link->phy->muru_onoff & MUMIMO_DL_CERT)
				mt7996_mcu_set_mimo(link->phy);
#endif

			msta_link->wcid.sta_disabled = 0;
			msta_link->wcid.tx_info |= MT_WCID_TX_INFO_SET;
			break;
		case MT76_STA_EVENT_AUTHORIZE:
			err = mt7996_mcu_add_sta(dev, vif, link_conf, link_sta,
						 link, msta_link,
						 CONN_STATE_PORT_SECURE, false);
			if (err)
				goto unlock;
			break;
		case MT76_STA_EVENT_DISASSOC:
			if (links == msta->valid_links) {
				ieee80211_iterate_stations_atomic(dev->mt76.hw, mt7996_check_mld_sta,
								&has_mld_sta);
				if (!has_mld_sta)
					mt7996_set_pse_drop(dev, 1);
			}

			for (i = 0; i < ARRAY_SIZE(msta_link->twt.flow); i++)
				mt7996_mac_twt_teardown_flow(dev, link, msta_link,
					     (struct ieee80211_twt_teardown *) &i);

			if (!sta->mlo)
				mt7996_mcu_add_sta(dev, vif, link_conf, link_sta,
						   link, msta_link,
						   CONN_STATE_DISCONNECT, false);
			else if (sta->mlo && links == BIT(link_id)) /* last link */
				mt7996_mcu_teardown_mld_sta(dev, link,
							    msta_link);
			msta_link->wcid.sta_disabled = 1;
			msta_link->wcid.sta = 0;
			links = links & ~BIT(link_id);
			break;
		}
	}
unlock:
	mutex_unlock(&dev->mt76.mutex);

	return err;
}

static void
mt7996_mac_sta_remove(struct mt7996_dev *dev, struct ieee80211_vif *vif,
		      struct ieee80211_sta *sta)
{
	unsigned long links = sta->valid_links ? sta->valid_links : BIT(0);
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;

	cancel_delayed_work(&msta->vif->beacon_mon_work);

	mutex_lock(&dev->mt76.mutex);
#ifdef CONFIG_MTK_VENDOR
	mt7996_mcu_set_csi(&dev->phy, 2, 8, 1, 0, sta->addr);
#endif
	mt7996_mac_sta_remove_links(dev, vif, sta, links | msta->valid_links);
	mutex_unlock(&dev->mt76.mutex);
}

static void
mt7996_set_active_links(struct ieee80211_vif *vif)
{
	u16 active_links;

	if (vif->type != NL80211_IFTYPE_STATION)
		return;

	if (!ieee80211_vif_is_mld(vif))
		return;

	active_links = mt76_select_links(vif, MT7996_MAX_RADIOS);
	if (hweight16(active_links) < 2)
		return;

	ieee80211_set_active_links_async(vif, active_links);
}

static int
mt7996_sta_state(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		 struct ieee80211_sta *sta, enum ieee80211_sta_state old_state,
		 enum ieee80211_sta_state new_state)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	enum mt76_sta_event ev;

	if (old_state == IEEE80211_STA_NOTEXIST &&
	    new_state == IEEE80211_STA_NONE)
		return mt7996_mac_sta_add(dev, vif, sta);

	if (old_state == IEEE80211_STA_NONE &&
	    new_state == IEEE80211_STA_NOTEXIST)
		mt7996_mac_sta_remove(dev, vif, sta);

	if (old_state == IEEE80211_STA_AUTH &&
	    new_state == IEEE80211_STA_ASSOC) {
		mt7996_set_active_links(vif);
		ev = MT76_STA_EVENT_ASSOC;
	} else if (old_state == IEEE80211_STA_ASSOC &&
		   new_state == IEEE80211_STA_AUTHORIZED) {
		ev = MT76_STA_EVENT_AUTHORIZE;
	} else if (old_state == IEEE80211_STA_ASSOC &&
		   new_state == IEEE80211_STA_AUTH) {
		ev = MT76_STA_EVENT_DISASSOC;
	} else {
		return 0;
	}

	return mt7996_mac_sta_event(dev, vif, sta, ev);
}

static void mt7996_tx(struct ieee80211_hw *hw,
		      struct ieee80211_tx_control *control,
		      struct sk_buff *skb)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct ieee80211_sta *sta = control->sta;
	struct mt7996_sta *msta = sta ? (void *)sta->drv_priv : NULL;
	struct mt76_phy *mphy = hw->priv;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = info->control.vif;
	struct mt7996_vif *mvif = vif ? (void *)vif->drv_priv : NULL;
	struct mt76_wcid *wcid = &dev->mt76.global_wcid;
	u8 link_id = u32_get_bits(info->control.flags,
				  IEEE80211_TX_CTRL_MLO_LINK);

	rcu_read_lock();

	/* Use primary link_id if the value from mac80211 is set to
	 * IEEE80211_LINK_UNSPECIFIED.
	 */
	if (link_id == IEEE80211_LINK_UNSPECIFIED) {
		if (msta)
			link_id = msta->deflink_id;
		else if (mvif)
			link_id = mvif->mt76.deflink_id;
	}

	if (vif && ieee80211_vif_is_mld(vif)) {
		struct ieee80211_bss_conf *link_conf;

		if (msta) {
			struct ieee80211_link_sta *link_sta;

			link_sta = rcu_dereference(sta->link[link_id]);
			if (!link_sta)
				link_sta = rcu_dereference(sta->link[msta->deflink_id]);

			if (link_sta) {
				memcpy(hdr->addr1, link_sta->addr, ETH_ALEN);
				if (ether_addr_equal(sta->addr, hdr->addr3))
					memcpy(hdr->addr3, link_sta->addr, ETH_ALEN);
			}
		}

		link_conf = rcu_dereference(vif->link_conf[link_id]);
		if (link_conf) {
			memcpy(hdr->addr2, link_conf->addr, ETH_ALEN);
			if (ether_addr_equal(vif->addr, hdr->addr3))
				memcpy(hdr->addr3, link_conf->addr, ETH_ALEN);
		}
	}

	if (mvif) {
		struct mt76_vif_link *mlink;

		mlink = rcu_dereference(mvif->mt76.link[link_id]);
		if (mlink && mlink->wcid)
			wcid = mlink->wcid;

		if (mvif->mt76.roc_phy &&
		    (info->flags & IEEE80211_TX_CTL_TX_OFFCHAN)) {
			mphy = mvif->mt76.roc_phy;
			if (mphy->roc_link)
				wcid = mphy->roc_link->wcid;
		} else if (mlink) {
			mphy = mt76_vif_link_phy(&dev->mt76, mlink);
		}
	}

	if (!mphy) {
		ieee80211_free_txskb(hw, skb);
		goto unlock;
	}

	if (msta) {
		struct mt7996_sta_link *msta_link;

		msta_link = mt7996_sta_link(msta, link_id);
		if (msta_link)
			wcid = &msta_link->wcid;
	}
	mt76_tx(mphy, control->sta, wcid, skb);
unlock:
	rcu_read_unlock();
}

static int mt7996_set_rts_threshold(struct ieee80211_hw *hw, int radio_idx,
				    u32 val)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	int i, ret = 0;

	mutex_lock(&dev->mt76.mutex);

	for (i = 0; i < hw->wiphy->n_radio; i++) {
		struct mt7996_phy *phy = dev->radio_phy[i];

		ret = mt7996_mcu_set_rts_thresh(phy, val);
		if (ret)
			break;
	}

	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static int
mt7996_ampdu_action(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    struct ieee80211_ampdu_params *params)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct ieee80211_sta *sta = params->sta;
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct ieee80211_txq *txq = sta->txq[params->tid];
	u16 tid = params->tid;
	u16 ssn = params->ssn;
	struct mt76_txq *mtxq;
	int ret = 0;

	if (!txq)
		return -EINVAL;

	mtxq = (struct mt76_txq *)txq->drv_priv;

	mutex_lock(&dev->mt76.mutex);

	switch (params->action) {
	case IEEE80211_AMPDU_RX_START:
		if (msta->stop_rx_ba_in_progress) {
			ret = -EAGAIN;
			break;
		}
		/* Since packets belonging to the same TID can be split over
		 * multiple links, store the AMPDU state for reordering in the
		 * primary link
		 */
		mt76_rx_aggr_start(&dev->mt76, &msta->deflink.wcid, tid,
				   ssn, params->buf_size);
		ret = mt7996_mcu_add_rx_ba(dev, params, vif, true);
		break;
	case IEEE80211_AMPDU_RX_STOP:
		mt76_rx_aggr_stop(&dev->mt76, &msta->deflink.wcid, tid);
		ret = mt7996_mcu_add_rx_ba(dev, params, vif, false);
		break;
	case IEEE80211_AMPDU_TX_OPERATIONAL:
		mtxq->aggr = true;
		mtxq->send_bar = false;
		ret = mt7996_mcu_add_tx_ba(dev, params, vif, true);
		break;
	case IEEE80211_AMPDU_TX_STOP_FLUSH:
	case IEEE80211_AMPDU_TX_STOP_FLUSH_CONT:
		mtxq->aggr = false;
		clear_bit(tid, &msta->deflink.wcid.ampdu_state);
		ret = mt7996_mcu_add_tx_ba(dev, params, vif, false);
		break;
	case IEEE80211_AMPDU_TX_START:
		set_bit(tid, &msta->deflink.wcid.ampdu_state);
		ret = IEEE80211_AMPDU_TX_START_IMMEDIATE;
		break;
	case IEEE80211_AMPDU_TX_STOP_CONT:
		mtxq->aggr = false;
		clear_bit(tid, &msta->deflink.wcid.ampdu_state);
		ret = mt7996_mcu_add_tx_ba(dev, params, vif, false);
		ieee80211_stop_tx_ba_cb_irqsafe(vif, sta->addr, tid);
		break;
	}

	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static int
mt7996_get_stats(struct ieee80211_hw *hw,
		 struct ieee80211_low_level_stats *stats)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	int i;

	mutex_lock(&dev->mt76.mutex);

	memset(stats, 0, sizeof(*stats));
	for (i = 0; i < hw->wiphy->n_radio; i++) {
		struct mt7996_phy *phy = dev->radio_phy[i];
		struct mt76_mib_stats *mib = &phy->mib;

		stats->dot11RTSSuccessCount += mib->rts_cnt;
		stats->dot11RTSFailureCount += mib->rts_retries_cnt;
		stats->dot11FCSErrorCount += mib->fcs_err_cnt;
		stats->dot11ACKFailureCount += mib->ack_fail_cnt;
	}

	mutex_unlock(&dev->mt76.mutex);

	return 0;
}

u64 __mt7996_get_tsf(struct ieee80211_hw *hw, struct mt7996_vif_link *link)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy = link->phy;
	union {
		u64 t64;
		u32 t32[2];
	} tsf;
	u16 n;

	if (!phy)
		return 0;

	lockdep_assert_held(&dev->mt76.mutex);

	n = link->mt76.omac_idx > HW_BSSID_MAX ? HW_BSSID_0
					       : link->mt76.omac_idx;
	/* TSF software read */
	mt76_rmw(dev, MT_LPON_TCR(phy->mt76->band_idx, n), MT_LPON_TCR_SW_MODE,
		 MT_LPON_TCR_SW_READ);
	tsf.t32[0] = mt76_rr(dev, MT_LPON_UTTR0(phy->mt76->band_idx));
	tsf.t32[1] = mt76_rr(dev, MT_LPON_UTTR1(phy->mt76->band_idx));

	return tsf.t64;
}

static u64
mt7996_get_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif_link *mconf;
	u64 ret = -1ULL;
	int i;

	mutex_lock(&dev->mt76.mutex);
	/* FIXME workaround for preventing kernel crash during ACS
	 * (i.e., link 0 is doing ACS while link 1 queries tsf)
	 */
	for (i = 0; i < IEEE80211_MLD_MAX_NUM_LINKS; i++) {
		mconf = mt7996_vif_link(dev, vif, i);
		if (mconf)
			break;
	}
	if (mconf)
		ret = __mt7996_get_tsf(hw, mconf);
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static void
mt7996_set_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
	       u64 timestamp)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif_link *link;
	struct mt7996_phy *phy;
	union {
		u64 t64;
		u32 t32[2];
	} tsf = { .t64 = timestamp, };
	u16 n;

	mutex_lock(&dev->mt76.mutex);

	link = mt7996_vif_link(dev, vif, mvif->mt76.deflink_id);
	if (!link)
		goto unlock;

	n = link->mt76.omac_idx > HW_BSSID_MAX ? HW_BSSID_0
					       : link->mt76.omac_idx;
	phy = link->phy;
	if (!phy)
		goto unlock;

	mt76_wr(dev, MT_LPON_UTTR0(phy->mt76->band_idx), tsf.t32[0]);
	mt76_wr(dev, MT_LPON_UTTR1(phy->mt76->band_idx), tsf.t32[1]);
	/* TSF software overwrite */
	mt76_rmw(dev, MT_LPON_TCR(phy->mt76->band_idx, n), MT_LPON_TCR_SW_MODE,
		 MT_LPON_TCR_SW_WRITE);

unlock:
	mutex_unlock(&dev->mt76.mutex);
}

static void
mt7996_offset_tsf(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  s64 timestamp)
{
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif_link *link;
	struct mt7996_phy *phy;
	union {
		u64 t64;
		u32 t32[2];
	} tsf = { .t64 = timestamp, };
	u16 n;

	mutex_lock(&dev->mt76.mutex);

	link = mt7996_vif_link(dev, vif, mvif->mt76.deflink_id);
	if (!link)
		goto unlock;

	phy = link->phy;
	if (!phy)
		goto unlock;

	n = link->mt76.omac_idx > HW_BSSID_MAX ? HW_BSSID_0
					       : link->mt76.omac_idx;
	mt76_wr(dev, MT_LPON_UTTR0(phy->mt76->band_idx), tsf.t32[0]);
	mt76_wr(dev, MT_LPON_UTTR1(phy->mt76->band_idx), tsf.t32[1]);
	/* TSF software adjust*/
	mt76_rmw(dev, MT_LPON_TCR(phy->mt76->band_idx, n), MT_LPON_TCR_SW_MODE,
		 MT_LPON_TCR_SW_ADJUST);

unlock:
	mutex_unlock(&dev->mt76.mutex);
}

static void
mt7996_set_coverage_class(struct ieee80211_hw *hw, int radio_idx,
			  s16 coverage_class)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy;

	mutex_lock(&dev->mt76.mutex);
	mt7996_for_each_phy(dev, phy) {
		phy->coverage_class = max_t(s16, coverage_class, 0);
		mt7996_mac_set_coverage_class(phy);
	}
	mutex_unlock(&dev->mt76.mutex);
}

static int
mt7996_set_antenna(struct ieee80211_hw *hw, int radio_idx,
		   u32 tx_ant, u32 rx_ant)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy;
	struct wiphy_radio *radio;
	int i;

	if (tx_ant != rx_ant)
		return -EINVAL;

	for (i = 0; i < hw->wiphy->n_radio; i++) {
		phy = dev->radio_phy[i];
		radio = &dev->radios[i];

		if (!(tx_ant & radio->antenna_mask))
			return -EINVAL;
	}

	mutex_lock(&dev->mt76.mutex);

	for (i = 0; i < hw->wiphy->n_radio; i++) {
		int shift;

		phy = dev->radio_phy[i];
		radio = &dev->radios[i];
		shift = dev->mt76.chainshift[phy->mt76->band_idx];

		phy->mt76->chainmask = (tx_ant & radio->antenna_mask) >> shift;
		phy->mt76->antenna_mask = phy->mt76->chainmask & phy->orig_antenna_mask;

		mt76_set_stream_caps(phy->mt76, true);
		mt7996_set_stream_vht_txbf_caps(phy);
		mt7996_set_stream_he_eht_caps(phy);
	}

	mutex_unlock(&dev->mt76.mutex);

	return 0;
}

static void mt7996_sta_statistics(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_sta *sta,
				  struct station_info *sinfo)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_sta_link *msta_link = &msta->deflink;
	struct rate_info *txrate = &msta_link->wcid.rate;

	if (txrate->legacy || txrate->flags) {
		if (txrate->legacy) {
			sinfo->txrate.legacy = txrate->legacy;
		} else {
			sinfo->txrate.mcs = txrate->mcs;
			sinfo->txrate.nss = txrate->nss;
			sinfo->txrate.bw = txrate->bw;
			sinfo->txrate.he_gi = txrate->he_gi;
			sinfo->txrate.he_dcm = txrate->he_dcm;
			sinfo->txrate.he_ru_alloc = txrate->he_ru_alloc;
			sinfo->txrate.eht_gi = txrate->eht_gi;
		}
		sinfo->txrate.flags = txrate->flags;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);
	}
	sinfo->txrate.flags = txrate->flags;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);

	sinfo->tx_failed = msta_link->wcid.stats.tx_failed;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_FAILED);

	sinfo->tx_retries = msta_link->wcid.stats.tx_retries;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_RETRIES);

	sinfo->ack_signal = (s8)msta_link->ack_signal;
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_ACK_SIGNAL);

	sinfo->avg_ack_signal =
		-(s8)ewma_avg_signal_read(&msta_link->avg_ack_signal);
	sinfo->filled |= BIT_ULL(NL80211_STA_INFO_ACK_SIGNAL_AVG);

	if (mtk_wed_device_active(&dev->mt76.mmio.wed)) {
		sinfo->tx_bytes = msta_link->wcid.stats.tx_bytes;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BYTES64);

		sinfo->rx_bytes = msta_link->wcid.stats.rx_bytes;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_BYTES64);

		sinfo->tx_packets = msta_link->wcid.stats.tx_packets;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_PACKETS);

		sinfo->rx_packets = msta_link->wcid.stats.rx_packets;
		sinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_PACKETS);
	}
}

static void mt7996_link_sta_statistics(struct ieee80211_hw *hw,
				       struct ieee80211_vif *vif,
				       struct ieee80211_link_sta *link_sta,
				       struct link_station_info *link_sinfo)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_sta *msta = (struct mt7996_sta *)link_sta->sta->drv_priv;
	struct mt7996_sta_link *msta_link;
	struct mt7996_vif_link *mconf;
	struct mt76_sta_stats *stats;
	u8 link_id = link_sta->link_id;
	int i;

	mutex_lock(&dev->mt76.mutex);
	msta_link = mt76_dereference(msta->link[link_id], &dev->mt76);
	if (!msta_link)
		goto out;
	stats = &msta_link->wcid.stats;

	mconf = mt7996_vif_link(dev, vif, link_id);
	if (!mconf)
		goto out;

	link_sinfo->signal = (s8)msta_link->signal;
	link_sinfo->filled |= BIT_ULL(NL80211_STA_INFO_SIGNAL);

	link_sinfo->chains = mt7996_rx_chainmask(mconf->phy);
	memcpy(link_sinfo->chain_signal, msta_link->chain_signal, IEEE80211_MAX_CHAINS);
	link_sinfo->filled |= BIT_ULL(NL80211_STA_INFO_CHAIN_SIGNAL);

	link_sinfo->signal_avg = -(s8)ewma_avg_signal_read(&msta_link->signal_avg);
	link_sinfo->filled |= BIT_ULL(NL80211_STA_INFO_SIGNAL_AVG);

	for (i = 0; i < IEEE80211_MAX_CHAINS; ++i)
		link_sinfo->chain_signal_avg[i] = -(s8)ewma_avg_signal_read(msta_link->chain_signal_avg + i);
	link_sinfo->filled |= BIT_ULL(NL80211_STA_INFO_CHAIN_SIGNAL_AVG);

	link_sinfo->ack_signal = (s8)msta_link->ack_signal;
	link_sinfo->filled |= BIT_ULL(NL80211_STA_INFO_ACK_SIGNAL);

	link_sinfo->avg_ack_signal = -(s8)ewma_avg_signal_read(&msta_link->avg_ack_signal);
	link_sinfo->filled |= BIT_ULL(NL80211_STA_INFO_ACK_SIGNAL_AVG);

	link_sinfo->txrate = msta_link->wcid.rate;
	link_sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BITRATE);

	link_sinfo->rxrate = msta_link->wcid.rx_rate;
	link_sinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_BITRATE);

	link_sinfo->tx_bytes = stats->tx_bytes;
	link_sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_BYTES64);

	link_sinfo->rx_bytes = stats->rx_bytes;
	link_sinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_BYTES64);

	link_sinfo->tx_failed = stats->tx_failed;
	link_sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_FAILED);

	link_sinfo->tx_retries = stats->tx_retries;
	link_sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_RETRIES);

	link_sinfo->rx_mpdu_count = stats->rx_mpdus;
	link_sinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_MPDUS);

	link_sinfo->fcs_err_count = stats->rx_fcs_err;
	link_sinfo->filled |= BIT_ULL(NL80211_STA_INFO_FCS_ERROR_COUNT);

	link_sinfo->tx_duration = stats->tx_airtime;
	link_sinfo->filled |= BIT_ULL(NL80211_STA_INFO_TX_DURATION);

	link_sinfo->rx_duration = stats->rx_airtime;
	link_sinfo->filled |= BIT_ULL(NL80211_STA_INFO_RX_DURATION);
out:
	mutex_unlock(&dev->mt76.mutex);
}

static void mt7996_link_rate_ctrl_update(void *data,
					 struct mt7996_sta_link *msta_link)
{
	struct mt7996_sta *msta = msta_link->sta;
	struct mt7996_dev *dev = msta->vif->deflink.phy->dev;
	u32 *changed = data;

	spin_lock_bh(&dev->mt76.sta_poll_lock);

	msta_link->changed |= *changed;
	if (list_empty(&msta_link->rc_list))
		list_add_tail(&msta_link->rc_list, &dev->sta_rc_list);

	spin_unlock_bh(&dev->mt76.sta_poll_lock);
}

static void mt7996_link_sta_rc_update(struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      struct ieee80211_link_sta *link_sta,
				      u32 changed)
{
	struct ieee80211_sta *sta = link_sta->sta;
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_sta_link *msta_link;

	rcu_read_lock();

	msta_link = mt7996_sta_link(msta, link_sta->link_id);
	if (msta_link) {
		struct mt7996_dev *dev = mt7996_hw_dev(hw);

		mt7996_link_rate_ctrl_update(&changed, msta_link);
		ieee80211_queue_work(hw, &dev->rc_work);
	}

	rcu_read_unlock();
}

static void mt7996_sta_rate_ctrl_update(void *data, struct ieee80211_sta *sta)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_sta_link *msta_link;
	struct mt7996_sta_rc_work_data *wd = data;

	msta_link = mt7996_sta_link(msta, wd->link_id);
	if (msta_link)
		mt7996_link_rate_ctrl_update(&wd->changed, msta_link);
}

static int
mt7996_set_bitrate_mask(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			const struct cfg80211_bitrate_mask *mask,
			unsigned int link_id)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif_link *link;
	struct mt7996_sta_rc_work_data data = {
		.link_id = link_id,
		.changed = IEEE80211_RC_SUPP_RATES_CHANGED,
	};

	mutex_lock(&dev->mt76.mutex);
	link = mt7996_vif_link(dev, vif, link_id);
	if (!link) {
		mutex_unlock(&dev->mt76.mutex);
		return -EINVAL;
	}

	link->bitrate_mask = *mask;
	mutex_unlock(&dev->mt76.mutex);

	/* if multiple rates across different preambles are given we can
	 * reconfigure this info with all peers using sta_rec command with
	 * the below exception cases.
	 * - single rate : if a rate is passed along with different preambles,
	 * we select the highest one as fixed rate. i.e VHT MCS for VHT peers.
	 * - multiple rates: if it's not in range format i.e 0-{7,8,9} for VHT
	 * then multiple MCS setting (MCS 4,5,6) is not supported.
	 */
	ieee80211_iterate_stations_atomic(hw, mt7996_sta_rate_ctrl_update,
					  &data);
	ieee80211_queue_work(hw, &dev->rc_work);

	return 0;
}

static void mt7996_sta_set_4addr(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta,
				 bool enabled)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	unsigned long valid_links = vif->valid_links ?: BIT(0);
	unsigned int link_id;

	mutex_lock(&dev->mt76.mutex);

	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt7996_sta_link *msta_link;
		struct mt7996_vif_link *link;

		link = mt7996_vif_link(dev, vif, link_id);
		if (!link)
			continue;

		msta_link = mt7996_sta_link_protected(dev, msta, link_id);
		if (!msta_link)
			continue;

		if (enabled)
			set_bit(MT_WCID_FLAG_4ADDR, &msta_link->wcid.flags);
		else
			clear_bit(MT_WCID_FLAG_4ADDR, &msta_link->wcid.flags);

		if (!msta_link->wcid.sta)
			continue;

		mt7996_mcu_wtbl_update_hdr_trans(dev, vif, link, msta_link);

		if (msta->deflink_id != link_id && is_mt7996(&dev->mt76))
			mt7996_mcu_ps_leave(dev, link, msta_link);
	}

	mutex_unlock(&dev->mt76.mutex);
}

static void mt7996_sta_set_decap_offload(struct ieee80211_hw *hw,
					 struct ieee80211_vif *vif,
					 struct ieee80211_sta *sta,
					 bool enabled)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	unsigned long valid_links = vif->valid_links ?: BIT(0);
	unsigned int link_id;

	mutex_lock(&dev->mt76.mutex);

	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt7996_sta_link *msta_link;
		struct mt7996_vif_link *link;

		link = mt7996_vif_link(dev, vif, link_id);
		if (!link)
			continue;

		msta_link = mt7996_sta_link_protected(dev, msta, link_id);
		if (!msta_link)
			continue;

		if (enabled)
			set_bit(MT_WCID_FLAG_HDR_TRANS,
				&msta_link->wcid.flags);
		else
			clear_bit(MT_WCID_FLAG_HDR_TRANS,
				  &msta_link->wcid.flags);

		if (!msta_link->wcid.sta)
			continue;

		mt7996_mcu_wtbl_update_hdr_trans(dev, vif, link, msta_link);
	}

	mutex_unlock(&dev->mt76.mutex);
}

static const char mt7996_gstrings_stats[][ETH_GSTRING_LEN] = {
	"tx_ampdu_cnt",
	"tx_stop_q_empty_cnt",
	"tx_mpdu_attempts",
	"tx_mpdu_success",
	"tx_rwp_fail_cnt",
	"tx_rwp_need_cnt",
	"tx_pkt_ebf_cnt",
	"tx_pkt_ibf_cnt",
	"tx_ampdu_len:0-1",
	"tx_ampdu_len:2-10",
	"tx_ampdu_len:11-19",
	"tx_ampdu_len:20-28",
	"tx_ampdu_len:29-37",
	"tx_ampdu_len:38-46",
	"tx_ampdu_len:47-55",
	"tx_ampdu_len:56-79",
	"tx_ampdu_len:80-103",
	"tx_ampdu_len:104-127",
	"tx_ampdu_len:128-151",
	"tx_ampdu_len:152-175",
	"tx_ampdu_len:176-199",
	"tx_ampdu_len:200-223",
	"tx_ampdu_len:224-247",
	"ba_miss_count",
	"tx_beamformer_ppdu_iBF",
	"tx_beamformer_ppdu_eBF",
	"tx_beamformer_rx_feedback_all",
	"tx_beamformer_rx_feedback_he",
	"tx_beamformer_rx_feedback_vht",
	"tx_beamformer_rx_feedback_ht",
	"tx_beamformer_rx_feedback_bw", /* zero based idx: 20, 40, 80, 160 */
	"tx_beamformer_rx_feedback_nc",
	"tx_beamformer_rx_feedback_nr",
	"tx_beamformee_ok_feedback_pkts",
	"tx_beamformee_feedback_trig",
	"tx_mu_beamforming",
	"tx_mu_mpdu",
	"tx_mu_successful_mpdu",
	"tx_su_successful_mpdu",
	"tx_msdu_pack_1",
	"tx_msdu_pack_2",
	"tx_msdu_pack_3",
	"tx_msdu_pack_4",
	"tx_msdu_pack_5",
	"tx_msdu_pack_6",
	"tx_msdu_pack_7",
	"tx_msdu_pack_8",

	/* rx counters */
	"rx_fifo_full_cnt",
	"rx_mpdu_cnt",
	"channel_idle_cnt",
	"rx_vector_mismatch_cnt",
	"rx_delimiter_fail_cnt",
	"rx_len_mismatch_cnt",
	"rx_ampdu_cnt",
	"rx_ampdu_bytes_cnt",
	"rx_ampdu_valid_subframe_cnt",
	"rx_ampdu_valid_subframe_b_cnt",
	"rx_pfdrop_cnt",
	"rx_vec_queue_overflow_drop_cnt",
	"rx_ba_cnt",

	/* per vif counters */
	"v_tx_mode_cck",
	"v_tx_mode_ofdm",
	"v_tx_mode_ht",
	"v_tx_mode_ht_gf",
	"v_tx_mode_vht",
	"v_tx_mode_he_su",
	"v_tx_mode_he_ext_su",
	"v_tx_mode_he_tb",
	"v_tx_mode_he_mu",
	"v_tx_mode_eht_su",
	"v_tx_mode_eht_trig",
	"v_tx_mode_eht_mu",
	"v_tx_bw_20",
	"v_tx_bw_40",
	"v_tx_bw_80",
	"v_tx_bw_160",
	"v_tx_bw_320",
	"v_tx_mcs_0",
	"v_tx_mcs_1",
	"v_tx_mcs_2",
	"v_tx_mcs_3",
	"v_tx_mcs_4",
	"v_tx_mcs_5",
	"v_tx_mcs_6",
	"v_tx_mcs_7",
	"v_tx_mcs_8",
	"v_tx_mcs_9",
	"v_tx_mcs_10",
	"v_tx_mcs_11",
	"v_tx_mcs_12",
	"v_tx_mcs_13",
	"v_tx_nss_1",
	"v_tx_nss_2",
	"v_tx_nss_3",
	"v_tx_nss_4",
};

#define MT7996_SSTATS_LEN ARRAY_SIZE(mt7996_gstrings_stats)

/* Ethtool related API */
static
void mt7996_get_et_strings(struct ieee80211_hw *hw,
			   struct ieee80211_vif *vif,
			   u32 sset, u8 *data)
{
	if (sset == ETH_SS_STATS)
		memcpy(data, mt7996_gstrings_stats,
		       sizeof(mt7996_gstrings_stats));
}

static
int mt7996_get_et_sset_count(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif, int sset)
{
	if (sset == ETH_SS_STATS)
		return MT7996_SSTATS_LEN;

	return 0;
}

static void mt7996_ethtool_worker(void *wi_data, struct ieee80211_sta *sta)
{
	struct mt76_ethtool_worker_info *wi = wi_data;
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_vif *mvif = msta->vif;
	struct mt7996_dev *dev = mvif->deflink.phy->dev;
	struct mt7996_sta_link *msta_link;
	struct mt7996_vif_link *mconf;
	struct ieee80211_vif *vif = container_of((void *)mvif, struct ieee80211_vif, drv_priv);

	msta_link = mt76_dereference(msta->link[msta->deflink_id], &dev->mt76);
	mconf = mt7996_vif_link(dev, vif, msta->deflink_id);

	if (mconf->mt76.idx != wi->idx)
		return;

	mt76_ethtool_worker(wi, &msta_link->wcid.stats, true);
}

static
void mt7996_get_et_stats(struct ieee80211_hw *hw,
			 struct ieee80211_vif *vif,
			 struct ethtool_stats *stats, u64 *data)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy = &dev->phy;
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_vif_link *mconf;
	struct mt76_mib_stats *mib = &phy->mib;
	struct mt76_ethtool_worker_info wi = {
		.data = data,
	};
	/* See mt7996_ampdu_stat_read_phy, etc */
	int i, ei = 0;

	if (!phy)
		return;

	mutex_lock(&dev->mt76.mutex);
	mconf = mt7996_vif_link(dev, vif, mvif->mt76.deflink_id);
	wi.idx = mconf->mt76.idx,

	mt7996_mac_update_stats(phy);

	data[ei++] = mib->tx_ampdu_cnt;
	data[ei++] = mib->tx_stop_q_empty_cnt;
	data[ei++] = mib->tx_mpdu_attempts_cnt;
	data[ei++] = mib->tx_mpdu_success_cnt;
	data[ei++] = mib->tx_rwp_fail_cnt;
	data[ei++] = mib->tx_rwp_need_cnt;
	data[ei++] = mib->tx_bf_ebf_ppdu_cnt;
	data[ei++] = mib->tx_bf_ibf_ppdu_cnt;

	/* Tx ampdu stat */
	for (i = 0; i < 15 /*ARRAY_SIZE(bound)*/; i++)
		data[ei++] = phy->mt76->aggr_stats[i];
	data[ei++] = phy->mib.ba_miss_cnt;

	/* Tx Beamformer monitor */
	data[ei++] = mib->tx_bf_ibf_ppdu_cnt;
	data[ei++] = mib->tx_bf_ebf_ppdu_cnt;

	/* Tx Beamformer Rx feedback monitor */
	data[ei++] = mib->tx_bf_rx_fb_all_cnt;
	data[ei++] = mib->tx_bf_rx_fb_he_cnt;
	data[ei++] = mib->tx_bf_rx_fb_vht_cnt;
	data[ei++] = mib->tx_bf_rx_fb_ht_cnt;

	data[ei++] = mib->tx_bf_rx_fb_bw;
	data[ei++] = mib->tx_bf_rx_fb_nc_cnt;
	data[ei++] = mib->tx_bf_rx_fb_nr_cnt;

	/* Tx Beamformee Rx NDPA & Tx feedback report */
	data[ei++] = mib->tx_bf_fb_cpl_cnt;
	data[ei++] = mib->tx_bf_fb_trig_cnt;

	/* Tx SU & MU counters */
	data[ei++] = mib->tx_mu_bf_cnt;
	data[ei++] = mib->tx_mu_mpdu_cnt;
	data[ei++] = mib->tx_mu_acked_mpdu_cnt;
	data[ei++] = mib->tx_su_acked_mpdu_cnt;

	/* Tx amsdu info (pack-count histogram) */
	for (i = 0; i < ARRAY_SIZE(mib->tx_amsdu); i++)
		data[ei++] = mib->tx_amsdu[i];

	/* rx counters */
	data[ei++] = mib->rx_fifo_full_cnt;
	data[ei++] = mib->rx_mpdu_cnt;
	data[ei++] = mib->channel_idle_cnt;
	data[ei++] = mib->rx_vector_mismatch_cnt;
	data[ei++] = mib->rx_delimiter_fail_cnt;
	data[ei++] = mib->rx_len_mismatch_cnt;
	data[ei++] = mib->rx_ampdu_cnt;
	data[ei++] = mib->rx_ampdu_bytes_cnt;
	data[ei++] = mib->rx_ampdu_valid_subframe_cnt;
	data[ei++] = mib->rx_ampdu_valid_subframe_bytes_cnt;
	data[ei++] = mib->rx_pfdrop_cnt;
	data[ei++] = mib->rx_vec_queue_overflow_drop_cnt;
	data[ei++] = mib->rx_ba_cnt;

	/* Add values for all stations owned by this vif */
	wi.initial_stat_idx = ei;
	ieee80211_iterate_stations_atomic(hw, mt7996_ethtool_worker, &wi);

	mutex_unlock(&dev->mt76.mutex);

	if (wi.sta_count == 0)
		return;

	ei += wi.worker_stat_count;
	if (ei != MT7996_SSTATS_LEN)
		dev_err(dev->mt76.dev, "ei: %d  MT7996_SSTATS_LEN: %d",
			ei, (int)MT7996_SSTATS_LEN);
}

static int
mt7996_twt_teardown_request(struct ieee80211_hw *hw,
			    struct ieee80211_sta *sta,
			    struct ieee80211_twt_teardown *td)
{
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_sta_link *msta_link;
	struct mt7996_vif_link *link;
	struct ieee80211_vif *vif;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	int ret = -EINVAL;

	vif = container_of((void *)msta->vif, struct ieee80211_vif, drv_priv);

	mutex_lock(&dev->mt76.mutex);
	msta_link = mt76_dereference(msta->link[0], &dev->mt76);
	link = mt7996_vif_link(dev, vif, 0);
	ret = mt7996_mac_twt_teardown_flow(dev, link, msta_link, td);
	mutex_unlock(&dev->mt76.mutex);

	if (ret)
		dev_err(dev->mt76.dev, "TWT teardown request failed ret=%d\n", ret);

	return ret;
}

static void
mt7996_background_radar_handle_7975_ifem(struct ieee80211_hw *hw,
					 struct cfg80211_chan_def *user_chandef,
					 struct cfg80211_chan_def *fw_chandef)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct cfg80211_chan_def *rdd2_chandef, *c = user_chandef;
	struct ieee80211_channel *first_chan;
	bool is_ifem_adie, expand = false;
	enum subchan_state {
		SUBCHAN_NOT_READY,
		SUBCHAN_PRIMARY_READY,
		SUBCHAN_SECONDARY_READY,
	};
	enum subchan_state state = SUBCHAN_NOT_READY;

	switch (mt76_chip(&dev->mt76)) {
	case MT7996_DEVICE_ID:
		is_ifem_adie = dev->var.fem == MT7996_FEM_INT &&
			       dev->var.type != MT7996_VAR_TYPE_233;
		break;
	case MT7992_DEVICE_ID:
		is_ifem_adie = dev->var.type == MT7992_VAR_TYPE_44 &&
			       dev->var.fem != MT7996_FEM_EXT;
		break;
	case MT7990_DEVICE_ID:
	default:
		return;
	}

	if (!user_chandef || (!is_ifem_adie && !bkg_max80))
		goto out;

	switch (user_chandef->width) {
	case NL80211_CHAN_WIDTH_160:
		first_chan = ieee80211_get_channel(hw->wiphy, user_chandef->center_freq1 - 70);
		/* the primary sub-channel does not require CAC */
		if (!(first_chan->flags & IEEE80211_CHAN_RADAR)) {
			dev->bg_nxt_freq = 0;
			c = fw_chandef;
			break;
		}

		/* the primary or secondary sub-channel has completed the CAC */
		rdd2_chandef = &dev->rdd2_chandef;
		if (rdd2_chandef->width == NL80211_CHAN_WIDTH_80 &&
		    (rdd2_chandef->center_freq1 == user_chandef->center_freq1 + 40 ||
		     rdd2_chandef->center_freq1 == user_chandef->center_freq1 - 40) &&
		    cfg80211_reg_can_beacon(hw->wiphy, rdd2_chandef, NL80211_IFTYPE_AP))
			state = rdd2_chandef->center_freq1 < user_chandef->center_freq1 ?
				SUBCHAN_PRIMARY_READY : SUBCHAN_SECONDARY_READY;

		dev->bg_nxt_freq = first_chan->center_freq;
		if (state == SUBCHAN_PRIMARY_READY)
			dev->bg_nxt_freq += 80;
		break;
	case NL80211_CHAN_WIDTH_80:
		if (!dev->bg_nxt_freq)
			goto out;
		break;
	default:
		goto out;
	}

	if (state || user_chandef->width == NL80211_CHAN_WIDTH_80) {
		c->chan = ieee80211_get_channel(hw->wiphy, dev->bg_nxt_freq);
		dev->bg_nxt_freq = 0;
		expand = true;
	} else {
		c->chan = ieee80211_get_channel(hw->wiphy, first_chan->center_freq + 80);
	}
	c->width = NL80211_CHAN_WIDTH_80;
	c->center_freq1 = c->chan->center_freq + 30;

	if (c == user_chandef)
		cfg80211_background_radar_update_channel(hw->wiphy, c, expand);
	return;
out:
	dev->bg_nxt_freq = 0;
}

static int
mt7996_set_radar_background(struct ieee80211_hw *hw,
			    struct cfg80211_chan_def *chandef)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy = mt7996_band_phy(dev, NL80211_BAND_5GHZ);
	int ret = -EINVAL;
	bool running;
	struct cfg80211_chan_def ifem_chandef = {};

	mutex_lock(&dev->mt76.mutex);

	if (dev->mt76.region == NL80211_DFS_UNSET)
		goto out;

	if (dev->rdd2_phy && dev->rdd2_phy != phy) {
		/* rdd2 is already locked */
		ret = -EBUSY;
		goto out;
	}

	mt7996_background_radar_handle_7975_ifem(hw, chandef, &ifem_chandef);

	/* rdd2 already configured on a radar channel */
	running = dev->rdd2_phy &&
		  cfg80211_chandef_valid(&dev->rdd2_chandef) &&
		  !!(dev->rdd2_chandef.chan->flags & IEEE80211_CHAN_RADAR);

	if (!chandef || running) {
		ret = mt7996_mcu_rdd_background_enable(phy, NULL);
		if (ret)
			goto out;

		if (!running)
			goto update_phy;
	}

	ret = mt7996_mcu_rdd_background_enable(phy,
					       ifem_chandef.chan ?
					       &ifem_chandef : chandef);
	if (ret)
		goto out;

update_phy:
	dev->rdd2_phy = chandef ? phy : NULL;
	if (chandef)
		dev->rdd2_chandef = *chandef;
out:
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static int
mt7996_net_fill_forward_path(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta,
			     struct net_device_path_ctx *ctx,
			     struct net_device_path *path)
{
#ifdef CONFIG_NET_MEDIATEK_SOC_WED
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_sta *msta = (struct mt7996_sta *)sta->drv_priv;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mtk_wed_device *wed = &dev->mt76.mmio.wed;
	struct mt7996_sta_link *msta_link;
	struct mt7996_vif_link *link;
	u8 tid = mvif->qos_map[path->mtk_wdma.tid >> 2];
	u8 link_id = (sta->mlo && tid % 2) ? msta->seclink_id : msta->deflink_id;

	link = mt7996_vif_link(dev, vif, msta->deflink_id);
	if (!link)
		return -EIO;

	msta_link = mt7996_sta_link(msta, link_id);
	if (!msta_link)
		return -EIO;

	if (!msta_link->wcid.sta || msta_link->wcid.idx > MT7996_WTBL_STA)
		return -EIO;

	if (dev->hif2) {
		switch (dev->option_type) {
		case 2:
			/* eagle + 7988d */
			if (msta_link->wcid.phy_idx == MT_BAND1)
				wed = &dev->mt76.mmio.wed_hif2;
			break;
		default:
			if ((is_mt7996(&dev->mt76) && msta_link->wcid.phy_idx == MT_BAND2) ||
			    (is_mt7992(&dev->mt76) && msta_link->wcid.phy_idx == MT_BAND1))
				wed = &dev->mt76.mmio.wed_hif2;
			break;
		}
	}

	if (!mtk_wed_device_active(wed) &&
	    !mt76_npu_device_active(&dev->mt76))
		goto out;

#ifdef CONFIG_NET_MEDIATEK_SOC_WED
	if (mtk_wed_device_active(wed))
		path->mtk_wdma.wdma_idx = wed->wdma_idx;
	else
#endif
	if (is_mt7996(&dev->mt76) && mt76_npu_device_active(&dev->mt76) &&
	    msta_link->wcid.phy_idx == MT_BAND2)
		path->mtk_wdma.wdma_idx = 1;
	else
		path->mtk_wdma.wdma_idx = link->mt76.band_idx;
	path->mtk_wdma.bss = link->mt76.idx;
	path->mtk_wdma.queue = 0;
	path->mtk_wdma.wcid = msta_link->wcid.idx;
	path->mtk_wdma.tid = tid;

	if (ieee80211_hw_check(hw, SUPPORTS_AMSDU_IN_AMPDU) &&
	    mtk_wed_is_amsdu_supported(wed))
		path->mtk_wdma.amsdu = msta_link->wcid.amsdu;
	else
		path->mtk_wdma.amsdu = 0;

out:
#endif
	path->type = DEV_PATH_MTK_WDMA;
	path->dev = ctx->dev;
	ctx->dev = NULL;

	return 0;
}

static int
mt7996_change_vif_links(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			u16 old_links, u16 new_links,
			struct ieee80211_bss_conf *old[IEEE80211_MLD_MAX_NUM_LINKS])
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_vif_link *mconf;
	unsigned long rem = old_links & ~new_links & ~vif->dormant_links &
			    mvif->mt76.valid_links;
	unsigned long add = new_links & ~old_links;
	int link_id, ret = 0;

	mt76_mlo_dbg(vif, "old=0x%x, new=0x%x, dormant=0x%x",
		     old_links, new_links, vif->dormant_links);

	mutex_lock(&dev->mt76.mutex);

	/* remove first */
	if (rem && vif->type == NL80211_IFTYPE_AP) {
		struct mt76_txq *mtxq = vif->txq ? (struct mt76_txq *)vif->txq->drv_priv : NULL;

		ret = mt7996_mcu_mld_reconf_stop_link(dev, vif, rem);
		if (ret)
			goto out;

		for_each_set_bit(link_id, &rem, IEEE80211_MLD_MAX_NUM_LINKS) {
			mconf = mt7996_vif_link(dev, vif, link_id);
			if (!mconf || !(mvif->mt76.valid_links & BIT(link_id)))
				continue;

			ret = mt7996_mcu_mld_link_oper(mconf->phy, old[link_id],
						       mconf, false);
			if (ret)
				goto out;

			if (mtxq && mtxq->wcid == mconf->msta_link.wcid.idx) {
				int link_next = ffs(vif->valid_links) - 1;

				mconf = mt7996_vif_link(dev, vif, link_next);
				if (mconf)
					mtxq->wcid = mconf->msta_link.wcid.idx;
			}
		}
		// mconf->msta_link.wcid.sta_disabled = 1;
	}

	for_each_set_bit(link_id, &add, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct ieee80211_bss_conf *conf =
			link_conf_dereference_protected(vif, link_id);

		mconf =	mt7996_vif_link(dev, vif, link_id);
		if (!mconf || !(mvif->mt76.valid_links & BIT(link_id)))
			continue;

		ret = mt7996_mcu_mld_link_oper(mconf->phy, conf, mconf, true);
		if (ret)
			goto out;
	}

	if (!old_links) {
		int idx;

		idx = get_own_mld_idx(dev->mld_idx_mask, true);
		if (idx < 0) {
			ret = -ENOSPC;
			goto out;
		}
		mvif->mld_group_idx = idx;
		dev->mld_idx_mask |= BIT_ULL(mvif->mld_group_idx);

		idx = get_free_idx(dev->mld_remap_idx_mask, 0, 15) - 1;
		if (idx < 0) {
			ret = -ENOSPC;
			goto out;
		}
		mvif->mld_remap_idx = idx;
		dev->mld_remap_idx_mask |= BIT_ULL(mvif->mld_remap_idx);
	}

	if (new_links)
		goto out;

	/* remove all links and fallback to non-MLO interface */
	rem = mvif->mt76.valid_links;
	for_each_set_bit(link_id, &rem, IEEE80211_MLD_MAX_NUM_LINKS) {
		mconf = mt7996_vif_link(dev, vif, link_id);
		if (!mconf || !mconf->phy)
			continue;

		mt7996_vif_link_remove(mconf->phy->mt76, vif, NULL, &mconf->mt76);
	}

	dev->mld_idx_mask &= ~BIT_ULL(mvif->mld_group_idx);
	dev->mld_remap_idx_mask &= ~BIT_ULL(mvif->mld_remap_idx);

out:
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static int
mt7996_set_ttlm(struct ieee80211_hw *hw, struct ieee80211_vif *vif)
{
	struct ieee80211_neg_ttlm merged_ttlm;
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct ieee80211_sta *sta;
	int ret;

	/* TODO check the intersection between Adv-TTLM and Neg-TTLM */
	if (vif->type != NL80211_IFTYPE_STATION ||
	    (vif->adv_ttlm.active && vif->neg_ttlm.valid))
		return -EOPNOTSUPP;

	mutex_lock(&dev->mt76.mutex);
	sta = ieee80211_find_sta(vif, vif->cfg.ap_addr);
	if (!sta) {
		mutex_unlock(&dev->mt76.mutex);
		return -EINVAL;
	}

	mt7996_get_merged_ttlm(vif, &merged_ttlm);

	ret = mt7996_mcu_peer_mld_ttlm_req(dev, vif, sta, &merged_ttlm);
	mutex_unlock(&dev->mt76.mutex);
	return ret;
}

static int
mt7996_set_sta_ttlm(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    struct ieee80211_sta *sta, struct ieee80211_neg_ttlm *neg_ttlm)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	int ret;

	mutex_lock(&dev->mt76.mutex);
	ret = mt7996_mcu_peer_mld_ttlm_req(dev, vif, sta, neg_ttlm);
	mutex_unlock(&dev->mt76.mutex);
	return ret;
}

static int
mt7996_set_attlm(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		 u16 disabled_links, u16 switch_time, u32 duration)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	int ret;

	mutex_lock(&dev->mt76.mutex);
	ret = mt7996_mcu_mld_set_attlm(dev, vif, disabled_links, switch_time, duration);
	mutex_unlock(&dev->mt76.mutex);
	return ret;
}

static enum ieee80211_neg_ttlm_res
mt7996_can_neg_ttlm(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		    struct ieee80211_neg_ttlm *neg_ttlm)
{
	/* TODO check intersection between adv-TTLM and neg-TTLM
	 * For now, we reject all possible overlapping cases of Adv-TTLM and
	 * Neg-TTLM
	 */
	return vif->adv_ttlm.active ? NEG_TTLM_RES_REJECT : NEG_TTLM_RES_ACCEPT;
}

static void
mt7996_reconfig_complete(struct ieee80211_hw *hw,
			 enum ieee80211_reconfig_type reconfig_type)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy;

	ieee80211_wake_queues(hw);
	mt7996_for_each_phy(dev, phy)
		ieee80211_queue_delayed_work(hw, &phy->mt76->mac_work,
					     MT7996_WATCHDOG_TIME);
}

static void
mt7996_event_callback(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		      const struct ieee80211_event *event)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	int i;

	switch (event->type) {
	case MLME_EVENT:
		if (event->u.mlme.data == ASSOC_EVENT &&
		    event->u.mlme.status == MLME_SUCCESS) {
			struct ieee80211_bss_conf *conf;
			struct mt7996_vif_link *mconf;
			struct mt7996_phy *phy;
			unsigned long cur, valid_links = vif->valid_links ?: BIT(0);
			unsigned int link_id;
			int next_time = INT_MAX;

			mutex_lock(&dev->mt76.mutex);
			cur = jiffies;
			for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
				conf = link_conf_dereference_protected(vif, link_id);
				mconf = mt7996_vif_link(dev, vif, link_id);

				if (!conf || !mconf)
					continue;

				phy = mconf->phy;
				mvif->beacon_received_time[phy->mt76->band_idx] = cur;
				next_time = min(next_time,
						MT7996_MAX_BEACON_LOSS *
						conf->beacon_int);

				/* trigger calibration for DFS link */
				if (!cfg80211_reg_can_beacon(hw->wiphy,
							     &phy->mt76->chandef,
							     NL80211_IFTYPE_AP))
					mt7996_mcu_set_chan_info(phy, UNI_CHANNEL_SWITCH,
								 true);
			}

			ieee80211_queue_delayed_work(hw, &mvif->beacon_mon_work,
						     msecs_to_jiffies(next_time));
			mutex_unlock(&dev->mt76.mutex);
			break;
		}

		mutex_lock(&dev->mt76.mutex);
		memset(mvif->probe_send_count, 0, sizeof(mvif->probe_send_count));
		for (i = 0; i < __MT_MAX_BAND; i++)
			mvif->probe[i] = NULL;
		mvif->lost_links = 0;
		mutex_unlock(&dev->mt76.mutex);

		cancel_delayed_work(&mvif->beacon_mon_work);
		break;
	default:
		break;
	}
}

static int
mt7996_set_eml_op_mode(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta,
		       struct ieee80211_eml_params *eml_params)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	int ret;

	mutex_lock(&dev->mt76.mutex);
	ret = mt7996_mcu_set_emlsr_mode(dev, vif, sta, eml_params);
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static int
mt7996_set_qos_map(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   struct cfg80211_qos_map *qos_map)
{
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	unsigned long valid_links = vif->valid_links ?: BIT(0);
	unsigned int link_id;
	int ret = 0;

	mutex_lock(&dev->mt76.mutex);
	for_each_set_bit(link_id, &valid_links, IEEE80211_MLD_MAX_NUM_LINKS) {
		struct mt7996_vif_link *mconf =
			mt7996_vif_link(dev, vif, link_id);

		if (!mconf)
			continue;

		ret = mt7996_mcu_set_qos_map(dev, vif, mconf, qos_map);
		if(ret)
			break;
	}
	mutex_unlock(&dev->mt76.mutex);

	return ret;
}

static void
mt7996_sta_channel_switch(struct ieee80211_hw *hw,
			  struct ieee80211_vif *vif,
			  struct ieee80211_channel_switch *ch_switch)
{
#define TX_PAUSED_GRACE_PERIOD		2000
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct ieee80211_bss_conf *conf;
	struct mt7996_vif_link *mconf;
	unsigned int link_id = ch_switch->link_id;
	int csa_time;

	if (vif->type != NL80211_IFTYPE_STATION)
		return;

	mutex_lock(&dev->mt76.mutex);

	conf = link_conf_dereference_protected(vif, link_id);
	mconf = mt7996_vif_link(dev, vif, link_id);
	if (!conf || !mconf) {
		mutex_unlock(&dev->mt76.mutex);
		return;
	}

	/* a new csa occurred while the original one was still in progress */
	if (mconf->state != MT7996_STA_CHSW_IDLE)
		mt7996_sta_chsw_state_reset(mconf);

	csa_time = (max_t(u8, ch_switch->count, 1) - 1) * conf->beacon_int;
	mconf->pause_timeout = TX_PAUSED_GRACE_PERIOD +
			       MT7996_MAX_BEACON_LOSS * conf->beacon_int +
			       cfg80211_chandef_dfs_cac_time(hw->wiphy,
							     &ch_switch->chandef);
	mconf->next_state = MT7996_STA_CHSW_PAUSE_TX;
	mvif->cs_links[0] = link_id;
	mutex_unlock(&dev->mt76.mutex);

	cancel_delayed_work(&mconf->sta_chsw_work);
	ieee80211_queue_delayed_work(hw, &mconf->sta_chsw_work,
				     msecs_to_jiffies(csa_time));
}


const struct ieee80211_ops mt7996_ops = {
	.add_chanctx = mt76_add_chanctx,
	.remove_chanctx = mt76_remove_chanctx,
	.change_chanctx = mt76_change_chanctx,
	.assign_vif_chanctx = mt76_assign_vif_chanctx,
	.unassign_vif_chanctx = mt76_unassign_vif_chanctx,
	.switch_vif_chanctx = mt76_switch_vif_chanctx,
	.tx = mt7996_tx,
	.start = mt7996_start,
	.stop = mt7996_stop,
	.add_interface = mt7996_add_interface,
	.remove_interface = mt7996_remove_interface,
	.config = mt7996_config,
	.conf_tx = mt7996_conf_tx,
	.configure_filter = mt7996_configure_filter,
	.vif_cfg_changed = mt7996_vif_cfg_changed,
	.link_info_changed = mt7996_link_info_changed,
	.sta_state = mt7996_sta_state,
	.sta_pre_rcu_remove = mt76_sta_pre_rcu_remove,
	.link_sta_rc_update = mt7996_link_sta_rc_update,
	.set_key = mt7996_set_key,
	.ampdu_action = mt7996_ampdu_action,
	.set_rts_threshold = mt7996_set_rts_threshold,
	.wake_tx_queue = mt76_wake_tx_queue,
	.hw_scan = mt76_hw_scan,
	.cancel_hw_scan = mt76_cancel_hw_scan,
	.remain_on_channel = mt76_remain_on_channel,
	.cancel_remain_on_channel = mt76_cancel_remain_on_channel,
	.release_buffered_frames = mt76_release_buffered_frames,
	.get_txpower = mt76_get_txpower,
	.channel_switch_beacon = mt7996_channel_switch_beacon,
	.pre_channel_switch = mt7996_pre_channel_switch,
	.post_channel_switch = mt7996_post_channel_switch,
	.get_stats = mt7996_get_stats,
	.get_et_sset_count = mt7996_get_et_sset_count,
	.get_et_stats = mt7996_get_et_stats,
	.get_et_strings = mt7996_get_et_strings,
	.get_tsf = mt7996_get_tsf,
	.set_tsf = mt7996_set_tsf,
	.offset_tsf = mt7996_offset_tsf,
	.get_survey = mt76_get_survey,
	.get_antenna = mt76_get_antenna,
	.set_antenna = mt7996_set_antenna,
	.set_bitrate_mask = mt7996_set_bitrate_mask,
	.set_coverage_class = mt7996_set_coverage_class,
	.sta_statistics = mt7996_sta_statistics,
	.link_sta_statistics = mt7996_link_sta_statistics,
	.sta_set_4addr = mt7996_sta_set_4addr,
	.sta_set_decap_offload = mt7996_sta_set_decap_offload,
	.add_twt_setup = mt7996_mac_add_twt_setup,
	.twt_teardown_request = mt7996_twt_teardown_request,
	.add_btwt = mt7996_mac_add_btwt,
	.del_btwt = mt7996_mac_del_btwt,
#ifdef CONFIG_MAC80211_DEBUGFS
	.sta_add_debugfs = mt7996_sta_add_debugfs,
	.link_sta_add_debugfs = mt7996_link_sta_add_debugfs,
	.vif_add_debugfs = mt7996_vif_add_debugfs,
#endif
	.set_radar_background = mt7996_set_radar_background,
	.net_fill_forward_path = mt7996_net_fill_forward_path,
#ifdef CONFIG_NET_MEDIATEK_SOC_WED
	.net_setup_tc = mt76_wed_net_setup_tc,
#elif defined(CONFIG_MT7996_NPU)
	.net_setup_tc = mt76_npu_net_setup_tc,
#endif
	.event_callback = mt7996_event_callback,
	.change_vif_links = mt7996_change_vif_links,
	.change_sta_links = mt7996_mac_sta_change_links,
	.reconfig_complete = mt7996_reconfig_complete,
	.set_eml_op_mode = mt7996_set_eml_op_mode,
	CFG80211_TESTMODE_CMD(mt76_testmode_cmd)
	CFG80211_TESTMODE_DUMP(mt76_testmode_dump)
	.set_qos_map = mt7996_set_qos_map,
	.set_attlm = mt7996_set_attlm,
	.set_sta_ttlm = mt7996_set_sta_ttlm,
	.can_neg_ttlm = mt7996_can_neg_ttlm,
	.set_ttlm = mt7996_set_ttlm,
	.channel_switch = mt7996_sta_channel_switch,
};
