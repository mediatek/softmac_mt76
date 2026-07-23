/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2026 MediaTek Inc.
 */

#if !defined(__MT7999_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define __MT7999_TRACE_H

#include <linux/types.h>
#include <linux/tracepoint.h>
#include "mt7999.h"

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mt7999

#define MAXNAME		32
#define DEV_ENTRY	__array(char, wiphy_name, 32)
#define DEV_ASSIGN(_w)	strscpy(__entry->wiphy_name, wiphy_name(_w), MAXNAME)
#define DEV_PR_FMT	"%s"
#define DEV_PR_ARG	__entry->wiphy_name

DECLARE_EVENT_CLASS(mt7999_mcu_debug,
	TP_PROTO(struct mt7999_dev *dev, int cmd, u8 id, u8 seq, u8 option),
	TP_ARGS(dev, cmd, id, seq, option),
	TP_STRUCT__entry(
		__field(int, cmd)
		__field(u8, id)
		__field(u8, seq)
		__field(u8, option)
		__field(u32, ts)
	),
	TP_fast_assign(
		__entry->cmd = cmd;
		__entry->id = id;
		__entry->seq = seq;
		__entry->option = option;
		__entry->ts = mt76_rr(dev, MT_LPON_FRCR(0));
	),
	TP_printk(
		"cmd: 0x%x, id: 0x%x, seq: %u, option: 0x%x, ts: %u",
		__entry->cmd,
		__entry->id,
		__entry->seq,
		__entry->option,
		__entry->ts
	)
);

DEFINE_EVENT(mt7999_mcu_debug, mt7999_mcu_send_message,
	TP_PROTO(struct mt7999_dev *dev, int cmd, u8 id, u8 seq, u8 option),
	TP_ARGS(dev, cmd, id, seq, option)
);

DEFINE_EVENT(mt7999_mcu_debug, mt7999_mcu_rx_event,
	TP_PROTO(struct mt7999_dev *dev, int cmd, u8 id, u8 seq, u8 option),
	TP_ARGS(dev, cmd, id, seq, option)
);

DEFINE_EVENT(mt7999_mcu_debug, mt7999_mcu_timeout,
	TP_PROTO(struct mt7999_dev *dev, int cmd, u8 id, u8 seq, u8 option),
	TP_ARGS(dev, cmd, id, seq, option)
);

TRACE_EVENT(mt7999_tx_prepare,
	TP_PROTO(struct mt7999_dev *dev, struct mt76_wcid *wcid, enum mt76_txq_id qid,
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

TRACE_EVENT(mt7999_fill_rx,
	TP_PROTO(struct mt7999_phy *phy, const void *data, size_t len),
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

TRACE_EVENT(mt7999_fill_rx_done,
	TP_PROTO(struct mt7999_phy *phy, u16 seqno, u16 hdr_gap),
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

TRACE_EVENT(mt7999_mac_tx_free,
	TP_PROTO(struct mt7999_dev *dev, u8 errno, void *data, int len, u16 wlan_id, u32 msdu),
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

TRACE_EVENT(mt7999_mac_prefer_ul,
	TP_PROTO(struct mt7999_phy *phy, u8 wmm_idx, bool ret),
	TP_ARGS(phy, wmm_idx, ret),

	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u8, band_idx)
		__field(u8, wmm_idx)
		__field(bool, ret)
		__field(u32, tx_bytes)
		__field(u32, rx_bytes)
		__field(u32, tx_packets)
		__field(u32, rx_packets)
	),

	TP_fast_assign(
		DEV_ASSIGN(phy->mt76->hw->wiphy);
		__entry->band_idx = phy->mt76->band_idx;
		__entry->wmm_idx = wmm_idx;
		__entry->ret = ret;
		__entry->tx_bytes = phy->tx_bytes[wmm_idx];
		__entry->tx_packets = phy->tx_packets[wmm_idx];
		__entry->rx_bytes = phy->rx_bytes[wmm_idx];
		__entry->rx_packets = phy->rx_packets[wmm_idx];
	),

	TP_printk(
		DEV_PR_FMT " band%u wmm_idx = %hhu, ret = %d, tx_bytes = %u, "
		"tx_packets = %u, rx_bytes = %u, rx_packets = %u, tpt = %llu (Mbps)",
		DEV_PR_ARG, __entry->band_idx, __entry->wmm_idx, __entry->ret,
		__entry->tx_bytes, __entry->tx_packets,
		__entry->rx_bytes, __entry->rx_packets,
		(((u64)__entry->tx_bytes + __entry->rx_bytes) << 3 >> 20)
	)
);

TRACE_EVENT(mt7999_mac_dynamic_txop,
	TP_PROTO(struct mt7999_phy *phy),
	TP_ARGS(phy),

	TP_STRUCT__entry(
		DEV_ENTRY
		__field(u8, band_idx)
		__array(u16, txop, 4)
	),

	TP_fast_assign(
		DEV_ASSIGN(phy->mt76->hw->wiphy);
		__entry->band_idx = phy->mt76->band_idx;
		memcpy(__entry->txop, phy->txop, sizeof(__entry->txop));
	),

	TP_printk(
		DEV_PR_FMT " band%u txop = 0x%02x, 0x%02x, 0x%02x, 0x%02x",
		DEV_PR_ARG, __entry->band_idx,
		__entry->txop[0], __entry->txop[1],
		__entry->txop[2], __entry->txop[3]
	)
);

#endif

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ./mt7999
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE mt7999_trace

#include <trace/define_trace.h>
