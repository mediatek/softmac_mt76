// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2024 MediaTek Inc.
 */

#include <linux/vmalloc.h>
#include "mt76.h"

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

void __mt76_dbg(struct mt76_dev *dev, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	dev_printk(KERN_DEBUG, dev->dev, "%pV", &vaf);

	va_end(args);
}
EXPORT_SYMBOL_GPL(__mt76_dbg);
