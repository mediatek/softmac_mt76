/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (C) 2024-2026 MediaTek Inc.
 */
#ifndef __MT76_DEBUG_H_
#define __MT76_DEBUG_H_

struct mt76_dev;
struct mt76_phy;
struct mt76_queue;

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

enum mt76_tx_debug_stats {
	MT_TX_FROM_MAC80211,
	MT_TX_TO_HW,

	/* tx drop reason */
	MT_TX_DROP_IN_TESTMODE,
	MT_TX_DROP_WCID_NOT_INIT,
	MT_TX_DROP_STOPPED_QUEUE,
	MT_TX_DROP_RESET_STATE,
	MT_TX_DROP_GET_TXWI_FAIL,
	MT_TX_DROP_DMA_FAIL,
	MT_TX_DROP_AGG_EXCEEDED,
	MT_TX_DROP_RING_FULL,
	MT_TX_DROP_INVALID_SKB,
	MT_TX_DROP_GET_TOKEN_FAIL,
	MT_TX_DROP_ADDR_TRANS_FAIL,
	MT_TX_DROP_INVALID_WCID,
	MT_TX_DROP_INVALID_LINK,

	MT_TX_DEBUG_STATS_MAX,
};

struct mt76_tx_debug {
	u32 data[MT_TX_DEBUG_STATS_MAX];
	spinlock_t lock;
};

enum mt76_rx_debug_stats {
	/* Per phy counters */
	MT_RX_FROM_HW,
	MT_RX_TO_MAC80211,

	/* rx drop reason */
	MT_RX_DROP_RXD_ERR,
	MT_RX_DROP_STATE_ERR,
	MT_RX_DROP_RFC_PKT,
	MT_RX_DROP_AGG_SN_LESS,
	MT_RX_DROP_AGG_DUP,

	MT_RX_DEBUG_STATS_PER_PHY_MAX,

	/* Per dev counters*/
	MT_RX_DROP_DMAD_RRO_REPEAT = 0,
	MT_RX_DROP_DMAD_RRO_OLDPKT,
	MT_RX_DROP_DMAD_RRO_PN_CHK_FAIL,
	MT_RX_DROP_DMAD_WO_FRAG,
	MT_RX_DROP_DMAD_WO_DROP,
	MT_RX_DROP_DMAD_ADDR_NOT_FOUND,
	MT_RX_DROP_DMAD_TOKEN_NOT_FOUND,
	MT_RX_DROP_DMAD_GET_TOKEN_FAIL,
	MT_RX_DROP_DMAD_GET_RXWI_FAIL,
	MT_RX_DROP_DMAD_NOMEM,
	MT_RX_DROP_DMAD_DMA_MAPPING_FAIL,
	MT_RX_DROP_FRAG,
	MT_RX_DROP_BUILD_SKB_FAIL,

	MT_RX_DEBUG_STATS_PER_Q_MAX,
};

struct mt76_rx_debug {
	u32 data[MT_RX_DEBUG_STATS_PER_PHY_MAX];
	spinlock_t lock;
};

#ifdef CONFIG_MTK_DEBUG
void mt76_dbg_record_tx_stats(struct mt76_phy *phy, enum mt76_tx_debug_stats reason);
void mt76_dbg_record_rx_stats(struct mt76_phy *phy, enum mt76_rx_debug_stats reason);
void mt76_dbg_record_rx_stats_q(struct mt76_queue *q, enum mt76_rx_debug_stats reason);
#else
static inline void
mt76_dbg_record_tx_stats(struct mt76_phy *phy, enum mt76_tx_debug_stats reason)
{
}
static inline void
mt76_dbg_record_rx_stats(struct mt76_phy *phy, enum mt76_rx_debug_stats reason)
{
}
static inline void
mt76_dbg_record_rx_stats_q(struct mt76_queue *q, enum mt76_rx_debug_stats reason)
{
}
#endif

#endif /* __MT76_DEBUG_H_ */
