/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 */

#if !defined(__MT76_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __MT76_TRACE_H

#include <linux/tracepoint.h>
#include "mt76.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mt76

#define MAXNAME		32
#define DEV_ENTRY	__array(char, wiphy_name, 32)
#define DEVICE_ASSIGN	strscpy(__entry->wiphy_name,	\
				wiphy_name(dev->hw->wiphy), MAXNAME)
#define DEV_PR_FMT	"%s"
#define DEV_PR_ARG	__entry->wiphy_name

#define REG_ENTRY	__field(u32, reg) __field(u32, val)
#define REG_ASSIGN	__entry->reg = reg; __entry->val = val
#define REG_PR_FMT	" %04x=%08x"
#define REG_PR_ARG	__entry->reg, __entry->val

#define TXID_ENTRY	__field(u8, wcid) __field(u8, pktid)
#define TXID_ASSIGN	__entry->wcid = wcid; __entry->pktid = pktid
#define TXID_PR_FMT	" [%d:%d]"
#define TXID_PR_ARG	__entry->wcid, __entry->pktid

DECLARE_EVENT_CLASS(dev_reg_evt,
	TP_PROTO(struct mt76_dev *dev, u32 reg, u32 val),
	TP_ARGS(dev, reg, val),
	TP_STRUCT__entry(
		DEV_ENTRY
		REG_ENTRY
	),
	TP_fast_assign(
		DEVICE_ASSIGN;
		REG_ASSIGN;
	),
	TP_printk(
		DEV_PR_FMT REG_PR_FMT,
		DEV_PR_ARG, REG_PR_ARG
	)
);

DEFINE_EVENT(dev_reg_evt, reg_rr,
	TP_PROTO(struct mt76_dev *dev, u32 reg, u32 val),
	TP_ARGS(dev, reg, val)
);

DEFINE_EVENT(dev_reg_evt, reg_wr,
	TP_PROTO(struct mt76_dev *dev, u32 reg, u32 val),
	TP_ARGS(dev, reg, val)
);

TRACE_EVENT(dev_irq,
	TP_PROTO(struct mt76_dev *dev, u32 val, u32 mask),

	TP_ARGS(dev, val, mask),

	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u32, val)
		__field(u32, mask)
	),

	TP_fast_assign(
		DEVICE_ASSIGN;
		__entry->val = val;
		__entry->mask = mask;
	),

	TP_printk(
		DEV_PR_FMT " %08x & %08x",
		DEV_PR_ARG, __entry->val, __entry->mask
	)
);

DECLARE_EVENT_CLASS(dev_txid_evt,
	TP_PROTO(struct mt76_dev *dev, u8 wcid, u8 pktid),
	TP_ARGS(dev, wcid, pktid),
	TP_STRUCT__entry(
		DEV_ENTRY
		TXID_ENTRY
	),
	TP_fast_assign(
		DEVICE_ASSIGN;
		TXID_ASSIGN;
	),
	TP_printk(
		DEV_PR_FMT TXID_PR_FMT,
		DEV_PR_ARG, TXID_PR_ARG
	)
);

DEFINE_EVENT(dev_txid_evt, mac_txdone,
	TP_PROTO(struct mt76_dev *dev, u8 wcid, u8 pktid),
	TP_ARGS(dev, wcid, pktid)
);

TRACE_EVENT(mt76_rx_complete,
	TP_PROTO(struct mt76_dev *dev, struct mt76_rx_status *status, bool sub),
	TP_ARGS(dev, status, sub),

	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u16, seqno)
		__field(u16, wcid)
		__field(u8, first_amsdu)
		__field(bool, sub)
	),

	TP_fast_assign(
		strscpy(__entry->wiphy_name,
			wiphy_name(mt76_dev_phy(dev, status->phy_idx)->hw->wiphy),
			MAXNAME);
		__entry->seqno = status->seqno;
		__entry->wcid = status->wcid ? status->wcid->idx : 0;
		__entry->first_amsdu = status->first_amsdu;
		__entry->sub = sub;
	),

	TP_printk(
		DEV_PR_FMT " seqno: %u, wcid: %u, first_amsdu: %x, sub: %d",
		DEV_PR_ARG, __entry->seqno, __entry->wcid, __entry->first_amsdu,
		__entry->sub
	)
);

TRACE_EVENT(mt76_rx_aggr_reorder,
	TP_PROTO(struct mt76_dev *dev, struct mt76_wcid *wcid, u16 _head, u16 seqno, bool sn_less),
	TP_ARGS(dev, wcid, _head, seqno, sn_less),

	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u16, wcid)
		__field(u16, _head)
		__field(u16, seqno)
		__field(bool, sn_less)
	),

	TP_fast_assign(
		strscpy(__entry->wiphy_name,
			wiphy_name(dev->phys[wcid->phy_idx]->hw->wiphy),
			MAXNAME);
		__entry->wcid = wcid->idx;
		__entry->_head = _head;
		__entry->seqno = seqno;
		__entry->sn_less = sn_less;
	),

	TP_printk(
		DEV_PR_FMT " wcid: %u, head: %u, seqno: %u, sn_less: %d",
		DEV_PR_ARG, __entry->wcid, __entry->_head, __entry->seqno,
		__entry->sn_less
	)
);

#endif

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
