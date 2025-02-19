/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2024 MediaTek Inc.
 */
#ifndef __MT76_DEBUG_H_
#define __MT76_DEBUG_H_

struct mt76_dev;

enum mt76_debug_mask {
	MT76_DBG_DEV = BIT(0),
	MT76_DBG_BSS = BIT(1),
	MT76_DBG_STA = BIT(2),
	MT76_DBG_CHAN = BIT(3),
	MT76_DBG_MLD = BIT(4),
	MT76_DBG_TXRX = BIT(5),
	MT76_DBG_SCAN = BIT(6),
	MT76_DBG_TEST = BIT(7),

	MT76_DBG_ALL = 0xffffffff,
};

__printf(2, 3) void mt76_info(struct mt76_dev *dev, const char *fmt, ...);
__printf(2, 3) void mt76_err(struct mt76_dev *dev, const char *fmt, ...);
__printf(2, 3) void mt76_warn(struct mt76_dev *dev, const char *fmt, ...);

__printf(2, 3) void __mt76_dbg(struct mt76_dev *dev,
			       const char *fmt, ...);
void mt76_dbg_dump(struct mt76_dev *dev,
		   enum mt76_debug_mask mask,
		   const char *msg, const char *prefix,
		   const void *buf, size_t len);

#define mt76_dbg(dev, dbg_mask, fmt, ...)			\
do {								\
	typeof(dbg_mask) mask = (dbg_mask);			\
	typeof(dev) _dev = (dev);				\
	if ((_dev->debug_mask) & mask)				\
		__mt76_dbg(_dev, fmt, ##__VA_ARGS__);	\
} while (0)

#endif /* __MT76_DEBUG_H_ */
