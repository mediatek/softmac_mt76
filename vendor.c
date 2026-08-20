// SPDX-License-Identifier: ISC

#include "mt76.h"
#include "vendor.h"

#ifdef CONFIG_MTK_VENDOR
const struct nla_policy
mt76_vendor_bss_color_ctrl_policy[NUM_MTK_VENDOR_ATTRS_BSS_COLOR_CTRL] = {
	[MTK_VENDOR_ATTR_AVAL_BSS_COLOR_BMP] = { .type = NLA_U64 },
	[MTK_VENDOR_ATTR_AVAL_BSS_COLOR_LINK_ID] = { .type = NLA_U8 },
};
EXPORT_SYMBOL_GPL(mt76_vendor_bss_color_ctrl_policy);

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
