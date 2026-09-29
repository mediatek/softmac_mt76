// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (C) 2022, MediaTek Inc. All rights reserved.
 */

#include <net/netlink.h>

#include "mt7996.h"
#include "mcu.h"
#include "../vendor.h"
#include "mtk_mcu.h"

#ifdef CONFIG_MTK_VENDOR
struct mt7996_amnt_data {
	u8 idx;
	u8 addr[ETH_ALEN];
	s8 rssi[4];
	u32 last_seen;
};

static void
mt7996_set_wireless_rts_sigta(struct ieee80211_hw *hw, u8 value) {
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy = &dev->phy;

	switch (value) {
	case BW_SIGNALING_STATIC:
	case BW_SIGNALING_DYNAMIC:
		mt7996_mcu_set_band_confg(phy, UNI_BAND_CONFIG_RTS_SIGTA_EN, true);
		mt7996_mcu_set_band_confg(phy, UNI_BAND_CONFIG_DIS_SECCH_CCA_DET, false);
		break;
	default:
		value = BW_SIGNALING_DISABLE;
		mt7996_mcu_set_band_confg(phy, UNI_BAND_CONFIG_RTS_SIGTA_EN, false);
		mt7996_mcu_set_band_confg(phy, UNI_BAND_CONFIG_DIS_SECCH_CCA_DET, true);
		break;
      }

	phy->rts_bw_sig = value;

	/* Set RTS Threshold to a lower Value */
	mt7996_mcu_set_rts_thresh(phy, 500);
}

void mt7996_vendor_amnt_fill_rx(struct mt7996_phy *phy, struct sk_buff *skb)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	struct mt7996_air_monitor_ctrl *ctrl = &phy->amnt_ctrl;
	struct ieee80211_hdr *hdr = mt76_skb_get_hdr(skb);
	__le16 fc = hdr->frame_control;
	u8 addr[ETH_ALEN];
	int i;

	if (!ieee80211_has_fromds(fc))
		ether_addr_copy(addr, hdr->addr2);
	else if (ieee80211_has_tods(fc))
		ether_addr_copy(addr, hdr->addr4);
	else
		ether_addr_copy(addr, hdr->addr3);

	spin_lock_bh(&phy->amnt_lock);
	for (i = 0; i < MT7996_AIR_MONITOR_MAX_ENTRY; i++) {
		struct mt7996_air_monitor_entry *entry;

		if (ether_addr_equal(addr, ctrl->entry[i].addr)) {
			entry = &ctrl->entry[i];
			entry->rssi[0] = status->chain_signal[0];
			entry->rssi[1] = status->chain_signal[1];
			entry->rssi[2] = status->chain_signal[2];
			entry->rssi[3] = status->chain_signal[3];
			entry->last_seen = jiffies;
			break;
		}
	}
	spin_unlock_bh(&phy->amnt_lock);
}

static int
mt7996_vendor_smesh_ctrl(struct mt7996_phy *phy, u8 write,
			 u8 enable, u8 *value)
{
#define UNI_CMD_SMESH_PARAM  0
	struct mt7996_dev *dev = phy->dev;
	struct smesh_param {
		u8 band;
		u8 _rsv[3];

		__le16 tag;
		__le16 length;

		u8 enable;
		bool a2;
		bool a1;
		bool data;
		bool mgnt;
		bool ctrl;
		u8 padding[2];
	} req = {
		.band = phy->mt76->band_idx,

		.tag = cpu_to_le16(UNI_CMD_SMESH_PARAM),
		.length = cpu_to_le16(sizeof(req) - 4),

		.enable = enable,
		.a2 = true,
		.a1 = true,
		.data = true,
		.mgnt = false,
		.ctrl = false,
	};
	struct smesh_param *res;
	struct sk_buff *skb;
	int ret;

	if (write)
		return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(CFG_SMESH),
					 &req, sizeof(req), true);

	if (!value)
		return -EINVAL;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_WM_UNI_CMD_QUERY(CFG_SMESH),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	res = (struct smesh_param *) skb->data;
	*value = res->enable;
	dev_kfree_skb(skb);

	return 0;
}

static int
mt7996_vendor_amnt_muar(struct mt7996_phy *phy, u8 muar_idx, u8 *addr)
{
#define UNI_CMD_MUAR_ENTRY  2
	struct mt7996_dev *dev = phy->dev;
	struct muar_entry {
		u8 band;
		u8 rsv[3];

		__le16 tag;
		__le16 length;

		bool smesh;
		u8 hw_bss_index;
		u8 muar_idx;
		u8 entry_add;
		u8 mac_addr[ETH_ALEN];
		u8 padding[2];
	} __packed req = {
		.band = phy->mt76->band_idx,

		.tag = cpu_to_le16(UNI_CMD_MUAR_ENTRY),
		.length = cpu_to_le16(sizeof(req) - 4),

		.smesh = true,
		.hw_bss_index = 0,
		.muar_idx = muar_idx,
		.entry_add = 1,
	};

	ether_addr_copy(req.mac_addr, addr);
	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(REPT_MUAR), &req,
				 sizeof(req), true);
}

static int
mt7996_vendor_amnt_set_en(struct mt7996_phy *phy, u8 enable)
{
	u8 status;
	int ret;

	ret = mt7996_vendor_smesh_ctrl(phy, 0, enable, &status);
	if (ret)
		return ret;

	if (status == enable)
		return 0;

	ret = mt7996_vendor_smesh_ctrl(phy, 1, enable, &status);
	if (ret)
		return ret;

	return 0;
}

static int
mt7996_vendor_amnt_set_addr(struct mt7996_phy *phy, u8 index, u8 *addr)
{
	struct mt7996_air_monitor_ctrl *amnt_ctrl = &phy->amnt_ctrl;
	struct mt7996_air_monitor_group *group;
	struct mt7996_air_monitor_entry *entry;
	int ret, i, j;

	if (index >= MT7996_AIR_MONITOR_MAX_ENTRY)
		return -1;

	spin_lock_bh(&phy->amnt_lock);
	entry = &amnt_ctrl->entry[index];

	if (is_zero_ether_addr(addr)) {
		group = &(amnt_ctrl->group[entry->group_idx]);
		group->used[entry->group_used_idx] = false;
		entry->enable = false;
	} else if (!entry->enable) {
		for (i = 0; i < MT7996_AIR_MONITOR_MAX_GROUP; i++) {
			group = &(amnt_ctrl->group[i]);
			if (!group->used[0])
				j = 0;
			else if (!group->used[1])
				j = 1;
			else
				continue;

			group->used[j] = entry->enable = true;
			entry->group_idx = i;
			entry->group_used_idx = j;
			entry->muar_idx = 32 + 4 * i + 2 * j;
			break;
		}
	}

	ether_addr_copy(entry->addr, addr);
	amnt_ctrl->enable &= ~(1 << entry->group_idx);
	amnt_ctrl->enable |= entry->enable << entry->group_idx;
	spin_unlock_bh(&phy->amnt_lock);

	ret = mt7996_vendor_amnt_muar(phy, entry->muar_idx, addr);
	if (ret)
		return ret;

	return mt7996_vendor_amnt_set_en(phy, amnt_ctrl->enable);
}

static int
mt7996_vendor_amnt_ctrl(struct wiphy *wiphy, struct wireless_dev *wdev,
			const void *data, int data_len)
{
	struct ieee80211_vif *vif = wdev_to_ieee80211_vif(wdev);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_vif_link *mconf;
	struct mt7996_phy *phy;
	struct nlattr *tb1[NUM_MTK_VENDOR_ATTRS_AMNT_CTRL];
	struct nlattr *tb2[NUM_MTK_VENDOR_ATTRS_AMNT_SET];
	u8 index = 0, link_id = 0;
	u8 mac_addr[ETH_ALEN];
	int err;

	err = nla_parse(tb1, MTK_VENDOR_ATTR_AMNT_CTRL_MAX, data, data_len,
			mt76_vendor_amnt_ctrl_policy, NULL);
	if (err)
		return err;

	if (ieee80211_vif_is_mld(vif) && tb1[MTK_VENDOR_ATTR_AMNT_CTRL_LINK_ID]) {
		link_id = nla_get_u8(tb1[MTK_VENDOR_ATTR_AMNT_CTRL_LINK_ID]);

		if (link_id >= IEEE80211_LINK_UNSPECIFIED)
			return -EINVAL;
	}

	rcu_read_lock();
	mconf = (struct mt7996_vif_link *)rcu_dereference(mvif->mt76.link[link_id]);
	phy = mconf ? mconf->phy : NULL;
	rcu_read_unlock();

	if (!phy || !tb1[MTK_VENDOR_ATTR_AMNT_CTRL_SET])
		return -EINVAL;

	err = nla_parse_nested(tb2, MTK_VENDOR_ATTR_AMNT_SET_MAX,
		tb1[MTK_VENDOR_ATTR_AMNT_CTRL_SET], mt76_vendor_amnt_set_policy, NULL);

	if (!tb2[MTK_VENDOR_ATTR_AMNT_SET_INDEX] ||
		!tb2[MTK_VENDOR_ATTR_AMNT_SET_MACADDR])
		return -EINVAL;

	index = nla_get_u8(tb2[MTK_VENDOR_ATTR_AMNT_SET_INDEX]);
	nla_memcpy(mac_addr, tb2[MTK_VENDOR_ATTR_AMNT_SET_MACADDR], ETH_ALEN);

	return mt7996_vendor_amnt_set_addr(phy, index, mac_addr);
}

int mt7996_vendor_amnt_sta_remove(struct mt7996_phy *phy,
				  struct ieee80211_link_sta *link_sta)
{
	u8 zero[ETH_ALEN] = {};
	int i;

	if (!phy->amnt_ctrl.enable)
		return 0;

	for (i = 0; i < MT7996_AIR_MONITOR_MAX_ENTRY; i++)
		if (ether_addr_equal(link_sta->addr, phy->amnt_ctrl.entry[i].addr))
			return mt7996_vendor_amnt_set_addr(phy, i, zero);
	return 0;
}

static int
mt7996_amnt_dump(struct mt7996_phy *phy, struct sk_buff *skb,
		 u8 amnt_idx, int *attrtype)
{
	struct mt7996_air_monitor_entry *entry;
	struct mt7996_amnt_data data;
	u32 last_seen = 0;

	if (amnt_idx >= MT7996_AIR_MONITOR_MAX_ENTRY)
		return 0;

	spin_lock_bh(&phy->amnt_lock);
	entry = &phy->amnt_ctrl.entry[amnt_idx];
	if (!entry->enable) {
		spin_unlock_bh(&phy->amnt_lock);
		return 0;
	}

	last_seen = jiffies_to_msecs(jiffies - entry->last_seen);
	ether_addr_copy(data.addr, entry->addr);
	data.rssi[0] = entry->rssi[0];
	data.rssi[1] = entry->rssi[1];
	data.rssi[2] = entry->rssi[2];
	data.rssi[3] = entry->rssi[3];
	spin_unlock_bh(&phy->amnt_lock);

	data.idx = amnt_idx;
	data.last_seen = last_seen;

	nla_put(skb, (*attrtype)++, sizeof(struct mt7996_amnt_data), &data);

	return 1;
}

static int
mt7996_vendor_amnt_ctrl_dump(struct wiphy *wiphy, struct wireless_dev *wdev,
			     struct sk_buff *skb, const void *data, int data_len,
			     unsigned long *storage)
{
	struct ieee80211_vif *vif = wdev_to_ieee80211_vif(wdev);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_vif_link *mconf;
	struct mt7996_phy *phy;
	struct nlattr *tb1[NUM_MTK_VENDOR_ATTRS_AMNT_CTRL];
	struct nlattr *tb2[NUM_MTK_VENDOR_ATTRS_AMNT_DUMP];
	void *a, *b;
	int err = 0, attrtype = 0, i, len = 0;
	u8 amnt_idx, link_id = 0;

	if (*storage == 1)
		return -ENOENT;
	*storage = 1;

	err = nla_parse(tb1, MTK_VENDOR_ATTR_AMNT_CTRL_MAX, data, data_len,
			mt76_vendor_amnt_ctrl_policy, NULL);
	if (err)
		return err;

	if (ieee80211_vif_is_mld(vif) && tb1[MTK_VENDOR_ATTR_AMNT_CTRL_LINK_ID]) {
		link_id = nla_get_u8(tb1[MTK_VENDOR_ATTR_AMNT_CTRL_LINK_ID]);

		if (link_id >= IEEE80211_LINK_UNSPECIFIED)
			return -EINVAL;
	}

	rcu_read_lock();
	mconf = (struct mt7996_vif_link *)rcu_dereference(mvif->mt76.link[link_id]);
	phy = mconf ? mconf->phy : NULL;
	rcu_read_unlock();

	if (!phy || !tb1[MTK_VENDOR_ATTR_AMNT_CTRL_DUMP])
		return -EINVAL;

	err = nla_parse_nested(tb2, MTK_VENDOR_ATTR_AMNT_DUMP_MAX,
			       tb1[MTK_VENDOR_ATTR_AMNT_CTRL_DUMP],
			       mt76_vendor_amnt_dump_policy, NULL);
	if (err)
		return err;

	if (!tb2[MTK_VENDOR_ATTR_AMNT_DUMP_INDEX])
		return -EINVAL;

	amnt_idx = nla_get_u8(tb2[MTK_VENDOR_ATTR_AMNT_DUMP_INDEX]);

	a = nla_nest_start(skb, MTK_VENDOR_ATTR_AMNT_CTRL_DUMP);
	b = nla_nest_start(skb, MTK_VENDOR_ATTR_AMNT_DUMP_RESULT);

	if (amnt_idx != 0xff) {
		len += mt7996_amnt_dump(phy, skb, amnt_idx, &attrtype);
	} else {
		for (i = 0; i < MT7996_AIR_MONITOR_MAX_ENTRY; i++)
			len += mt7996_amnt_dump(phy, skb, i, &attrtype);
	}

	nla_nest_end(skb, b);

	nla_put_u8(skb, MTK_VENDOR_ATTR_AMNT_DUMP_LEN, len);

	nla_nest_end(skb, a);

	return len + 1;
}

static int mt7996_vendor_pp_ctrl(struct wiphy *wiphy, struct wireless_dev *wdev,
				 const void *data, int data_len)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct ieee80211_vif *vif = wdev_to_ieee80211_vif(wdev);
	struct nlattr *tb[NUM_MTK_VENDOR_ATTRS_PP_CTRL];
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy;
	struct cfg80211_chan_def *chandef;
	struct mt7996_vif_link *mconf;
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	int err;
	u8 mode = 0, link_id = 0;
	u16 punct_bitmap = 0;

	err = nla_parse(tb, MTK_VENDOR_ATTR_PP_CTRL_MAX, data, data_len,
			mt76_vendor_pp_ctrl_policy, NULL);

	if (tb[MTK_VENDOR_ATTR_PP_MODE])
		mode = nla_get_u8(tb[MTK_VENDOR_ATTR_PP_MODE]);
	else
		return -EINVAL;

	if (ieee80211_vif_is_mld(vif) && tb[MTK_VENDOR_ATTR_PP_LINK_ID]) {
		link_id = nla_get_u8(tb[MTK_VENDOR_ATTR_PP_LINK_ID]);
		if (link_id >= IEEE80211_LINK_UNSPECIFIED)
			return -EINVAL;
	}

	rcu_read_lock();
	mconf = (struct mt7996_vif_link *)rcu_dereference(mvif->mt76.link[link_id]);
	if (!mconf) {
		rcu_read_unlock();
		goto error;
	}

	phy = mconf->phy;
	if (!phy) {
		rcu_read_unlock();
		goto error;
	}
	rcu_read_unlock();

	chandef = &phy->mt76->chandef;
	if (chandef->chan->band == NL80211_BAND_2GHZ)
		return 0;

	switch (mode) {
	case PP_FW_MODE:
		err = mt7996_mcu_set_pp_alg_ctrl(phy, PP_ALG_SET_TIMER);
		if (err)
			return err;

		err = mt7996_mcu_set_pp_alg_ctrl(phy, PP_ALG_SET_THR);
		if (err)
			return err;
		fallthrough;
	case PP_USR_MODE:
		if (tb[MTK_VENDOR_ATTR_PP_BITMAP])
			punct_bitmap = nla_get_u16(tb[MTK_VENDOR_ATTR_PP_BITMAP]);
		fallthrough;
	case PP_DISABLE:
		err = mt7996_mcu_set_pp_en(phy, mode, punct_bitmap);
		break;
	default:
		return -EINVAL;
	}

	return err;
error:
	dev_err(dev->mt76.dev, "Invalid link id: %d\n", link_id);
	return -EINVAL;
}

static int mt7996_vendor_rfeature_ctrl(struct wiphy *wiphy,
				       struct wireless_dev *wdev,
				       const void *data,
				       int data_len)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct ieee80211_vif *vif = wdev_to_ieee80211_vif(wdev);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_vif_link *mconf;
	struct nlattr *tb[NUM_MTK_VENDOR_ATTRS_RFEATURE_CTRL];
	int err;
	u8 band_idx, link_id = 0;

	err = nla_parse(tb, MTK_VENDOR_ATTR_RFEATURE_CTRL_MAX, data, data_len,
			mt76_vendor_rfeature_ctrl_policy, NULL);
	if (err)
		return err;

	if (ieee80211_vif_is_mld(vif) && tb[MTK_VENDOR_ATTR_RFEATURE_CTRL_LINK_ID]) {
		link_id = nla_get_u8(tb[MTK_VENDOR_ATTR_RFEATURE_CTRL_LINK_ID]);
		if (link_id >= IEEE80211_LINK_UNSPECIFIED)
			return -EINVAL;
	}

	rcu_read_lock();
	mconf = (struct mt7996_vif_link *)rcu_dereference(mvif->mt76.link[link_id]);
	if (!mconf || !mconf->phy) {
		rcu_read_unlock();
		return -EINVAL;
	}

	band_idx = mconf->phy->mt76->band_idx;
	rcu_read_unlock();

	if (tb[MTK_VENDOR_ATTR_RFEATURE_CTRL_TRIG_TYPE_CFG]) {
		u8 enable, trig_type;
		int rem;
		struct nlattr *cur;

		nla_for_each_nested(cur, tb[MTK_VENDOR_ATTR_RFEATURE_CTRL_TRIG_TYPE_CFG], rem) {
			switch (nla_type(cur)) {
			case MTK_VENDOR_ATTR_RFEATURE_CTRL_TRIG_TYPE_EN:
				enable = nla_get_u8(cur);
				break;
			case MTK_VENDOR_ATTR_RFEATURE_CTRL_TRIG_TYPE:
				trig_type = nla_get_u8(cur);
				break;
			default:
				return -EINVAL;
			};
		}

		err = mt7996_mcu_set_rfeature_trig_type(dev, band_idx, enable, trig_type);
		if (err)
			return err;
	} else if (tb[MTK_VENDOR_ATTR_RFEATURE_CTRL_ACK_PLCY]) {
		u8 ack_policy;

		ack_policy = nla_get_u8(tb[MTK_VENDOR_ATTR_RFEATURE_CTRL_ACK_PLCY]);
		switch (ack_policy) {
		case MU_DL_ACK_POLICY_TF_FOR_ACK:
			return mt7996_mcu_set_muru_cmd(dev, UNI_CMD_MURU_SET_MUDL_ACK_POLICY,
						       ack_policy);
		default:
			return 0;
		}
	} else if (tb[MTK_VENDOR_ATTR_RFEATURE_CTRL_TRIG_VARIANT_TYPE]) {
		u8 trig_var;

		trig_var = nla_get_u8(tb[MTK_VENDOR_ATTR_RFEATURE_CTRL_TRIG_VARIANT_TYPE]);

		return mt7996_mcu_set_muru_cmd(dev, UNI_CMD_MURU_SET_TRIG_VARIANT,
					       trig_var);
	} else if (tb[MTK_VENDOR_ATTR_RFEATURE_CTRL_CODING_TYPE]) {
		u8 coding_type;

		coding_type = nla_get_u8(tb[MTK_VENDOR_ATTR_RFEATURE_CTRL_CODING_TYPE]);

		return mt7996_set_coding_type(hw, coding_type, link_id);
	}

	return 0;
}

static int mt7996_vendor_wireless_ctrl(struct wiphy *wiphy,
				       struct wireless_dev *wdev,
				       const void *data,
				       int data_len)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct ieee80211_vif *vif = wdev_to_ieee80211_vif(wdev);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_vif_link *mconf;
	struct nlattr *tb[NUM_MTK_VENDOR_ATTRS_WIRELESS_CTRL];
	int err;
	u8 val8, band_idx, link_id = 0;
	u16 val16;
	u32 val32;
	bool band_idx_get = false;

	err = nla_parse(tb, MTK_VENDOR_ATTR_WIRELESS_CTRL_MAX, data, data_len,
			mt76_vendor_wireless_ctrl_policy, NULL);
	if (err)
		return err;

	if (ieee80211_vif_is_mld(vif) && tb[MTK_VENDOR_ATTR_WIRELESS_CTRL_LINK_ID]) {
		link_id = nla_get_u8(tb[MTK_VENDOR_ATTR_WIRELESS_CTRL_LINK_ID]);

		if (link_id >= IEEE80211_LINK_UNSPECIFIED)
			return -EINVAL;
	}

	rcu_read_lock();
	mconf = (struct mt7996_vif_link *)rcu_dereference(mvif->mt76.link[link_id]);
	if (mconf && mconf->phy) {
		band_idx = mconf->phy->mt76->band_idx;
		band_idx_get = true;
	}
	rcu_read_unlock();

	if (!band_idx_get && (tb[MTK_VENDOR_ATTR_WIRELESS_CTRL_OFDMA] ||
	    tb[MTK_VENDOR_ATTR_WIRELESS_CTRL_NUSERS_OFDMA] ||
	    tb[MTK_VENDOR_ATTR_WIRELESS_CTRL_MIMO] ||
	    tb[MTK_VENDOR_ATTR_WIRELESS_CTRL_CERT]))
		return -EINVAL;

	if (tb[MTK_VENDOR_ATTR_WIRELESS_CTRL_OFDMA]) {
		val8 = nla_get_u8(tb[MTK_VENDOR_ATTR_WIRELESS_CTRL_OFDMA]);
		val32 = FIELD_PREP(RATE_CFG_MODE, RATE_PARAM_FIXED_OFDMA) |
			FIELD_PREP(RATE_CFG_VAL, val8) |
			FIELD_PREP(RATE_CFG_BAND_IDX, band_idx);
		ieee80211_iterate_active_interfaces_atomic(hw, IEEE80211_IFACE_ITER_RESUME_ALL,
			mt7996_set_wireless_vif, &val32);
		if (val8 == 3) /* DL20and80 */
			mt7996_mcu_set_muru_cmd(dev, UNI_CMD_MURU_SET_20M_DYN_ALGO, 1);
	} else if (tb[MTK_VENDOR_ATTR_WIRELESS_CTRL_BA_BUFFER_SIZE]) {
		val16 = nla_get_u16(tb[MTK_VENDOR_ATTR_WIRELESS_CTRL_BA_BUFFER_SIZE]);
		hw->max_tx_aggregation_subframes = val16;
		hw->max_rx_aggregation_subframes = val16;
	} else if (tb[MTK_VENDOR_ATTR_WIRELESS_CTRL_PPDU_TX_TYPE]) {
		val8 = nla_get_u8(tb[MTK_VENDOR_ATTR_WIRELESS_CTRL_PPDU_TX_TYPE]);
		mt7996_mcu_set_ppdu_tx_type(dev, val8);
	} else if (tb[MTK_VENDOR_ATTR_WIRELESS_CTRL_NUSERS_OFDMA]) {
		val8 = nla_get_u8(tb[MTK_VENDOR_ATTR_WIRELESS_CTRL_NUSERS_OFDMA]);
		mt7996_mcu_set_nusers_ofdma(dev, band_idx, val8);
	} else if (tb[MTK_VENDOR_ATTR_WIRELESS_CTRL_MIMO]) {
		val8 = nla_get_u8(tb[MTK_VENDOR_ATTR_WIRELESS_CTRL_MIMO]);
		val32 = FIELD_PREP(RATE_CFG_MODE, RATE_PARAM_FIXED_MIMO) |
			FIELD_PREP(RATE_CFG_VAL, val8) |
			FIELD_PREP(RATE_CFG_BAND_IDX, band_idx);
		ieee80211_iterate_active_interfaces_atomic(hw, IEEE80211_IFACE_ITER_RESUME_ALL,
			mt7996_set_wireless_vif, &val32);
	} else if (tb[MTK_VENDOR_ATTR_WIRELESS_CTRL_CERT]) {
		val8 = nla_get_u8(tb[MTK_VENDOR_ATTR_WIRELESS_CTRL_CERT]);
		dev->cert_mode = val8;
		mt7996_mcu_set_cert(dev);
		mt7996_mcu_set_bypass_smthint(dev, band_idx, val8);
	} else if (tb[MTK_VENDOR_ATTR_WIRELESS_CTRL_AMSDU]) {
		val8 = nla_get_u8(tb[MTK_VENDOR_ATTR_WIRELESS_CTRL_AMSDU]);
		mt7996_set_wireless_amsdu(hw, val8);
	} else if (tb[MTK_VENDOR_ATTR_WIRELESS_CTRL_RTS_SIGTA]) {
		val8 = nla_get_u8(tb[MTK_VENDOR_ATTR_WIRELESS_CTRL_RTS_SIGTA]);
		mt7996_set_wireless_rts_sigta(hw, val8);
	}

	return 0;
}

static int
mt7996_vendor_epcs_ctrl(struct wiphy *wiphy, struct wireless_dev *wdev,
			const void *data, int data_len)
{
	size_t len = sizeof(struct mt7996_wmm_params) * IEEE80211_NUM_ACS;
	struct ieee80211_vif *vif = wdev_to_ieee80211_vif(wdev);
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct nlattr *tb[NUM_MTK_VENDOR_ATTRS_EPCS_CTRL];
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_wmm_params *params;
	u8 addr[ETH_ALEN], link_id;
	struct ieee80211_sta *sta;
	bool enable;
	u16 wmm_idx;
	int ret;

	if (!ieee80211_vif_is_mld(vif))
		return -EPERM;

	ret = nla_parse(tb, MTK_VENDOR_ATTR_EPCS_CTRL_MAX, data, data_len,
			mt76_vendor_epcs_ctrl_policy, NULL);
	if (ret)
		return ret;

	if (!tb[MTK_VENDOR_ATTR_EPCS_ADDR] ||
	    !tb[MTK_VENDOR_ATTR_EPCS_LINK_ID] ||
	    !tb[MTK_VENDOR_ATTR_EPCS_ENABLE] ||
	    !tb[MTK_VENDOR_ATTR_EPCS_WMM_IDX] ||
	    !tb[MTK_VENDOR_ATTR_EPCS_WMM_PARAMS])
		return -EINVAL;

	link_id = nla_get_u8(tb[MTK_VENDOR_ATTR_EPCS_LINK_ID]);
	if (link_id >= IEEE80211_MLD_MAX_NUM_LINKS)
		return -EINVAL;

	enable = nla_get_u8(tb[MTK_VENDOR_ATTR_EPCS_ENABLE]);

	wmm_idx = nla_get_u16(tb[MTK_VENDOR_ATTR_EPCS_WMM_IDX]);
	if (wmm_idx >= EPCS_MAX_WMM_PARAMS)
		return -EINVAL;

	params = kzalloc(len, GFP_KERNEL);
	if (!params)
		return -ENOMEM;
	nla_memcpy(params, tb[MTK_VENDOR_ATTR_EPCS_WMM_PARAMS], len);

	mutex_lock(&dev->mt76.mutex);
	nla_memcpy(addr, tb[MTK_VENDOR_ATTR_EPCS_ADDR], ETH_ALEN);
	sta = ieee80211_find_sta_by_ifaddr(hw, addr, NULL);
	if (!sta)
		ret = -EINVAL;
	else
		ret = mt7996_mcu_epcs_ctrl(UNI_EPCS_CTRL_ENABLE, dev, sta,
					   link_id, enable, wmm_idx, params);
	mutex_unlock(&dev->mt76.mutex);

	kfree(params);

	return ret;
}

static int mt7996_vendor_scs_ctrl(struct wiphy *wiphy, struct wireless_dev *wdev,
				  const void *data, int data_len)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct ieee80211_sta *sta;
	struct mt7996_sta *msta;
	struct mt7996_sta_link *msta_link;
	struct nlattr *tb[NUM_MTK_VENDOR_ATTRS_SCS_CTRL];
	u8 sta_addr[ETH_ALEN];
	u8 scs_id, req_type, dir, link_id, qos_ie_len;
	u8 *qos_ie = NULL;
	int err;

	err = nla_parse(tb, MTK_VENDOR_ATTR_SCS_CTRL_MAX, data, data_len,
			mt76_vendor_scs_ctrl_policy, NULL);
	if (err)
		return err;

	if (!tb[MTK_VENDOR_ATTR_SCS_ID] || !tb[MTK_VENDOR_ATTR_SCS_REQ_TYPE] ||
	    !tb[MTK_VENDOR_ATTR_SCS_MAC_ADDR] || !tb[MTK_VENDOR_ATTR_SCS_LINK_ID])
		return -EINVAL;

	scs_id = nla_get_u8(tb[MTK_VENDOR_ATTR_SCS_ID]);
	req_type = nla_get_u8(tb[MTK_VENDOR_ATTR_SCS_REQ_TYPE]);
	nla_memcpy(sta_addr, tb[MTK_VENDOR_ATTR_SCS_MAC_ADDR], ETH_ALEN);
	link_id = nla_get_u8(tb[MTK_VENDOR_ATTR_SCS_LINK_ID]);

	if (req_type == SCS_REQ_TYPE_ADD || req_type == SCS_REQ_TYPE_CHANGE) {
		if (!tb[MTK_VENDOR_ATTR_SCS_DIR] || !tb[MTK_VENDOR_ATTR_SCS_QOS_IE])
			return -EINVAL;

		dir = nla_get_u8(tb[MTK_VENDOR_ATTR_SCS_DIR]);
		qos_ie_len = nla_len(tb[MTK_VENDOR_ATTR_SCS_QOS_IE]);
		qos_ie = kzalloc(qos_ie_len, GFP_KERNEL);
		if (!qos_ie)
			return -ENOMEM;

		nla_memcpy(qos_ie, tb[MTK_VENDOR_ATTR_SCS_QOS_IE], qos_ie_len);
	}

	mutex_lock(&dev->mt76.mutex);
	sta = ieee80211_find_sta_by_ifaddr(hw, sta_addr, NULL);
	if (!sta) {
		err = -EINVAL;
		goto out;
	}

	msta = (struct mt7996_sta *)sta->drv_priv;
	msta_link = mt76_dereference(msta->link[link_id], &dev->mt76);
	if (!msta_link) {
		err = -EINVAL;
		goto out;
	}

	err = mt7996_mcu_set_muru_qos_cfg(dev, msta_link->wcid.idx, dir, scs_id,
					  req_type, qos_ie, qos_ie_len);

out:
	kfree(qos_ie);
	mutex_unlock(&dev->mt76.mutex);

	return err;
}

static int mt7996_vendor_csi_ctrl(struct wiphy *wiphy,
				  struct wireless_dev *wdev,
				  const void *data,
				  int data_len)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy;
	struct nlattr *tb[NUM_MTK_VENDOR_ATTRS_CSI_CTRL];
	u8 radio_idx = 0;
	int err;

	err = nla_parse(tb, MTK_VENDOR_ATTR_CSI_CTRL_MAX, data, data_len,
			mt76_vendor_csi_ctrl_policy, NULL);
	if (err)
		return err;

	if (tb[MTK_VENDOR_ATTR_CSI_CTRL_RADIO_IDX])
		radio_idx = nla_get_u8(tb[MTK_VENDOR_ATTR_CSI_CTRL_RADIO_IDX]);

	if (!mt7996_radio_valid(dev, radio_idx))
		goto error;
	phy = dev->radio_phy[radio_idx];

	if (tb[MTK_VENDOR_ATTR_CSI_CTRL_CFG]) {
		u8 mode = 0, type = 0, v1 = 0;
		u32 v2 = 0;
		u8 mac_addr[ETH_ALEN] = {};
		struct nlattr *cur;
		int rem;

		nla_for_each_nested(cur, tb[MTK_VENDOR_ATTR_CSI_CTRL_CFG], rem) {
			switch (nla_type(cur)) {
			case MTK_VENDOR_ATTR_CSI_CTRL_CFG_MODE:
				mode = nla_get_u8(cur);
				break;
			case MTK_VENDOR_ATTR_CSI_CTRL_CFG_TYPE:
				type = nla_get_u8(cur);
				break;
			case MTK_VENDOR_ATTR_CSI_CTRL_CFG_VAL1:
				v1 = nla_get_u8(cur);
				break;
			case MTK_VENDOR_ATTR_CSI_CTRL_CFG_VAL2:
				v2 = nla_get_u32(cur);
				break;
			default:
				return -EINVAL;
			};
		}

		if (tb[MTK_VENDOR_ATTR_CSI_CTRL_MAC_ADDR])
			nla_memcpy(mac_addr,
				   tb[MTK_VENDOR_ATTR_CSI_CTRL_MAC_ADDR], ETH_ALEN);

		err = mt7996_mcu_set_csi(phy, mode, type, v1, v2, mac_addr);
		if (err < 0)
			return err;

		spin_lock_bh(&phy->csi.lock);

		phy->csi.enable = !!mode;

		/* clean up old csi stats */
		if ((mode == CSI_CONTROL_MODE_STOP || mode == CSI_CONTROL_MODE_SET)
			&& !list_empty(&phy->csi.list)) {
			struct csi_data *c, *tmp_c;

			list_for_each_entry_safe(c, tmp_c, &phy->csi.list, node) {
				list_del(&c->node);
				kfree(c);
				phy->csi.count--;
			}
		} else if (mode == CSI_CONTROL_MODE_START) {
			phy->csi.last_record = 0;
		}

		spin_unlock_bh(&phy->csi.lock);

		if (mode == CSI_CONTROL_MODE_SET && type == CSI_CONFIG_STA_FILTER && v1 == 2)
			phy->csi.interval = v2;
	}

	return 0;

error:
	dev_err(dev->mt76.dev, "Invalid radio idx: %d\n", radio_idx);
	return -EINVAL;
}

static int
mt7996_vendor_csi_ctrl_dump(struct wiphy *wiphy, struct wireless_dev *wdev,
			    struct sk_buff *skb, const void *data, int data_len,
			    unsigned long *storage)
{
#define RESERVED_SET	BIT(31)
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt7996_dev *dev = mt7996_hw_dev(hw);
	struct mt7996_phy *phy;
	struct nlattr *tb[NUM_MTK_VENDOR_ATTRS_CSI_CTRL] = {0};
	u8 radio_idx = 0;
	int err = 0;

	if (*storage & RESERVED_SET) {
		if ((*storage & GENMASK(15, 0)) == 0)
			return -ENOENT;
	}

	if (data) {
		err = nla_parse(tb, MTK_VENDOR_ATTR_CSI_CTRL_MAX, data, data_len,
				mt76_vendor_csi_ctrl_policy, NULL);
		if (err)
			return err;
	}

	if (tb[MTK_VENDOR_ATTR_CSI_CTRL_RADIO_IDX])
		radio_idx = nla_get_u8(tb[MTK_VENDOR_ATTR_CSI_CTRL_RADIO_IDX]);

	if (!mt7996_radio_valid(dev, radio_idx))
		return -EINVAL;
	phy = dev->radio_phy[radio_idx];

	if (!(*storage & RESERVED_SET) && tb[MTK_VENDOR_ATTR_CSI_CTRL_DUMP_NUM]) {
		*storage = nla_get_u16(tb[MTK_VENDOR_ATTR_CSI_CTRL_DUMP_NUM]);
		*storage |= RESERVED_SET;
	}

	(*storage)--;

	spin_lock_bh(&phy->csi.lock);

	if (!list_empty(&phy->csi.list)) {
		struct csi_data *csi;
		void *a, *b;
		int i;

		csi = list_first_entry(&phy->csi.list, struct csi_data, node);

		a = nla_nest_start(skb, MTK_VENDOR_ATTR_CSI_CTRL_DATA);
		if (!a)
			goto out;

		if (nla_put_u8(skb, MTK_VENDOR_ATTR_CSI_DATA_VER, 1) ||
		    nla_put_u8(skb, MTK_VENDOR_ATTR_CSI_DATA_RSSI, csi->rssi) ||
		    nla_put_u8(skb, MTK_VENDOR_ATTR_CSI_DATA_SNR, csi->snr) ||
		    nla_put_u8(skb, MTK_VENDOR_ATTR_CSI_DATA_BW, csi->data_bw) ||
		    nla_put_u8(skb, MTK_VENDOR_ATTR_CSI_DATA_CH_IDX, csi->pri_ch_idx) ||
		    nla_put_u8(skb, MTK_VENDOR_ATTR_CSI_DATA_MODE, csi->rx_mode))
			goto out;

		if (nla_put_u16(skb, MTK_VENDOR_ATTR_CSI_DATA_TX_ANT, csi->tx_idx) ||
		    nla_put_u16(skb, MTK_VENDOR_ATTR_CSI_DATA_RX_ANT, csi->rx_idx))
			goto out;

		if (nla_put_u32(skb, MTK_VENDOR_ATTR_CSI_DATA_INFO, csi->ext_info) ||
		    nla_put_u32(skb, MTK_VENDOR_ATTR_CSI_DATA_CHAIN_INFO, csi->chain_info) ||
		    nla_put_u32(skb, MTK_VENDOR_ATTR_CSI_DATA_TS, csi->ts))
			goto out;

		if (nla_put(skb, MTK_VENDOR_ATTR_CSI_DATA_TA, ETH_ALEN, csi->ta))
			goto out;

		if (nla_put_u32(skb, MTK_VENDOR_ATTR_CSI_DATA_NUM, csi->data_num))
			goto out;

		b = nla_nest_start(skb, MTK_VENDOR_ATTR_CSI_DATA_I);
		if (!b)
			goto out;

		for (i = 0; i < csi->data_num; i++)
			if (nla_put_u16(skb, i, csi->data_i[i]))
				goto out;
		nla_nest_end(skb, b);

		b = nla_nest_start(skb, MTK_VENDOR_ATTR_CSI_DATA_Q);
		if (!b)
			goto out;

		for (i = 0; i < csi->data_num; i++)
			if (nla_put_u16(skb, i, csi->data_q[i]))
				goto out;
		nla_nest_end(skb, b);

		nla_nest_end(skb, a);

		list_del(&csi->node);
		kfree(csi);
		phy->csi.count--;

		spin_unlock_bh(&phy->csi.lock);
		return skb->len;
	} else
		err = -ENOENT;

out:
	spin_unlock_bh(&phy->csi.lock);

	return err;
}

static int mt7996_parse_afc_table(struct mt7996_dev *dev, struct nlattr *tb, int delta)
{
	int ch, bw, err = 0;
	struct mt76_dev *mdev = &dev->mt76;
	s8 *table;

	if (!mdev->afc_power_table)
		err = mt7996_alloc_afc_table(dev);

	if (err) {
		mt7996_free_afc_table(dev);
		return err;
	}

	table = nla_data(tb);

	for (ch = 0; ch < MAX_CHANNEL_NUM_6G; ch++) {
		memcpy(mdev->afc_power_table[ch], table + ch * afc_power_table_num,
		       afc_power_table_num * sizeof(s8));
		for (bw = 0; bw < afc_power_table_num; bw++)
			if (mdev->afc_power_table[ch][bw] != AFC_INVALID_POWER)
				mdev->afc_power_table[ch][bw] -= delta;
	}

	return 0;
}


static int mt7996_vendor_txpower_ctrl(struct wiphy *wiphy,
				      struct wireless_dev *wdev,
				      const void *data,
				      int data_len)
{
#define FR_RATE_IDX_OFDM_6M 0x004b
	struct mt7996_dev *dev;
	struct mt7996_phy *phy;
	struct mt76_phy *mphy;
	struct ieee80211_vif *vif = wdev_to_ieee80211_vif(wdev);
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_vif_link *mconf;
	struct nlattr *tb[NUM_MTK_VENDOR_ATTRS_TXPOWER_CTRL], *table;
	int err, delta;
	u8 val, link_id = 0, idx;
	s8 new_power_limit = 127;

	err = nla_parse(tb, MTK_VENDOR_ATTR_TXPOWER_CTRL_MAX, data, data_len,
			mt76_vendor_txpower_ctrl_policy, NULL);
	if (err)
		return err;


	if (ieee80211_vif_is_mld(vif) && tb[MTK_VENDOR_ATTR_TXPOWER_CTRL_LINK_ID]) {
		link_id = nla_get_u8(tb[MTK_VENDOR_ATTR_TXPOWER_CTRL_LINK_ID]);

		if (link_id >= IEEE80211_LINK_UNSPECIFIED)
			return -EINVAL;
	}

	rcu_read_lock();
	mconf = (struct mt7996_vif_link *)rcu_dereference(mvif->mt76.link[link_id]);
	if (!mconf || !mconf->phy) {
		rcu_read_unlock();
		return -EINVAL;
	}

	phy = mconf->phy;
	rcu_read_unlock();

	mphy = phy->mt76;
	dev = phy->dev;
	delta = mt76_tx_power_path_delta(hweight16(mphy->chainmask));

	if (mphy->cap.has_6ghz &&
	    tb[MTK_VENDOR_ATTR_TXPOWER_CTRL_AFC_LPI]) {
		val = nla_get_u8(tb[MTK_VENDOR_ATTR_TXPOWER_CTRL_AFC_LPI]);
		mphy->dev->lpi_mode = !!val;
	}

	if (mphy->cap.has_6ghz &&
	    tb[MTK_VENDOR_ATTR_TXPOWER_CTRL_LPI_PSD]) {
		val = nla_get_u8(tb[MTK_VENDOR_ATTR_TXPOWER_CTRL_LPI_PSD]);
		mphy->dev->lpi_psd = val;

		err = mt7996_mcu_set_lpi_psd(phy, val);
		if (err)
			return err;
	}

	if (tb[MTK_VENDOR_ATTR_TXPOWER_CTRL_SKU_FILENAME]) {
		err = nla_strscpy(mphy->sku_filename,
				  tb[MTK_VENDOR_ATTR_TXPOWER_CTRL_SKU_FILENAME],
				  SKU_FILENAME_MAX);

		mphy->sku_idx = 0;
		if (err > 0 && mphy->sku_filename[0]) {
			struct ieee80211_channel *chan = mphy->main_chandef.chan;

			mt7996_init_txpower(phy);
			phy->sku_limit_en = true;
			phy->sku_path_en = true;
			new_power_limit = chan->max_power;
		} else {
			mphy->sku_filename[0] = '\0';
			mt7996_init_txpower(phy);
			phy->sku_limit_en = false;
			phy->sku_path_en = false;
		}
	} else if (tb[MTK_VENDOR_ATTR_TXPOWER_CTRL_SKU_IDX]) {
		u8 orig_sku_idx = mphy->sku_idx;
		struct ieee80211_channel *chan = mphy->main_chandef.chan;

		mphy->sku_idx = nla_get_u8(tb[MTK_VENDOR_ATTR_TXPOWER_CTRL_SKU_IDX]);

		if (mt76_find_power_limits_node(mphy) == NULL)
			mphy->sku_idx = 0;

		mphy->sku_filename[0] = '\0';
		if (orig_sku_idx != mphy->sku_idx)
			mt7996_init_txpower(phy);
		new_power_limit = chan->max_power;

		phy->sku_limit_en = true;
		phy->sku_path_en = true;
	} else {
		mphy->sku_idx = 0;
		mphy->sku_filename[0] = '\0';
		mt7996_init_txpower(phy);
		phy->sku_limit_en = false;
		phy->sku_path_en = false;
	}

	err = mt7996_mcu_set_tx_power_ctrl(phy, UNI_TXPOWER_SKU_POWER_LIMIT_CTRL,
					   phy->sku_limit_en);
	if (err)
		return err;
	err = mt7996_mcu_set_tx_power_ctrl(phy, UNI_TXPOWER_BACKOFF_POWER_LIMIT_CTRL,
					   phy->sku_path_en);
	if (err)
		return err;

	if (mphy->cap.has_6ghz &&
	    tb[MTK_VENDOR_ATTR_TXPOWER_CTRL_LPI_BCN_ENHANCE]) {
		val = nla_get_u8(tb[MTK_VENDOR_ATTR_TXPOWER_CTRL_LPI_BCN_ENHANCE]);
		mphy->dev->lpi_bcn_enhance = val;
		idx = MT7996_BEACON_RATES_TBL + 2 * phy->mt76->band_idx;

		err = mt7996_mcu_set_fixed_rate_table(phy, idx, FR_RATE_IDX_OFDM_6M,
						      true);
		if (err)
			return err;
	}

	if (mphy->cap.has_6ghz) {
		table = tb[MTK_VENDOR_ATTR_TXPOWER_CTRL_AFC_TABLE];
		if (table) {
			err = mt7996_parse_afc_table(dev, table, delta);
			if (err)
				return err;
		} else
			mt7996_free_afc_table(dev);
	}

	new_power_limit = min_t(s8, phy->txpower, new_power_limit);
	err = mt7996_mcu_set_txpower_sku(phy, new_power_limit);

	return err;
}

static const struct wiphy_vendor_command mt7996_vendor_commands[] = {
	{
		.info = {
			.vendor_id = MTK_NL80211_VENDOR_ID,
			.subcmd = MTK_NL80211_VENDOR_SUBCMD_MU_CTRL,
		},
		.flags = WIPHY_VENDOR_CMD_NEED_NETDEV |
			 WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = mt76_vendor_mu_ctrl,
		.dumpit = mt76_vendor_mu_ctrl_dump,
		.policy = mt76_vendor_mu_ctrl_policy,
		.maxattr = MTK_VENDOR_ATTR_MU_CTRL_MAX,
	},
	{
		.info = {
		        .vendor_id = MTK_NL80211_VENDOR_ID,
		        .subcmd = MTK_NL80211_VENDOR_SUBCMD_WIRELESS_CTRL,
		},
		.flags = WIPHY_VENDOR_CMD_NEED_NETDEV |
		        WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = mt7996_vendor_wireless_ctrl,
		.dumpit = mt76_vendor_wireless_ctrl_dump,
		.policy = mt76_vendor_wireless_ctrl_policy,
		.maxattr = MTK_VENDOR_ATTR_WIRELESS_CTRL_MAX,
	},
	{
		.info = {
			.vendor_id = MTK_NL80211_VENDOR_ID,
			.subcmd = MTK_NL80211_VENDOR_SUBCMD_AMNT_CTRL,
		},
		.flags = WIPHY_VENDOR_CMD_NEED_NETDEV |
			 WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = mt7996_vendor_amnt_ctrl,
		.dumpit = mt7996_vendor_amnt_ctrl_dump,
		.policy = mt76_vendor_amnt_ctrl_policy,
		.maxattr = MTK_VENDOR_ATTR_AMNT_CTRL_MAX,
	},
	{
		.info = {
			.vendor_id = MTK_NL80211_VENDOR_ID,
			.subcmd = MTK_NL80211_VENDOR_SUBCMD_BSS_COLOR_CTRL,
		},
		.flags = WIPHY_VENDOR_CMD_NEED_NETDEV |
			WIPHY_VENDOR_CMD_NEED_RUNNING,
		.dumpit = mt76_vendor_bss_color_ctrl_dump,
		.policy = mt76_vendor_bss_color_ctrl_policy,
		.maxattr = MTK_VENDOR_ATTR_BSS_COLOR_CTRL_MAX,
	},
	{
		.info = {
			.vendor_id = MTK_NL80211_VENDOR_ID,
			.subcmd = MTK_NL80211_VENDOR_SUBCMD_EDCCA_CTRL,
		},
		.flags = WIPHY_VENDOR_CMD_NEED_NETDEV |
			 WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = mt76_vendor_edcca_ctrl,
		.dumpit = mt76_vendor_edcca_ctrl_dump,
		.policy = mt76_vendor_edcca_ctrl_policy,
		.maxattr = MTK_VENDOR_ATTR_EDCCA_CTRL_MAX,
	},
	{
		.info = {
			.vendor_id = MTK_NL80211_VENDOR_ID,
			.subcmd = MTK_NL80211_VENDOR_SUBCMD_3WIRE_CTRL,
		},
		.flags = WIPHY_VENDOR_CMD_NEED_NETDEV |
			 WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = mt76_vendor_3wire_ctrl,
		.policy = mt76_vendor_3wire_ctrl_policy,
		.maxattr = MTK_VENDOR_ATTR_3WIRE_CTRL_MAX,
	},
	{
		.info = {
			.vendor_id = MTK_NL80211_VENDOR_ID,
			.subcmd = MTK_NL80211_VENDOR_SUBCMD_IBF_CTRL,
		},
		.flags = WIPHY_VENDOR_CMD_NEED_NETDEV |
			WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = mt76_vendor_ibf_ctrl,
		.dumpit = mt76_vendor_ibf_ctrl_dump,
		.policy = mt76_vendor_ibf_ctrl_policy,
		.maxattr = MTK_VENDOR_ATTR_IBF_CTRL_MAX,
	},
	{
		.info = {
			.vendor_id = MTK_NL80211_VENDOR_ID,
			.subcmd = MTK_NL80211_VENDOR_SUBCMD_PP_CTRL,
		},
		.flags = WIPHY_VENDOR_CMD_NEED_NETDEV |
			WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = mt7996_vendor_pp_ctrl,
		.policy = mt76_vendor_pp_ctrl_policy,
		.maxattr = MTK_VENDOR_ATTR_PP_CTRL_MAX,
	},
	{
		.info = {
			.vendor_id = MTK_NL80211_VENDOR_ID,
			.subcmd = MTK_NL80211_VENDOR_SUBCMD_RFEATURE_CTRL,
		},
		.flags = WIPHY_VENDOR_CMD_NEED_NETDEV |
			WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = mt7996_vendor_rfeature_ctrl,
		.policy = mt76_vendor_rfeature_ctrl_policy,
		.maxattr = MTK_VENDOR_ATTR_RFEATURE_CTRL_MAX,
	},
	{
		.info = {
			.vendor_id = MTK_NL80211_VENDOR_ID,
			.subcmd = MTK_NL80211_VENDOR_SUBCMD_BACKGROUND_RADAR_CTRL,
		},
		.flags = WIPHY_VENDOR_CMD_NEED_NETDEV |
			WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = mt76_vendor_background_radar_ctrl,
		.policy = mt76_vendor_background_radar_ctrl_policy,
		.maxattr = MTK_VENDOR_ATTR_BACKGROUND_RADAR_CTRL_MAX,
	},
	{
		.info = {
			.vendor_id = MTK_NL80211_VENDOR_ID,
			.subcmd = MTK_NL80211_VENDOR_SUBCMD_BEACON_CTRL,
		},
		.flags = WIPHY_VENDOR_CMD_NEED_NETDEV |
			 WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = mt76_vendor_beacon_ctrl,
		.policy = mt76_vendor_beacon_ctrl_policy,
		.maxattr = MTK_VENDOR_ATTR_BEACON_CTRL_MAX,
	},
	{
		.info = {
			.vendor_id = MTK_NL80211_VENDOR_ID,
			.subcmd = MTK_NL80211_VENDOR_SUBCMD_CSI_CTRL,
		},
		.flags = WIPHY_VENDOR_CMD_NEED_NETDEV |
			 WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = mt7996_vendor_csi_ctrl,
		.dumpit = mt7996_vendor_csi_ctrl_dump,
		.policy = mt76_vendor_csi_ctrl_policy,
		.maxattr = MTK_VENDOR_ATTR_CSI_CTRL_MAX,
	},
	{
		.info = {
			.vendor_id = MTK_NL80211_VENDOR_ID,
			.subcmd = MTK_NL80211_VENDOR_SUBCMD_EPCS_CTRL,
		},
		.flags = WIPHY_VENDOR_CMD_NEED_NETDEV |
			 WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = mt7996_vendor_epcs_ctrl,
		.policy = mt76_vendor_epcs_ctrl_policy,
		.maxattr = MTK_VENDOR_ATTR_EPCS_CTRL_MAX,
	},
	{
		.info = {
			.vendor_id = MTK_NL80211_VENDOR_ID,
			.subcmd = MTK_NL80211_VENDOR_SUBCMD_TXPOWER_CTRL,
		},
		.flags = WIPHY_VENDOR_CMD_NEED_NETDEV |
			 WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = mt7996_vendor_txpower_ctrl,
		.policy = mt76_vendor_txpower_ctrl_policy,
		.maxattr = MTK_VENDOR_ATTR_TXPOWER_CTRL_MAX,
	},
	{
		.info = {
			.vendor_id = MTK_NL80211_VENDOR_ID,
			.subcmd = MTK_NL80211_VENDOR_SUBCMD_SCS_CTRL,
		},
		.flags = WIPHY_VENDOR_CMD_NEED_NETDEV |
			 WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = mt7996_vendor_scs_ctrl,
		.policy = mt76_vendor_scs_ctrl_policy,
		.maxattr = MTK_VENDOR_ATTR_SCS_CTRL_MAX,
	},
	{
		.info = {
			.vendor_id = MTK_NL80211_VENDOR_ID,
			.subcmd = MTK_NL80211_VENDOR_SUBCMD_DFS_TX_CTRL,
		},
		.flags = WIPHY_VENDOR_CMD_NEED_NETDEV |
			WIPHY_VENDOR_CMD_NEED_RUNNING,
		.doit = mt76_vendor_dfs_tx_ctrl,
		.policy = mt76_vendor_dfs_tx_ctrl_policy,
		.maxattr = MTK_VENDOR_ATTR_DFS_TX_CTRL_MAX,
	},
};

static int mt7996_vendor_ibf_ctrl(struct mt76_dev *mdev, bool enabled)
{
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);

	dev->ibf = enabled;
	return mt7996_mcu_set_txbf(dev, BF_HW_EN_UPDATE);
}

static int mt7996_vendor_ibf_ctrl_dump(struct mt76_dev *mdev, bool *enabled)
{
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);

	*enabled = dev->ibf;
	return 0;
}

static int mt7996_vendor_mu_ctrl(struct mt76_dev *mdev, u8 radio_idx, u8 muru_onoff)
{
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);
	struct mt7996_phy *phy;
	u32 value;

	if (!mt7996_radio_valid(dev, radio_idx))
		return -EINVAL;

	phy = dev->radio_phy[radio_idx];
	value = FIELD_PREP(RATE_CFG_MODE, RATE_PARAM_AUTO_MU) |
		FIELD_PREP(RATE_CFG_VAL, muru_onoff) |
		FIELD_PREP(RATE_CFG_BAND_IDX, phy->mt76->band_idx);
	ieee80211_iterate_active_interfaces_atomic(dev->mt76.hw,
						   IEEE80211_IFACE_ITER_RESUME_ALL,
						   mt7996_set_wireless_vif, &value);

	return 0;
}

static int mt7996_vendor_mu_ctrl_cfg(struct mt76_dev *mdev,
				     const struct nlattr *attr)
{
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);
	struct mt7996_muru *muru;
	int err;

	muru = kzalloc(sizeof(*muru), GFP_KERNEL);
	if (!muru)
		return -ENOMEM;

	nla_memcpy(muru, attr, sizeof(*muru));
	err = mt7996_mcu_set_muru_cfg(dev, muru);
	kfree(muru);

	return err;
}

static int mt7996_vendor_mu_ctrl_dump(struct mt76_dev *mdev, u8 radio_idx,
				      u8 *muru_onoff)
{
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);

	if (!mt7996_radio_valid(dev, radio_idx))
		return -EINVAL;

	*muru_onoff = dev->radio_phy[radio_idx]->muru_onoff;
	return 0;
}

static int mt7996_vendor_edcca_ctrl(struct mt76_dev *mdev, u8 radio_idx,
				    bool enabled)
{
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);

	if (!mt7996_radio_valid(dev, radio_idx))
		return -EINVAL;

	return mt7996_mcu_edcca_enable(dev->radio_phy[radio_idx], enabled);
}

static int mt7996_vendor_edcca_ctrl_threshold(struct mt76_dev *mdev, u8 radio_idx,
					      u8 value[MT76_VENDOR_EDCCA_BW_NUM])
{
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);
	struct mt7996_phy *phy;

	if (!mt7996_radio_valid(dev, radio_idx))
		return -EINVAL;

	phy = dev->radio_phy[radio_idx];
	memcpy(phy->user_edcca_threshold, value,
	       sizeof(phy->user_edcca_threshold));

	return mt7996_mcu_edcca_threshold_ctrl(phy,
					       phy->user_edcca_threshold, true);
}

static int mt7996_vendor_edcca_ctrl_dump(struct mt76_dev *mdev, u8 radio_idx,
					 u8 value[MT76_VENDOR_EDCCA_BW_NUM])
{
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);

	if (!mt7996_radio_valid(dev, radio_idx))
		return -EINVAL;

	return mt7996_mcu_edcca_threshold_ctrl(dev->radio_phy[radio_idx], value,
					       false);
}

static int mt7996_vendor_background_radar_ctrl(struct mt76_dev *mdev,
					       bool disable_timer)
{
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);

	return mt7996_mcu_rdd_background_disable_timer(dev, disable_timer);
}

static int mt7996_vendor_dfs_tx_ctrl(struct mt76_phy *mphy, u8 radio_idx, u8 mode)
{
	struct mt7996_phy *phy = mphy->priv;
	struct mt7996_dev *dev = phy->dev;
	int rdd_idx, err;

	if (!mt7996_radio_valid(dev, radio_idx)) {
		dev_err(dev->mt76.dev, "Invalid radio idx %d\n", radio_idx);
		return -EINVAL;
	}

	phy = dev->radio_phy[radio_idx];
	rdd_idx = mt7996_get_rdd_idx(phy, false);
	if (rdd_idx < 0) {
		dev_err(dev->mt76.dev, "Invalid rdd idx %d\n", rdd_idx);
		return -EINVAL;
	}

	err = mt7996_mcu_rdd_cmd(dev, RDD_DET_MODE, rdd_idx, mode);
	if (!err)
		phy->dfs_tx_mode = mode;

	return err;
}

static const struct mt76_vendor_ops mt7996_vendor_ops = {
	.commands = mt7996_vendor_commands,
	.n_commands = ARRAY_SIZE(mt7996_vendor_commands),
	.events = mt76_vendor_events,
	.n_events = MT76_VENDOR_N_EVENTS,
	.ibf_ctrl = mt7996_vendor_ibf_ctrl,
	.ibf_ctrl_dump = mt7996_vendor_ibf_ctrl_dump,
	.mu_ctrl = mt7996_vendor_mu_ctrl,
	.mu_ctrl_cfg = mt7996_vendor_mu_ctrl_cfg,
	.mu_ctrl_dump = mt7996_vendor_mu_ctrl_dump,
	.edcca_ctrl = mt7996_vendor_edcca_ctrl,
	.edcca_ctrl_threshold = mt7996_vendor_edcca_ctrl_threshold,
	.edcca_ctrl_dump = mt7996_vendor_edcca_ctrl_dump,
	.background_radar_ctrl = mt7996_vendor_background_radar_ctrl,
	.dfs_tx_ctrl = mt7996_vendor_dfs_tx_ctrl,
	.set_beacon = mt7996_set_beacon_vif,
};

void mt7996_vendor_register(struct mt7996_phy *phy)
{
	if (phy == &phy->dev->phy)
		mt76_vendor_register(phy->mt76, &mt7996_vendor_ops);

	INIT_LIST_HEAD(&phy->csi.list);
	spin_lock_init(&phy->csi.lock);

	spin_lock_init(&phy->amnt_lock);
}
#endif
