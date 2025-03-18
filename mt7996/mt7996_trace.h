/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#if !defined(__MT7996_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __MT7996_TRACE_H

#include <linux/types.h>
#include <linux/tracepoint.h>
#include "mt7996.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mt7996

#define MAXNAME		32
#define DEV_ENTRY	__array(char, wiphy_name, 32)
#define DEV_ASSIGN(_w)	strlcpy(__entry->wiphy_name, wiphy_name(_w), MAXNAME)
#define DEV_PR_FMT	"%s"
#define DEV_PR_ARG	__entry->wiphy_name

DECLARE_EVENT_CLASS(mt7996_mcu_debug,
	TP_PROTO(struct mt7996_dev *dev, bool uni, u8 id, u8 ext_id,
		 const void *data, size_t len),
	TP_ARGS(dev, uni, id, ext_id, data, len),
	TP_STRUCT__entry(
		__field(bool, uni)
		__field(u8, id)
		__field(u8, ext_id)
		__field(size_t, len)
		__dynamic_array(u8, data, len)
	),
	TP_fast_assign(
		__entry->uni = uni;
		__entry->id = id;
		__entry->ext_id = ext_id;
		__entry->len = len;
		memcpy(__get_dynamic_array(data), data, len);
	),
	TP_printk(
		"uni: %d, id: %u, ext_id: %u, len: %zu",
		__entry->uni,
		__entry->id,
		__entry->ext_id,
		__entry->len
	)
);

DEFINE_EVENT(mt7996_mcu_debug, mt7996_mcu_cmd,
	TP_PROTO(struct mt7996_dev *dev, bool uni, u8 id, u8 ext_id,
		 const void *data, size_t len),
	TP_ARGS(dev, uni, id, ext_id, data, len)
);

DEFINE_EVENT(mt7996_mcu_debug, mt7996_mcu_event,
	TP_PROTO(struct mt7996_dev *dev, bool uni, u8 id, u8 ext_id,
		 const void *data, size_t len),
	TP_ARGS(dev, uni, id, ext_id, data, len)
);

TRACE_EVENT(mt7996_tx_prepare,
	TP_PROTO(struct mt7996_dev *dev, struct mt76_wcid *wcid, enum mt76_txq_id qid,
		 const void *txwi, const void *data, size_t len),
	TP_ARGS(dev, wcid, qid, txwi, data, len),

	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u16, wcid)
		__field(u8, qid)
		__array(u8, txwi, MT_TXD_SIZE)
		__field(size_t, len)
		__dynamic_array(u8, data, len)
	),

	TP_fast_assign(
		DEV_ASSIGN(dev->mt76.phys[wcid->phy_idx]->hw->wiphy);
		__entry->wcid = wcid->idx;
		__entry->qid = qid;
		memcpy(__entry->txwi, txwi, MT_TXD_SIZE);
		__entry->len = len;
		memcpy(__get_dynamic_array(data), data, len);
	),

	TP_printk(
		DEV_PR_FMT " wcid: %u, qid: %u, len: %zu",
		DEV_PR_ARG, __entry->wcid, __entry->qid, __entry->len
	)
);

TRACE_EVENT(mt7996_fill_rx,
	TP_PROTO(struct mt7996_phy *phy, const void *data, size_t len),
	TP_ARGS(phy, data, len),

	TP_STRUCT__entry(
		DEV_ENTRY
		__field(size_t, len)
		__dynamic_array(u8, data, len)
	),

	TP_fast_assign(
		DEV_ASSIGN(phy->mt76->hw->wiphy);
		__entry->len = len;
		memcpy(__get_dynamic_array(data), data, len);
	),

	TP_printk(
		DEV_PR_FMT " len: %zu",
		DEV_PR_ARG, __entry->len
	)
);

TRACE_EVENT(mt7996_fill_rx_done,
	TP_PROTO(struct mt7996_phy *phy, u16 seqno, u16 hdr_gap),
	TP_ARGS(phy, seqno, hdr_gap),

	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u16, seqno)
		__field(u16, hdr_gap)
	),

	TP_fast_assign(
		DEV_ASSIGN(phy->mt76->hw->wiphy);
		__entry->seqno = seqno;
		__entry->hdr_gap = hdr_gap;
	),

	TP_printk(
		DEV_PR_FMT " seqno: %u, hdr_gap: %u",
		DEV_PR_ARG, __entry->seqno, __entry->hdr_gap
	)
);

TRACE_EVENT(mt7996_mac_tx_free,
	TP_PROTO(struct mt7996_dev *dev, u8 errno, void *data, int len, u16 wlan_id, u32 msdu),
	TP_ARGS(dev, errno, data, len, wlan_id, msdu),

	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u8, errno)
		__field(size_t, len)
		__dynamic_array(u8, data, len)
		__field(u16, wlan_id)
		__field(u32, msdu)
	),

	TP_fast_assign(
		DEV_ASSIGN(dev->mphy.hw->wiphy);
		__entry->errno = errno;
		__entry->len = len;
		memcpy(__get_dynamic_array(data), data, len);
		__entry->wlan_id = wlan_id;
		__entry->msdu = msdu;
	),

	TP_printk(
		DEV_PR_FMT " errno = %u, wlan_id = %u, msdu = %u",
		DEV_PR_ARG, __entry->errno, __entry->wlan_id, __entry->msdu
	)
);

#endif

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ./mt7996
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE mt7996_trace

#include <trace/define_trace.h>
