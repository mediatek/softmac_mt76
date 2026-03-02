// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2024 MediaTek Inc.
 */

#include <linux/vmalloc.h>
#include "mt76.h"

unsigned int mt76_debug_mask = 0x3f;
module_param(mt76_debug_mask, uint, 0644);
MODULE_PARM_DESC(mt76_debug_mask, "Debugging mask");

void mt76_info(struct mt76_dev *dev, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	dev_info(dev->dev, "%pV", &vaf);

	va_end(args);
}
EXPORT_SYMBOL_GPL(mt76_info);

void mt76_err(struct mt76_dev *dev, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	dev_err(dev->dev, "%pV", &vaf);

	va_end(args);
}
EXPORT_SYMBOL_GPL(mt76_err);

void mt76_warn(struct mt76_dev *dev, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	dev_warn_ratelimited(dev->dev, "%pV", &vaf);

	va_end(args);
}
EXPORT_SYMBOL_GPL(mt76_warn);

void __mt76_dbg(enum mt76_debug_mask mask, struct mt76_dev *dev, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	if (!(mask & mt76_debug_mask))
		return;

	va_start(args, fmt);
	vaf.va = &args;

	if (dev)
		dev_printk(KERN_DEBUG, dev->dev, "%pV", &vaf);
	else
		pr_info("%pV", &vaf);

	va_end(args);
}
EXPORT_SYMBOL_GPL(__mt76_dbg);

#ifdef CONFIG_MTK_DEBUG

void mt76_dbg_record_tx_stats(struct mt76_phy *phy, enum mt76_tx_debug_stats reason)
{
	switch (reason) {
	case MT_TX_FROM_MAC80211:
	case MT_TX_TO_HW:
		break;
	/* use phy0 for per-dev stats */
	case MT_TX_DROP_GET_TXWI_FAIL:
	case MT_TX_DROP_DMA_FAIL:
	case MT_TX_DROP_AGG_EXCEEDED:
	case MT_TX_DROP_GET_TOKEN_FAIL:
		phy = &phy->dev->phy;
		break;
	default:
		break;
	}

	spin_lock_bh(&phy->tx_dbg_stats.lock);
	phy->tx_dbg_stats.data[reason]++;
	spin_unlock_bh(&phy->tx_dbg_stats.lock);
}
EXPORT_SYMBOL_GPL(mt76_dbg_record_tx_stats);

void mt76_dbg_record_rx_stats(struct mt76_phy *phy, enum mt76_rx_debug_stats reason)
{
	spin_lock_bh(&phy->rx_dbg_stats.lock);
	phy->rx_dbg_stats.data[reason]++;
	spin_unlock_bh(&phy->rx_dbg_stats.lock);
}
EXPORT_SYMBOL_GPL(mt76_dbg_record_rx_stats);

void mt76_dbg_record_rx_stats_q(struct mt76_queue *q, enum mt76_rx_debug_stats reason)
{
	q->rx_drop[reason]++;
}
EXPORT_SYMBOL_GPL(mt76_dbg_record_rx_stats_q);

#endif
