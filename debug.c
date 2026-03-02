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
