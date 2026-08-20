// SPDX-License-Identifier: ISC

#include "mt76.h"
#include "mt76_connac_mcu.h"
#include "vendor.h"

#ifdef CONFIG_MTK_VENDOR
const struct nla_policy
mt76_vendor_bss_color_ctrl_policy[NUM_MTK_VENDOR_ATTRS_BSS_COLOR_CTRL] = {
	[MTK_VENDOR_ATTR_AVAL_BSS_COLOR_BMP] = { .type = NLA_U64 },
	[MTK_VENDOR_ATTR_AVAL_BSS_COLOR_LINK_ID] = { .type = NLA_U8 },
};
EXPORT_SYMBOL_GPL(mt76_vendor_bss_color_ctrl_policy);

const struct nla_policy
mt76_vendor_wireless_ctrl_policy[NUM_MTK_VENDOR_ATTRS_WIRELESS_CTRL] = {
	[MTK_VENDOR_ATTR_WIRELESS_CTRL_AMSDU] = { .type = NLA_U8 },
	[MTK_VENDOR_ATTR_WIRELESS_CTRL_AMPDU] = { .type = NLA_U8 },
	[MTK_VENDOR_ATTR_WIRELESS_CTRL_CERT] = { .type = NLA_U8 },
	[MTK_VENDOR_ATTR_WIRELESS_CTRL_RTS_SIGTA] = { .type = NLA_U8 },
	[MTK_VENDOR_ATTR_WIRELESS_CTRL_FIXED_MCS] = { .type = NLA_U8 },
	[MTK_VENDOR_ATTR_WIRELESS_CTRL_OFDMA] = { .type = NLA_U8 },
	[MTK_VENDOR_ATTR_WIRELESS_CTRL_PPDU_TX_TYPE] = { .type = NLA_U8 },
	[MTK_VENDOR_ATTR_WIRELESS_CTRL_NUSERS_OFDMA] = { .type = NLA_U8 },
	[MTK_VENDOR_ATTR_WIRELESS_CTRL_MIMO] = { .type = NLA_U8 },
	[MTK_VENDOR_ATTR_WIRELESS_CTRL_BA_BUFFER_SIZE] = { .type = NLA_U16 },
	[MTK_VENDOR_ATTR_WIRELESS_CTRL_LINK_ID] = { .type = NLA_U8 },
};
EXPORT_SYMBOL_GPL(mt76_vendor_wireless_ctrl_policy);

const struct nla_policy
mt76_vendor_ibf_ctrl_policy[NUM_MTK_VENDOR_ATTRS_IBF_CTRL] = {
	[MTK_VENDOR_ATTR_IBF_CTRL_ENABLE] = { .type = NLA_U8 },
};
EXPORT_SYMBOL_GPL(mt76_vendor_ibf_ctrl_policy);

const struct nla_policy
mt76_vendor_3wire_ctrl_policy[NUM_MTK_VENDOR_ATTRS_3WIRE_CTRL] = {
	[MTK_VENDOR_ATTR_3WIRE_EXT_SEL] = { .type = NLA_U8 },
	[MTK_VENDOR_ATTR_3WIRE_CEB_BAND_SEL] = { .type = NLA_U8 },
};
EXPORT_SYMBOL_GPL(mt76_vendor_3wire_ctrl_policy);

const struct nl80211_vendor_cmd_info mt76_vendor_events[MT76_VENDOR_N_EVENTS] = {
	[MTK_NL80211_VENDOR_EVENT_PP_BMP_UPDATE] = {
		.vendor_id = MTK_NL80211_VENDOR_ID,
		.subcmd = MTK_NL80211_VENDOR_EVENT_PP_BMP_UPDATE,
	},
};
EXPORT_SYMBOL_GPL(mt76_vendor_events);

int mt76_vendor_bss_color_ctrl_dump(struct wiphy *wiphy,
				    struct wireless_dev *wdev,
				    struct sk_buff *skb,
				    const void *data, int data_len,
				    unsigned long *storage)
{
	struct ieee80211_vif *vif = wdev_to_ieee80211_vif(wdev);
	struct ieee80211_bss_conf *bss_conf;
	struct nlattr *tb[NUM_MTK_VENDOR_ATTRS_BSS_COLOR_CTRL];
	u8 link_id = 0;
	int err;

	if (*storage == 1)
		return -ENOENT;
	*storage = 1;

	err = nla_parse(tb, MTK_VENDOR_ATTR_BSS_COLOR_CTRL_MAX, data, data_len,
			mt76_vendor_bss_color_ctrl_policy, NULL);
	if (err)
		return err;

	if (ieee80211_vif_is_mld(vif) &&
	    tb[MTK_VENDOR_ATTR_AVAL_BSS_COLOR_LINK_ID]) {
		link_id = nla_get_u8(tb[MTK_VENDOR_ATTR_AVAL_BSS_COLOR_LINK_ID]);
		if (link_id >= IEEE80211_LINK_UNSPECIFIED)
			return -EINVAL;
	}

	rcu_read_lock();
	bss_conf = rcu_dereference(vif->link_conf[link_id]);
	if (!bss_conf) {
		rcu_read_unlock();
		return -ENOLINK;
	}

	if (nla_put_u64_64bit(skb, MTK_VENDOR_ATTR_AVAL_BSS_COLOR_BMP,
			      ~bss_conf->used_color_bitmap, NL80211_ATTR_PAD)) {
		rcu_read_unlock();
		return -ENOMEM;
	}

	rcu_read_unlock();
	return 1;
}
EXPORT_SYMBOL_GPL(mt76_vendor_bss_color_ctrl_dump);

int mt76_vendor_pp_bitmap_update(struct mt76_phy *phy, u16 bitmap)
{
	struct cfg80211_chan_def *chandef = &phy->main_chandef;
	struct nlattr *data;
	struct sk_buff *skb;

	skb = cfg80211_vendor_event_alloc(phy->hw->wiphy, NULL, 20,
					  MTK_NL80211_VENDOR_EVENT_PP_BMP_UPDATE,
					  GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	if (nla_put_u32(skb, NL80211_ATTR_WIPHY_FREQ, chandef->chan->center_freq))
		goto out;

	data = nla_nest_start(skb, NL80211_ATTR_VENDOR_DATA);
	if (!data || nla_put_u16(skb, MTK_VENDOR_ATTR_PP_BITMAP, bitmap))
		goto out;

	nla_nest_end(skb, data);
	cfg80211_vendor_event(skb, GFP_ATOMIC);

	return 0;
out:
	dev_kfree_skb(skb);
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(mt76_vendor_pp_bitmap_update);

int mt76_vendor_ibf_ctrl(struct wiphy *wiphy, struct wireless_dev *wdev,
			 const void *data, int data_len)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt76_dev *dev = hw->priv;
	struct nlattr *tb[NUM_MTK_VENDOR_ATTRS_IBF_CTRL];
	const struct mt76_vendor_ops *ops = dev->vendor_ops;
	int err;

	if (!ops || !ops->ibf_ctrl)
		return -EOPNOTSUPP;

	err = nla_parse(tb, MTK_VENDOR_ATTR_IBF_CTRL_MAX, data, data_len,
			mt76_vendor_ibf_ctrl_policy, NULL);
	if (err)
		return err;

	if (!tb[MTK_VENDOR_ATTR_IBF_CTRL_ENABLE])
		return 0;

	return ops->ibf_ctrl(dev, nla_get_u8(tb[MTK_VENDOR_ATTR_IBF_CTRL_ENABLE]));
}
EXPORT_SYMBOL_GPL(mt76_vendor_ibf_ctrl);

int mt76_vendor_ibf_ctrl_dump(struct wiphy *wiphy,
			      struct wireless_dev *wdev,
			      struct sk_buff *skb, const void *data,
			      int data_len, unsigned long *storage)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt76_dev *dev = hw->priv;
	const struct mt76_vendor_ops *ops = dev->vendor_ops;
	bool enabled;
	int err;

	if (!ops || !ops->ibf_ctrl_dump)
		return -EOPNOTSUPP;

	if (*storage == 1)
		return -ENOENT;
	*storage = 1;

	err = ops->ibf_ctrl_dump(dev, &enabled);
	if (err)
		return err;

	if (nla_put_u8(skb, MTK_VENDOR_ATTR_IBF_DUMP_ENABLE, enabled))
		return -ENOMEM;

	return 1;
}
EXPORT_SYMBOL_GPL(mt76_vendor_ibf_ctrl_dump);

int mt76_vendor_3wire_ctrl(struct wiphy *wiphy, struct wireless_dev *wdev,
			   const void *data, int data_len)
{
#define UNI_PTA_3WIRE_CTRL_EXT_EN	0
#define EXT_SEL_MASK			GENMASK(2, 0)
#define CEB_BAND_SEL_MASK		GENMASK(4, 3)
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt76_dev *dev = hw->priv;
	struct nlattr *tb[NUM_MTK_VENDOR_ATTRS_3WIRE_CTRL];
	struct {
		u8 __rsv1[4];

		__le16 tag;
		__le16 len;
		u8 three_wire_mode;
	} __packed req = {
		.tag = cpu_to_le16(UNI_PTA_3WIRE_CTRL_EXT_EN),
		.len = cpu_to_le16(sizeof(req) - 4),
	};
	u8 ext_sel, ceb_band_sel;
	int err;

	err = nla_parse(tb, MTK_VENDOR_ATTR_3WIRE_CTRL_MAX, data, data_len,
			mt76_vendor_3wire_ctrl_policy, NULL);
	if (err)
		return err;

	if (!tb[MTK_VENDOR_ATTR_3WIRE_EXT_SEL] ||
	    !tb[MTK_VENDOR_ATTR_3WIRE_CEB_BAND_SEL])
		return -EINVAL;

	ext_sel = nla_get_u8(tb[MTK_VENDOR_ATTR_3WIRE_EXT_SEL]);
	ceb_band_sel = nla_get_u8(tb[MTK_VENDOR_ATTR_3WIRE_CEB_BAND_SEL]);
	req.three_wire_mode = u8_encode_bits(ext_sel, EXT_SEL_MASK) |
			      u8_encode_bits(ceb_band_sel, CEB_BAND_SEL_MASK);

	return mt76_mcu_send_msg(dev, MCU_WM_UNI_CMD(PTA_3WIRE_CTRL), &req,
				 sizeof(req), false);
}
EXPORT_SYMBOL_GPL(mt76_vendor_3wire_ctrl);

int mt76_vendor_wireless_ctrl_dump(struct wiphy *wiphy,
				   struct wireless_dev *wdev,
				   struct sk_buff *skb, const void *data,
				   int data_len, unsigned long *storage)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);

	if (*storage == 1)
		return -ENOENT;
	*storage = 1;

	if (nla_put_u8(skb, MTK_VENDOR_ATTR_WIRELESS_DUMP_AMSDU,
		       ieee80211_hw_check(hw, SUPPORTS_AMSDU_IN_AMPDU)))
		return -ENOMEM;

	return 1;
}
EXPORT_SYMBOL_GPL(mt76_vendor_wireless_ctrl_dump);
#endif

void mt76_vendor_register(struct mt76_dev *dev,
			  const struct mt76_vendor_ops *ops)
{
	struct wiphy *wiphy = dev->hw->wiphy;

	dev->vendor_ops = ops;

	wiphy->vendor_commands = ops->commands;
	wiphy->n_vendor_commands = ops->n_commands;
	wiphy->vendor_events = ops->events;
	wiphy->n_vendor_events = ops->n_events;
}
EXPORT_SYMBOL_GPL(mt76_vendor_register);
