/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2023 MediaTek Inc.
 */

#ifndef __MT7996_MTK_MCU_H
#define __MT7996_MTK_MCU_H

#include "../mt76_connac_mcu.h"

#ifdef CONFIG_MTK_DEBUG

struct bf_pfmu_tag {
	__le16 tag;
	__le16 len;

	u8 pfmu_id;
	bool bfer;
	u8 band_idx;
	u8 __rsv[5];
	u8 buf[56];
} __packed;

struct bf_starec_read {
	__le16 tag;
	__le16 len;

	__le16 wlan_idx;
	u8 __rsv[2];
} __packed;

struct bf_fbk_rpt_info {
	__le16 tag;
	__le16 len;

	__le16 wlan_idx; // Only need for dynamic_pfmu_update 0x4
	u8 action;
	u8 band_idx;
	u8 __rsv[4];

} __packed;

struct bf_txsnd_info {
	__le16 tag;
	__le16 len;

	u8 action;
	u8 read_clr;
	u8 vht_opt;
	u8 he_opt;
	__le16 wlan_idx;
	u8 glo_opt;
	u8 snd_intv;
	u8 snd_stop;
	u8 max_snd_stas;
	u8 tx_time;
	u8 mcs;
	u8 ldpc;
	u8 inf;
	u8 man;
	u8 ac_queue;
	u8 sxn_protect;
	u8 direct_fbk;
	u8 __rsv[2];
} __packed;

#define MAX_PHASE_GROUP_NUM	13

struct bf_phase_comp {
	__le16 tag;
	__le16 len;

	u8 bw;
	u8 jp_band;
	u8 band_idx;
	bool read_from_e2p;
	bool disable;
	u8 group;
	u8 rsv[2];
	u8 buf[44];
} __packed;

struct bf_tx_apply {
	__le16 tag;
	__le16 len;

	__le16 wlan_idx;
	bool ebf;
	bool ibf;
	bool mu_txbf;
	bool phase_cal;
	u8 rsv[2];
} __packed;

struct bf_phase_cal {
	__le16 tag;
	__le16 len;

	u8 group_l_m_n;
	u8 group;
	u8 sx2;
	u8 cal_type;
	u8 lna_gain_level;
	u8 band_idx;
	u8 version;
	u8 rsv[1];
} __packed;

struct bf_txcmd {
	__le16 tag;
	__le16 len;

	u8 action;
	u8 bf_manual;
	u8 bf_bit;
	u8 rsv[5];
} __packed;

struct bf_pfmu_data_all {
	__le16 tag;
	__le16 len;

	u8 pfmu_id;
	u8 band_idx;
	u8 rsv[2];

	u8 buf[640];
} __packed;

#define TXBF_DUT_MAC_SUBADDR		0x22
#define TXBF_GOLDEN_MAC_SUBADDR		0x11

struct mt7996_tm_bf_req {
	u8 _rsv[4];

	union {
		struct bf_sounding_on sounding;
		struct bf_tx_apply tx_apply;
		struct bf_pfmu_tag pfmu_tag;
		struct bf_pfmu_data_all pfmu_data_all;
		struct bf_phase_cal phase_cal;
		struct bf_phase_comp phase_comp;
		struct bf_txcmd txcmd;
	};
} __packed;

enum tm_trx_mac_type {
	TM_TRX_MAC_TX = 1,
	TM_TRX_MAC_RX,
	TM_TRX_MAC_TXRX,
	TM_TRX_MAC_TXRX_RXV,
	TM_TRX_MAC_RXV,
	TM_TRX_MAC_RX_RXV,
};

enum tm_trx_param_idx {
	TM_TRX_PARAM_RSV,
	/* MAC */
	TM_TRX_PARAM_SET_TRX,
	TM_TRX_PARAM_RX_FILTER,
	TM_TRX_PARAM_RX_FILTER_PKT_LEN,
	TM_TRX_PARAM_SLOT_TIME,
	TM_TRX_PARAM_CLEAN_PERSTA_TXQUEUE,
	TM_TRX_PARAM_AMPDU_WTBL,
	TM_TRX_PARAM_MU_RX_AID,
	TM_TRX_PARAM_PHY_MANUAL_TX,

	/* PHY */
	TM_TRX_PARAM_RX_PATH,
	TM_TRX_PARAM_TX_STREAM,
	TM_TRX_PARAM_TSSI_STATUS,
	TM_TRX_PARAM_DPD_STATUS,
	TM_TRX_PARAM_RATE_POWER_OFFSET_ON_OFF,
	TM_TRX_PARAM_THERMO_COMP_STATUS,
	TM_TRX_PARAM_FREQ_OFFSET,
	TM_TRX_PARAM_FAGC_RSSI_PATH,
	TM_TRX_PARAM_PHY_STATUS_COUNT,
	TM_TRX_PARAM_RXV_INDEX,

	TM_TRX_PARAM_ANTENNA_PORT,
	TM_TRX_PARAM_THERMAL_ONOFF,
	TM_TRX_PARAM_TX_POWER_CONTROL_ALL_RF,
	TM_TRX_PARAM_RATE_POWER_OFFSET,
	TM_TRX_PARAM_SLT_CMD_TEST,
	TM_TRX_PARAM_SKU,
	TM_TRX_PARAM_POWER_PERCENTAGE_ON_OFF,
	TM_TRX_PARAM_BF_BACKOFF_ON_OFF,
	TM_TRX_PARAM_POWER_PERCENTAGE_LEVEL,
	TM_TRX_PARAM_FRTBL_CFG,
	TM_TRX_PARAM_PREAMBLE_PUNC_ON_OFF,

	TM_TRX_PARAM_MAX_NUM,
};

enum trx_action {
	TM_TRX_ACTION_SET,
	TM_TRX_ACTION_GET,
};

struct tm_trx_set {
	u8 type;
	u8 enable;
	u8 band_idx;
	u8 rsv;
} __packed;

struct mt7996_tm_trx_req {
	u8 param_num;
	u8 _rsv[3];

	__le16 tag;
	__le16 len;

	__le16 param_idx;
	u8 band_idx;
	u8 testmode_en;
	u8 action;
	u8 rsv[3];

	u32 data;
	struct tm_trx_set set_trx;

	u8 buf[220];
} __packed;

struct mt7996_mcu_bf_basic_event {
	struct mt7996_mcu_rxd rxd;

	u8 __rsv1[4];

	__le16 tag;
	__le16 len;
};

struct mt7996_mcu_bf_starec_read {

	struct mt7996_mcu_bf_basic_event event;

	__le16 pfmu_id;
	bool is_su_mu;
	u8 txbf_cap;
	u8 sounding_phy;
	u8 ndpa_rate;
	u8 ndp_rate;
	u8 rpt_poll_rate;
	u8 tx_mode;
	u8 nc;
	u8 nr;
	u8 bw;
	u8 total_mem_require;
	u8 mem_require_20m;
	u8 mem_row0;
	u8 mem_col0:6;
	u8 mem_row0_msb:2;
	u8 mem_row1;
	u8 mem_col1:6;
	u8 mem_row1_msb:2;
	u8 mem_row2;
	u8 mem_col2:6;
	u8 mem_row2_msb:2;
	u8 mem_row3;
	u8 mem_col3:6;
	u8 mem_row3_msb:2;

	__le16 smart_ant;
	u8 se_idx;
	u8 auto_sounding_ctrl;

	u8 bf_timeout;
	u8 bf_dbw;
	u8 bf_ncol;
	u8 bf_nrow;

	u8 nr_lt_bw80;
	u8 nc_lt_bw80;
	u8 ru_start_idx;
	u8 ru_end_idx;

	bool trigger_su;
	bool trigger_mu;

	bool ng16_su;
	bool ng16_mu;

	bool codebook42_su;
	bool codebook75_mu;

	u8 he_ltf;
	u8 rsv[3];
};

#define TXBF_PFMU_ID_NUM_MAX 48

#define TXBF_PFMU_ID_NUM_MAX_TBTC_BAND0 TXBF_PFMU_ID_NUM_MAX
#define TXBF_PFMU_ID_NUM_MAX_TBTC_BAND1 TXBF_PFMU_ID_NUM_MAX
#define TXBF_PFMU_ID_NUM_MAX_TBTC_BAND2 TXBF_PFMU_ID_NUM_MAX

/* CFG_BF_STA_REC shall be varied based on BAND Num */
#define CFG_BF_STA_REC_NUM (TXBF_PFMU_ID_NUM_MAX_TBTC_BAND0 + TXBF_PFMU_ID_NUM_MAX_TBTC_BAND1 + TXBF_PFMU_ID_NUM_MAX_TBTC_BAND2)

#define BF_SND_CTRL_STA_DWORD_CNT   ((CFG_BF_STA_REC_NUM + 0x1F) >> 5)

#ifndef ALIGN_4
	#define ALIGN_4(_value)             (((_value) + 3) & ~3u)
#endif /* ALIGN_4 */

#define CFG_WIFI_RAM_BAND_NUM 3

struct uni_event_bf_txsnd_sta_info {
	u8 snd_intv;       /* Sounding interval upper bound, unit:15ms */
	u8 snd_intv_cnt;   /* Sounding interval counter */
	u8 snd_tx_cnt;     /* Tx sounding count for debug */
	u8 snd_stop_reason;  /* Bitwise reason to put in Stop Queue */
};

struct mt7996_mcu_tx_snd_info {

	struct mt7996_mcu_bf_basic_event event;

	u8 vht_opt;
	u8 he_opt;
	u8 glo_opt;
	u8 __rsv;
	__le32 snd_rec_su_sta[BF_SND_CTRL_STA_DWORD_CNT];
	__le32 snd_rec_vht_mu_sta[BF_SND_CTRL_STA_DWORD_CNT];
	__le32 snd_rec_he_tb_sta[BF_SND_CTRL_STA_DWORD_CNT];
	__le32 snd_rec_eht_tb_sta[BF_SND_CTRL_STA_DWORD_CNT];
	__le16 wlan_idx_for_mc_snd[ALIGN_4(CFG_WIFI_RAM_BAND_NUM)];
	__le16 wlan_idx_for_he_tb_snd[ALIGN_4(CFG_WIFI_RAM_BAND_NUM)];
	__le16 wlan_idx_for_eht_tb_snd[ALIGN_4(CFG_WIFI_RAM_BAND_NUM)];
	__le16 ul_length;
	u8 mcs;
	u8 ldpc;
	struct uni_event_bf_txsnd_sta_info snd_sta_info[CFG_BF_STA_REC_NUM];
};

struct mt7996_mcu_txbf_fbk_info {

	struct mt7996_mcu_bf_basic_event event;

	__le32 u4DeQInterval;     /* By ms */
	__le32 u4PollPFMUIntrStatTimeOut; /* micro-sec */
	__le32 u4RptPktTimeOutListNum;
	__le32 u4RptPktListNum;
	__le32 u4PFMUWRTimeOutCnt;
	__le32 u4PFMUWRFailCnt;
	__le32 u4PFMUWRDoneCnt;
	__le32 u4PFMUWRTimeoutFreeCnt;
	__le32 u4FbRptPktDropCnt;
	__le32 au4RxPerStaFbRptCnt[CFG_BF_STA_REC_NUM];
};

struct pfmu_ru_field {
	__le32 ru_start_id:7;
	__le32 _rsv1:1;
	__le32 ru_end_id:7;
	__le32 _rsv2:1;
} __packed;

struct pfmu_partial_bw_info {
	__le32 partial_bw_info:9;
	__le32 _rsv1:7;
} __packed;

struct mt7996_pfmu_tag1 {
	__le32 pfmu_idx:10;
	__le32 ebf:1;
	__le32 data_bw:3;
	__le32 lm:3;
	__le32 is_mu:1;
	__le32 nr:3;
	__le32 nc:3;
	__le32 codebook:2;
	__le32 ngroup:2;
	__le32 invalid_prof:1;
	__le32 _rsv:3;

	__le32 col_id1:7, row_id1:9;
	__le32 col_id2:7, row_id2:9;
	__le32 col_id3:7, row_id3:9;
	__le32 col_id4:7, row_id4:9;

	union {
		struct pfmu_ru_field field;
		struct pfmu_partial_bw_info bw_info;
	};
	__le32 mob_cal_en:1;
	__le32 _rsv2:3;
	__le32 mob_ru_alloc:9;	/* EHT profile uses full 9 bit */
	__le32 _rsv3:3;

	__le32 snr_sts0:8, snr_sts1:8, snr_sts2:8, snr_sts3:8;
	__le32 snr_sts4:8, snr_sts5:8, snr_sts6:8, snr_sts7:8;

	__le32 _rsv4;
} __packed;

struct mt7996_pfmu_tag2 {
	__le32 smart_ant:24;
	__le32 se_idx:5;
	__le32 _rsv:3;

	__le32 _rsv1:16;
	__le32 ibf_timeout:8;
	__le32 _rsv2:8;

	__le32 ibf_data_bw:3;
	__le32 ibf_nc:3;
	__le32 ibf_nr:3;
	__le32 ibf_ru:9;
	__le32 _rsv3:14;

	__le32 mob_delta_t:8;
	__le32 mob_lq_result:7;
	__le32 _rsv5:1;
	__le32 _rsv6:16;

	__le32 _rsv7;
} __packed;

struct mt7996_pfmu_tag_event {
	struct mt7996_mcu_bf_basic_event event;

	u8 bfer;
	u8 __rsv[3];

	struct mt7996_pfmu_tag1 t1;
	struct mt7996_pfmu_tag2 t2;
};

struct mt7996_pfmu_tag {
	struct mt7996_pfmu_tag1 t1;
	struct mt7996_pfmu_tag2 t2;
};

enum bf_lm_type {
	BF_LM_LEGACY,
	BF_LM_HT,
	BF_LM_VHT,
	BF_LM_HE,
	BF_LM_EHT,
};

struct mt7996_txbf_phase_out {
	u8 c0_l;
	u8 c1_l;
	u8 c2_l;
	u8 c3_l;
	u8 c0_m;
	u8 c1_m;
	u8 c2_m;
	u8 c3_m;
	u8 c0_mh;
	u8 c1_mh;
	u8 c2_mh;
	u8 c3_mh;
	u8 c0_h;
	u8 c1_h;
	u8 c2_h;
	u8 c3_h;
	u8 c0_uh;
	u8 c1_uh;
	u8 c2_uh;
	u8 c3_uh;
};

struct mt7992_txbf_phase_out {
	u8 c0_l;
	u8 c1_l;
	u8 c2_l;
	u8 c3_l;
	u8 c4_l;
	u8 c0_m;
	u8 c1_m;
	u8 c2_m;
	u8 c3_m;
	u8 c4_m;
	u8 c0_mh;
	u8 c1_mh;
	u8 c2_mh;
	u8 c3_mh;
	u8 c4_mh;
	u8 c0_h;
	u8 c1_h;
	u8 c2_h;
	u8 c3_h;
	u8 c4_h;
	u8 c0_uh;
	u8 c1_uh;
	u8 c2_uh;
	u8 c3_uh;
	u8 c4_uh;
};

struct txbf_rx_phase {
	u8 rx_uh;
	u8 rx_h;
	u8 rx_m;
	u8 rx_l;
	u8 rx_ul;
};

struct txbf_rx_phase_ext {
	u8 rx_uh;
	u8 rx_h;
	u8 rx_mh;
	u8 rx_m;
	u8 rx_l;
	u8 rx_ul;
};

struct mt7996_txbf_phase_info_2g {
	struct txbf_rx_phase r0;
	struct txbf_rx_phase r1;
	struct txbf_rx_phase r2;
	struct txbf_rx_phase r3;
	struct txbf_rx_phase r2_sx2;
	struct txbf_rx_phase r3_sx2;
	u8 m_t0_h;
	u8 m_t1_h;
	u8 m_t2_h;
	u8 m_t2_h_sx2;
	u8 r0_reserved;
	u8 r1_reserved;
	u8 r2_reserved;
	u8 r3_reserved;
	u8 r2_sx2_reserved;
	u8 r3_sx2_reserved;
};

struct mt7996_txbf_phase_info_5g {
	struct txbf_rx_phase_ext r0;
	struct txbf_rx_phase_ext r1;
	struct txbf_rx_phase_ext r2;
	struct txbf_rx_phase_ext r3;
	struct txbf_rx_phase r2_sx2;	/* no middle-high in r2_sx2 */
	struct txbf_rx_phase r3_sx2;	/* no middle-high in r3_sx2 */
	u8 m_t0_h;
	u8 m_t1_h;
	u8 m_t2_h;
	u8 m_t2_h_sx2;
	u8 r0_reserved;
	u8 r1_reserved;
	u8 r2_reserved;
	u8 r3_reserved;
	u8 r2_sx2_reserved;
	u8 r3_sx2_reserved;
};

struct mt7992_txbf_phase_info_2g {
	struct txbf_rx_phase_ext r0;
	struct txbf_rx_phase_ext r1;
	struct txbf_rx_phase_ext r2;
	struct txbf_rx_phase_ext r3;
	u8 m_t0_h;
	u8 m_t1_h;
	u8 m_t2_h;
};

struct mt7992_txbf_phase_info_5g {
	struct txbf_rx_phase_ext r0;
	struct txbf_rx_phase_ext r1;
	struct txbf_rx_phase_ext r2;
	struct txbf_rx_phase_ext r3;
	struct txbf_rx_phase_ext r4;
	u8 m_t0_h;
	u8 m_t1_h;
	u8 m_t2_h;
	u8 m_t3_h;
};

struct mt7996_txbf_phase {
	u8 status;
	union {
		union {
			struct mt7996_txbf_phase_info_2g phase_2g;
			struct mt7996_txbf_phase_info_5g phase_5g;
		} v1;
		union {
			struct mt7992_txbf_phase_info_2g phase_2g;
			struct mt7992_txbf_phase_info_5g phase_5g;
		} v2;
		u8 buf[44];
	};
};

#define phase_assign(group, v, field, dump, ...)	({					\
	if (group) {										\
		phase->v.phase_5g.field = cal->v.phase_5g.field;				\
		if (dump)									\
			mt76_tm_dbg(&dev->mt76, "%s = %d", #field, phase->v.phase_5g.field);	\
	} else {										\
		phase->v.phase_2g.field = cal->v.phase_5g.field;				\
		if (dump)									\
			mt76_tm_dbg(&dev->mt76, "%s = %d", #field, phase->v.phase_2g.field);	\
	}											\
})

#define phase_assign_rx(group, v, rx, dump, ...)	({					\
	phase_assign(group, v, rx.rx_uh, dump);							\
	phase_assign(group, v, rx.rx_h, dump);							\
	phase_assign(group, v, rx.rx_m, dump);							\
	phase_assign(group, v, rx.rx_l, dump);							\
	phase_assign(group, v, rx.rx_ul, dump);							\
})

#define phase_assign_rx_ext(group, v, rx, dump, ...)	({					\
	phase_assign(group, v, rx.rx_uh, dump);							\
	phase_assign(group, v, rx.rx_h, dump);							\
	phase_assign(group, v, rx.rx_mh, dump);							\
	phase_assign(group, v, rx.rx_m, dump);							\
	phase_assign(group, v, rx.rx_l, dump);							\
	phase_assign(group, v, rx.rx_ul, dump);							\
})

#define phase_assign_rx_v1(group, v, rx, ...)	({						\
	if (group) {										\
		phase_assign(group, v, rx.rx_uh, true);						\
		phase_assign(group, v, rx.rx_h, true);						\
		phase->v.phase_5g.rx.rx_mh = cal->v.phase_5g.rx.rx_mh;				\
		mt76_tm_dbg(&dev->mt76, "%s.rx_mh = %d", #rx, phase->v.phase_5g.rx.rx_mh);	\
		phase_assign(group, v, rx.rx_m, true);						\
		phase_assign(group, v, rx.rx_l, true);						\
		phase_assign(group, v, rx.rx_ul, true);						\
	} else {										\
		phase_assign_rx(group, v, rx, true, ...);					\
	}											\
})

#define GROUP_L		0
#define GROUP_M		1
#define GROUP_H		2

struct mt7996_pfmu_data {
	__le16 subc_idx;
	__le16 phi11;
	__le16 phi21;
	__le16 phi31;
};

struct mt7996_pfmu_data_5x5 {
	__le16 subc_idx;
	__le16 phi11;
	__le16 phi21;
	__le16 phi31;
	__le16 phi41;
};

struct mt7996_ibf_cal_info {
	struct mt7996_mcu_bf_basic_event event;

	u8 category_id;
	u8 group_l_m_n;
	u8 group;
	bool sx2;
	u8 status;
	u8 cal_type;
	u8 nsts;
	u8 version;
	union {
		struct {
			struct mt7996_txbf_phase_out phase_out;
			union {
				struct mt7996_txbf_phase_info_2g phase_2g;
				struct mt7996_txbf_phase_info_5g phase_5g;
			};
		} v1;
		struct {
			struct mt7992_txbf_phase_out phase_out;
			union {
				struct mt7992_txbf_phase_info_2g phase_2g;
				struct mt7992_txbf_phase_info_5g phase_5g;
			};
		} v2;
		u8 buf[64];
	};
} __packed;

enum {
	IBF_PHASE_CAL_UNSPEC,
	IBF_PHASE_CAL_NORMAL,
	IBF_PHASE_CAL_VERIFY,
	IBF_PHASE_CAL_NORMAL_INSTRUMENT,
	IBF_PHASE_CAL_VERIFY_INSTRUMENT,
};

enum ibf_version {
	IBF_VER_1,
	IBF_VER_2 = 3,
};

static inline int get_ibf_version(struct mt7996_dev *dev)
{
	switch (mt76_chip(&dev->mt76)) {
	case MT7996_DEVICE_ID:
		return IBF_VER_1;
	case MT7992_DEVICE_ID:
	default:
		return IBF_VER_2;
	}
}

#define MT7996_TXBF_SUBCAR_NUM		64
#define MT7996_TXBF_PFMU_DATA_LEN	(MT7996_TXBF_SUBCAR_NUM * sizeof(struct mt7996_pfmu_data))
#define MT7996_TXBF_PFMU_DATA_LEN_5X5	(MT7996_TXBF_SUBCAR_NUM * \
					 sizeof(struct mt7996_pfmu_data_5x5))

enum {
	UNI_EVENT_BF_PFMU_TAG = 0x5,
	UNI_EVENT_BF_PFMU_DATA = 0x7,
	UNI_EVENT_BF_STAREC = 0xB,
	UNI_EVENT_BF_CAL_PHASE = 0xC,
	UNI_EVENT_BF_FBK_INFO = 0x17,
	UNI_EVENT_BF_TXSND_INFO = 0x18,
	UNI_EVENT_BF_PLY_INFO = 0x19,
	UNI_EVENT_BF_METRIC_INFO = 0x1A,
	UNI_EVENT_BF_TXCMD_CFG_INFO = 0x1B,
	UNI_EVENT_BF_SND_CNT_INFO = 0x1D,
	UNI_EVENT_BF_MAX_NUM
};

enum {
	BF_SND_READ_INFO = 0,
	BF_SND_CFG_OPT,
	BF_SND_CFG_INTV,
	BF_SND_STA_STOP,
	BF_SND_CFG_MAX_STA,
	BF_SND_CFG_BFRP,
	BF_SND_CFG_INF,
	BF_SND_CFG_TXOP_SND
};

#endif

#endif
