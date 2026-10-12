/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (C) 2026 MediaTek Inc.
 */

#ifndef __MT7915_MTK_MCU_H
#define __MT7915_MTK_MCU_H

#include "../mt76_connac_mcu.h"

struct mt7915_mcu_sr_basic_event {
	struct mt76_connac2_mcu_rxd rxd;

	u8 sub_event_id;
	u8 arg_num;
	u8 band_idx;
	u8 status;
	u8 rsv[4];
	__le32 value;
} __packed;

struct mt7915_mcu_sr_swsd_event {
	struct mt7915_mcu_sr_basic_event basic;
	u8 rsv[32];
	__le32 sr_tx_airtime[2];
	__le32 obss_airtime[2];
	__le32 my_tx_airtime[2];
	__le32 my_rx_airtime[2];
	__le32 channel_busy_time[2];
	__le32 total_airtime[2];
	__le32 total_airtime_ratio[2];
	__le32 obss_airtime_ratio[2];
	u8 rule[2];
	u8 rsv2[102];
} __packed;

struct mt7915_mcu_sr_hw_ind_event {
	struct mt7915_mcu_sr_basic_event basic;
	u8 rsv[2];
	__le16 non_srg_valid_cnt;
	u8 rsv2[4];
	__le16 inter_bss_ppdu_cnt;
	u8 rsv3[6];
	__le32 sr_ampdu_mpdu_cnt;
	__le32 sr_ampdu_mpdu_acked_cnt;
} __packed;
#endif
