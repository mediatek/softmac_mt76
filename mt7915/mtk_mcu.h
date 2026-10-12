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

#define le_to_cpu(x)				\
	_Generic((x),				\
		 __u8: (u8)(x),			\
		 __le16: (u16)le16_to_cpu(x),	\
		 __le32: (u32)le32_to_cpu(x),	\
		 __le64: (u64)le64_to_cpu(x),	\
		 default: (x)			\
		)

enum radar_pulse_type {
	LONG_PULSE,
	SHORT_PULSE,
	HW_PULSE,
};

enum radar_type {
	RADAR_TYPE_FCC_1_JP_1,
	RADAR_TYPE_FCC_2,
	RADAR_TYPE_FCC_3,
	RADAR_TYPE_FCC_4,
	RADAR_TYPE_FCC_6,
	RADAR_TYPE_ETSI_1,
	RADAR_TYPE_ETSI_2,
	RADAR_TYPE_ETSI_3,
	RADAR_TYPE_ETSI_4,
	RADAR_TYPE_ETSI_5_2PRI,
	RADAR_TYPE_ETSI_5_3PRI,
	RADAR_TYPE_ETSI_6_2PRI,
	RADAR_TYPE_ETSI_6_3PRI,
	RADAR_TYPE_JP_2,
	RADAR_TYPE_JP_3,
	RADAR_TYPE_JP_4,
	RADAR_TYPE_KR_1,
	RADAR_TYPE_KR_2,
	RADAR_TYPE_KR_3,
	RADAR_TYPE_NUM,
};

struct pulse_buffer {
	__le32 start;
	__le16 pulse_width;
	__le16 pulse_power;

	union {
		struct {
			u8 mdrdy_flag;
			u8 rsv[3];
		} sw;
		struct {
			u8 sc_pass;
			u8 sw_reset;
			u8 mdrdy_flag;
			u8 tx_active;
		} hw;
	} type;
};

struct sw_radar_type {
	u8 detected;
	u8 enabled;
	u8 staggered;
	u8 crpn_min;
	u8 crpn_max;
	u8 crpr_min;
	u8 pw_min;
	u8 pw_max;
	u32 pri_min;
	u32 pri_max;
	u8 crbn_min;
	u8 crbn_max;
	u8 stgpn_min;
	u8 stgpn_max;
	u8 stgpr_min;
	/* Staggered PRF radar: min PRI diff between 1st and 2nd PRI */
	u32 stg_pri_diff_min;
};
#endif
