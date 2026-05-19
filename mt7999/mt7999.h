/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (C) 2026 MediaTek Inc.
 */

#ifndef __MT7999_H
#define __MT7999_H

#include <linux/interrupt.h>
#include <linux/ktime.h>
#include <linux/pci.h>
#include "../mt76_connac.h"
#include "regs.h"

#define MT7999_MAX_RADIOS		3
#define MT7999_MAX_INTERFACES		19	/* per-band */
#define MT7999_MAX_AP_INTERFACES	16	/* per-band */
#define MT7999_MAX_WMM_SETS		4
#define MT7999_MAX_MBSSID		16
#define MT7999_WTBL_BMC_SIZE		64
#define MT7999_WTBL_RESERVED		(mt7999_wtbl_size(dev) - 1)
#define MT7999_WTBL_STA			(MT7999_WTBL_RESERVED - \
					 mt7999_max_interface_num(dev))
#define MT7999_WTBL_GROUP_SIZE		((1600 >> 3) + 1)

#define MT7999_WATCHDOG_TIME		(HZ / 10)
#define MT7999_RESET_TIMEOUT		(30 * HZ)

#define MT7999_TX_RING_SIZE		2048
#define MT7999_TX_MCU_RING_SIZE		256
#define MT7999_TX_FWDL_RING_SIZE	128

#define MT7999_RX_RING_SIZE		1536
#define MT7999_RX_MCU_RING_SIZE		512
#define MT7999_RX_MCU_RING_SIZE_WA	1024
#define MT7999_NPU_TX_RING_SIZE		1024
#define MT7999_NPU_RX_RING_SIZE		1024
#define MT7999_NPU_TXD_SIZE		3

/* scatter-gather of mcu event is not supported in connac3 */
#define MT7999_RX_MCU_BUF_SIZE		2048
/* keep data_room + shinfo within a 2048 slot: 2 buffers per 4K page */
#define MT7999_RX_DATA_BUF_SIZE		SKB_WITH_OVERHEAD(2048)
#define MT7999_RX_TXFREE_BUF_SIZE	1792
#define MT7999_RX_MSDU_PAGE_BUF_SIZE	128

#define MT7999_DEVICE_ID		0x80f2
#define MT7999_DEVICE_ID_2		0x80f3
#define MT7999_FIRMWARE			"mediatek/mt7999/mt7999_fw.bin"
#define MT7999_FIRMWARE_TM		"mediatek/mt7999/mt7999_fw_tm.bin"

#define MT7999_EEPROM_DEFAULT		"mediatek/mt7999/mt7999_eeprom_e1.bin"
#define MT7999_EEPROM_DEFAULT_INT	"mediatek/mt7999/mt7999_eeprom_2i5i6i_e1.bin"
#define MT7999_EEPROM_DEFAULT_233	"mediatek/mt7999/mt7999_eeprom_233_e1.bin"
#define MT7999_EEPROM_DEFAULT_233_INT	"mediatek/mt7999/mt7999_eeprom_233_2i5i6i_e1.bin"
#define MT7999_EEPROM_SIZE		0x6400
#define MT7999_EEPROM_CAL_DATA_SIZE	0x1000
#define MT7999_EEPROM_BLOCK_SIZE	16
#define MT7999_EXT_EEPROM_BLOCK_SIZE	1024
#define MT7999_WED_TOKEN_SIZE		16384
#define MT7999_SW_TOKEN_SIZE		15360
#define MT7999_PER_BAND_TOKEN_SIZE	4000

#define MT7999_SKU_PATH_NUM		494

#define MT7999_MAX_TWT_AGRT		16
#define MT7999_MAX_BTWT_AGRT		4
#define MT7999_MAX_STA_TWT_AGRT		8
#define MT7999_MAX_BTWT_MEMBER_CNT	8
#define MT7999_BTWT_AGRT_OFFSET		(MT7999_MAX_TWT_AGRT - MT7999_MAX_BTWT_AGRT)
#define MT7999_MIN_TWT_DUR		64
#define MT7999_MAX_QUEUE		(__MT_RXQ_MAX +	__MT_MCUQ_MAX + 3)
#define MT7999_NTBBT_BEFORE_REJECT	100

/* NOTE: used to map mt76_rates. idx may change if firmware expands table */
#define MT7999_BASIC_RATES_TBL		31
#define MT7999_BEACON_RATES_TBL		25

#define MT7999_THERMAL_THROTTLE_MAX	100
#define MT7999_CDEV_THROTTLE_MAX	99
#define MT7999_CRIT_TEMP_IDX		0
#define MT7999_MAX_TEMP_IDX		1
#define MT7999_CRIT_TEMP		110
#define MT7999_MAX_TEMP			120
#define MT7999_DUMMY_BUFF_SIZE		16

#define MT7999_MAX_HIF_RXD_IN_PG	5
#define MT7999_RRO_MSDU_PG_HASH_SIZE	127
#define MT7999_RRO_MAX_SESSION		1024
#define MT7999_RRO_WINDOW_MAX_LEN	1024
#define MT7999_RRO_PARTICULAR_SESSION	1
#define MT7999_RRO_ADDR_ELEM_LEN	128
#define MT7999_RRO_BA_BITMAP_SESSION_SIZE	(MT7999_RRO_MAX_SESSION /	\
						 MT7999_RRO_ADDR_ELEM_LEN)
#define MT7999_RRO_WINDOW_MAX_SIZE	(MT7999_RRO_WINDOW_MAX_LEN *		\
					 MT7999_RRO_BA_BITMAP_SESSION_SIZE)

/* RRO 3.1 */
#define MT7999_RRO_MSDU_PG_CR_CNT	8
#define MT7999_RRO_MSDU_PG_SIZE_PER_CR	0x10000

#define MT7999_DRR_STA_BSS_GRP_MASK	GENMASK(5, 0)
#define MT7999_DRR_STA_AC0_QNTM_MASK	GENMASK(9, 7)
#define MT7999_DRR_STA_AC1_QNTM_MASK	GENMASK(12, 10)
#define MT7999_DRR_STA_AC2_QNTM_MASK	GENMASK(15, 13)
#define MT7999_DRR_STA_AC3_QNTM_MASK	GENMASK(18, 16)

#define MT7999_BUILD_TIME_LEN		20

#define to_rssi(field, rcpi)	((FIELD_GET(field, rcpi) - 220) / 2)

#define MT7999_MAX_BEACON_LOSS		20
#define MT7999_MAX_PROBE_TIMEOUT	500
#define MT7999_MAX_PROBE_TRIES		2

#define IP_DSCP_NUM			64

#define MT7999_HW_DROP_STAT	4
#define MT7999_HW_DROP_REASON	0x20

#define WMM_PBC_QUEUE_NUM	5
#define WMM_PBC_BOUND_MGMT	32
struct mt7999_vif;
struct mt7999_sta;
struct mt7999_dfs_pulse;
struct mt7999_dfs_pattern;
struct ra_rate_params;

enum mt7999_ram_type {
	MT7999_RAM_TYPE_WM,
	MT7999_RAM_TYPE_WM_TM = MT7999_RAM_TYPE_WM,
	__MT7999_RAM_TYPE_MAX,
};

enum mt7999_var_type {
	MT7999_VAR_TYPE_444,
	MT7999_VAR_TYPE_233,
};

enum mt7999_fem_type {
	MT7999_FEM_EXT,
	MT7999_FEM_INT,
};

enum mt7999_eeprom_mode {
	EEPROM_MODE_DEFAULT_BIN,
	EEPROM_MODE_EFUSE,
	EEPROM_MODE_FLASH,
	EEPROM_MODE_EXT,
	EEPROM_MODE_BIN,
};

#define MT7999_EFUSE_BASE_OFFS_DDIE	0x0
#define MT7999_EFUSE_BASE_OFFS_ADIE0	0x400
#define MT7999_EFUSE_BASE_OFFS_ADIE2	0x1200
#define MT7999_EFUSE_BASE_OFFS_ADIE1	0x1e00
#define MT7999_EFUSE_END_OFFS		0x2a00

enum mt7999_ddie_type {
	DDIE_7999,
	DDIE_TYPE_NUM,
};

enum mt7999_adie_type {
	ADIE_7975,
	ADIE_7976,
	ADIE_7977,
	ADIE_7978,
	ADIE_7979,
	ADIE_7777,
	ADIE_7770,
	ADIE_7773,
	ADIE_TYPE_NUM,
};

enum mt7999_coredump_state {
	MT7999_COREDUMP_IDLE = 0,
	MT7999_COREDUMP_MANUAL_WA,
	MT7999_COREDUMP_MANUAL_WM,
	MT7999_COREDUMP_AUTO,
	__MT7999_COREDUMP_TYPE_MAX,
};

enum mt7999_txq_id {
	MT7999_TXQ_WED_RX = 6,
	MT7999_TXQ_FWDL = 16,
	MT7999_TXQ_MCU_WM,
	MT7999_TXQ_BAND0,
	MT7999_TXQ_BAND1,
	MT7999_TXQ_MCU_WM_VIP,
	MT7999_TXQ_BAND2,
};

enum mt7999_rxq_id {
	MT7999_RXQ_MCU_WM = 0,
	MT7999_RXQ_MCU_SDO,
	MT7999_RXQ_MCU_WA_MAIN = 2,
	MT7999_RXQ_MCU_WA_TRI = 3,
	MT7999_RXQ_BAND0 = 4,
	MT7999_RXQ_BAND2 = 5,
	MT7999_RXQ_RRO_BAND0 = 8,
	MT7999_RXQ_RRO_BAND1 = 9,
	MT7999_RXQ_RRO_BAND2 = 9,
	MT7999_RXQ_MSDU_PG_BAND0 = 10,
	MT7999_RXQ_TXFREE0 = 6,
	MT7999_RXQ_TXFREE1 = 7,
	MT7999_RXQ_TXFREE2 = 12,
	MT7999_RXQ_RRO_IND = 0,
	MT7999_RXQ_RRO_RXDMAD_C = 0,
	MT7999_RXQ_ICS = 11,
};

enum {
	MT7999_TXFREE_FROM_MAC,
};

enum {
	MT7999_RRO_BMC_NOT_BYPASS_UC_BASED_ON_WTBL,
	MT7999_RRO_ALL_NOT_BYPASS,
	MT7999_RRO_BMC_BYPASS_UC_BASED_ON_WTBL,
	MT7999_RRO_ALL_BYPASS,
};

enum mt7999_sta_chsw_state {
	MT7999_STA_CHSW_IDLE,
	MT7999_STA_CHSW_PAUSE_TX,
	MT7999_STA_CHSW_RESUME_TX,
	MT7999_STA_CHSW_TIMEOUT,
};

struct mt7999_twt_flow {
	struct list_head list;
	u64 start_tsf;
	u64 tsf;
	u32 duration;
	u16 peer_id_grp_id;
	__le16 mantissa;
	u8 exp;
	u8 table_id;
	u8 id;
	u8 persist;
	u8 tid_dl_bmp;
	u8 tid_ul_bmp;
	u8 member_cnt;
	u8 bss_idx;
	u8 next_twt_size;
	u16 sta_list[MT7999_MAX_BTWT_MEMBER_CNT];
	u8 protection:1;
	u8 flowtype:1;
	u8 trigger:1;
	u8 sched:1;
	u8 suspended:1;
};
struct mt7999_twt_event {
	struct list_head list;
	u8 btwt_id;
	u16 wcid_idx;
};

DECLARE_EWMA(avg_signal, 10, 8)

enum mt7999_dpd_ch_num {
	DPD_CH_NUM_BW20_2G,
	DPD_CH_NUM_BW20_5G,
	DPD_CH_NUM_BW20_5G_SKIP,
	DPD_CH_NUM_BW80_5G,
	DPD_CH_NUM_BW160_5G,
	DPD_CH_NUM_BW20_6G,
	DPD_CH_NUM_BW80_6G,
	DPD_CH_NUM_BW160_6G,
	DPD_CH_NUM_BW320_6G,
	DPD_CH_NUM_TYPE_MAX,
};

enum vow_feature_flag {
	VOW_FF_BWC_REFILL_PERIOD,
	VOW_FF_BAND1_SEARCH_RULE,
	VOW_FF_BAND0_SEARCH_RULE,
	VOW_FF_WATF_ENABLE,
	VOW_FF_GRP_NO_CHANGE_IN_TXOP,
	VOW_FF_ATF_ENABLE,
	VOW_FF_BWC_TOKEN_REFILL_ENABLE,
	VOW_FF_BWC_ENABLE,
	VOW_FF_ATF_RTS_STA_LOCK,
	VOW_FF_ATF_KEEP_QUANTUM,
	VOW_FF_TX_CNT_MODE_CTRL,
	VOW_FF_TX_MEASURE_MODE_ENABLE,
	VOW_FF_BACKOFF_CTRL,
	VOW_FF_ATF_RTS_FAIL_CHARGE,
	VOW_FF_ZERO_EIFS,
	VOW_FF_RX_RIFS_ENABLE,
	VOW_FF_VOW_CTRL,
	VOW_FF_SPL_STA_NUM,
	VOW_FF_ATF_SCH_CTRL,
	VOW_FF_NUM
};

enum {
	VOW_SEARCH_AC_FIRST,
	VOW_SEARCH_WMM_FIRST
};

/* Default DRR airtime quantum of each level */
enum {
	VOW_DRR_QUANTUM_L0 = 6,
	VOW_DRR_QUANTUM_L1 = 12,
	VOW_DRR_QUANTUM_L2 = 16,
	VOW_DRR_QUANTUM_L3 = 20,
	VOW_DRR_QUANTUM_L4 = 24,
	VOW_DRR_QUANTUM_L5 = 28,
	VOW_DRR_QUANTUM_L6 = 32,
	VOW_DRR_QUANTUM_L7 = 36
};

enum {
	VOW_DRR_QUANTUM_IDX0,
	VOW_DRR_QUANTUM_IDX1,
	VOW_DRR_QUANTUM_IDX2,
	VOW_DRR_QUANTUM_IDX3,
	VOW_DRR_QUANTUM_IDX4,
	VOW_DRR_QUANTUM_IDX5,
	VOW_DRR_QUANTUM_IDX6,
	VOW_DRR_QUANTUM_IDX7,
};

enum {
	VOW_SCH_TYPE_FOLLOW_POLICY,
	VOW_SCH_TYPE_FOLLOW_HW,
	VOW_SCH_TYPE_NUM
};

enum {
	VOW_SCH_POLICY_SRR, /* Shared Round-Robin */
	VOW_SCH_POLICY_WRR, /* Weighted Round-Robin */
	VOW_SCH_POLICY_RR, /* Round-Robin without ATF */
	VOW_SCH_POLICY_NUM
};

#define VOW_STA_MAX_WAIT_TIME	12

enum vow_drr_ctrl_id {
	VOW_DRR_CTRL_STA_ALL,
	VOW_DRR_CTRL_STA_BSS_GROUP,
	VOW_DRR_CTRL_AIRTIME_DEFICIT_BOUND = 0x10,
	VOW_DRR_CTRL_AIRTIME_QUANTUM_ALL = 0x28,
	VOW_DRR_CTRL_STA_PAUSE = 0x30
};

enum {
	LOW_POWER_PCIE_L1SS,
	LOW_POWER_TX_TPO_POWER_BOOST,		/* PB-TPO */
	LOW_POWER_TX_TPO_POWER_REDUCE,		/* LP-TPO */
	LOW_POWER_TX_TPO_ANTENNA_REDUCE,	/* Min Tx TPO */
	LOW_POWER_RX_ULTRA_SAVE,
	LOW_POWER_TX_PST,
	LOW_POWER_RADIO_ULTRA_SAVE,
};
#define LOW_POWER_TPO (LOW_POWER_TX_TPO_POWER_BOOST | \
		       LOW_POWER_TX_TPO_POWER_REDUCE | \
		       LOW_POWER_TX_TPO_ANTENNA_REDUCE)

struct mt7999_vow_ctrl {
	bool atf_enable;
	bool watf_enable;
	u8 drr_quantum[VOW_DRR_QUANTUM_NUM];
	u8 max_deficit;
	u8 sch_type;
	u8 sch_policy;
};

struct mt7999_vow_sta_ctrl {
	bool paused;
	u8 bss_grp_idx;
	u8 drr_quantum[IEEE80211_NUM_ACS];
};

struct mt7999_sta_link {
	struct mt76_wcid wcid; /* must be first */

	struct mt7999_sta *sta;

	struct list_head rc_list;

	s8 chain_signal[IEEE80211_MAX_CHAINS];
	int signal;
	struct ewma_avg_signal chain_signal_avg[IEEE80211_MAX_CHAINS];
	struct ewma_avg_signal signal_avg;

	s8 chain_ack_signal[IEEE80211_MAX_CHAINS];
	int ack_signal;
	struct ewma_avg_signal avg_ack_signal;

	u8 chain_ack_snr[IEEE80211_MAX_CHAINS];

	unsigned long changed;

	struct mt76_connac_sta_key_conf bip;

	struct {
		u8 flowid_mask;
		struct mt7999_twt_flow flow[MT7999_MAX_STA_TWT_AGRT];
	} twt;

	struct rcu_head rcu_head;
	struct mt7999_vow_sta_ctrl vow;
};

struct mt7999_sta {
	struct mt7999_sta_link deflink; /* must be first */
	struct mt7999_sta_link __rcu *link[IEEE80211_MLD_MAX_NUM_LINKS];
	u8 deflink_id;
	u8 seclink_id;
	u16 setup_links;

	struct mt7999_vif *vif;

	bool stop_rx_ba_in_progress;
};

struct mt7999_vif_link {
	struct mt76_vif_link mt76; /* must be first */

	struct mt7999_sta_link msta_link;
	struct mt7999_phy *phy;
	struct cfg80211_bitrate_mask bitrate_mask;

	u8 mld_idx;
	u8 bpcc;
	u8 mbssid_idx;

	s64 tsf_offset[IEEE80211_MLD_MAX_NUM_LINKS];

	/* sta channel switch */
	struct delayed_work sta_chsw_work;
	enum mt7999_sta_chsw_state state;
	enum mt7999_sta_chsw_state next_state;
	u32 pause_timeout;
};

struct mt7999_vif_link_info {
	struct ieee80211_tx_queue_params queue_params[IEEE80211_NUM_ACS];
};

struct mt7999_vif {
	struct mt7999_vif_link deflink; /* must be first */
	struct mt76_vif_data mt76;

	struct mt7999_vif_link_info link_info[IEEE80211_MLD_MAX_NUM_LINKS];

	struct mt7999_sta sta;

	u8 mld_group_idx;
	u8 mld_remap_idx;

	u8 cs_links[__MT_MAX_BAND];

	/* for beacon monitoring */
	struct delayed_work beacon_mon_work;
	unsigned long beacon_received_time[__MT_MAX_BAND];
	u16 lost_links;
	void *probe[__MT_MAX_BAND];
	unsigned long probe_send_time[__MT_MAX_BAND];
	int probe_send_count[__MT_MAX_BAND];

	/* QoS map support */
	u8 qos_map[IP_DSCP_NUM];

	/* sta channel switch */
	u16 tx_paused_links;
};

/* crash-dump */
struct mt7999_crash_data {
	guid_t guid;
	struct timespec64 timestamp;
	bool supported;

	u8 *memdump_buf;
	size_t memdump_buf_len;
};

struct mt7999_hif {
	struct list_head list;

	struct device *dev;
	void __iomem *regs;
	int irq;
	int wed_irq;

	enum pci_bus_speed speed;
	enum pcie_link_width width;
};

struct mt7999_scs_ctrl {
	u8 scs_enable;
	s8 sta_min_rssi;
};

enum {
	SCS_DISABLE = 0,
	SCS_ENABLE,
};

#define WED_RRO_ADDR_SIGNATURE_MASK	GENMASK(31, 24)
#define WED_RRO_ADDR_COUNT_MASK		GENMASK(14, 4)
#define WED_RRO_ADDR_HEAD_HIGH_MASK	GENMASK(3, 0)
struct mt7999_wed_rro_addr {
	__le32 head_low;
	__le32 data;
};

struct mt7999_wed_rro_session_id {
	struct list_head list;
	u16 id;
	u16 wcid;
};

struct mt7999_msdu_page {
	struct list_head list;

	struct mt76_queue *q;
	dma_addr_t dma_addr;
	void *buf;
};

/* data1 */
#define RRO_HIF_DATA1_LS_MASK		BIT(30)
#define RRO_HIF_DATA1_SDL_MASK		GENMASK(29, 16)
/* data4 */
#define RRO_HIF_DATA4_RX_TOKEN_ID_MASK	GENMASK(15, 0)
struct mt7999_rro_hif {
	__le32 data0;
	__le32 data1;
	__le32 data2;
	__le32 data3;
	__le32 data4;
	__le32 data5;
};

#define MSDU_PAGE_INFO_OWNER_MASK	BIT(31)
#define MSDU_PAGE_INFO_PG_HIGH_MASK	GENMASK(3, 0)
struct mt7999_msdu_page_info {
	struct mt7999_rro_hif rxd[MT7999_MAX_HIF_RXD_IN_PG];
	__le32 pg_low;
	__le32 data;
};

#define MT7999_MAX_RRO_RRS_RING 4
struct mt7999_rro_queue_regs_emi {
	struct {
		__le16 idx;
		__le16 rsv;
	} ring[MT7999_MAX_RRO_RRS_RING];
};

struct mt7999_sta_rc_work_data {
	unsigned int link_id;
	u32 changed;
};

struct mt7999_wmm_params {
	int ecwmin;
	int ecwmax;
	int aifsn;
	int txop_limit;
	int acm;
};

#define MT7999_MAX_RRO_RRS_RING 4

struct mt7999_rro_cidx_didx_emi {
	struct {
		u16 idx;
		u16 rsv;
	} ring[MT7999_MAX_RRO_RRS_RING];
};

#ifdef CONFIG_MTK_VENDOR
#define MT7999_AIR_MONITOR_MAX_ENTRY	16
#define MT7999_AIR_MONITOR_MAX_GROUP	(MT7999_AIR_MONITOR_MAX_ENTRY >> 1)

struct mt7999_air_monitor_group {
	bool used[2];
};

struct mt7999_air_monitor_entry {
	bool enable;

	u8 group_idx;
	u8 group_used_idx;
	u8 muar_idx;
	u8 addr[ETH_ALEN];
	u32 last_seen;
	s8 rssi[4];
	struct ieee80211_sta *sta;
};

struct mt7999_air_monitor_ctrl {
	u8 enable;
	struct mt7999_air_monitor_group group[MT7999_AIR_MONITOR_MAX_GROUP];
	struct mt7999_air_monitor_entry entry[MT7999_AIR_MONITOR_MAX_ENTRY];
};

enum {
	CSI_BW20,
	CSI_BW40,
	CSI_BW80,
	CSI_BW160,
	CSI_BW320
};

#define CSI_BW20_DATA_COUNT	64
#define CSI_BW40_DATA_COUNT	128
#define CSI_BW80_DATA_COUNT	256
#define CSI_BW160_DATA_COUNT	512
#define CSI_BW320_DATA_COUNT	1024

struct csi_data {
	u8 fw_ver;
	u8 ch_bw;
	u16 data_num;
	s16 data_i[CSI_BW320_DATA_COUNT];
	s16 data_q[CSI_BW320_DATA_COUNT];
	u8 band;
	s8 rssi;
	u8 snr;
	u32 ts;
	u8 data_bw;
	u8 pri_ch_idx;
	u8 ta[ETH_ALEN];
	u32 ext_info;
	u16 rx_mode;
	u16 rx_rate;
	u32 chain_info;
	u16 tx_idx;
	u16 rx_idx;
	u32 segment_num;
	u8 remain_last;
	u16 pkt_sn;
	u8 tr_stream;

	struct list_head node;
};
#endif

enum edcca_bw_id {
	EDCCA_BW_20 = 0,
	EDCCA_BW_40,
	EDCCA_BW_80,
	EDCCA_BW_160,
	EDCCA_MAX_BW_NUM,
};

#define MT7999_EDCCA_DEFAULT_THRESHOLD 0x7f

struct mt7999_rro_ba_session {
	u32 ack_sn         :12;
	u32 win_sz         :3;
	u32 bn             :1;
	u32 last_in_sn     :12;
	u32 bc             :1;
	u32 bd             :1;
	u32 sat            :1;
	u32 cn             :1;
	u32 within_cnt     :12;
	u32 to_sel         :3;
	u32 rsv            :1;
	u32 last_in_rxtime :12;
};

struct mt7999_phy {
	struct mt76_phy *mt76;
	struct mt7999_dev *dev;

	struct ieee80211_sband_iftype_data iftype[NUM_NL80211_BANDS][NUM_NL80211_IFTYPES];

	struct thermal_cooling_device *cdev;
	u8 cdev_state;
	u8 throttle_state;
	u32 throttle_temp[2]; /* 0: critical high, 1: maximum */
	u8 throttle_level[4];

	u64 omac_mask;

	u16 noise;

	s16 coverage_class;
	u8 slottime;

	u16 beacon_rate;

	u32 rx_ampdu_ts;
	u32 ampdu_ref;
	int txpower;

	struct mt76_mib_stats mib;
	struct mt76_channel_state state_ts;
	struct delayed_work ipi_work;

	u16 orig_chainmask;
	u16 orig_antenna_mask;

	bool has_aux_rx;
	bool counter_reset;
	bool rdd_tx_paused;
	bool sku_limit_en;
	bool sku_path_en;

	u8 pp_mode;
	u16 punct_bitmap;

	struct mt7999_scs_ctrl scs_ctrl;
	u32 hw_drop[MT7999_HW_DROP_STAT][MT7999_HW_DROP_REASON];

	u8 muru_onoff;

#ifdef CONFIG_MTK_VENDOR
	u8 rts_bw_sig;
	spinlock_t amnt_lock;
	struct mt7999_air_monitor_ctrl amnt_ctrl;

	struct {
		struct list_head list;
		spinlock_t lock;
		u32 count;
		bool enable;

		struct csi_data buffered_csi;
		u32 active_bitmap;
		u32 interval;
		u32 last_record;
	} csi;
#endif

	bool sr_enable:1;
	bool enhanced_sr_enable:1;

#ifdef CONFIG_MTK_DEBUG
	bool thermal_protection_enable:1;
	bool mru_probe_enable:1;

	bool rdd_log_en;
#endif

	bool rdd_emulate;

	u8 user_edcca_threshold[EDCCA_MAX_BW_NUM];
	/* Index 0 (TxBSS) is not used */
	struct mt7999_vif_link __rcu *mbssid_conf[MT7999_MAX_MBSSID];

	u32 tx_bytes[MT7999_MAX_WMM_SETS];
	u32 tx_packets[MT7999_MAX_WMM_SETS];
	u32 rx_bytes[MT7999_MAX_WMM_SETS];
	u32 rx_packets[MT7999_MAX_WMM_SETS];
	u16 txop[MT7999_MAX_WMM_SETS];
};

enum {
	FWDL_REQUEST_DL,
	FWDL_DONE,
	FWDL_FAILED,
};

#define SLOT_NUM 512
#define SLOT_SIZE 2048

struct mt7999_fwdl_desc {
	__le64 *buf;
	dma_addr_t dma_addr;
};

struct mt7999_fwdl_entry {
	void *buf;
	dma_addr_t dma_addr;
};

struct mt7999_fwdl_queue {
	struct mt7999_fwdl_desc desc;
	struct mt7999_fwdl_entry entry[SLOT_NUM];
	u16 slot_ready;
};

struct mt7999_firmware_download {
	struct mt7999_dev *dev;
	struct tasklet_struct irq_tasklet;
	spinlock_t irq_lock;
	u32 irq_mask;

	struct completion wait;
	bool wait_ack;
	u16 msg_sn;

	unsigned long action;
	u16 errno;
	u16 notif_sn;
	u16 notif_id;
	u32 resp_id;
	u16 block_id;
	u16 slot_done;
	u16 size;

	struct mt7999_fwdl_queue q;
};

struct mt7999_dev {
	union { /* must be first */
		struct mt76_dev mt76;
		struct mt76_phy mphy;
	};

	struct mt7999_phy *radio_phy[MT7999_MAX_RADIOS];
	struct wiphy_radio radios[MT7999_MAX_RADIOS];
	struct wiphy_radio_freq_range radio_freqs[MT7999_MAX_RADIOS];

	int irq;
	int wed_irq;
	struct mt7999_hif *hif2;
	struct mt7999_reg_desc reg;
	u8 q_id[MT7999_MAX_QUEUE];
	bool q_valid[MT7999_MAX_QUEUE];
	u32 q_int_mask[MT7999_MAX_QUEUE];

	u64 mld_idx_mask;
	u64 mld_remap_idx_mask;

	const struct mt76_bus_ops *bus_ops;
	struct mt7999_phy phy;

	/* monitor rx chain configured channel */
	struct cfg80211_chan_def rdd2_chandef;
	struct mt7999_phy *rdd2_phy;

	u32 hif_idx;

	struct work_struct init_work;
	struct work_struct rc_work;
	struct work_struct dump_work;
	struct work_struct reset_work;
	struct delayed_work scs_work;
	wait_queue_head_t reset_wait;
	struct {
		u32 state;
		u32 l1_reset;
		u32 l1_reset_last;
		u32 wa_reset_count;
		u32 wm_reset_count;
		bool hw_full_reset:1;
		bool hw_init_done:1;
		bool restart:1;
	} recovery;

	/* protects coredump data */
	struct mutex dump_mutex;
	u8 dump_state;
#ifdef CONFIG_DEV_COREDUMP
	struct {
		struct mt7999_crash_data *crash_data[__MT7999_RAM_TYPE_MAX];
	} coredump;
#endif
	char fw_build_date[MT7999_BUILD_TIME_LEN];
	struct mt7999_firmware_download *fwdl;

	struct list_head sta_rc_list;
	struct list_head twt_list;

	u32 hw_pattern;

	u8 eeprom_mode;
	bool apply_cal;

	bool has_uhr:1;
	bool ultra_save:1;
	bool enable_1rpd:1;
	bool disable_dynamic_1rpd;

	struct {
		struct {
			void *ptr;
			dma_addr_t phy_addr;
		} addr_elem[MT7999_RRO_ADDR_ELEM_LEN];
		struct {
			void *ptr;
			dma_addr_t phy_addr;
		} session;
		struct {
			void *ptr;
			dma_addr_t phy_addr;
		} msdu_pg[MT7999_RRO_MSDU_PG_CR_CNT];
		struct {
			struct mt7999_rro_queue_regs_emi *ptr;
			dma_addr_t phy_addr;
		} emi_rings_cpu;
		struct {
			struct mt7999_rro_queue_regs_emi *ptr;
			dma_addr_t phy_addr;
		} emi_rings_dma;
		struct work_struct work;
		struct list_head poll_list;
		spinlock_t lock;
		struct list_head page_cache;
		struct list_head page_map[MT7999_RRO_MSDU_PG_HASH_SIZE];
	} wed_rro;

	dma_addr_t npu_txd_addr[2 * MT7999_NPU_TXD_SIZE];

	struct {
		void *ptr;
		dma_addr_t phy_addr;
	} dummy_buff[2];

	bool testmode_enable;
	u32 bg_nxt_freq;

	u32 option_type;

	bool ibf;
	u8 fw_debug_wm;
	u16 fw_debug_bin;
	u16 fw_debug_seq;
	bool fw_debug_muru_disable;
	bool idxlog_enable;

	struct dentry *debugfs_dir;
	struct rchan *relay_fwlog;
	struct rchan *relay_idxlog;

	struct debugfs_blob_wrapper cal;
	u32 cur_prek_offset;
	struct {
		const u32 *rev;
		u8 dpd_ch_num[DPD_CH_NUM_TYPE_MAX];
	} prek;

	struct {
		u16 table_mask;
		u8 n_agrt;
		u8 n_agrt_bc;
		struct mt7999_twt_flow btwt_flow[MT7999_MAX_BTWT_AGRT];
		struct work_struct event_work;
		struct list_head event_list;
		spinlock_t event_lock;
	} twt;

	spinlock_t reg_lock;

	u8 wtbl_size_group;
	struct {
		u8 type:4;
		u8 fem:4;
	} var;

	struct mt7999_vow_ctrl vow;
	bool radio_ultra_save;

	bool wmm_pbc_enable;
	struct work_struct wmm_pbc_work;
	u16 pbc_lower_bound[WMM_PBC_QUEUE_NUM];
	u16 pbc_upper_bound[WMM_PBC_QUEUE_NUM];
	bool disable_dynamic_txop;
#ifdef CONFIG_MTK_DEBUG
	u16 wlan_idx;
	bool wtbl_query_cr;
	bool ple_query_cr;
	struct {
		u32 fw_dbg_module;
		u8 fw_dbg_lv;
		u32 bcn_total_cnt[__MT_MAX_BAND];
		u32 sid;

		u8 dump_ple_txd;
		u32 token_idx;
		u32 fid_idx;
		struct {
			bool pcie_l1ss_enable:1;
			bool ultra_save:1;
			bool one_rpd:1;
			bool mmps:1;
			bool mdpc:1;
			bool dcm:1;
			bool alpl:1;
			u8 tpo;
			u8 pb_tpo;
			u8 lp_tpo;
			u8 min_tx_tpo;
			u8 pst;
		} lp;

		bool beacon_loss_keep_conn;
		bool ignore_radar_event;
	} dbg;
	const struct mt7999_dbg_reg_desc *dbg_reg;
	bool sr_pp_disable:1;
	bool red_enable:1;

	struct {
		u8 de_amsdu;
		u8 subfrm_len_type;
		u8 subfrm_len_endian;
	} mesh_hw_amsdu_ctrl;
#endif
#ifdef CONFIG_MTK_VENDOR
	u8 cert_mode;
	u8 coding_type;
#endif

#if defined CONFIG_NL80211_TESTMODE || defined CONFIG_MTK_DEBUG
	struct {
		void *txbf_phase_cal;
		void *txbf_pfmu_data;
		void *txbf_pfmu_tag;
	} test;
	u8 rf_mode;
#endif
};

enum rdd_idx {
	MT_RDD_IDX_BAND2,	/* RDD idx for band idx 2 */
	MT_RDD_IDX_BAND1,	/* RDD idx for band idx 1 */
	MT_RDD_IDX_BACKGROUND,	/* RDD idx for background chain */
};

enum mt7999_rdd_cmd {
	RDD_STOP,
	RDD_START,
	RDD_DET_MODE,
	RDD_RADAR_EMULATE,
	RDD_START_TXQ = 20,
	RDD_CAC_START = 50,
	RDD_CAC_END,
	RDD_NORMAL_START,
	RDD_DISABLE_DFS_CAL,
	RDD_PULSE_DBG,
	RDD_READ_PULSE,
	RDD_RESUME_BF,
	RDD_IRQ_OFF,
	RDD_DISABLE_ZW_TIMER,
};

/* WTBL */
enum mt7999_wtbl_type {
	WTBL_TYPE_LMAC, 	/* WTBL in LMAC */
	WTBL_TYPE_UMAC, 	/* WTBL in UMAC */
	WTBL_TYPE_KEY,		/* Key Table */
	MAX_NUM_WTBL_TYPE
};

enum mt7999_twt_check_result {
	TWT_CHECK_NOT_EXIST,
	TWT_CHECK_SAME,
	TWT_CHECK_NEED_MODIFY,
};

static inline int
mt7999_get_rdd_idx(struct mt7999_phy *phy, bool is_background)
{
	if (!phy->mt76->cap.has_5ghz)
		return -1;

	if (is_background)
		return MT_RDD_IDX_BACKGROUND;

	if (phy->mt76->band_idx == MT_BAND2)
		return MT_RDD_IDX_BAND2;

	return MT_RDD_IDX_BAND1;
}

static inline struct mt7999_dev *
mt7999_hw_dev(struct ieee80211_hw *hw)
{
	struct mt76_phy *phy = hw->priv;

	return container_of(phy->dev, struct mt7999_dev, mt76);
}

static inline struct mt7999_phy *
__mt7999_phy(struct mt7999_dev *dev, enum mt76_band_id band)
{
	struct mt76_phy *phy = dev->mt76.phys[band];

	if (!phy)
		return NULL;

	return phy->priv;
}

static inline struct mt7999_phy *
mt7999_phy2(struct mt7999_dev *dev)
{
	return __mt7999_phy(dev, MT_BAND1);
}

static inline struct mt7999_phy *
mt7999_phy3(struct mt7999_dev *dev)
{
	return __mt7999_phy(dev, MT_BAND2);
}

static inline bool
mt7999_band_valid(struct mt7999_dev *dev, u8 band)
{
	return band <= MT_BAND2;
}

static inline bool
mt7999_radio_valid(struct mt7999_dev *dev, u8 radio)
{
	if (radio >= dev->mt76.hw->wiphy->n_radio ||
	    !dev->radio_phy[radio])
		return false;
	return true;
}

static inline struct mt7999_phy *
mt7999_band_phy(struct mt7999_dev *dev, enum nl80211_band band)
{
	struct mt76_phy *mphy;

	mphy = dev->mt76.band_phys[band];
	if (!mphy)
		return NULL;

	return mphy->priv;
}

static inline struct mt7999_vif_link *
mt7999_vif_link(struct mt7999_dev *dev, struct ieee80211_vif *vif, int link_id)
{
	return (struct mt7999_vif_link *)mt76_vif_link(&dev->mt76, vif, link_id);
}

static inline struct mt7999_phy *
mt7999_vif_link_phy(struct mt7999_vif_link *link)
{
	struct mt76_phy *mphy = mt76_vif_link_phy(&link->mt76);

	if (!mphy)
		return NULL;

	return mphy->priv;
}

static inline struct mt7999_vif_link *
mt7999_vif_conf_link(struct mt7999_dev *dev, struct ieee80211_vif *vif,
		     struct ieee80211_bss_conf *link_conf)
{
	return (struct mt7999_vif_link *)mt76_vif_conf_link(&dev->mt76, vif,
							    link_conf);
}

static inline struct mt7999_sta_link *
mt7999_sta_link(struct mt7999_sta *msta, u8 link_id)
{
	if (link_id >= IEEE80211_MLD_MAX_NUM_LINKS)
		return NULL;

	return rcu_dereference(msta->link[link_id]);
}

static inline struct mt7999_sta_link *
mt7999_sta_link_protected(struct mt7999_dev *dev, struct mt7999_sta *msta,
			  u8 link_id)
{
	if (link_id >= IEEE80211_MLD_MAX_NUM_LINKS)
		return NULL;

	return mt76_dereference(msta->link[link_id], &dev->mt76);
}

#define mt7999_for_each_phy(dev, phy)					\
	for (int __i = 0; __i < ARRAY_SIZE((dev)->radio_phy); __i++)	\
		if (((phy) = (dev)->radio_phy[__i]) != NULL)

#define mt7999_for_each_btwt_flow(dev, flow)				\
	for (int __i = 0; __i < MT7999_MAX_BTWT_AGRT; __i++)		\
		if (((dev)->twt.table_mask &				\
		     BIT(__i + MT7999_BTWT_AGRT_OFFSET)) &&		\
		    ((flow) = &(dev)->twt.btwt_flow[__i]))

static inline u8
mt7999_btwt_has_id(struct mt7999_twt_flow *flow, u8 id)
{
	return (flow->peer_id_grp_id & 0xff) == id;
}

static inline void
mt7999_get_merged_ttlm(struct ieee80211_vif *vif,
		       struct ieee80211_neg_ttlm *merged_ttlm)
{
	u16 map = vif->valid_links;
	int tid;

	if (vif->neg_ttlm.valid) {
		memcpy(merged_ttlm->downlink, vif->neg_ttlm.downlink,
		       sizeof(merged_ttlm->downlink));
		memcpy(merged_ttlm->uplink, vif->neg_ttlm.uplink,
		       sizeof(merged_ttlm->uplink));
		return;
	}

	if (vif->adv_ttlm.active)
		map &= vif->adv_ttlm.map;

	for (tid = 0; tid < IEEE80211_TTLM_NUM_TIDS; tid++) {
		merged_ttlm->downlink[tid] = map;
		merged_ttlm->uplink[tid] = map;
	}
}

extern const struct ieee80211_ops mt7999_ops;
extern struct pci_driver mt7999_pci_driver;
extern struct pci_driver mt7999_hif_driver;
extern const struct mt76_testmode_ops mt7999_testmode_ops;

struct mt7999_dev *mt7999_mmio_probe(struct device *pdev,
				     void __iomem *mem_base, u32 device_id);
void mt7999_rro_hw_init(struct mt7999_dev *dev);
void mt7999_wfsys_reset(struct mt7999_dev *dev);
void mt7999_init_work_reset(struct mt7999_dev *dev);
void mt7999_rro_hw_init(struct mt7999_dev *dev);
irqreturn_t mt7999_irq_handler(int irq, void *dev_instance);
u64 __mt7999_get_tsf(struct ieee80211_hw *hw, struct mt7999_vif_link *link);
int mt7999_register_device(struct mt7999_dev *dev);
void mt7999_unregister_device(struct mt7999_dev *dev);
int mt7999_vif_link_add(struct mt76_phy *mphy, struct ieee80211_vif *vif,
			struct ieee80211_bss_conf *link_conf,
			struct mt76_vif_link *mlink);
void mt7999_vif_link_remove(struct mt76_phy *mphy, struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf,
			    struct mt76_vif_link *mlink);
const char *mt7999_eeprom_name(struct mt7999_dev *dev);
int mt7999_eeprom_init(struct mt7999_dev *dev);
int mt7999_eeprom_check_fw_mode(struct mt7999_dev *dev);
int mt7999_eeprom_parse_hw_cap(struct mt7999_dev *dev, struct mt7999_phy *phy);
int mt7999_eeprom_get_target_power(struct mt7999_dev *dev,
				   struct ieee80211_channel *chan);
s8 mt7999_eeprom_get_power_delta(struct mt7999_dev *dev, int band);
bool mt7999_eeprom_has_background_radar(struct mt7999_dev *dev);
int mt7999_dma_init(struct mt7999_dev *dev);
void mt7999_dma_reset(struct mt7999_dev *dev, bool force);
void mt7999_dma_prefetch(struct mt7999_dev *dev);
void mt7999_dma_cleanup(struct mt7999_dev *dev);
void mt7999_dma_start(struct mt7999_dev *dev, bool reset, bool wed_reset);
int mt7999_init_tx_queues(struct mt7999_phy *phy, int idx,
			  int n_desc, int ring_base, struct mtk_wed_device *wed);
void mt7999_init_txpower(struct mt7999_phy *phy);
int mt7999_txbf_init(struct mt7999_dev *dev);
void mt7999_reset(struct mt7999_dev *dev);
void mt7999_coredump(struct mt7999_dev *dev, u8 state);
int mt7999_run(struct mt7999_phy *phy);
int mt7999_mcu_init(struct mt7999_dev *dev);
int mt7999_mcu_init_firmware(struct mt7999_dev *dev);
int mt7999_mcu_twt_agrt_update(struct mt7999_dev *dev,
			       struct mt7999_vif_link *link,
			       struct mt7999_twt_flow *flow,
			       int cmd);
void mt7999_mac_btwt_cleanup(struct mt7999_dev *dev,
			     struct mt7999_vif_link *link);
void mt7999_btwt_table_cleanup(struct mt7999_dev *dev,
			       struct mt7999_vif_link *link);
int mt7999_mcu_add_dev_info(struct mt7999_phy *phy, struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf,
			    struct mt76_vif_link *mlink, bool enable);
int mt7999_mcu_add_bss_info(struct mt7999_phy *phy, struct ieee80211_vif *vif,
			    struct ieee80211_bss_conf *link_conf,
			    struct mt76_vif_link *mlink,
			    struct mt7999_sta_link *msta_link, int enable);
int mt7999_mcu_update_bss_rfch(struct mt7999_phy *phy,
			       struct mt7999_vif_link *link);
int mt7999_mcu_add_sta(struct mt7999_dev *dev,
		       struct ieee80211_bss_conf *link_conf,
		       struct ieee80211_link_sta *link_sta,
		       struct mt7999_vif_link *link,
		       struct mt7999_sta_link *msta_link,
		       int conn_state, bool newly);
int mt7999_mcu_set_peer_mld(struct mt7999_dev *dev, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta);
int mt7999_mcu_teardown_mld_sta(struct mt7999_dev *dev,
				struct mt7999_vif_link *link,
				struct mt7999_sta_link *msta_link);
void mt7999_mcu_update_sta_rec_bw(void *data, struct ieee80211_sta *sta);
int mt7999_mcu_add_tx_ba(struct mt7999_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 struct ieee80211_vif *vif, bool enable);
int mt7999_mcu_add_rx_ba(struct mt7999_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 struct ieee80211_vif *vif, bool enable);
int mt7999_mcu_update_bss_color(struct mt7999_dev *dev,
				struct mt76_vif_link *mlink,
				struct cfg80211_he_bss_color *he_bss_color);
int mt7999_mcu_add_beacon(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			  struct ieee80211_bss_conf *link_conf, bool enabled);
int mt7999_mcu_beacon_inband_discov(struct mt7999_dev *dev,
				    struct ieee80211_bss_conf *link_conf,
				    struct mt7999_vif_link *link, u32 changed);
int mt7999_mcu_add_obss_spr(struct mt7999_phy *phy,
			    struct mt7999_vif_link *link,
			    struct ieee80211_he_obss_pd *he_obss_pd);
int mt7999_mcu_add_rate_ctrl(struct mt7999_dev *dev, struct mt7999_sta *msta,
			     struct ieee80211_vif *vif, u8 link_id,
			     bool changed);
int mt7999_set_channel(struct mt76_phy *mphy);
int mt7999_mcu_set_chan_info(struct mt7999_phy *phy, u16 tag, bool sta);
int mt7999_mcu_set_tx(struct mt7999_dev *dev, struct ieee80211_vif *vif,
		      struct ieee80211_bss_conf *link_conf);
int mt7999_mcu_set_fixed_rate_ctrl(struct mt7999_dev *dev, void *data, u16 tag,
				   u16 version);
int mt7999_mcu_set_fixed_rate_param(struct mt7999_dev *dev,
				    struct mt7999_sta *msta,
				    struct ra_rate_params *params,
				    u8 link_id, u16 bitmap);
int mt7999_mcu_sta_rate_update(struct mt7999_dev *dev, struct mt7999_sta *msta,
			       u8 link_id, u32 field);
int mt7999_mcu_set_eeprom(struct mt7999_dev *dev);
int mt7999_mcu_get_eeprom(struct mt7999_dev *dev, u32 offset, u8 *buf, u32 buf_len,
			  enum mt7999_eeprom_mode mode);
int mt7999_mcu_get_efuse_free_block(struct mt7999_dev *dev, void *info);
int mt7999_mcu_write_ext_eeprom(struct mt7999_dev *dev, u32 offset,
				u32 data_len, u8 *write_buf);
int mt7999_mcu_get_chip_config(struct mt7999_dev *dev, u32 *cap);
int mt7999_mcu_set_ser(struct mt7999_dev *dev, u8 action, u8 set, u8 band);
int mt7999_mcu_set_txbf(struct mt7999_dev *dev, u8 action);
int mt7999_mcu_set_fcc5_lpn(struct mt7999_dev *dev, int val);
int mt7999_mcu_set_pulse_th(struct mt7999_dev *dev,
			    const struct mt7999_dfs_pulse *pulse);
int mt7999_mcu_set_radar_th(struct mt7999_dev *dev, int index,
			    const struct mt7999_dfs_pattern *pattern);
int mt7999_mcu_set_radio_ultra_save_en(struct mt7999_phy *phy, bool enable);
int mt7999_mcu_set_radio_en(struct mt7999_phy *phy, bool enable);
int mt7999_mcu_set_ack_timeout(struct mt7999_phy *phy, u8 type, u32 timeout);
int mt7999_mcu_set_rts_thresh(struct mt7999_phy *phy, u32 val);
int mt7999_mcu_set_protection(struct mt7999_phy *phy, struct mt7999_vif_link *link,
			      u8 ht_mode, bool use_cts_prot);
int mt7999_mcu_set_timing(struct mt7999_phy *phy, struct ieee80211_vif *vif,
			  struct ieee80211_bss_conf *link_conf);
int mt7999_mcu_set_bssid_mapping_addr(struct mt76_dev *dev, u8 band);
int mt7999_mcu_get_chan_mib_info(struct mt7999_phy *phy, bool chan_switch);
int mt7999_mcu_get_temperature(struct mt7999_phy *phy);
int mt7999_mcu_set_thermal_throttling(struct mt7999_phy *phy, u8 state);
int mt7999_mcu_set_thermal_protect(struct mt7999_phy *phy, bool enable);
int mt7999_mcu_set_thermal_info_query(struct mt7999_phy *phy);
int mt7999_mcu_set_txpower_sku(struct mt7999_phy *phy,
			       int txpower_setting);
int mt7999_mcu_rdd_resume_tx(struct mt7999_phy *phy);
int mt7999_mcu_rdd_cmd(struct mt7999_dev *dev, int cmd, u8 rdd_idx, u8 val);
int mt7999_alloc_afc_table(struct mt7999_dev *dev);
void mt7999_free_afc_table(struct mt7999_dev *dev);
int mt7999_mcu_rdd_background_disable_timer(struct mt7999_dev *dev,
					    bool disable_timer);
int mt7999_mcu_rdd_background_enable(struct mt7999_phy *phy,
				     struct cfg80211_chan_def *chandef);
int mt7999_mcu_set_fixed_rate_table(struct mt7999_phy *phy, u8 table_idx,
				    u16 rate_idx, bool beacon);
int mt7999_mcu_rf_regval(struct mt7999_dev *dev, u32 regidx,
			 u32 *val, bool set, bool efem);
int mt7999_mcu_set_hdr_trans(struct mt7999_dev *dev, bool hdr_trans);
int mt7999_mcu_set_rro(struct mt7999_dev *dev, u16 tag, u16 val);
int mt7999_mcu_set_rro_flush_timeout(struct mt7999_dev *dev);
int mt7999_mcu_sdo_cmd(struct mt7999_dev *dev, bool query, u32 a1, u32 a2, u32 a3);
int mt7999_mcu_red_config(struct mt7999_dev *dev, bool enable);
int mt7999_mcu_fw_log_2_host(struct mt7999_dev *dev, u8 ctrl);
int mt7999_mcu_fw_dbg_ctrl(struct mt7999_dev *dev, u32 module, u8 level);
int mt7999_mcu_fw_time_sync(struct mt76_dev *dev);
void mt7999_mcu_rx_event(struct mt7999_dev *dev, struct sk_buff *skb);
void mt7999_mcu_exit(struct mt7999_dev *dev);
int mt7999_mcu_get_per_sta_info(struct mt76_dev *dev, u16 tag,
	                        u16 sta_num, u16 *sta_list);
int mt7999_mcu_get_all_sta_info(struct mt76_dev *dev, u16 tag);
int mt7999_mcu_get_bss_acq_pkt_cnt(struct mt7999_dev *dev);
int mt7999_mcu_set_sniffer_mode(struct mt7999_phy *phy, bool enabled);
int mt7999_mcu_mld_reconf_stop_link(struct mt7999_dev *dev,
				    struct ieee80211_vif *vif,
				    u16 removed_links);
int mt7999_mcu_mld_link_oper(struct mt7999_dev *dev,
			     struct ieee80211_bss_conf *link_conf,
			     struct mt7999_vif_link *link, bool add);
int mt7999_mcu_mld_set_attlm(struct mt7999_dev *dev, struct ieee80211_vif *vif,
			     u16 disabled_links, u16 switch_time, u32 duration);
int mt7999_mcu_peer_mld_ttlm_req(struct mt7999_dev *dev, struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta,
				 struct ieee80211_neg_ttlm *neg_ttlm);
int mt7999_mcu_set_tx_power_ctrl(struct mt7999_phy *phy, u8 power_ctrl_id, u8 data);
int mt7999_mcu_get_tx_power_info(struct mt7999_phy *phy, u8 category, void *event);
int mt7999_mcu_apply_group_cal(struct mt7999_dev *dev);
int mt7999_mcu_apply_tx_dpd(struct mt7999_phy *phy);
#ifdef CONFIG_NL80211_TESTMODE
void mt7999_tm_rf_test_event(struct mt7999_dev *dev, struct sk_buff *skb);
#endif
int mt7999_mcu_set_scs(struct mt7999_phy *phy, u8 enable);
void mt7999_mcu_scs_sta_poll(struct work_struct *work);
int mt7999_mcu_set_band_confg(struct mt7999_phy *phy, u16 option, bool enable);
int mt7999_mcu_set_vow_drr_ctrl(struct mt7999_phy *phy,
				struct mt7999_vif_link *mconf,
				struct mt7999_sta_link *msta_link,
				enum vow_drr_ctrl_id id);
int mt7999_mcu_set_vow_feature_ctrl(struct mt7999_phy *phy, unsigned long flags);
void mt7999_mcu_wmm_pbc_work(struct work_struct *work);
int mt7999_mcu_ba_trigger_enable(struct mt7999_dev *dev, u8 enable);
int mt7999_mcu_init_rx_airtime(struct mt7999_dev *dev);

static inline bool mt7999_has_hwrro(struct mt7999_dev *dev)
{
	return dev->mt76.hwrro_mode != MT76_HWRRO_OFF;
}

static inline u8 mt7999_max_interface_num(struct mt7999_dev *dev)
{
	return min(MT7999_MAX_INTERFACES * (1 + mt7999_band_valid(dev, MT_BAND1) +
					    mt7999_band_valid(dev, MT_BAND2)),
		   MT7999_WTBL_BMC_SIZE);
}

static inline u16 mt7999_wtbl_size(struct mt7999_dev *dev)
{
	return (dev->wtbl_size_group << 8) + MT7999_WTBL_BMC_SIZE;
}

void mt7999_dual_hif_set_irq_mask(struct mt7999_dev *dev, bool write_reg,
				  u32 clear, u32 set);

static inline void mt7999_irq_enable(struct mt7999_dev *dev, u32 mask)
{
	if (dev->hif2)
		mt7999_dual_hif_set_irq_mask(dev, false, 0, mask);
	else
		mt76_set_irq_mask(&dev->mt76, 0, 0, mask);

	tasklet_schedule(&dev->mt76.irq_tasklet);
}

static inline void mt7999_irq_disable(struct mt7999_dev *dev, u32 mask)
{
	if (dev->hif2)
		mt7999_dual_hif_set_irq_mask(dev, true, mask, 0);
	else
		mt76_set_irq_mask(&dev->mt76, MT_INT_MASK_CSR, mask, 0);
}

void mt7999_memcpy_fromio(struct mt7999_dev *dev, void *buf, u32 offset,
			  size_t len);

static inline u16 mt7999_rx_chainmask(struct mt7999_phy *phy)
{
	int max_nss = hweight16(phy->orig_antenna_mask);
	int cur_nss = hweight8(phy->mt76->antenna_mask);
	u16 tx_chainmask = phy->mt76->chainmask;

	if (cur_nss != max_nss)
		return tx_chainmask;

	return tx_chainmask | (BIT(fls(tx_chainmask)) * phy->has_aux_rx);
}

static inline bool mt7999_has_ext_eeprom(struct mt7999_dev *dev)
{
	switch (mt76_chip(&dev->mt76)) {
	case MT7999_DEVICE_ID:
	default:
		return false;
	}
}

static inline bool
mt7999_vow_should_enable(struct mt7999_dev *dev)
{
	return (!wiphy_ext_feature_isset(mt76_hw(dev)->wiphy,
					 NL80211_EXT_FEATURE_AIRTIME_FAIRNESS) ||
		mtk_wed_device_active(&dev->mt76.mmio.wed)) &&
		test_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state);
}

void mt7999_set_pcie_l1ss(struct mt7999_dev *dev, bool en);
void mt7999_mac_init(struct mt7999_dev *dev);
void mt7999_mac_reset_counters(struct mt7999_phy *phy);
void mt7999_mac_enable_nf(struct mt7999_dev *dev, u8 band);
void mt7999_mac_write_txwi(struct mt7999_dev *dev, __le32 *txwi,
			   struct sk_buff *skb, struct mt76_wcid *wcid,
			   struct ieee80211_key_conf *key, int pid,
			   enum mt76_txq_id qid, u32 changed);
void mt7999_mac_update_beacons(struct mt7999_phy *phy);
int mt7999_mac_set_coverage_class(struct mt7999_phy *phy);
void mt7999_mac_work(struct work_struct *work);
void mt7999_mac_reset_work(struct work_struct *work);
void mt7999_mac_dump_work(struct work_struct *work);
void mt7999_mac_sta_rc_work(struct work_struct *work);
void mt7999_mac_update_stats(struct mt7999_phy *phy);
int mt7999_mac_twt_teardown_flow(struct mt7999_dev *dev,
				 struct mt7999_vif_link *link,
				 struct mt7999_sta_link *msta_link,
				 struct ieee80211_twt_teardown *td);
void mt7999_mac_sta_remove_link(struct mt7999_dev *dev,
				struct ieee80211_sta *sta,
				unsigned int link_id, bool flush);
int mt7999_mac_add_twt_setup(struct ieee80211_hw *hw,
			     struct ieee80211_sta *sta, u8 link_id,
			     struct ieee80211_twt_setup *twt);
int mt7999_mac_add_btwt(struct ieee80211_hw *hw,
			struct ieee80211_vif *vif, u8 link_id,
			struct cfg80211_broadcast_twt_settings *btwt);
int mt7999_mac_del_btwt(struct ieee80211_hw *hw,
			struct ieee80211_vif *vif, u8 link_id, u8 id);
int mt7999_mac_sta_poll(struct mt76_dev *dev);
void mt7999_irq_tasklet_fwdl(struct tasklet_struct *t);
void mt7999_sta_chsw_state_reset(struct mt7999_vif_link *mconf);
int mt7999_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
			  enum mt76_txq_id qid, struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta,
			  struct mt76_tx_info *tx_info);
void mt7999_tx_token_put(struct mt7999_dev *dev);
void mt7999_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb, u32 *info);
void mt7999_rro_msdu_page_map_free(struct mt7999_dev *dev);
int mt7999_rro_msdu_page_add(struct mt76_dev *mdev, struct mt76_queue *q,
			     dma_addr_t dma_addr, void *data);
void mt7999_rro_rx_process(struct mt76_dev *mdev, void *data);
bool mt7999_rx_check(struct mt76_dev *mdev, void *data, int len);
void mt7999_stats_work(struct work_struct *work);
void mt7999_beacon_mon_work(struct work_struct *work);
void mt7999_sta_chsw_work(struct work_struct *work);
void mt7999_twt_event_work(struct work_struct *work);
void mt7999_twt_event_list_free(struct mt7999_dev *dev);
int mt76_dfs_start_rdd(struct mt7999_dev *dev, bool force);
int mt7999_dfs_init_radar_detector(struct mt7999_phy *phy);
void mt7999_set_stream_caps(struct mt7999_phy *phy);
void mt7999_set_stream_vht_txbf_caps(struct mt7999_phy *phy);
void mt7999_update_channel(struct mt76_phy *mphy);
int mt7999_init_dev_debugfs(struct mt7999_phy *phy);
int mt7999_init_band_debugfs(struct mt7999_phy *phy);
void mt7999_debugfs_rx_fw_monitor(struct mt7999_dev *dev, const void *data, int len);
bool mt7999_debugfs_rx_log(struct mt7999_dev *dev, const void *data, int len);
int mt7999_mcu_add_key(struct mt76_dev *dev, struct mt7999_vif_link *link,
		       struct ieee80211_key_conf *key, int mcu_cmd,
		       struct mt76_wcid *wcid, enum set_key_cmd cmd);
int mt7999_mcu_wtbl_update_hdr_trans(struct mt7999_dev *dev,
				     struct ieee80211_vif *vif,
				     struct mt7999_vif_link *link,
				     struct mt7999_sta_link *msta_link);
int mt7999_mcu_cp_support(struct mt7999_dev *dev, u8 mode);
int mt7999_mcu_set_emlsr_mode(struct mt7999_dev *dev,
			      struct ieee80211_vif *vif,
			      struct ieee80211_sta *sta,
			      struct ieee80211_eml_params *eml_params);
int mt7999_mcu_set_pp_en(struct mt7999_phy *phy, u8 mode, u16 bitmap);
int mt7999_mcu_set_pp_sta_dscb(struct mt7999_phy *phy, struct cfg80211_chan_def *chandef,
			       u8 omac_idx);
int mt7999_mcu_set_pp_alg_ctrl(struct mt7999_phy *phy, u8 action);
int mt7999_mcu_epcs_ctrl(struct mt7999_dev *dev, struct ieee80211_sta *sta,
			 u8 link_id, bool enable, u16 wmm_idx,
			 struct mt7999_wmm_params *params);
int mt7999_mcu_get_tsf(struct mt7999_phy *phy, struct mt7999_vif_link *link,
		       u64 *tsf);
int mt7999_mcu_get_tsf_offset(struct mt7999_phy *phy, struct mt7999_vif *mvif,
			      int rpting_link_id, int rpted_link_id);
int mt7999_mcu_set_qos_map(struct mt7999_dev *dev, struct ieee80211_vif *vif,
			   struct mt7999_vif_link *link,
			   struct cfg80211_qos_map *usr_qos_map);
#ifdef CONFIG_MAC80211_DEBUGFS
void mt7999_sta_add_debugfs(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			    struct ieee80211_sta *sta, struct dentry *dir);
void mt7999_link_sta_add_debugfs(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
				 struct ieee80211_link_sta *link_sta,
				 struct dentry *dir);
void mt7999_vif_add_debugfs(struct ieee80211_hw *hw, struct ieee80211_vif *vif);
#endif
int mt7999_mmio_wed_init(struct mt7999_dev *dev, void *pdev_ptr,
			 bool hif2, int *irq);
u32 mt7999_wed_init_buf(void *ptr, dma_addr_t phys, int token_id);
void mt7999_mac_ba_trigger(struct mt7999_dev *dev, u16 wlan_idx, u8 tid);

int mt7999_dma_rro_init(struct mt7999_dev *dev);
void mt7999_dma_rro_start(struct mt7999_dev *dev);
int mt7999_vow_init(struct mt7999_phy *phy);

#ifdef CONFIG_MT7999_NPU
int __mt7999_npu_hw_init(struct mt7999_dev *dev);
int mt7999_npu_hw_init(struct mt7999_dev *dev);
int mt7999_npu_hw_stop(struct mt7999_dev *dev);
int mt7999_npu_rx_queues_init(struct mt7999_dev *dev);
#else
static inline int __mt7999_npu_hw_init(struct mt7999_dev *dev)
{
	return 0;
}

static inline int mt7999_npu_hw_init(struct mt7999_dev *dev)
{
	return 0;
}

static inline int mt7999_npu_hw_stop(struct mt7999_dev *dev)
{
	return 0;
}

static inline int mt7999_npu_rx_queues_init(struct mt7999_dev *dev)
{
	return 0;
}
#endif /* CONFIG_MT7999_NPU */

static inline void mt7999_set_pse_drop(struct mt7999_dev *dev, bool enable)
{
#ifdef CONFIG_NET_MEDIATEK_SOC_WED
	if (!mtk_wed_device_active(&dev->mt76.mmio.wed))
		return;

	mtk_wed_device_ppe_drop(&dev->mt76.mmio.wed, enable);
#endif /* CONFIG_NET_MEDIATEK_SOC_WED */
}

static inline bool mt7999_check_token_range(struct mt7999_dev *dev, u32 msdu)
{
#ifdef CONFIG_NET_MEDIATEK_SOC_WED
	return (!mtk_wed_device_active(&dev->mt76.mmio.wed) ||
		!in_range(msdu, 0, dev->mt76.mmio.wed.wlan.nbuf));
#else
	return true;
#endif /* CONFIG_NET_MEDIATEK_SOC_WED */
}

#ifdef CONFIG_MTK_VENDOR
void mt7999_set_wireless_vif(void *data, u8 *mac, struct ieee80211_vif *vif);
void mt7999_vendor_register(struct mt7999_phy *phy);
void mt7999_vendor_amnt_fill_rx(struct mt7999_phy *phy, struct sk_buff *skb);
int mt7999_vendor_amnt_sta_remove(struct mt7999_phy *phy,
				  struct ieee80211_link_sta *link_sta);
void mt7999_set_wireless_amsdu(struct ieee80211_hw *hw, u8 en);
void mt7999_mcu_set_mimo(struct mt7999_phy *phy);
int mt7999_set_muru_cfg(struct mt7999_dev *dev, u8 action, u8 val);
int mt7999_mcu_set_muru_cfg(struct mt7999_dev *dev, void *data);
void mt7999_set_beacon_vif(struct ieee80211_vif *vif, u8 val);
int mt7999_mcu_set_csi(struct mt7999_phy *phy, u8 mode,
		       u8 cfg, u8 v1, u32 v2, u8 *mac_addr);
int mt7999_vendor_pp_bitmap_update(struct mt7999_phy *phy, u16 bitmap);
int mt7999_mcu_set_lpi_psd(struct mt7999_phy *phy, u8 enable);
int mt7999_vendor_tsf_offset_event(struct mt7999_phy *phy, s64 *tsf_offset, int tsf_len);
#endif

int mt7999_mcu_edcca_enable(struct mt7999_phy *phy, bool enable);
int mt7999_mcu_edcca_threshold_ctrl(struct mt7999_phy *phy, u8 *value, bool set);
int mt7999_mcu_set_tpo(struct mt7999_dev *dev, u8 type, u8 val);
int mt7999_mcu_set_lp_option(struct mt7999_dev *dev, u8 *arg);
void mt7999_mcu_enable_1rpd(struct mt7999_dev *dev, bool enable);
int mt7999_mcu_set_pst(struct mt7999_dev *dev, u32 band, u32 cmd, u32 val);
int mt7999_mcu_flush_ple(struct mt7999_dev *dev, u16 threshold,
			 u8 *sta_bitmap, u16 sta_cnt);
void mt7999_irq_fwdl_enable(struct mt7999_dev *dev, u32 mask);
void mt7999_irq_fwdl_disable(struct mt7999_dev *dev, u32 mask);

int mt7999_set_coding_type(struct ieee80211_hw *hw, u8 coding_type, u8 link_id);
int mt7999_mcu_set_muru_qos_cfg(struct mt7999_dev *dev, u16 wlan_idx, u8 dir,
				u8 scs_id, u8 req_type, u8 *qos_ie, u8 qos_ie_len);
void mt7999_tm_update_channel(struct mt7999_phy *phy);
void mt7999_mcu_rx_bf_event(struct mt7999_dev *dev, struct sk_buff *skb);

void mt7999_mtk_init_dev_debugfs(struct mt7999_dev *dev, struct dentry *dir);
void mt7999_mtk_init_band_debugfs(struct mt7999_phy *phy, struct dentry *dir);
int mt7999_mcu_muru_dbg_info(struct mt7999_dev *dev, u16 item, u8 val);
int mt7999_mcu_set_sr_enable(struct mt7999_phy *phy, u8 action, u64 val, bool set);
void mt7999_mcu_rx_sr_event(struct mt7999_dev *dev, struct sk_buff *skb);
int mt7999_mcu_bf_pfmu_tag_read(struct mt7999_phy *phy, u8 pfmu_id, bool bfer);
int mt7999_mcu_bf_starec_read(struct mt7999_phy *phy, u16 wlan_idx);
int mt7999_mcu_bf_fbk_rpt_read(struct mt7999_phy *phy, u8 action);
int mt7999_mcu_set_muru_fixed_rate_enable(struct mt7999_dev *dev, u8 action, int val);
int mt7999_mcu_set_muru_fixed_rate_parameter(struct mt7999_dev *dev, u8 action, void *para);
int mt7999_mcu_set_txbf_snd_info(struct mt7999_dev *dev, void *para);
int mt7999_mcu_set_muru_cmd(struct mt7999_dev *dev, u16 action, int val);
int mt7999_mcu_muru_set_prot_frame_thr(struct mt7999_dev *dev, u32 val);
int mt7999_mcu_set_bypass_smthint(struct mt7999_dev *dev, u8 band_idx, u8 val);
int mt7999_mcu_set_rfeature_trig_type(struct mt7999_dev *dev, u8 band_idx,
				      u8 enable, u8 trig_type);
void mt7999_mcu_set_ppdu_tx_type(struct mt7999_dev *dev, u8 ppdu_type);
void mt7999_mcu_set_nusers_ofdma(struct mt7999_dev *dev, u8 band_idx, u8 ofdma_user_cnt);
void mt7999_mcu_set_cert(struct mt7999_dev *dev);

int mt7999_mcu_set_vow_drr_dbg(struct mt7999_dev *dev, u32 val);
int mt7999_mcu_thermal_debug(struct mt7999_phy *phy, u8 mode, u8 action);

#define PKT_BIN_DEBUG_MAGIC	0xc8763123
enum {
	PKT_BIN_DEBUG_FIRST = 4,
	PKT_BIN_DEBUG_MCU = PKT_BIN_DEBUG_FIRST,
	PKT_BIN_DEBUG_TXD,
	PKT_BIN_DEBUG_TX,
	PKT_BIN_DEBUG_RX,
	PKT_BIN_DEBUG_RX_RAW,
	PKT_BIN_DEBUG_MCU_EVENT,

	__PKT_BIN_DEBUG_MAX
};

int mt7999_mcu_mlo_agc(struct mt7999_dev *dev, const void *data, int len);
int mt7999_mcu_set_sr_pp_en(struct mt7999_dev *dev, u8 enable);
int mt7999_mcu_set_mru_probe_en(struct mt7999_phy *phy);

int mt7999_mcu_set_amsdu_algo(struct mt7999_dev *dev, u16 wlan_idx, u8 algo_en);
int mt7999_mcu_set_amsdu_para(struct mt7999_dev *dev, u16 wlan_idx,
			      u8 amsdu_en, u8 num, u16 length);

#ifdef CONFIG_MTK_DEBUG
int mt7999_mcu_sta_get_wtbl(struct mt7999_dev *dev, u16 idx,
			    enum mt7999_wtbl_type type,
			    u16 start_dw, u16 len_in_dw, void *buf);
int mt7999_mcu_get_sta_pause(struct mt7999_dev *dev, u8 band_idx,
			     u32 *sta_pause, u32 *twt_pause,
			     size_t cr_num_of_all_ac, size_t cr_num_of_twt);
int mt7999_mcu_regval(struct mt7999_dev *dev,
		      u32 *regidx, u32 *regval, u16 num, bool set);
void mt7999_packet_log_to_host(struct mt7999_dev *dev, const void *data, int len, int type, int des_len);
int mt7999_mcu_set_rdd_log_config(struct mt7999_dev *dev, u8 val);
void mt7999_mcu_dump_radar_pulse(struct mt7999_phy *phy, void *rdd_report);
#else
static inline void
mt7999_packet_log_to_host(struct mt7999_dev *dev, const void *data, int len, int type, int des_len)
{
}
#endif

#ifdef CONFIG_MULDF_HELPER
int mt7999_muldf_init(struct mt7999_dev *dev);
#else
static inline int mt7999_muldf_init(struct mt7999_dev *dev)
{
	return 0;
}
#endif

#endif
