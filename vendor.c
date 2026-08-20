// SPDX-License-Identifier: ISC

#include "mt76.h"
#include "vendor.h"

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
