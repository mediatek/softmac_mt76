/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2026 MediaTek Inc.
 */
#ifndef __MT7996_MULDF_H_
#define __MT7996_MULDF_H_

#ifdef CONFIG_MULDF_HELPER

struct mt7996_dev;

enum {
	UNI_CANARY_MONITOR_TXPWR_RPT,
	UNI_CANARY_MONITOR_COMMON_INFO,
	UNI_CANARY_MONITOR_TASK_INFO,
	UNI_CANARY_MONITOR_SEC_INFO,
	UNI_CANARY_MONITOR_MAC_QUEUE_INFO,
	UNI_CANARY_MONITOR_STATE_MACHINE_INFO,
	UNI_CANARY_MONITOR_STATE_MAC_PAUSE_INFO,
	UNI_CANARY_MONITOR_TAG_NUM
};

#define MAC_QUEUE_BATCH_MAX_NUM 6
struct canary_monitor_mac_queue_info_event {
	__le16 tag;
	__le16 len;
	__le16 count;
	__le16 rsv;
	struct cm_mac_queue_info entry[MAC_QUEUE_BATCH_MAX_NUM];
} __packed;

struct canary_monitor_state_info_event {
	__le16 tag;
	__le16 len;
	u8 cr_idx;
	u8 bss_idx;
	u8 rsv[2];
	__le32 val;
} __packed;

#define MAC_PAUSE_BATCH_MAX_NUM 176
struct canary_monitor_mac_pause_info_event {
	__le16 tag;
	__le16 len;
	__le16 count;
	__le16 rsv;
	struct cm_mac_pause_info entry[MAC_PAUSE_BATCH_MAX_NUM];
} __packed;

void mt7996_muldf_canary_monitor_event(struct mt7996_dev *dev,
				       struct sk_buff *skb);

#endif
#endif
