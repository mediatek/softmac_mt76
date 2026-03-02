/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2024 MediaTek Inc.
 */
#ifndef __MT76_DEBUG_H_
#define __MT76_DEBUG_H_

struct mt76_dev;

enum mt76_debug_mask {
	MT76_DBG_DEV = BIT(0),
	MT76_DBG_PHY = BIT(1),
	MT76_DBG_BSS = BIT(2),
	MT76_DBG_STA = BIT(3),
	MT76_DBG_MLO = BIT(4),
	MT76_DBG_TM = BIT(5),

	MT76_DBG_ALL = 0xffffffff,
};

__printf(2, 3) void mt76_info(struct mt76_dev *dev, const char *fmt, ...);
__printf(2, 3) void mt76_err(struct mt76_dev *dev, const char *fmt, ...);
__printf(2, 3) void mt76_warn(struct mt76_dev *dev, const char *fmt, ...);
__printf(3, 4) void __mt76_dbg(enum mt76_debug_mask mask, struct mt76_dev *dev,
			       const char *fmt, ...);

#define vif_name(_vif)	netdev_name(ieee80211_vif_to_wdev(_vif)->netdev)

#define mt76_dev_dbg(dev, fmt, ...)					\
	__mt76_dbg(MT76_DBG_DEV, dev, "%s: " fmt,			\
		   __func__, ##__VA_ARGS__)				\

#define mt76_phy_dbg(phy, fmt, ...)					\
	__mt76_dbg(MT76_DBG_PHY, phy->dev, "band %u: %s: " fmt,	\
		   phy->band_idx, __func__, ##__VA_ARGS__)		\

#define mt76_bss_dbg(vif, fmt, ...)					\
	__mt76_dbg(MT76_DBG_BSS, NULL, "%s: %s: " fmt,			\
		   vif_name(vif), __func__, ##__VA_ARGS__)		\

#define mt76_sta_dbg(vif, fmt, ...)					\
	__mt76_dbg(MT76_DBG_STA, NULL, "%s: %s: " fmt,			\
		   vif_name(vif), __func__, ##__VA_ARGS__)		\

#define mt76_mlo_dbg(vif, fmt, ...)					\
	__mt76_dbg(MT76_DBG_MLO, NULL, "%s: %s: " fmt,			\
		   vif_name(vif), __func__, ##__VA_ARGS__)		\

#define mt76_tm_dbg(dev, fmt, ...)					\
	__mt76_dbg(MT76_DBG_TM, dev, "%s: " fmt,			\
		   __func__, ##__VA_ARGS__)				\

#endif /* __MT76_DEBUG_H_ */
