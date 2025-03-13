// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2022 MediaTek Inc.
 */

#include "mt7996.h"
#include "mac.h"
#include "mcu.h"
#include "testmode.h"
#include "eeprom.h"
#include "mtk_mcu.h"

enum tm_changed {
	TM_CHANGED_TXPOWER,
	TM_CHANGED_FREQ_OFFSET,
	TM_CHANGED_SKU_EN,
	TM_CHANGED_TX_LENGTH,
	TM_CHANGED_TX_TIME,
	TM_CHANGED_CFG,
	TM_CHANGED_OFF_CHAN_CH,
	TM_CHANGED_OFF_CHAN_CENTER_CH,
	TM_CHANGED_OFF_CHAN_BW,
	TM_CHANGED_IPI_THRESHOLD,
	TM_CHANGED_IPI_PERIOD,
	TM_CHANGED_IPI_RESET,
	TM_CHANGED_TXBF_ACT,
	TM_CHANGED_TX_ANTENNA,
	TM_CHANGED_TX_RATE_NSS,
	TM_CHANGED_TX_RATE_IDX,

	/* must be last */
	NUM_TM_CHANGED
};

static const u8 tm_change_map[] = {
	[TM_CHANGED_TXPOWER] = MT76_TM_ATTR_TX_POWER,
	[TM_CHANGED_FREQ_OFFSET] = MT76_TM_ATTR_FREQ_OFFSET,
	[TM_CHANGED_SKU_EN] = MT76_TM_ATTR_SKU_EN,
	[TM_CHANGED_TX_LENGTH] = MT76_TM_ATTR_TX_LENGTH,
	[TM_CHANGED_TX_TIME] = MT76_TM_ATTR_TX_TIME,
	[TM_CHANGED_CFG] = MT76_TM_ATTR_CFG,
	[TM_CHANGED_OFF_CHAN_CH] = MT76_TM_ATTR_OFF_CH_SCAN_CH,
	[TM_CHANGED_OFF_CHAN_CENTER_CH] = MT76_TM_ATTR_OFF_CH_SCAN_CENTER_CH,
	[TM_CHANGED_OFF_CHAN_BW] = MT76_TM_ATTR_OFF_CH_SCAN_BW,
	[TM_CHANGED_IPI_THRESHOLD] = MT76_TM_ATTR_IPI_THRESHOLD,
	[TM_CHANGED_IPI_PERIOD] = MT76_TM_ATTR_IPI_PERIOD,
	[TM_CHANGED_IPI_RESET] = MT76_TM_ATTR_IPI_RESET,
	[TM_CHANGED_TXBF_ACT] = MT76_TM_ATTR_TXBF_ACT,
	[TM_CHANGED_TX_ANTENNA] = MT76_TM_ATTR_TX_ANTENNA,
	[TM_CHANGED_TX_RATE_NSS] = MT76_TM_ATTR_TX_RATE_NSS,
	[TM_CHANGED_TX_RATE_IDX] = MT76_TM_ATTR_TX_RATE_IDX,
};

static void mt7996_tm_ipi_work(struct work_struct *work);
static int mt7996_tm_txbf_apply_tx(struct mt7996_phy *phy, u16 wlan_idx,
				   bool ebf, bool ibf, bool phase_cal);

static u32 mt7996_tm_bw_mapping(enum nl80211_chan_width width, enum bw_mapping_method method)
{
	static const u32 width_to_bw[][NUM_BW_MAP] = {
		[NL80211_CHAN_WIDTH_40] = {FW_CDBW_40MHZ, TM_CBW_40MHZ, BF_CDBW_40MHZ, 40,
					   FIRST_CONTROL_CHAN_BITMAP_BW40},
		[NL80211_CHAN_WIDTH_80] = {FW_CDBW_80MHZ, TM_CBW_80MHZ, BF_CDBW_80MHZ, 80,
					   FIRST_CONTROL_CHAN_BITMAP_BW80},
		[NL80211_CHAN_WIDTH_80P80] = {FW_CDBW_8080MHZ, TM_CBW_8080MHZ, BF_CDBW_8080MHZ,
					      80, 0x0},
		[NL80211_CHAN_WIDTH_160] = {FW_CDBW_160MHZ, TM_CBW_160MHZ, BF_CDBW_160MHZ, 160,
					    FIRST_CONTROL_CHAN_BITMAP_BW160},
		[NL80211_CHAN_WIDTH_5] = {FW_CDBW_5MHZ, TM_CBW_5MHZ, BF_CDBW_5MHZ, 5, 0x0},
		[NL80211_CHAN_WIDTH_10] = {FW_CDBW_10MHZ, TM_CBW_10MHZ, BF_CDBW_10MHZ, 10, 0x0},
		[NL80211_CHAN_WIDTH_20] = {FW_CDBW_20MHZ, TM_CBW_20MHZ, BF_CDBW_20MHZ, 20, 0x0},
		[NL80211_CHAN_WIDTH_20_NOHT] = {FW_CDBW_20MHZ, TM_CBW_20MHZ, BF_CDBW_20MHZ,
						20, 0x0},
		[NL80211_CHAN_WIDTH_320] = {FW_CDBW_320MHZ, TM_CBW_320MHZ, BF_CDBW_320MHZ,
					    320, 0x0},
	};

	if (width >= ARRAY_SIZE(width_to_bw))
		return 0;

	return width_to_bw[width][method];
}

static u8 mt7996_tm_rate_mapping(u8 tx_rate_mode, enum rate_mapping_type type)
{
	static const u8 rate_to_phy[][NUM_RATE_MAP] = {
		[MT76_TM_TX_MODE_CCK] = {MT_PHY_TYPE_CCK, BF_LM_LEGACY},
		[MT76_TM_TX_MODE_OFDM] = {MT_PHY_TYPE_OFDM, BF_LM_LEGACY},
		[MT76_TM_TX_MODE_HT] = {MT_PHY_TYPE_HT, BF_LM_HT},
		[MT76_TM_TX_MODE_VHT] = {MT_PHY_TYPE_VHT, BF_LM_VHT},
		[MT76_TM_TX_MODE_HE_SU] = {MT_PHY_TYPE_HE_SU, BF_LM_HE},
		[MT76_TM_TX_MODE_HE_EXT_SU] = {MT_PHY_TYPE_HE_EXT_SU, BF_LM_HE},
		[MT76_TM_TX_MODE_HE_TB] = {MT_PHY_TYPE_HE_TB, BF_LM_HE},
		[MT76_TM_TX_MODE_HE_MU] = {MT_PHY_TYPE_HE_MU, BF_LM_HE},
		[MT76_TM_TX_MODE_EHT_SU] = {MT_PHY_TYPE_EHT_SU, BF_LM_EHT},
		[MT76_TM_TX_MODE_EHT_TRIG] = {MT_PHY_TYPE_EHT_TRIG, BF_LM_EHT},
		[MT76_TM_TX_MODE_EHT_MU] = {MT_PHY_TYPE_EHT_MU, BF_LM_EHT},
	};

	if (tx_rate_mode > MT76_TM_TX_MODE_MAX)
		return 0;

	return rate_to_phy[tx_rate_mode][type];
}

static u8 mt7996_tm_band_mapping(enum nl80211_band band)
{
	static const u8 ch_band[] = {
		[NL80211_BAND_2GHZ] = 0,
		[NL80211_BAND_5GHZ] = 1,
		[NL80211_BAND_6GHZ] = 2,
	};

	if (band >= NUM_NL80211_BANDS)
		return 0;

	return ch_band[band];
}

static int
mt7996_tm_check_antenna(struct mt7996_phy *phy)
{
	struct mt76_testmode_data *td = &phy->mt76->test;
	struct mt7996_dev *dev = phy->dev;
	u8 band_idx = phy->mt76->band_idx;
	u32 chainmask = phy->mt76->chainmask;
	u32 aux_rx_mask;

	chainmask = chainmask >> dev->chainshift[band_idx];
	aux_rx_mask = BIT(fls(chainmask)) * phy->has_aux_rx;
	if (td->tx_antenna_mask & ~(chainmask | aux_rx_mask)) {
		mt76_err(&dev->mt76,
			 "%s: antenna mask 0x%x exceeds limit (chainmask 0x%x, %s auxiliary RX)\n",
			 __func__, td->tx_antenna_mask, chainmask,
			 phy->has_aux_rx ? "has" : "no");
		return -EINVAL;
	}

	return 0;
}

static int
mt7996_tm_set(struct mt7996_dev *dev, u32 func_idx, u32 data)
{
	struct mt7996_tm_req req = {
		.rf_test = {
			.tag = cpu_to_le16(UNI_RF_TEST_CTRL),
			.len = cpu_to_le16(sizeof(req.rf_test)),
			.action = RF_ACTION_SET,
			.op.rf.func_idx = func_idx,
			.op.rf.param.func_data = cpu_to_le32(data),
		},
	};
	bool wait = (data == RF_CMD(START_TX)) ? true : false;

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(TESTMODE_CTRL), &req,
				 sizeof(req), wait);
}

static int
mt7996_tm_get(struct mt7996_dev *dev, u32 func_idx, u32 data, u32 *result)
{
	struct mt7996_tm_req req = {
		.rf_test = {
			.tag = cpu_to_le16(UNI_RF_TEST_CTRL),
			.len = cpu_to_le16(sizeof(req.rf_test)),
			.action = RF_ACTION_GET,
			.op.rf.func_idx = func_idx,
			.op.rf.param.func_data = cpu_to_le32(data),
		},
	};
	struct mt7996_tm_event *event;
	struct sk_buff *skb;
	int ret;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_WM_UNI_CMD_QUERY(TESTMODE_CTRL),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	event = (struct mt7996_tm_event *)skb->data;
	*result = event->result.payload_length;

	dev_kfree_skb(skb);

	return ret;
}

static void
mt7996_tm_set_antenna(struct mt7996_phy *phy, u32 func_idx)
{
#define SPE_INDEX_MASK		BIT(31)
#define TX_ANTENNA_MASK		GENMASK(4, 0)
#define RX_ANTENNA_MASK		GENMASK(20, 16)
	struct mt7996_dev *dev = phy->dev;
	struct mt76_testmode_data *td = &phy->mt76->test;
	u32 antenna_mask;

	if (!mt76_testmode_param_present(td, MT76_TM_ATTR_TX_ANTENNA))
		return;

	if (func_idx == SET_ID(TX_PATH))
		antenna_mask = td->tx_spe_idx ? (SPE_INDEX_MASK | td->tx_spe_idx) :
						td->tx_antenna_mask & TX_ANTENNA_MASK;
	else if (func_idx == SET_ID(RX_PATH))
		antenna_mask = u32_encode_bits(td->tx_antenna_mask, RX_ANTENNA_MASK);
	else
		return;

	mt7996_tm_set(dev, func_idx, antenna_mask);
}

static void
mt7996_tm_set_mac_addr(struct mt7996_dev *dev, u8 *addr, u32 func_idx)
{
#define REMAIN_PART_TAG		BIT(18)
	u32 own_mac_first = 0, own_mac_remain = 0;
	int len = sizeof(u32);

	memcpy(&own_mac_first, addr, len);
	mt7996_tm_set(dev, func_idx, own_mac_first);
	/* Set the remain part of mac address */
	memcpy(&own_mac_remain, addr + len, ETH_ALEN - len);
	mt7996_tm_set(dev, func_idx | REMAIN_PART_TAG, own_mac_remain);
}

static int
mt7996_tm_rf_switch_mode(struct mt7996_dev *dev, u32 op_mode)
{
	struct mt7996_tm_req req = {
		.rf_test = {
			.tag = cpu_to_le16(UNI_RF_TEST_CTRL),
			.len = cpu_to_le16(sizeof(req.rf_test)),
			.action = RF_ACTION_SWITCH_TO_RF_TEST,
			.op.op_mode = cpu_to_le32(op_mode),
		},
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(TESTMODE_CTRL), &req,
				 sizeof(req), false);
}

static void
mt7996_tm_init(struct mt7996_phy *phy, bool en)
{
	struct ieee80211_vif *vif = phy->mt76->monitor_vif;
	struct mt7996_dev *dev = phy->dev;
	struct mt76_testmode_data *td = &phy->mt76->test;
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_vif_link *deflink = &mvif->deflink;
	u8 rf_test_mode;
	int state;

	if (!test_bit(MT76_STATE_RUNNING, &phy->mt76->state))
		return;

	if (en) {
		rf_test_mode = RF_OPER_RF_TEST;
		state = CONN_STATE_PORT_SECURE;
		/* use firmware counter for RX stats */
		td->flag |= MT_TM_FW_RX_COUNT;
		INIT_DELAYED_WORK(&phy->ipi_work, mt7996_tm_ipi_work);
	} else {
		rf_test_mode = RF_OPER_NORMAL;
		state = CONN_STATE_DISCONNECT;
		memset(td, 0, sizeof(*td));
		kfree(phy->mt76->lists);
		phy->mt76->lists = NULL;
	}

	mt7996_mcu_set_tx_power_ctrl(phy, POWER_CTRL(ATE_MODE), en);
	mt7996_mcu_set_tx_power_ctrl(phy, POWER_CTRL(SKU_POWER_LIMIT), !en);
	mt7996_mcu_set_tx_power_ctrl(phy, POWER_CTRL(BACKOFF_POWER_LIMIT), !en);

	mt7996_tm_rf_switch_mode(dev, rf_test_mode);

	mt7996_mcu_add_bss_info(phy, vif, &vif->bss_conf,
				&deflink->mt76, &deflink->msta_link, en);
	mt7996_mcu_add_sta(dev, vif, &vif->bss_conf, NULL, deflink,
			   &deflink->msta_link, state, false);

	if (en)
		mt7996_tm_set(dev, SET_ID(BAND_IDX), phy->mt76->band_idx);
}

void
mt7996_tm_update_channel(struct mt7996_phy *phy)
{
#define CHAN_FREQ_BW_80P80_TAG		(SET_ID(CHAN_FREQ) | BIT(16))
#define FAST_CAL_NONE			BIT(20)
#define FAST_CAL_RX			BIT(21)
#define FAST_CAL_POWER			BIT(22)
	struct mt7996_dev *dev = phy->dev;
	struct mt76_testmode_data *td = &phy->mt76->test;
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	struct ieee80211_channel *chan = chandef->chan;
	u8 dbw, width = chandef->width, pri_sel = 0;
	int width_mhz;

	if (!chan) {
		mt76_err(&dev->mt76, "%s: no channel found, update failed\n", __func__);
		return;
	}

	/* system bw */
	mt7996_tm_set(dev, SET_ID(CBW), mt7996_tm_bw_mapping(width, BW_MAP_NL_TO_FW));

	if (width == NL80211_CHAN_WIDTH_80P80) {
		width = NL80211_CHAN_WIDTH_160;
		mt7996_tm_set(dev, CHAN_FREQ_BW_80P80_TAG, chandef->center_freq2 * 1000);
	}

	width_mhz = mt7996_tm_bw_mapping(width, BW_MAP_NL_TO_MHZ);

	/* data (per-packet) bw */
	dbw = width;
	if (mt76_testmode_param_present(td, MT76_TM_ATTR_TX_PKT_BW)) {
		int pkt_bw_mhz = mt7996_tm_bw_mapping(td->tx_pkt_bw, BW_MAP_NL_TO_MHZ);

		if (pkt_bw_mhz > width_mhz) {
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "%s: per-packet bw cannot exceed system bw, use %d MHz instead\n",
				 __func__, width_mhz);
			td->tx_pkt_bw = width;
		}
		dbw = td->tx_pkt_bw;
	}
	mt7996_tm_set(dev, SET_ID(DBW), mt7996_tm_bw_mapping(dbw, BW_MAP_NL_TO_FW));

	/* control channel selection index */
	if (mt76_testmode_param_present(td, MT76_TM_ATTR_TX_PRI_SEL)) {
		if (td->tx_pri_sel > width_mhz / 20 - 1) {
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "%s: invalid primary channel selection index, use 0 instead\n",
				 __func__);
			td->tx_pri_sel = 0;
		}
		pri_sel = td->tx_pri_sel;
	}
	mt7996_tm_set(dev, SET_ID(PRIMARY_CH), pri_sel);
	mt7996_tm_set(dev, SET_ID(BAND), mt7996_tm_band_mapping(chan->band));

	if (mt76_testmode_param_present(td, MT76_TM_ATTR_FAST_CAL)) {
		switch (td->fast_cal) {
		case MT76_TM_FAST_CAL_TYPE_RX:
			mt7996_tm_set(dev, SET_ID(CAL_BITMAP), FAST_CAL_RX);
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "%s: apply RX fast cal (skip TX cal)\n", __func__);
			break;
		case MT76_TM_FAST_CAL_TYPE_POWER:
			mt7996_tm_set(dev, SET_ID(CAL_BITMAP), FAST_CAL_POWER);
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "%s: apply power fast cal (skip DPD cal)\n", __func__);
			break;
		case MT76_TM_FAST_CAL_TYPE_NONE:
		case MT76_TM_FAST_CAL_TYPE_TX:
		default:
			/* same as not setting any cal bitmap */
			mt7996_tm_set(dev, SET_ID(CAL_BITMAP), FAST_CAL_NONE);
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "%s: apply full cal\n", __func__);
			break;
		}
	}

	/* trigger switch channel calibration */
	mt7996_tm_set(dev, SET_ID(CHAN_FREQ), chandef->center_freq1 * 1000);
}

static void
mt7996_tm_tx_stop(struct mt76_phy *mphy)
{
	struct mt76_testmode_data *td = &mphy->test;
	struct mt7996_phy *phy = mphy->priv;
	struct mt7996_dev *dev = phy->dev;

	mt7996_tm_set(dev, SET_ID(COMMAND), RF_CMD(STOP_TEST));
	td->tx_pending = 0;
}

static void
mt7996_tm_set_tx_frames(struct mt7996_phy *phy, bool en)
{
#define FRAME_CONTROL		0x88
	struct mt76_testmode_data *td = &phy->mt76->test;
	struct mt7996_dev *dev = phy->dev;

	if (!en) {
		/* trigger firmware to stop TX */
		mt7996_tm_tx_stop(phy->mt76);
		return;
	}

	/* TODO: RU operation, replace mcs, nss, and ldpc */
	mt7996_tm_set(dev, SET_ID(MAC_HEADER), FRAME_CONTROL);
	mt7996_tm_set(dev, SET_ID(SEQ_CTRL), 0);
	mt7996_tm_set(dev, SET_ID(TX_COUNT), td->tx_count);
	mt7996_tm_set(dev, SET_ID(TX_MODE),
		      mt7996_tm_rate_mapping(td->tx_rate_mode, RATE_MODE_TO_PHY));
	mt7996_tm_set(dev, SET_ID(TX_RATE), td->tx_rate_idx);

	if (mt76_testmode_param_present(td, MT76_TM_ATTR_TX_POWER))
		mt7996_tm_set(dev, SET_ID(POWER), td->tx_power[0]);

	if (mt76_testmode_param_present(td, MT76_TM_ATTR_TX_TIME)) {
		mt7996_tm_set(dev, SET_ID(TX_LEN), 0);
		mt7996_tm_set(dev, SET_ID(TX_TIME), td->tx_time);
	} else {
		mt7996_tm_set(dev, SET_ID(TX_LEN), td->tx_mpdu_len);
		mt7996_tm_set(dev, SET_ID(TX_TIME), 0);
	}

	mt7996_tm_set_antenna(phy, SET_ID(TX_PATH));
	mt7996_tm_set_antenna(phy, SET_ID(RX_PATH));
	mt7996_tm_set(dev, SET_ID(STBC), td->tx_rate_stbc);
	mt7996_tm_set(dev, SET_ID(ENCODE_MODE), td->tx_rate_ldpc);
	mt7996_tm_set(dev, SET_ID(IBF_ENABLE), td->ibf);
	mt7996_tm_set(dev, SET_ID(EBF_ENABLE), td->ebf);
	mt7996_tm_set(dev, SET_ID(IPG), td->tx_ipg);
	mt7996_tm_set(dev, SET_ID(GI), td->tx_rate_sgi);
	mt7996_tm_set(dev, SET_ID(NSS), td->tx_rate_nss);
	mt7996_tm_set(dev, SET_ID(AID_OFFSET), 0);
	mt7996_tm_set(dev, SET_ID(PUNCTURE), td->tx_preamble_puncture);

	mt7996_tm_set(dev, SET_ID(MAX_PE), 2);
	mt7996_tm_set(dev, SET_ID(HW_TX_MODE), 0);
	if (!td->bf_en)
		mt7996_tm_update_channel(phy);

	/* trigger firmware to start TX */
	mt7996_tm_set(dev, SET_ID(COMMAND), RF_CMD(START_TX));
}

static int
mt7996_tm_rx_stats_user_ctrl(struct mt7996_phy *phy, u16 user_idx)
{
	struct mt7996_dev *dev = phy->dev;
	struct mt7996_tm_rx_req req = {
		.band = phy->mt76->band_idx,
		.user_ctrl = {
			.tag = cpu_to_le16(UNI_TM_RX_STAT_SET_USER_CTRL),
			.len = cpu_to_le16(sizeof(req.user_ctrl)),
			.band_idx = phy->mt76->band_idx,
			.user_idx = cpu_to_le16(user_idx),
		},
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(TESTMODE_RX_STAT), &req,
				 sizeof(req), false);
}

static void
mt7996_tm_set_rx_frames(struct mt7996_phy *phy, bool en)
{
#define RX_MU_DISABLE	0xf800
	struct mt76_testmode_data *td = &phy->mt76->test;
	struct mt7996_dev *dev = phy->dev;
	u8 own_mac[ETH_ALEN] = {0};
	int ret;

	if (!en) {
		/* trigger firmware to stop RX */
		mt7996_tm_set(dev, SET_ID(COMMAND), RF_CMD(STOP_TEST));
		return;
	}

	ret = mt7996_tm_rx_stats_user_ctrl(phy, td->aid);
	if (ret) {
		mt76_err(&dev->mt76, "%s: failed to set RX stats user control (%d)\n",
			 __func__, ret);
		return;
	}

	if (!td->bf_en)
		mt7996_tm_update_channel(phy);

	if (td->tx_rate_mode >= MT76_TM_TX_MODE_HE_MU) {
		if (td->aid)
			mt7996_tm_set(dev, SET_ID(RX_MU_AID), td->aid);
		else
			mt7996_tm_set(dev, SET_ID(RX_MU_AID), RX_MU_DISABLE);
	}
	mt7996_tm_set(dev, SET_ID(TX_MODE),
		      mt7996_tm_rate_mapping(td->tx_rate_mode, RATE_MODE_TO_PHY));
	mt7996_tm_set(dev, SET_ID(GI), td->tx_rate_sgi);
	mt7996_tm_set_antenna(phy, SET_ID(TX_PATH));
	mt7996_tm_set_antenna(phy, SET_ID(RX_PATH));
	mt7996_tm_set(dev, SET_ID(MAX_PE), 2);

	if (td->bf_en)
		memcpy(own_mac, td->addr[1], ETH_ALEN);
	mt7996_tm_set_mac_addr(dev, own_mac, SET_ID(SA));

	/* trigger firmware to start RX */
	mt7996_tm_set(dev, SET_ID(COMMAND), RF_CMD(START_RX));
}

static void
mt7996_tm_set_tx_cont(struct mt7996_phy *phy, bool en)
{
#define CONT_WAVE_MODE_OFDM	3
	struct mt76_testmode_data *td = &phy->mt76->test;
	struct mt7996_dev *dev = phy->dev;

	if (!en) {
		/* trigger firmware to stop CONT TX  */
		mt7996_tm_set(dev, SET_ID(COMMAND), RF_CMD(STOP_TEST));
		return;
	}

	mt7996_tm_update_channel(phy);
	mt7996_tm_set(dev, SET_ID(TX_MODE),
		      mt7996_tm_rate_mapping(td->tx_rate_mode, RATE_MODE_TO_PHY));
	mt7996_tm_set(dev, SET_ID(TX_RATE), td->tx_rate_idx);
	/* fix payload is OFDM */
	mt7996_tm_set(dev, SET_ID(CONT_WAVE_MODE), CONT_WAVE_MODE_OFDM);
	mt7996_tm_set(dev, SET_ID(ANT_MASK), td->tx_antenna_mask);

	/* trigger firmware to start CONT TX */
	mt7996_tm_set(dev, SET_ID(COMMAND), RF_CMD(CONT_WAVE));
}

static int
mt7996_tm_group_prek(struct mt7996_phy *phy, enum mt76_testmode_state state)
{
	struct mt7996_dev *dev = phy->dev;
	struct mt76_dev *mdev = &dev->mt76;
	struct mt7996_tm_req req = {
		.rf_test = {
			.tag = cpu_to_le16(UNI_RF_TEST_CTRL),
			.len = cpu_to_le16(sizeof(req.rf_test)),
			.action = RF_ACTION_IN_RF_TEST,
			.icap_len = RF_TEST_ICAP_LEN,
			.op.rf.func_idx = cpu_to_le32(RF_TEST_RE_CAL),
			.op.rf.param.cal_param.func_data = cpu_to_le32(RF_PRE_CAL),
			.op.rf.param.cal_param.band_idx = phy->mt76->band_idx,
		},
	};
	u32 i, group_size, dpd_size, size, offs, *pre_cal;
	u8 *eeprom, do_precal;
	int ret = 0;

	if (!dev->flash_mode) {
		mt76_err(&dev->mt76, "%s: currently not in FLASH or BIN FILE mode, return\n",
			 __func__);
		return -EOPNOTSUPP;
	}

	eeprom = mdev->eeprom.data;
	dev->cur_prek_offset = 0;
	group_size = MT_EE_CAL_GROUP_SIZE;
	dpd_size = MT_EE_CAL_DPD_SIZE;
	size = group_size + dpd_size;
	offs = MT_EE_DO_PRE_CAL;
	do_precal = (MT_EE_WIFI_CAL_GROUP_2G * !!PREK(GROUP_SIZE_2G)) |
		    (MT_EE_WIFI_CAL_GROUP_5G * !!PREK(GROUP_SIZE_5G)) |
		    (MT_EE_WIFI_CAL_GROUP_6G * !!PREK(GROUP_SIZE_6G));

	switch (state) {
	case MT76_TM_STATE_GROUP_PREK:
		if (!dev->cal) {
			dev->cal = devm_kzalloc(mdev->dev, size, GFP_KERNEL);
			if (!dev->cal)
				return -ENOMEM;
		}

		ret = mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(TESTMODE_CTRL), &req,
					sizeof(req), false);
		wait_event_timeout(mdev->mcu.wait, dev->cur_prek_offset == group_size,
				   30 * HZ);

		if (ret)
			mt76_err(&dev->mt76, "%s: failed to send mcu msg (%d)\n",
				 __func__, ret);
		else
			eeprom[offs] |= do_precal;
		break;
	case MT76_TM_STATE_GROUP_PREK_DUMP:
		pre_cal = (u32 *)dev->cal;
		if (!pre_cal) {
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "%s: no group pre-cal found\n", __func__);
			return ret;
		}
		mt76_dbg(&dev->mt76, MT76_DBG_TEST, "Group Pre-Cal:\n");
		for (i = 0; i < (group_size / sizeof(u32)); i += 4)
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "[0x%08lx] 0x%8x 0x%8x 0x%8x 0x%8x\n",
				 i * sizeof(u32), pre_cal[i], pre_cal[i + 1],
				 pre_cal[i + 2], pre_cal[i + 3]);
		break;
	case MT76_TM_STATE_GROUP_PREK_CLEAN:
		pre_cal = (u32 *)dev->cal;
		if (!pre_cal)
			return ret;
		memset(pre_cal, 0, group_size);
		eeprom[offs] &= ~MT_EE_WIFI_CAL_GROUP;
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int
mt7996_tm_dpd_prek_send_req(struct mt7996_phy *phy, struct mt7996_tm_req *req,
			    const struct ieee80211_channel *chan_list, u32 channel_size,
			    enum nl80211_chan_width width, u32 func_data)
{
	struct mt7996_dev *dev = phy->dev;
	struct mt76_phy *mphy = phy->mt76;
	struct cfg80211_chan_def chandef_backup, *chandef = &mphy->chandef;
	struct ieee80211_channel chan_backup;
	int i, ret, skip_ch_num = DPD_CH_NUM(BW20_5G_SKIP);

	if (!chan_list)
		return -EOPNOTSUPP;
	if (!channel_size)
		return 0;

	req->rf_test.op.rf.param.cal_param.func_data = cpu_to_le32(func_data);

	memcpy(&chan_backup, chandef->chan, sizeof(struct ieee80211_channel));
	memcpy(&chandef_backup, chandef, sizeof(struct cfg80211_chan_def));

	for (i = 0; i < channel_size; i++) {
		if (chan_list[i].band == NL80211_BAND_5GHZ &&
		    chan_list[i].hw_value >= dpd_5g_skip_ch_list[0].hw_value &&
		    chan_list[i].hw_value <= dpd_5g_skip_ch_list[skip_ch_num - 1].hw_value)
			continue;

		memcpy(chandef->chan, &chan_list[i], sizeof(struct ieee80211_channel));
		chandef->width = width;

		/* set channel switch reason */
		mphy->hw->conf.flags |= IEEE80211_CONF_OFFCHANNEL;
		mt7996_mcu_set_chan_info(phy, UNI_CHANNEL_SWITCH, false);

		ret = mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(TESTMODE_CTRL), req,
					sizeof(*req), false);
		if (ret) {
			mt76_err(&dev->mt76, "%s: failed to send mcu msg (%d)\n",
				 __func__, ret);
			goto out;
		}
	}

out:
	mphy->hw->conf.flags &= ~IEEE80211_CONF_OFFCHANNEL;
	memcpy(chandef, &chandef_backup, sizeof(struct cfg80211_chan_def));
	memcpy(chandef->chan, &chan_backup, sizeof(struct ieee80211_channel));
	mt7996_mcu_set_chan_info(phy, UNI_CHANNEL_SWITCH, false);

	return ret;
}

static int
mt7996_tm_dpd_prek(struct mt7996_phy *phy, enum mt76_testmode_state state)
{
	struct mt7996_dev *dev = phy->dev;
	struct mt76_dev *mdev = &dev->mt76;
	struct mt76_phy *mphy = phy->mt76;
	struct mt7996_tm_req req = {
		.rf_test = {
			.tag = cpu_to_le16(UNI_RF_TEST_CTRL),
			.len = cpu_to_le16(sizeof(req.rf_test)),
			.action = RF_ACTION_IN_RF_TEST,
			.icap_len = RF_TEST_ICAP_LEN,
			.op.rf.func_idx = cpu_to_le32(RF_TEST_RE_CAL),
			.op.rf.param.cal_param.band_idx = mphy->band_idx,
		},
	};
	u32 i, j, group_size, dpd_size, size, offs, *pre_cal;
	u32 func_data, wait_on_prek_offset = 0;
	u8 do_precal, *eeprom;
	int ret = 0;

	if (!dev->flash_mode) {
		mt76_err(&dev->mt76, "%s: currently not in FLASH or BIN FILE mode, return\n",
			 __func__);
		return -EOPNOTSUPP;
	}

	eeprom = mdev->eeprom.data;
	dev->cur_prek_offset = 0;
	group_size = MT_EE_CAL_GROUP_SIZE;
	dpd_size = MT_EE_CAL_DPD_SIZE;
	size = group_size + dpd_size;
	offs = MT_EE_DO_PRE_CAL;

	if (!dev->cal && state < MT76_TM_STATE_DPD_DUMP) {
		dev->cal = devm_kzalloc(mdev->dev, size, GFP_KERNEL);
		if (!dev->cal)
			return -ENOMEM;
	}

	switch (state) {
	case MT76_TM_STATE_DPD_2G:
		ret = mt7996_tm_dpd_prek_send_req(phy, &req, dpd_2g_ch_list_bw20,
						  DPD_CH_NUM(BW20_2G),
						  NL80211_CHAN_WIDTH_20, RF_DPD_FLAT_CAL);
		wait_on_prek_offset += DPD_CH_NUM(BW20_2G) * DPD_PER_CH_BW20_SIZE;
		wait_event_timeout(mdev->mcu.wait, dev->cur_prek_offset == wait_on_prek_offset,
				   30 * HZ);

		do_precal = MT_EE_WIFI_CAL_DPD_2G;
		break;
	case MT76_TM_STATE_DPD_5G:
		/* 5g channel bw20 calibration */
		ret = mt7996_tm_dpd_prek_send_req(phy, &req, mphy->sband_5g.sband.channels,
						  mphy->sband_5g.sband.n_channels,
						  NL80211_CHAN_WIDTH_20, RF_DPD_FLAT_5G_CAL);
		if (ret)
			return ret;
		wait_on_prek_offset += DPD_CH_NUM(BW20_5G) * DPD_PER_CH_BW20_SIZE;
		wait_event_timeout(mdev->mcu.wait, dev->cur_prek_offset == wait_on_prek_offset,
				   30 * HZ);

		/* griffin does not support mem dpd cal */
		func_data = PREK(DPD_MEM_SIZE) ? RF_DPD_FLAT_5G_MEM_CAL : RF_DPD_FLAT_5G_CAL;

		/* 5g channel bw80 calibration */
		ret = mt7996_tm_dpd_prek_send_req(phy, &req, dpd_5g_ch_list_bw80,
						  DPD_CH_NUM(BW80_5G),
						  NL80211_CHAN_WIDTH_80, func_data);
		if (ret)
			return ret;
		wait_on_prek_offset += DPD_CH_NUM(BW80_5G) * DPD_PER_CH_GT_BW20_SIZE;
		wait_event_timeout(mdev->mcu.wait, dev->cur_prek_offset == wait_on_prek_offset,
				   30 * HZ);

		/* 5g channel bw160 calibration */
		ret = mt7996_tm_dpd_prek_send_req(phy, &req, dpd_5g_ch_list_bw160,
						  DPD_CH_NUM(BW160_5G),
						  NL80211_CHAN_WIDTH_160, func_data);
		wait_on_prek_offset += DPD_CH_NUM(BW160_5G) * DPD_PER_CH_GT_BW20_SIZE;
		wait_event_timeout(mdev->mcu.wait, dev->cur_prek_offset == wait_on_prek_offset,
				   30 * HZ);

		do_precal = MT_EE_WIFI_CAL_DPD_5G;
		break;
	case MT76_TM_STATE_DPD_6G:
		/* 6g channel bw20 calibration */
		ret = mt7996_tm_dpd_prek_send_req(phy, &req, mphy->sband_6g.sband.channels,
						  mphy->sband_6g.sband.n_channels,
						  NL80211_CHAN_WIDTH_20, RF_DPD_FLAT_6G_CAL);
		if (ret)
			return ret;
		wait_on_prek_offset += DPD_CH_NUM(BW20_6G) * DPD_PER_CH_BW20_SIZE;
		wait_event_timeout(mdev->mcu.wait, dev->cur_prek_offset == wait_on_prek_offset,
				   30 * HZ);

		/* griffin does not support mem dpd cal */
		func_data = PREK(DPD_MEM_SIZE) ? RF_DPD_FLAT_6G_MEM_CAL : RF_DPD_FLAT_6G_CAL;

		/* 6g channel bw80 calibration */
		ret = mt7996_tm_dpd_prek_send_req(phy, &req, dpd_6g_ch_list_bw80,
						  DPD_CH_NUM(BW80_6G),
						  NL80211_CHAN_WIDTH_80, func_data);
		if (ret)
			return ret;
		wait_on_prek_offset += DPD_CH_NUM(BW80_6G) * DPD_PER_CH_GT_BW20_SIZE;
		wait_event_timeout(mdev->mcu.wait, dev->cur_prek_offset == wait_on_prek_offset,
				   30 * HZ);

		/* 6g channel bw160 calibration */
		ret = mt7996_tm_dpd_prek_send_req(phy, &req, dpd_6g_ch_list_bw160,
						  DPD_CH_NUM(BW160_6G),
						  NL80211_CHAN_WIDTH_160, func_data);
		if (ret)
			return ret;
		wait_on_prek_offset += DPD_CH_NUM(BW160_6G) * DPD_PER_CH_GT_BW20_SIZE;
		wait_event_timeout(mdev->mcu.wait, dev->cur_prek_offset == wait_on_prek_offset,
				   30 * HZ);

		/* 6g channel bw320 calibration */
		ret = mt7996_tm_dpd_prek_send_req(phy, &req, dpd_6g_ch_list_bw320,
						  DPD_CH_NUM(BW320_6G),
						  NL80211_CHAN_WIDTH_320, func_data);
		wait_on_prek_offset += DPD_CH_NUM(BW320_6G) * DPD_PER_CH_GT_BW20_SIZE;
		wait_event_timeout(mdev->mcu.wait, dev->cur_prek_offset == wait_on_prek_offset,
				   30 * HZ);

		do_precal = MT_EE_WIFI_CAL_DPD_6G;
		break;
	case MT76_TM_STATE_DPD_DUMP:
		if (!dev->cal) {
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "%s: no dpd pre-cal found\n", __func__);
			return ret;
		}
		pre_cal = (u32 *)dev->cal;
		mt76_dbg(&dev->mt76, MT76_DBG_TEST, "DPD Pre-Cal:\n");
		for (i = 0; i < dpd_size / sizeof(u32); i += 4) {
			j = i + (group_size / sizeof(u32));
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "[0x%08lx] 0x%8x 0x%8x 0x%8x 0x%8x\n",
				 j * sizeof(u32), pre_cal[j], pre_cal[j + 1],
				 pre_cal[j + 2], pre_cal[j + 3]);
		}
		return 0;
	case MT76_TM_STATE_DPD_CLEAN:
		pre_cal = (u32 *)dev->cal;
		if (!pre_cal)
			return ret;
		memset(pre_cal + (group_size / sizeof(u32)), 0, dpd_size);
		do_precal = MT_EE_WIFI_CAL_DPD;
		eeprom[offs] &= ~do_precal;
		return 0;
	default:
		return -EINVAL;
	}

	if (!ret)
		eeprom[offs] |= do_precal;

	return ret;
}

static int
mt7996_tm_dump_precal(struct mt76_phy *mphy, struct sk_buff *msg, int flag, int type)
{
#define DPD_PER_CHAN_SIZE_MASK		GENMASK(31, 30)
#define DPD_2G_RATIO_MASK		GENMASK(29, 20)
#define DPD_5G_RATIO_MASK		GENMASK(19, 10)
#define DPD_6G_RATIO_MASK		GENMASK(9, 0)
	struct mt7996_phy *phy = mphy->priv;
	struct mt7996_dev *dev = phy->dev;
	u32 i, group_size, dpd_size, total_size, size, dpd_info = 0;
	u32 dpd_size_2g, dpd_size_5g, dpd_size_6g;
	u32 base, offs, transmit_size = 1000;
	u8 *pre_cal, *eeprom;
	void *precal;
	enum prek_ops {
		PREK_GET_INFO,
		PREK_SYNC_ALL,
		PREK_SYNC_GROUP,
		PREK_SYNC_DPD_2G,
		PREK_SYNC_DPD_5G,
		PREK_SYNC_DPD_6G,
		PREK_CLEAN_GROUP,
		PREK_CLEAN_DPD,
	};

	if (!dev->cal) {
		mt76_err(&dev->mt76, "%s: no pre-cal found\n", __func__);
		return 0;
	}

	group_size = MT_EE_CAL_GROUP_SIZE;
	dpd_size = MT_EE_CAL_DPD_SIZE;
	total_size = group_size + dpd_size;
	pre_cal = dev->cal;
	eeprom = dev->mt76.eeprom.data;
	offs = MT_EE_DO_PRE_CAL;

	dpd_size_2g = MT_EE_CAL_DPD_SIZE_2G;
	dpd_size_5g = MT_EE_CAL_DPD_SIZE_5G;
	dpd_size_6g = MT_EE_CAL_DPD_SIZE_6G;

	switch (type) {
	case PREK_SYNC_ALL:
		base = 0;
		size = total_size;
		break;
	case PREK_SYNC_GROUP:
		base = 0;
		size = group_size;
		break;
	case PREK_SYNC_DPD_2G:
		base = group_size;
		size = dpd_size_2g;
		break;
	case PREK_SYNC_DPD_5G:
		base = group_size + dpd_size_2g;
		size = dpd_size_5g;
		break;
	case PREK_SYNC_DPD_6G:
		base = group_size + dpd_size_2g + dpd_size_5g;
		size = dpd_size_6g;
		break;
	case PREK_GET_INFO:
		break;
	default:
		return 0;
	}

	if (!flag) {
		if (eeprom[offs] & MT_EE_WIFI_CAL_DPD) {
			dpd_info |= u32_encode_bits(1, DPD_PER_CHAN_SIZE_MASK) |
				    u32_encode_bits(dpd_size_2g / MT_EE_CAL_UNIT,
						    DPD_2G_RATIO_MASK) |
				    u32_encode_bits(dpd_size_5g / MT_EE_CAL_UNIT,
						    DPD_5G_RATIO_MASK) |
				    u32_encode_bits(dpd_size_6g / MT_EE_CAL_UNIT,
						    DPD_6G_RATIO_MASK);
		}
		dev->cur_prek_offset = 0;
		precal = nla_nest_start(msg, MT76_TM_ATTR_PRECAL_INFO);
		if (!precal)
			return -ENOMEM;
		nla_put_u32(msg, 0, group_size);
		nla_put_u32(msg, 1, dpd_size);
		nla_put_u32(msg, 2, dpd_info);
		nla_put_u32(msg, 3, transmit_size);
		nla_put_u32(msg, 4, eeprom[offs]);
		nla_nest_end(msg, precal);
	} else {
		precal = nla_nest_start(msg, MT76_TM_ATTR_PRECAL);
		if (!precal)
			return -ENOMEM;

		transmit_size = (dev->cur_prek_offset + transmit_size < size) ?
				transmit_size : (size - dev->cur_prek_offset);
		for (i = 0; i < transmit_size; i++) {
			if (nla_put_u8(msg, i, pre_cal[base + dev->cur_prek_offset + i]))
				return -ENOMEM;
		}
		dev->cur_prek_offset += transmit_size;

		nla_nest_end(msg, precal);
	}

	return 0;
}

static int
mt7996_tm_rx_gain_cal(struct mt7996_phy *phy, enum mt76_testmode_state state)
{
	struct mt7996_dev *dev = phy->dev;
	struct mt76_phy *mphy = phy->mt76;
	struct mt7996_tm_req req = {
		.rf_test = {
			.tag = cpu_to_le16(UNI_RF_TEST_CTRL),
			.len = cpu_to_le16(sizeof(req.rf_test)),
			.action = RF_ACTION_IN_RF_TEST,
			.icap_len = RF_TEST_ICAP_LEN,
			.op.rf.func_idx = cpu_to_le32(RF_TEST_RX_GAIN_CAL),
			.op.rf.param.cal_param.func_data = cpu_to_le32(RF_RX_GAIN_CAL),
			.op.rf.param.cal_param.band_idx = mphy->band_idx,
		},
	};
	u8 *eeprom = dev->mt76.eeprom.data;
	u32 i, j, size, *cal;
	int ret = 0;

	if (!dev->flash_mode) {
		mt76_err(&dev->mt76, "%s: currently not in FLASH or BIN FILE mode, return\n",
			 __func__);
		return -EOPNOTSUPP;
	}

	dev->cur_prek_offset = 0;
	size = MT_EE_CAL_RX_GAIN_SIZE;

	switch (state) {
	case MT76_TM_STATE_RX_GAIN_CAL:
		ret = mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(TESTMODE_CTRL), &req,
					sizeof(req), false);
		if (ret) {
			mt76_err(&dev->mt76, "%s: failed to send mcu msg (%d)\n",
				 __func__, ret);
			return ret;
		}

		wait_event_timeout(dev->mt76.mcu.wait, dev->cur_prek_offset == size, 30 * HZ);
		break;
	case MT76_TM_STATE_RX_GAIN_CAL_DUMP:
		cal = (u32 *)eeprom;
		mt76_dbg(&dev->mt76, MT76_DBG_TEST, "RX Gain Cal:\n");
		for (i = 0; i < (size / sizeof(u32)); i += 4) {
			j = MT_EE_RX_GAIN_CAL / sizeof(u32) + i;
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "[0x%08lx] 0x%8x 0x%8x 0x%8x 0x%8x\n",
				 j * sizeof(u32), cal[j], cal[j + 1],
				 cal[j + 2], cal[j + 3]);
		}
		break;
	case MT76_TM_STATE_RX_GAIN_CAL_CLEAN:
		memset(eeprom + MT_EE_RX_GAIN_CAL, 0, size);
		eeprom[MT_EE_DO_RX_GAIN_CAL] &= ~u8_encode_bits(GENMASK(2, 0),
								MT_EE_WIFI_CAL_RX_GAIN);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void
mt7996_tm_re_cal_event(struct mt7996_dev *dev, struct mt7996_tm_rf_test_result *result,
		       struct mt7996_tm_rf_test_data *data)
{
	u32 base, cal_idx, cal_type, len = 0;
	u8 *cal = dev->cal;

	cal_idx = le32_to_cpu(data->cal_idx);
	cal_type = le32_to_cpu(data->cal_type);
	len = le32_to_cpu(result->payload_length);
	len = len - sizeof(struct mt7996_tm_rf_test_data);

	switch (cal_type) {
	case RF_RX_GAIN_CAL:
		cal = dev->mt76.eeprom.data;
		base = MT_EE_RX_GAIN_CAL;
		break;
	case RF_PRE_CAL:
		base = 0;
		break;
	case RF_DPD_FLAT_CAL:
		base = MT_EE_CAL_GROUP_SIZE;
		break;
	case RF_DPD_FLAT_5G_CAL:
	case RF_DPD_FLAT_5G_MEM_CAL:
		base = MT_EE_CAL_GROUP_SIZE + MT_EE_CAL_DPD_SIZE_2G;
		break;
	case RF_DPD_FLAT_6G_CAL:
	case RF_DPD_FLAT_6G_MEM_CAL:
		base = MT_EE_CAL_GROUP_SIZE + MT_EE_CAL_DPD_SIZE_2G +
		       MT_EE_CAL_DPD_SIZE_5G;
		break;
	default:
		mt76_err(&dev->mt76, "%s: unknown calibration type %x\n",
			 __func__, cal_type);
		return;
	}

	memcpy(cal + base + dev->cur_prek_offset, data->cal_data, len);
	dev->cur_prek_offset += len;
	wake_up(&dev->mt76.mcu.wait);
}

void mt7996_tm_rf_test_event(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct mt7996_tm_event *event;
	struct mt7996_tm_rf_test_result *result;
	struct mt7996_tm_rf_test_data *data;
	static u32 event_type;

	skb_pull(skb, sizeof(struct mt7996_mcu_rxd));
	event = (struct mt7996_tm_event *)skb->data;
	result = (struct mt7996_tm_rf_test_result *)&event->result;
	data = (struct mt7996_tm_rf_test_data *)result->data;

	event_type = le32_to_cpu(result->func_idx);

	switch (event_type) {
	case RF_TEST_RE_CAL:
		mt7996_tm_re_cal_event(dev, result, data);
		break;
	default:
		break;
	}
}

static u8
mt7996_tm_get_center_chan(struct mt7996_phy *phy, struct cfg80211_chan_def *chandef)
{
	struct mt76_phy *mphy = phy->mt76;
	const struct ieee80211_channel *chan = mphy->sband_5g.sband.channels;
	u32 bitmap, i, offset, width_mhz, size = mphy->sband_5g.sband.n_channels;
	u16 first_control = 0, control_chan = chandef->chan->hw_value;
	bool not_first;

	bitmap = mt7996_tm_bw_mapping(chandef->width, BW_MAP_NL_TO_CONTROL_BITMAP_5G);
	if (!bitmap)
		return control_chan;

	width_mhz = mt7996_tm_bw_mapping(chandef->width, BW_MAP_NL_TO_MHZ);
	offset = width_mhz / 10 - 2;

	for (i = 0; i < size; i++) {
		not_first = (chandef->width != NL80211_CHAN_WIDTH_160) ?
			    (i % bitmap) : (i >= 32) || !((1 << i) & bitmap);
		if (not_first)
			continue;

		if (control_chan >= chan[i].hw_value)
			first_control = chan[i].hw_value;
		else
			break;
	}

	if (first_control == 0)
		return control_chan;

	return first_control + offset;
}

static int
mt7996_tm_set_offchan(struct mt7996_phy *phy, bool no_center)
{
	struct mt76_phy *mphy = phy->mt76;
	struct mt7996_dev *dev = phy->dev;
	struct ieee80211_hw *hw = mphy->hw;
	struct mt76_testmode_data *td = &phy->mt76->test;
	struct cfg80211_chan_def chandef = {};
	struct ieee80211_channel *chan;
	int ret, freq = ieee80211_channel_to_frequency(td->offchan_ch, NL80211_BAND_5GHZ);

	if (!mphy->cap.has_5ghz || !freq) {
		ret = -EINVAL;
		mt76_err(&dev->mt76, "%s: failed to set offchan (invalid band or channel)\n",
			 __func__);
		goto out;
	}

	chandef.width = td->offchan_bw;
	chan = ieee80211_get_channel(hw->wiphy, freq);
	chandef.chan = chan;
	if (no_center)
		td->offchan_center_ch = mt7996_tm_get_center_chan(phy, &chandef);
	chandef.center_freq1 = ieee80211_channel_to_frequency(td->offchan_center_ch,
							      NL80211_BAND_5GHZ);
	if (!cfg80211_chandef_valid(&chandef)) {
		ret = -EINVAL;
		mt76_err(&dev->mt76, "%s: failed to set offchan (invalid chandef)\n",
			 __func__);
		goto out;
	}

	memset(&dev->rdd2_chandef, 0, sizeof(struct cfg80211_chan_def));

	ret = mt7996_mcu_rdd_background_enable(phy, &chandef);

	if (ret)
		goto out;

	dev->rdd2_phy = phy;
	dev->rdd2_chandef = chandef;

	return 0;

out:
	td->offchan_ch = 0;
	td->offchan_center_ch = 0;
	td->offchan_bw = 0;

	return ret;
}

static void
mt7996_tm_ipi_hist_ctrl(struct mt7996_phy *phy, struct mt7996_tm_rdd_ipi_ctrl *data, u8 cmd)
{
#define MT_IPI_RESET		0x830a5dfc
#define MT_IPI_RESET_MASK	BIT(28)
#define MT_IPI_COUNTER_BASE	0x83041000
#define MT_IPI_COUNTER(idx)	(MT_IPI_COUNTER_BASE + ((idx) * 4))
	struct mt7996_dev *dev = phy->dev;
	bool val;
	int i;

	if (cmd == RDD_SET_IPI_HIST_RESET) {
		val = mt76_rr(dev, MT_IPI_RESET) & MT_IPI_RESET_MASK;
		mt76_rmw_field(dev, MT_IPI_RESET, MT_IPI_RESET_MASK, !val);
		return;
	}

	for (i = 0; i < POWER_INDICATE_HIST_MAX; i++)
		data->ipi_hist_val[i] = mt76_rr(dev, MT_IPI_COUNTER(i));
}

static void
mt7996_tm_ipi_work(struct work_struct *work)
{
#define PRECISION	100
	struct mt7996_phy *phy = container_of(work, struct mt7996_phy, ipi_work.work);
	struct mt7996_dev *dev = phy->dev;
	struct mt76_testmode_data *td = &phy->mt76->test;
	struct mt7996_tm_rdd_ipi_ctrl data;
	u32 ipi_idx, ipi_free_count, ipi_percentage;
	u32 ipi_hist_count_th = 0, ipi_hist_total_count = 0;
	u32 self_idle_ratio, ipi_idle_ratio, channel_load;
	u32 *ipi_hist_data;
	const char *power_lower_bound, *power_upper_bound;
	static const char * const ipi_idx_to_power_bound[] = {
		[RDD_IPI_HIST_0] = "-92",
		[RDD_IPI_HIST_1] = "-89",
		[RDD_IPI_HIST_2] = "-86",
		[RDD_IPI_HIST_3] = "-83",
		[RDD_IPI_HIST_4] = "-80",
		[RDD_IPI_HIST_5] = "-75",
		[RDD_IPI_HIST_6] = "-70",
		[RDD_IPI_HIST_7] = "-65",
		[RDD_IPI_HIST_8] = "-60",
		[RDD_IPI_HIST_9] = "-55",
		[RDD_IPI_HIST_10] = "inf",
	};

	memset(&data, 0, sizeof(data));
	mt7996_tm_ipi_hist_ctrl(phy, &data, RDD_IPI_HIST_ALL_CNT);

	ipi_hist_data = data.ipi_hist_val;
	for (ipi_idx = 0; ipi_idx < POWER_INDICATE_HIST_MAX; ipi_idx++) {
		power_lower_bound = ipi_idx ? ipi_idx_to_power_bound[ipi_idx - 1] : "-inf";
		power_upper_bound = ipi_idx_to_power_bound[ipi_idx];

		mt76_dbg(&dev->mt76, MT76_DBG_TEST,
			 "IPI %d (power range: (%s, %s] dBm): ipi count = %d\n",
			 ipi_idx, power_lower_bound, power_upper_bound,
			 ipi_hist_data[ipi_idx]);

		if (td->ipi_threshold <= ipi_idx && ipi_idx <= RDD_IPI_HIST_10)
			ipi_hist_count_th += ipi_hist_data[ipi_idx];

		ipi_hist_total_count += ipi_hist_data[ipi_idx];
	}

	ipi_free_count = ipi_hist_data[RDD_IPI_FREE_RUN_CNT];

	mt76_dbg(&dev->mt76, MT76_DBG_TEST,
		 "IPI threshold %d: ipi_hist_count_th = %d, ipi_free_count = %d\n",
		 td->ipi_threshold, ipi_hist_count_th, ipi_free_count);
	mt76_dbg(&dev->mt76, MT76_DBG_TEST, "TX assert time =  %d [ms]\n",
		 data.tx_assert_time / 1000);

	/* calculate channel load = (self idle ratio - idle ratio) / self idle ratio */
	if (ipi_hist_count_th >= UINT_MAX / (100 * PRECISION))
		ipi_percentage = 100 * PRECISION *
				 (ipi_hist_count_th / (100 * PRECISION)) /
				 (ipi_free_count / (100 * PRECISION));
	else
		ipi_percentage = PRECISION * 100 * ipi_hist_count_th / ipi_free_count;

	ipi_idle_ratio = ((100 * PRECISION) - ipi_percentage) / PRECISION;

	self_idle_ratio = PRECISION * 100 *
			  (td->ipi_period - (data.tx_assert_time / 1000)) /
			  td->ipi_period / PRECISION;

	if (self_idle_ratio < ipi_idle_ratio)
		channel_load = 0;
	else
		channel_load = self_idle_ratio - ipi_idle_ratio;

	if (self_idle_ratio <= td->ipi_threshold) {
		mt76_dbg(&dev->mt76, MT76_DBG_TEST,
			 "band[%d]: self idle ratio = %d%%, idle ratio = %d%%\n",
			 phy->mt76->band_idx, self_idle_ratio, ipi_idle_ratio);
		return;
	}

	channel_load = (100 * channel_load) / self_idle_ratio;
	mt76_dbg(&dev->mt76, MT76_DBG_TEST,
		 "band[%d]: chan load = %d%%, self idle ratio = %d%%, idle ratio = %d%%\n",
		 phy->mt76->band_idx, channel_load, self_idle_ratio, ipi_idle_ratio);
}

static int
mt7996_tm_set_ipi(struct mt7996_phy *phy)
{
	struct mt76_testmode_data *td = &phy->mt76->test;

	/* reset IPI CR */
	mt7996_tm_ipi_hist_ctrl(phy, NULL, RDD_SET_IPI_HIST_RESET);

	cancel_delayed_work(&phy->ipi_work);
	ieee80211_queue_delayed_work(phy->mt76->hw, &phy->ipi_work,
				     msecs_to_jiffies(td->ipi_period));

	return 0;
}

static int
mt7996_tm_set_trx_mac(struct mt7996_phy *phy, u8 type, bool en)
{
#define UNI_TM_TRX_CTRL 0
	struct mt7996_dev *dev = phy->dev;
	struct mt7996_tm_trx_req req = {
		.param_num = 1,
		.tag = cpu_to_le16(UNI_TM_TRX_CTRL),
		.len = cpu_to_le16(sizeof(req) - 4),
		.param_idx = cpu_to_le16(TM_TRX_PARAM_SET_TRX),
		.band_idx = phy->mt76->band_idx,
		.testmode_en = 1,
		.action = TM_TRX_ACTION_SET,
		.set_trx = {
			.type = type,
			.enable = en,
			.band_idx = phy->mt76->band_idx,
		}
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(TESTMODE_TRX_PARAM),
				 &req, sizeof(req), false);
}

static int
mt7996_tm_txbf_init(struct mt7996_phy *phy, u16 *val)
{
#define EBF_BBP_RX_OFFSET	0x10280
#define EBF_BBP_RX_ENABLE	(BIT(0) | BIT(15))
	struct mt7996_dev *dev = phy->dev;
	struct mt76_testmode_data *td = &phy->mt76->test;
	struct ieee80211_vif *vif = phy->mt76->monitor_vif;
	struct mt7996_vif *mvif = (struct mt7996_vif *)vif->drv_priv;
	struct mt7996_vif_link *deflink = &mvif->deflink;
	enum nl80211_chan_width width = NL80211_CHAN_WIDTH_20;
	void *phase_cal, *pfmu_data, *pfmu_tag;
	u8 nss, band_idx = phy->mt76->band_idx;
	u8 addr, peer_addr, bss_addr;
	bool enable = val[0];

	if (!enable) {
		td->bf_en = false;
		return 0;
	}

	if (!dev->test.txbf_phase_cal) {
		phase_cal = devm_kzalloc(dev->mt76.dev,
					 sizeof(struct mt7996_txbf_phase) *
					 MAX_PHASE_GROUP_NUM,
					 GFP_KERNEL);
		if (!phase_cal)
			return -ENOMEM;

		dev->test.txbf_phase_cal = phase_cal;
	}

	if (!dev->test.txbf_pfmu_data) {
		/* allocate max size for 5x5 pfmu data */
		pfmu_data = devm_kzalloc(dev->mt76.dev,
					 MT7996_TXBF_PFMU_DATA_LEN_5X5,
					 GFP_KERNEL);
		if (!pfmu_data)
			return -ENOMEM;

		dev->test.txbf_pfmu_data = pfmu_data;
	}

	if (!dev->test.txbf_pfmu_tag) {
		pfmu_tag = devm_kzalloc(dev->mt76.dev,
					sizeof(struct mt7996_pfmu_tag), GFP_KERNEL);
		if (!pfmu_tag)
			return -ENOMEM;

		dev->test.txbf_pfmu_tag = pfmu_tag;
	}

	td->bf_en = true;
	dev->ibf = td->ibf;

	/* 00:11:11:11:11:11 for golden/instrument (own mac addr)
	 * 00:22:22:22:22:22 for DUT (own mac addr)
	 * 00:22:22:22:22:22 for bssid
	 */
	bss_addr = TXBF_DUT_MAC_SUBADDR;
	if (td->is_txbf_dut) {
		addr = TXBF_DUT_MAC_SUBADDR;
		peer_addr = TXBF_GOLDEN_MAC_SUBADDR;
	} else {
		addr = TXBF_GOLDEN_MAC_SUBADDR;
		peer_addr = TXBF_DUT_MAC_SUBADDR;
	}
	memset(td->addr, 0, sizeof(td->addr));
	memset(td->addr[0] + 1, peer_addr, ETH_ALEN - 1);
	memset(td->addr[1] + 1, addr, ETH_ALEN - 1);
	memset(td->addr[2] + 1, bss_addr, ETH_ALEN - 1);
	memcpy(vif->addr, td->addr[1], ETH_ALEN);
	mt7996_tm_set_mac_addr(dev, td->addr[0], SET_ID(DA));
	mt7996_tm_set_mac_addr(dev, td->addr[1], SET_ID(SA));
	mt7996_tm_set_mac_addr(dev, td->addr[2], SET_ID(BSSID));

	mt7996_mcu_add_dev_info(phy, phy->mt76->monitor_vif, &phy->mt76->monitor_vif->bss_conf, &mvif->deflink.mt76, true);
	mt7996_mcu_add_bss_info(phy, vif, &vif->bss_conf, &deflink->mt76,
				&deflink->msta_link, true);

	if (td->ibf) {
		if (td->is_txbf_dut) {
			/* Enable ITxBF Capability */
			mt7996_mcu_set_txbf(dev, BF_HW_EN_UPDATE);
			mt7996_tm_set_trx_mac(phy, TM_TRX_MAC_TX, true);

			td->tx_ipg = 999;
			td->tx_mpdu_len = 1024;
			td->tx_antenna_mask = phy->mt76->chainmask >> dev->chainshift[band_idx];
			nss = hweight8(td->tx_antenna_mask);
			if (nss > 1 && nss <= 4)
				td->tx_rate_idx = 15 + 8 * (nss - 2);
			else
				td->tx_rate_idx = 31;
		} else {
			td->tx_antenna_mask = 1;
			td->tx_mpdu_len = 1024;
			td->tx_rate_idx = 0;
			mt76_set(dev, EBF_BBP_RX_OFFSET, EBF_BBP_RX_ENABLE);
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "%s: set BBP RX CR = %x\n",
				 __func__, mt76_rr(dev, EBF_BBP_RX_OFFSET));
		}

		td->tx_rate_mode = MT76_TM_TX_MODE_HT;
		td->tx_rate_sgi = 0;
		/* 5T5R ibf */
		if (nss == 5) {
			td->tx_rate_mode = MT76_TM_TX_MODE_VHT;
			td->tx_rate_idx = 7;
			td->tx_rate_nss = 4;
		}
	} else {
		if (td->is_txbf_dut) {
			/* Enable ETxBF Capability */
			mt7996_mcu_set_txbf(dev, BF_HW_EN_UPDATE);
			td->tx_antenna_mask = phy->mt76->chainmask >> dev->chainshift[band_idx];
			td->tx_spe_idx = 24 + phy->mt76->band_idx;
			if (td->tx_rate_mode == MT76_TM_TX_MODE_VHT ||
			    td->tx_rate_mode == MT76_TM_TX_MODE_HE_SU)
				mt7996_tm_set(dev, SET_ID(NSS), td->tx_rate_nss);

			mt7996_tm_set(dev, SET_ID(ENCODE_MODE), td->tx_rate_ldpc);
			mt7996_tm_set(dev, SET_ID(TX_COUNT), td->tx_count);
		} else {
			/* Turn On BBP CR for RX */
			mt76_set(dev, EBF_BBP_RX_OFFSET, EBF_BBP_RX_ENABLE);
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "%s: set BBP RX CR = %x\n",
				 __func__, mt76_rr(dev, EBF_BBP_RX_OFFSET));

			td->tx_antenna_mask = 1;
		}
		width = phy->mt76->chandef.width;

		if (td->tx_rate_mode == MT76_TM_TX_MODE_EHT_MU)
			td->tx_rate_mode = MT76_TM_TX_MODE_EHT_SU;
	}
	mt76_testmode_param_set(td, MT76_TM_ATTR_TX_ANTENNA);

	mt7996_tm_set(dev, SET_ID(TX_MODE),
		      mt7996_tm_rate_mapping(td->tx_rate_mode, RATE_MODE_TO_PHY));
	mt7996_tm_set(dev, SET_ID(TX_RATE), td->tx_rate_idx);
	mt7996_tm_set(dev, SET_ID(GI), td->tx_rate_sgi);
	mt7996_tm_set(dev, SET_ID(CBW),
		      mt7996_tm_bw_mapping(width, BW_MAP_NL_TO_FW));
	mt7996_tm_set(dev, SET_ID(DBW),
		      mt7996_tm_bw_mapping(width, BW_MAP_NL_TO_FW));
	mt7996_tm_set_antenna(phy, SET_ID(TX_PATH));
	mt7996_tm_set_antenna(phy, SET_ID(RX_PATH));
	mt7996_tm_set(dev, SET_ID(IPG), td->tx_ipg);
	mt7996_tm_set(dev, SET_ID(TX_LEN), td->tx_mpdu_len);
	mt7996_tm_set(dev, SET_ID(TX_TIME), 0);
	mt7996_tm_set(dev, SET_ID(COMMAND), RF_CMD(TX_COMMIT));

	return 0;
}

static inline void
mt7996_tm_txbf_phase_copy(struct mt7996_dev *dev, void *des, void *src, int group)
{
	int phase_size;

	if (group && get_ibf_version(dev) == IBF_VER_1)
		phase_size = sizeof(struct mt7996_txbf_phase_info_5g);
	else if (get_ibf_version(dev) == IBF_VER_1)
		phase_size = sizeof(struct mt7996_txbf_phase_info_2g);
	else if (group)
		phase_size = sizeof(struct mt7992_txbf_phase_info_5g);
	else
		phase_size = sizeof(struct mt7992_txbf_phase_info_2g);

	memcpy(des, src, phase_size);
}

static int
mt7996_tm_txbf_phase_comp(struct mt7996_phy *phy, u16 *val)
{
	struct mt7996_dev *dev = phy->dev;
	struct mt7996_tm_bf_req req = {
		.phase_comp = {
			.tag = cpu_to_le16(BF_IBF_PHASE_COMP),
			.len = cpu_to_le16(sizeof(req.phase_comp)),
			.bw = val[0],
			.jp_band = (val[2] == 1) ? 1 : 0,
			.band_idx = phy->mt76->band_idx,
			.read_from_e2p = val[3],
			.disable = val[4],
			.group = val[2],
		}
	};
	struct mt7996_txbf_phase *phase = (struct mt7996_txbf_phase *)dev->test.txbf_phase_cal;
	int group = val[2];

	if (!phase)
		return -EINVAL;

	wait_event_timeout(dev->mt76.mcu.wait, phase[group].status != 0, HZ);
	mt7996_tm_txbf_phase_copy(dev, req.phase_comp.buf, phase[group].buf, group);

	mt76_dbg(&dev->mt76, MT76_DBG_TEST, "%s: phase comp info\n", __func__);
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_NONE, 16, 1,
		       &req, sizeof(req), 0);

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(BF), &req,
				 sizeof(req), false);
}

static int
mt7996_tm_txbf_profile_tag_write(struct mt7996_phy *phy, u8 pfmu_idx, struct mt7996_pfmu_tag *tag)
{
	struct mt7996_dev *dev = phy->dev;
	struct mt7996_tm_bf_req req = {
		.pfmu_tag = {
			.tag = cpu_to_le16(BF_PFMU_TAG_WRITE),
			.len = cpu_to_le16(sizeof(req.pfmu_tag)),
			.pfmu_id = pfmu_idx,
			.bfer = true,
			.band_idx = phy->mt76->band_idx,
		}
	};

	memcpy(req.pfmu_tag.buf, tag, sizeof(*tag));
	wait_event_timeout(dev->mt76.mcu.wait, tag->t1.pfmu_idx != 0, HZ);

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(BF), &req,
				 sizeof(req), false);
}

static int
mt7996_tm_add_txbf_sta(struct mt7996_phy *phy, u8 pfmu_idx, u8 nr, u8 nc, bool ebf)
{
	struct mt7996_dev *dev = phy->dev;
	struct mt76_testmode_data *td = &phy->mt76->test;
	struct {
		struct sta_req_hdr hdr;
		struct sta_rec_bf bf;
	} __packed req = {
		.hdr = {
			.bss_idx = phy->mt76->band_idx,
			.wlan_idx_lo = to_wcid_lo(phy->mt76->band_idx + 1),
			.tlv_num = 1,
			.is_tlv_append = 1,
			.muar_idx = 0,
			.wlan_idx_hi = to_wcid_hi(phy->mt76->band_idx + 1),
		},
		.bf = {
			.tag = cpu_to_le16(STA_REC_BF),
			.len = cpu_to_le16(sizeof(req.bf)),
			.pfmu = cpu_to_le16(pfmu_idx),
			.sounding_phy = 1,
			.bf_cap = ebf,
			.ncol = nc,
			.nrow = nr,
			.ibf_timeout = 0xff,
			.tx_mode = mt7996_tm_rate_mapping(td->tx_rate_mode, RATE_MODE_TO_PHY),
		},
	};
	u8 ndp_rate, ndpa_rate, rept_poll_rate;
	u8 bf_bw = phy->mt76->chandef.width;

	if ((td->tx_rate_mode == MT76_TM_TX_MODE_HE_SU ||
	     td->tx_rate_mode == MT76_TM_TX_MODE_EHT_SU) && !td->ibf) {
		rept_poll_rate = 0x49;
		ndpa_rate = 0x49;
		ndp_rate = 0;
	} else if (td->tx_rate_mode == MT76_TM_TX_MODE_VHT && !td->ibf) {
		rept_poll_rate = 0x9;
		ndpa_rate = 0x9;
		ndp_rate = 0;
	} else {
		rept_poll_rate = 0;
		ndpa_rate = 0;
		if (nr == 1)
			ndp_rate = 8;
		else if (nr == 2)
			ndp_rate = 16;
		else if (nr == 4)
			ndp_rate = 32;
		else
			ndp_rate = 24;

		/* 5T5R ebf profile for ibf cal */
		if (nr == 4 && td->ibf && ebf) {
			ndp_rate = 0;
			ndpa_rate = 11;
		}
	}

	req.bf.ndp_rate = ndp_rate;
	req.bf.ndpa_rate = ndpa_rate;
	req.bf.rept_poll_rate = rept_poll_rate;
	if (mt76_testmode_param_present(td, MT76_TM_ATTR_TX_PKT_BW))
		bf_bw = td->tx_pkt_bw;
	req.bf.bw = mt7996_tm_bw_mapping(bf_bw, BW_MAP_NL_TO_BF);
	req.bf.tx_mode = (td->tx_rate_mode == MT76_TM_TX_MODE_EHT_SU) ? 0xf : req.bf.tx_mode;

	if (ebf) {
		req.bf.mem[0].row = 0;
		req.bf.mem[1].row = 1;
		req.bf.mem[2].row = 2;
		req.bf.mem[3].row = 3;
	} else {
		req.bf.mem[0].row = 4;
		req.bf.mem[1].row = 5;
		req.bf.mem[2].row = 6;
		req.bf.mem[3].row = 7;
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_WMWA_UNI_CMD(STA_REC_UPDATE), &req,
				 sizeof(req), true);
}

static int
mt7996_tm_txbf_profile_update(struct mt7996_phy *phy, u16 *val, bool ebf)
{
#define MT_ARB_IBF_ENABLE			(BIT(0) | GENMASK(9, 8))
	struct mt76_testmode_data *td = &phy->mt76->test;
	struct mt7996_dev *dev = phy->dev;
	struct mt7996_pfmu_tag *tag = dev->test.txbf_pfmu_tag;
	u8 rate, pfmu_idx = val[0], nc = val[2], nr;
	u8 dbw = phy->mt76->chandef.width;
	int ret;
	bool is_atenl = val[5];

	if (!tag)
		return -EINVAL;

	if (td->tx_antenna_mask == 3)
		nr = 1;
	else if (td->tx_antenna_mask == 7)
		nr = 2;
	else if (td->tx_antenna_mask == 31)
		nr = 4;
	else
		nr = 3;

	memset(tag, 0, sizeof(*tag));
	tag->t1.pfmu_idx = pfmu_idx;
	tag->t1.ebf = ebf;
	tag->t1.nr = nr;
	tag->t1.nc = nc;
	tag->t1.invalid_prof = true;
	if (mt76_testmode_param_present(td, MT76_TM_ATTR_TX_PKT_BW))
		dbw = td->tx_pkt_bw;
	tag->t1.data_bw = mt7996_tm_bw_mapping(dbw, BW_MAP_NL_TO_BF);
	tag->t2.se_idx = td->tx_spe_idx;

	if (ebf) {
		tag->t1.row_id1 = 0;
		tag->t1.row_id2 = 1;
		tag->t1.row_id3 = 2;
		tag->t1.row_id4 = 3;
		tag->t1.lm = mt7996_tm_rate_mapping(td->tx_rate_mode, RATE_MODE_TO_LM);
	} else {
		tag->t1.row_id1 = 4;
		tag->t1.row_id2 = 5;
		tag->t1.row_id3 = 6;
		tag->t1.row_id4 = 7;
		rate = nr == 4 ? td->tx_rate_mode : MT76_TM_TX_MODE_OFDM;
		tag->t1.lm = mt7996_tm_rate_mapping(rate, RATE_MODE_TO_LM);

		tag->t2.ibf_timeout = 0xff;
		tag->t2.ibf_nr = nr;
		tag->t2.ibf_nc = nc;
	}

	ret = mt7996_tm_txbf_profile_tag_write(phy, pfmu_idx, tag);
	if (ret)
		return ret;

	ret = mt7996_tm_add_txbf_sta(phy, pfmu_idx, nr, nc, ebf);
	if (ret)
		return ret;

	if (!is_atenl && !td->ibf) {
		mt76_set(dev, MT_ARB_TQSAXM0(phy->mt76->band_idx), MT_ARB_TQSAXM_ALTX_START_MASK);
		mt76_dbg(&dev->mt76, MT76_DBG_TEST,
			 "%s: set TX queue start CR for AX management (0x%x) = 0x%x\n",
			 __func__, MT_ARB_TQSAXM0(phy->mt76->band_idx),
			 mt76_rr(dev, MT_ARB_TQSAXM0(phy->mt76->band_idx)));
	} else if (!is_atenl && td->ibf && ebf) {
		/* iBF's ebf profile update */
		mt76_set(dev, MT_ARB_TQSAXM0(phy->mt76->band_idx), MT_ARB_IBF_ENABLE);
		mt76_dbg(&dev->mt76, MT76_DBG_TEST,
			 "%s: set TX queue start CR for AX management (0x%x) = 0x%x\n",
			 __func__, MT_ARB_TQSAXM0(phy->mt76->band_idx),
			 mt76_rr(dev, MT_ARB_TQSAXM0(phy->mt76->band_idx)));
	}

	if (!ebf && is_atenl)
		return mt7996_tm_txbf_apply_tx(phy, 1, false, true, true);

	return 0;
}

static int
mt7996_tm_txbf_phase_cal(struct mt7996_phy *phy, u16 *val)
{
	struct mt7996_dev *dev = phy->dev;
	struct mt7996_tm_bf_req req = {
		.phase_cal = {
			.tag = cpu_to_le16(BF_PHASE_CALIBRATION),
			.len = cpu_to_le16(sizeof(req.phase_cal)),
			.group = val[0],
			.group_l_m_n = val[1],
			.sx2 = val[2],
			.cal_type = val[3],
			.lna_gain_level = val[4],
			.band_idx = phy->mt76->band_idx,
			.version = val[5],
		},
	};
	struct mt7996_txbf_phase *phase = (struct mt7996_txbf_phase *)dev->test.txbf_phase_cal;

	if (!phase)
		return -EINVAL;

	/* reset phase status before update phase cal data */
	phase[req.phase_cal.group].status = 0;

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(BF), &req,
				 sizeof(req), false);
}

static int
mt7996_tm_txbf_profile_update_all(struct mt7996_phy *phy, u16 *val)
{
	struct mt7996_dev *dev = phy->dev;
	u8 nss = hweight16(phy->mt76->chainmask);
	u16 pfmu_idx = val[0];
	u16 subc_id = val[1];
	u16 angle11 = val[2];
	u16 angle21 = val[3];
	u16 angle31 = val[4];
	u16 angle41 = val[5];
	u16 angle51 = val[6];
	s16 phi11 = 0, phi21 = 0, phi31 = 0, phi41 = 0;
	s16 *pfmu_data;
	int offs = subc_id * sizeof(struct mt7996_pfmu_data) / sizeof(*pfmu_data);

	if (!dev->test.txbf_pfmu_data ||
	    subc_id > MT7996_TXBF_SUBCAR_NUM - 1)
		return -EINVAL;

	if (nss == 2) {
		phi11 = (s16)(angle21 - angle11);
	} else if (nss == 3) {
		phi11 = (s16)(angle31 - angle11);
		phi21 = (s16)(angle31 - angle21);
	} else if (nss == 5) {
		phi11 = (s16)(angle51 - angle11);
		phi21 = (s16)(angle51 - angle21);
		phi31 = (s16)(angle51 - angle31);
		phi41 = (s16)(angle51 - angle41);
		offs = subc_id * sizeof(struct mt7996_pfmu_data_5x5) / sizeof(*pfmu_data);
	} else {
		phi11 = (s16)(angle41 - angle11);
		phi21 = (s16)(angle41 - angle21);
		phi31 = (s16)(angle41 - angle31);
	}

	pfmu_data = (s16 *)dev->test.txbf_pfmu_data;
	pfmu_data += offs;

	if (subc_id < 32)
		pfmu_data[0] = cpu_to_le16(subc_id + 224);
	else
		pfmu_data[0] = cpu_to_le16(subc_id - 32);

	pfmu_data[1] = cpu_to_le16(phi11);
	pfmu_data[2] = cpu_to_le16(phi21);
	pfmu_data[3] = cpu_to_le16(phi31);
	if (nss == 5)
		pfmu_data[4] = cpu_to_le16(phi41);

	if (subc_id == MT7996_TXBF_SUBCAR_NUM - 1) {
		struct mt7996_tm_bf_req req = {
			.pfmu_data_all = {
				.tag = cpu_to_le16(BF_PROFILE_WRITE_20M_ALL_5X5),
				.len = cpu_to_le16(sizeof(req.pfmu_data_all)),
				.pfmu_id = pfmu_idx,
				.band_idx = phy->mt76->band_idx,
			},
		};
		int size = MT7996_TXBF_PFMU_DATA_LEN_5X5;

		if (nss != 5) {
			size = MT7996_TXBF_PFMU_DATA_LEN;
			req.pfmu_data_all.tag = cpu_to_le16(BF_PROFILE_WRITE_20M_ALL);
			req.pfmu_data_all.len = cpu_to_le16(sizeof(req.pfmu_data_all) -
							    MT7996_TXBF_PFMU_DATA_LEN_5X5 + size);
		}
		memcpy(req.pfmu_data_all.buf, dev->test.txbf_pfmu_data, size);

		return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(BF),
					 &req, sizeof(req), true);
	}

	return 0;
}

static int
mt7996_tm_txbf_e2p_update(struct mt7996_phy *phy)
{
#define TXBF_PHASE_EEPROM_START_OFFSET		0xc00
#define TXBF_PHASE_GROUP_EEPROM_OFFSET_VER_1	46
#define TXBF_PHASE_G0_EEPROM_OFFSET_VER_2	29
#define TXBF_PHASE_GX_EEPROM_OFFSET_VER_2	sizeof(struct mt7992_txbf_phase_info_5g)
	struct mt7996_txbf_phase *phase, *p;
	struct mt7996_dev *dev = phy->dev;
	u8 *eeprom = dev->mt76.eeprom.data;
	u16 offset;
	int i;

	offset = TXBF_PHASE_EEPROM_START_OFFSET;
	phase = (struct mt7996_txbf_phase *)dev->test.txbf_phase_cal;
	if (!phase)
		return -EINVAL;

	for (i = 0; i < MAX_PHASE_GROUP_NUM; i++) {
		p = &phase[i];

		/* copy valid phase cal data to eeprom */
		if (p->status)
			mt7996_tm_txbf_phase_copy(dev, eeprom + offset, p->buf, i);

		if (get_ibf_version(dev) == IBF_VER_1)
			offset += TXBF_PHASE_GROUP_EEPROM_OFFSET_VER_1;
		else
			offset += i ? TXBF_PHASE_GX_EEPROM_OFFSET_VER_2 :
				      TXBF_PHASE_G0_EEPROM_OFFSET_VER_2;
	}

	return 0;
}

static int
mt7996_tm_txbf_apply_tx(struct mt7996_phy *phy, u16 wlan_idx, bool ebf,
			bool ibf, bool phase_cal)
{
	struct mt7996_dev *dev = phy->dev;
	struct mt7996_tm_bf_req req = {
		.tx_apply = {
			.tag = cpu_to_le16(BF_DATA_PACKET_APPLY),
			.len = cpu_to_le16(sizeof(req.tx_apply)),
			.wlan_idx = cpu_to_le16(wlan_idx),
			.ebf = ebf,
			.ibf = ibf,
			.phase_cal = phase_cal,
		},
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(BF), &req, sizeof(req), false);
}

static int
mt7996_tm_txbf_set_tx(struct mt7996_phy *phy, u16 *val)
{
	bool bf_on = val[0], update = val[3];
	struct mt7996_dev *dev = phy->dev;
	struct mt7996_pfmu_tag *tag = dev->test.txbf_pfmu_tag;
	struct mt76_testmode_data *td = &phy->mt76->test;

	if (!tag)
		return -EINVAL;

	if (bf_on) {
		mt7996_tm_set_rx_frames(phy, false);
		mt7996_tm_set_tx_frames(phy, false);
		mt7996_mcu_set_txbf_internal(phy, BF_PFMU_TAG_READ, 2, true);
		tag->t1.invalid_prof = false;
		mt7996_tm_txbf_profile_tag_write(phy, 2, tag);
		td->bf_ever_en = true;

		if (update)
			mt7996_tm_txbf_apply_tx(phy, 1, 0, 1, 1);
	} else {
		if (!td->bf_ever_en) {
			mt7996_tm_set_rx_frames(phy, false);
			mt7996_tm_set_tx_frames(phy, false);

			if (update)
				mt7996_tm_txbf_apply_tx(phy, 1, 0, 0, 0);
		} else {
			td->bf_ever_en = false;

			mt7996_mcu_set_txbf_internal(phy, BF_PFMU_TAG_READ, 2, true);
			tag->t1.invalid_prof = true;
			mt7996_tm_txbf_profile_tag_write(phy, 2, tag);
		}
	}

	return 0;
}

static int
mt7996_tm_trigger_sounding(struct mt7996_phy *phy, u16 *val, bool en)
{
	struct mt7996_dev *dev = phy->dev;
	u8 sounding_mode = val[0];
	u8 sta_num = val[1];
	u32 sounding_interval = (u32)val[2] << 2;	/* input unit: 4ms */
	u16 tag = en ? BF_SOUNDING_ON : BF_SOUNDING_OFF;
	struct mt7996_tm_bf_req req = {
		.sounding = {
			.tag = cpu_to_le16(tag),
			.len = cpu_to_le16(sizeof(req.sounding)),
			.snd_mode = sounding_mode,
			.sta_num = sta_num,
			.wlan_id = {
				cpu_to_le16(val[3]),
				cpu_to_le16(val[4]),
				cpu_to_le16(val[5]),
				cpu_to_le16(val[6])
			},
			.snd_period = cpu_to_le32(sounding_interval),
		},
	};

	if (sounding_mode > SOUNDING_MODE_MAX)
		return -EINVAL;

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(BF),
				 &req, sizeof(req), false);
}

static int
mt7996_tm_txbf_txcmd(struct mt7996_phy *phy, u16 *val)
{
	struct mt7996_dev *dev = phy->dev;
	struct mt7996_tm_bf_req req = {
		.txcmd = {
			.tag = cpu_to_le16(BF_CMD_TXCMD),
			.len = cpu_to_le16(sizeof(req.txcmd)),
			.action = val[0],
			.bf_manual = val[1],
			.bf_bit = val[2],
		},
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WM_UNI_CMD(BF), &req, sizeof(req), false);
}

static int
mt7996_tm_set_txbf(struct mt7996_phy *phy)
{
#define TXBF_IS_DUT_MASK	BIT(0)
#define TXBF_IBF_MASK		BIT(1)
	struct mt76_testmode_data *td = &phy->mt76->test;
	struct mt7996_dev *dev = phy->dev;
	u16 *val = td->txbf_param;

	mt76_dbg(&dev->mt76, MT76_DBG_TEST,
		 "%s: act = %u, val = %u, %u, %u, %u, %u, %u, %u, %u\n",
		 __func__, td->txbf_act, val[0], val[1], val[2], val[3],
		 val[4], val[5], val[6], val[7]);

	switch (td->txbf_act) {
	case MT76_TM_TXBF_ACT_GOLDEN_INIT:
	case MT76_TM_TXBF_ACT_INIT:
	case MT76_TM_TX_EBF_ACT_GOLDEN_INIT:
	case MT76_TM_TX_EBF_ACT_INIT:
		td->ibf = !u32_get_bits(td->txbf_act, TXBF_IBF_MASK);
		td->ebf = true;
		td->is_txbf_dut = !!u32_get_bits(td->txbf_act, TXBF_IS_DUT_MASK);
		return mt7996_tm_txbf_init(phy, val);
	case MT76_TM_TXBF_ACT_UPDATE_CH:
		mt7996_tm_update_channel(phy);
		break;
	case MT76_TM_TXBF_ACT_PHASE_COMP:
		return mt7996_tm_txbf_phase_comp(phy, val);
	case MT76_TM_TXBF_ACT_TX_PREP:
		return mt7996_tm_txbf_set_tx(phy, val);
	case MT76_TM_TXBF_ACT_IBF_PROF_UPDATE:
		return mt7996_tm_txbf_profile_update(phy, val, false);
	case MT76_TM_TXBF_ACT_EBF_PROF_UPDATE:
		return mt7996_tm_txbf_profile_update(phy, val, true);
	case MT76_TM_TXBF_ACT_PHASE_CAL:
		return mt7996_tm_txbf_phase_cal(phy, val);
	case MT76_TM_TXBF_ACT_PROF_UPDATE_ALL_CMD:
	case MT76_TM_TXBF_ACT_PROF_UPDATE_ALL:
		return mt7996_tm_txbf_profile_update_all(phy, val);
	case MT76_TM_TXBF_ACT_E2P_UPDATE:
		return mt7996_tm_txbf_e2p_update(phy);
	case MT76_TM_TXBF_ACT_APPLY_TX: {
		u16 wlan_idx = val[0];
		bool ebf = !!val[1], ibf = !!val[2], phase_cal = !!val[4];

		return mt7996_tm_txbf_apply_tx(phy, wlan_idx, ebf, ibf, phase_cal);
	}
	case MT76_TM_TXBF_ACT_TRIGGER_SOUNDING:
		return mt7996_tm_trigger_sounding(phy, val, true);
	case MT76_TM_TXBF_ACT_STOP_SOUNDING:
		memset(val, 0, sizeof(td->txbf_param));
		return mt7996_tm_trigger_sounding(phy, val, false);
	case MT76_TM_TXBF_ACT_PROFILE_TAG_READ:
	case MT76_TM_TXBF_ACT_PROFILE_TAG_WRITE:
	case MT76_TM_TXBF_ACT_PROFILE_TAG_INVALID: {
		struct mt7996_pfmu_tag *tag = dev->test.txbf_pfmu_tag;
		u8 pfmu_idx = val[0];
		bool bfer = !!val[1];

		if (!tag) {
			mt76_err(&dev->mt76, "%s: pfmu tag is not initialized\n", __func__);
			return -EINVAL;
		}

		if (td->txbf_act == MT76_TM_TXBF_ACT_PROFILE_TAG_WRITE)
			return mt7996_tm_txbf_profile_tag_write(phy, pfmu_idx, tag);
		else if (td->txbf_act == MT76_TM_TXBF_ACT_PROFILE_TAG_READ)
			return mt7996_mcu_set_txbf_internal(phy, BF_PFMU_TAG_READ, pfmu_idx, bfer);

		tag->t1.invalid_prof = !!val[0];

		return 0;
	}
	case MT76_TM_TXBF_ACT_STA_REC_READ:
		return mt7996_mcu_set_txbf_internal(phy, BF_STA_REC_READ, val[0], 0);
	case MT76_TM_TXBF_ACT_TXCMD:
		return mt7996_tm_txbf_txcmd(phy, val);
	default:
		break;
	};

	return 0;
}

static void
mt7996_tm_update_params(struct mt7996_phy *phy, u32 changed)
{
	struct mt76_testmode_data *td = &phy->mt76->test;
	struct mt7996_dev *dev = phy->dev;

	if (changed & BIT(TM_CHANGED_FREQ_OFFSET)) {
		mt7996_tm_set(dev, SET_ID(FREQ_OFFSET), td->freq_offset);
		mt7996_tm_set(dev, SET_ID(FREQ_OFFSET_C2), td->freq_offset);
	}
	if (changed & BIT(TM_CHANGED_TXPOWER))
		mt7996_tm_set(dev, SET_ID(POWER), td->tx_power[0]);
	if (changed & BIT(TM_CHANGED_SKU_EN)) {
		mt7996_tm_update_channel(phy);
		mt7996_mcu_set_tx_power_ctrl(phy, POWER_CTRL(SKU_POWER_LIMIT), td->sku_en);
		mt7996_mcu_set_tx_power_ctrl(phy, POWER_CTRL(BACKOFF_POWER_LIMIT), td->sku_en);
		mt7996_mcu_set_txpower_sku(phy, phy->mt76->monitor_vif->bss_conf.txpower);
	}
	if (changed & BIT(TM_CHANGED_TX_LENGTH)) {
		mt7996_tm_set(dev, SET_ID(TX_LEN), td->tx_mpdu_len);
		mt7996_tm_set(dev, SET_ID(TX_TIME), 0);
	}
	if (changed & BIT(TM_CHANGED_TX_TIME)) {
		mt7996_tm_set(dev, SET_ID(TX_LEN), 0);
		mt7996_tm_set(dev, SET_ID(TX_TIME), td->tx_time);
	}
	if (changed & BIT(TM_CHANGED_CFG)) {
		u32 func_idx = td->cfg.enable ? SET_ID(CFG_ON) : SET_ID(CFG_OFF);

		mt7996_tm_set(dev, func_idx, td->cfg.type);
	}
	if ((changed & BIT(TM_CHANGED_OFF_CHAN_CH)) &&
	    (changed & BIT(TM_CHANGED_OFF_CHAN_BW)))
		mt7996_tm_set_offchan(phy, !(changed & BIT(TM_CHANGED_OFF_CHAN_CENTER_CH)));
	if ((changed & BIT(TM_CHANGED_IPI_THRESHOLD)) &&
	    (changed & BIT(TM_CHANGED_IPI_PERIOD)))
		mt7996_tm_set_ipi(phy);
	if (changed & BIT(TM_CHANGED_IPI_RESET))
		mt7996_tm_ipi_hist_ctrl(phy, NULL, RDD_SET_IPI_HIST_RESET);
	if (changed & BIT(TM_CHANGED_TXBF_ACT))
		mt7996_tm_set_txbf(phy);
	if (changed & BIT(TM_CHANGED_TX_ANTENNA)) {
		mt76_testmode_param_set(td, MT76_TM_ATTR_TX_ANTENNA);
		mt7996_tm_set_antenna(phy, SET_ID(TX_PATH));
		mt7996_tm_set_antenna(phy, SET_ID(RX_PATH));
	}
	if (changed & BIT(TM_CHANGED_TX_RATE_NSS))
		mt7996_tm_set(dev, SET_ID(NSS), td->tx_rate_nss);
	if (changed & BIT(TM_CHANGED_TX_RATE_IDX))
		mt7996_tm_set(dev, SET_ID(TX_RATE), td->tx_rate_idx);
}

static int
mt7996_tm_set_state(struct mt76_phy *mphy, enum mt76_testmode_state state)
{
	struct mt76_testmode_data *td = &mphy->test;
	struct mt7996_phy *phy = mphy->priv;
	struct mt7996_dev *dev = phy->dev;
	enum mt76_testmode_state prev_state = td->state;

	if (!dev->testmode_enable)
		return -EPERM;

	if (prev_state != MT76_TM_STATE_OFF)
		mt7996_tm_set(dev, SET_ID(BAND_IDX), mphy->band_idx);

	if (state >= MT76_TM_STATE_GROUP_PREK && state <= MT76_TM_STATE_GROUP_PREK_CLEAN)
		return mt7996_tm_group_prek(phy, state);
	else if (state >= MT76_TM_STATE_DPD_2G && state <= MT76_TM_STATE_DPD_CLEAN)
		return mt7996_tm_dpd_prek(phy, state);
	else if (state >= MT76_TM_STATE_RX_GAIN_CAL && state <= MT76_TM_STATE_RX_GAIN_CAL_CLEAN)
		return mt7996_tm_rx_gain_cal(phy, state);

	if (prev_state == MT76_TM_STATE_TX_FRAMES ||
	    state == MT76_TM_STATE_TX_FRAMES)
		mt7996_tm_set_tx_frames(phy, state == MT76_TM_STATE_TX_FRAMES);
	else if (prev_state == MT76_TM_STATE_RX_FRAMES ||
		 state == MT76_TM_STATE_RX_FRAMES)
		mt7996_tm_set_rx_frames(phy, state == MT76_TM_STATE_RX_FRAMES);
	else if (prev_state == MT76_TM_STATE_TX_CONT ||
		 state == MT76_TM_STATE_TX_CONT)
		mt7996_tm_set_tx_cont(phy, state == MT76_TM_STATE_TX_CONT);
	else if (prev_state == MT76_TM_STATE_OFF ||
		 state == MT76_TM_STATE_OFF)
		mt7996_tm_init(phy, !(state == MT76_TM_STATE_OFF));

	if ((state == MT76_TM_STATE_IDLE &&
	     prev_state == MT76_TM_STATE_OFF) ||
	    (state == MT76_TM_STATE_OFF &&
	     prev_state == MT76_TM_STATE_IDLE)) {
		u32 changed = 0;
		int i, ret;

		for (i = 0; i < ARRAY_SIZE(tm_change_map); i++) {
			u16 cur = tm_change_map[i];

			if (mt76_testmode_param_present(td, cur))
				changed |= BIT(i);
		}

		ret = mt7996_tm_check_antenna(phy);
		if (ret)
			return ret;

		mt7996_tm_update_params(phy, changed);
	}

	return 0;
}

static int
mt7996_tm_set_params(struct mt76_phy *mphy, struct nlattr **tb,
		     enum mt76_testmode_state new_state)
{
	struct mt76_testmode_data *td = &mphy->test;
	struct mt7996_phy *phy = mphy->priv;
	struct mt7996_dev *dev = phy->dev;
	u32 changed = 0;
	int i, ret;

	BUILD_BUG_ON(NUM_TM_CHANGED >= 32);

	if (new_state == MT76_TM_STATE_OFF ||
	    td->state == MT76_TM_STATE_OFF)
		return 0;

	ret = mt7996_tm_check_antenna(phy);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(tm_change_map); i++) {
		if (tb[tm_change_map[i]])
			changed |= BIT(i);
	}

	mt7996_tm_set(dev, SET_ID(BAND_IDX), mphy->band_idx);
	mt7996_tm_update_params(phy, changed);

	return 0;
}

static int
mt7996_tm_get_rx_stats(struct mt7996_phy *phy)
{
	struct mt7996_dev *dev = phy->dev;
	struct mt7996_tm_rx_req req = {
		.band = phy->mt76->band_idx,
		.rx_stat_all = {
			.tag = cpu_to_le16(UNI_TM_RX_STAT_GET_ALL_V2),
			.len = cpu_to_le16(sizeof(req.rx_stat_all)),
			.band_idx = phy->mt76->band_idx,
		},
	};
	struct mt76_testmode_data *td = &phy->mt76->test;
	struct mt7996_tm_rx_event *rx_stats;
	struct mt7996_tm_rx_event_stat_all *rx_stats_all;
	struct sk_buff *skb;
	enum mt76_rxq_id qid;
	int i, ret = 0;
	u16 fcs_err_count, fcs_ok_count;
	u16 len_mismatch;
	u32 mdrdy_count;

	if (td->state != MT76_TM_STATE_RX_FRAMES)
		return 0;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_WM_UNI_CMD_QUERY(TESTMODE_RX_STAT),
					&req, sizeof(req), true, &skb);

	if (ret)
		return ret;

	rx_stats = (struct mt7996_tm_rx_event *)skb->data;
	rx_stats_all = &rx_stats->rx_stat_all;

	td->last_rx.freq_offset = le32_to_cpu(rx_stats_all->user_info[0].freq_offset);
	td->last_rx.snr = le32_to_cpu(rx_stats_all->user_info[0].snr);
	for (i = 0; i < td->last_rx.path; i++) {
		td->last_rx.rcpi[i] = le16_to_cpu(rx_stats_all->rxv_info[i].rcpi);
		td->last_rx.rssi[i] = le16_to_cpu(rx_stats_all->rxv_info[i].rssi);
		td->last_rx.ib_rssi[i] = rx_stats_all->fagc[i].ib_rssi;
		td->last_rx.wb_rssi[i] = rx_stats_all->fagc[i].wb_rssi;
	}

	if (phy->mt76->band_idx == MT_BAND2)
		qid = MT_RXQ_BAND2;
	else if (phy->mt76->band_idx == MT_BAND1)
		qid = MT_RXQ_BAND1;
	else
		qid = MT_RXQ_MAIN;

	mdrdy_count = le32_to_cpu(rx_stats_all->band_info.mac_rx_mdrdy_cnt);
	fcs_ok_count = le16_to_cpu(rx_stats_all->band_info.mac_rx_fcs_ok_cnt);
	fcs_err_count = le16_to_cpu(rx_stats_all->band_info.mac_rx_fcs_err_cnt);
	len_mismatch = le16_to_cpu(rx_stats_all->band_info.mac_rx_len_mismatch);
	td->rx_stats[qid].packets += mdrdy_count;
	td->rx_stats[qid].rx_success += fcs_ok_count;
	td->rx_stats[qid].fcs_error += fcs_err_count;
	td->rx_stats[qid].len_mismatch += len_mismatch;

	dev_kfree_skb(skb);

	return ret;
}

static void
mt7996_tm_reset_trx_stats(struct mt76_phy *mphy)
{
	struct mt7996_phy *phy = mphy->priv;

	memset(&mphy->test.rx_stats, 0, sizeof(mphy->test.rx_stats));
	mt7996_tm_set(phy->dev, SET_ID(TRX_COUNTER_RESET), 0);
}

static int
mt7996_tm_get_tx_stats(struct mt7996_phy *phy)
{
	struct mt7996_dev *dev = phy->dev;
	struct mt76_testmode_data *td = &phy->mt76->test;
	int ret;

	if (td->state != MT76_TM_STATE_TX_FRAMES)
		return 0;

	ret = mt7996_tm_get(dev, GET_ID(TXED_COUNT), 0, &td->tx_done);
	if (ret)
		return ret;

	td->tx_pending = td->tx_count - td->tx_done;

	return ret;
}

static int
mt7996_tm_dump_stats(struct mt76_phy *mphy, struct sk_buff *msg)
{
	struct mt76_testmode_data *td = &mphy->test;
	struct mt7996_phy *phy = mphy->priv;
	struct mt7996_dev *dev = phy->dev;
	int band_idx = mphy->band_idx;

	if (!td->last_rx.path) {
		td->last_rx.path = hweight32(mphy->chainmask >> dev->chainshift[band_idx]);
		if (phy->has_aux_rx)
			td->last_rx.path++;
		td->last_rx.rcpi = devm_kzalloc(dev->mt76.dev, td->last_rx.path, GFP_KERNEL);
		td->last_rx.rssi = devm_kzalloc(dev->mt76.dev, td->last_rx.path, GFP_KERNEL);
		td->last_rx.ib_rssi = devm_kzalloc(dev->mt76.dev, td->last_rx.path, GFP_KERNEL);
		td->last_rx.wb_rssi = devm_kzalloc(dev->mt76.dev, td->last_rx.path, GFP_KERNEL);
	}

	mt7996_tm_set(dev, SET_ID(BAND_IDX), band_idx);
	mt7996_tm_get_rx_stats(phy);
	mt7996_tm_get_tx_stats(phy);

	return 0;
}

static bool
mt7996_tm_efuse_update_is_valid(struct mt7996_dev *dev, u32 offset, u8 *write_buf)
{
#define PROT_OFFS_MAX_SIZE	8
#define EFUSE_PROT_END_OFFSET	0xffff
#define EFUSE_PROT_ALL_MASK	GENMASK(15, 0)
	static const struct efuse_region ddie_prot_offs[][PROT_OFFS_MAX_SIZE] = {
		[DDIE_7996] = {{.start = 0x10, .end = 0x18f, .prot_mask = -1},
			       {.start = 0x1b0, .end = 0x2bf, .prot_mask = -1},
			       {.start = 0x2c0, .end = 0x2cf, .prot_mask = GENMASK(15, 6)},
			       {.start = 0x2d0, .end = 0x2ff, .prot_mask = -1},
			       {.start = 0x300, .end = 0x30f, .prot_mask = GENMASK(15, 1)},
			       {.start = 0x310, .end = 0x31f, .prot_mask = GENMASK(15, 1)},
			       {.start = 0x320, .end = 0x3ff, .prot_mask = -1},
			       {.start = -1}},
		[DDIE_7992] = {{.start = 0x10, .end = 0x18f, .prot_mask = -1},
			       {.start = 0x1b0, .end = 0x3ff, .prot_mask = -1},
			       {.start = -1}},
		[DDIE_7990] = {{.start = 0x10, .end = 0x18f, .prot_mask = -1},
			       {.start = 0x1b0, .end = 0x3ff, .prot_mask = -1},
			       {.start = -1}},
	};
	static const struct efuse_region adie_prot_offs[][PROT_OFFS_MAX_SIZE] = {
		[ADIE_7975] = {{.start = 0x5c0, .end = 0x62f, .prot_mask = -1},
			       {.start = 0x6c0, .end = 0x6ff, .prot_mask = -1},
			       {.start = 0x7a0, .end = 0x7af, .prot_mask = BIT(1) | BIT(9)},
			       {.start = 0x7b0, .end = 0x7bf, .prot_mask = -1},
			       {.start = -1}},
		[ADIE_7976] = {{.start = 0x0, .end = 0x7f, .prot_mask = -1},
			       {.start = 0x790, .end = 0x79f,
				.prot_mask = GENMASK(15, 10) | GENMASK(8, 0)},
			       {.start = 0x7a0, .end = 0x7af,
				.prot_mask = BIT(6) | BIT(8) | BIT(10)},
			       {.start = 0x7b0, .end = 0x7bf, .prot_mask = -1},
			       {.start = -1}},
		[ADIE_7977] = {{.start = 0x0, .end = 0x5f, .prot_mask = -1},
			       {.start = 0x60, .end = 0x6f, .prot_mask = GENMASK(14, 0)},
			       {.start = 0x70, .end = 0x7f,
				.prot_mask = GENMASK(15, 14) | GENMASK(12, 0)},
			       {.start = 0x80, .end = 0x10f, .prot_mask = -1},
			       {.start = -1}},
	};
	static const struct efuse_region *prot_offs;
	u8 read_buf[MT76_TM_EEPROM_BLOCK_SIZE], *eeprom = dev->mt76.eeprom.data;
	int ret, i = 0;
	u16 base;

	if (!write_buf)
		return false;

	memset(read_buf, 0, MT76_TM_EEPROM_BLOCK_SIZE);
	ret = mt7996_mcu_get_eeprom(dev, offset, read_buf,
				    MT76_TM_EEPROM_BLOCK_SIZE, EFUSE_MODE);
	if (ret && ret != -EINVAL)
		return false;

	/* no change in this block, so skip it */
	if (!memcmp(eeprom + offset, read_buf, MT76_TM_EEPROM_BLOCK_SIZE))
		return false;

	memcpy(write_buf, eeprom + offset, MT76_TM_EEPROM_BLOCK_SIZE);

	switch (mt76_chip(&dev->mt76)) {
	case MT7996_DEVICE_ID:
		if (offset < EFUSE_BASE_OFFS_ADIE0) {
			base = EFUSE_BASE_OFFS_DDIE;
			prot_offs = ddie_prot_offs[DDIE_7996];
		} else if (offset >= EFUSE_BASE_OFFS_ADIE0 &&
			   offset < EFUSE_BASE_OFFS_ADIE2) {
			base = EFUSE_BASE_OFFS_ADIE0;
			if (dev->var.type == MT7996_VAR_TYPE_233 ||
			    dev->var.fem == MT7996_FEM_EXT)
				prot_offs = adie_prot_offs[ADIE_7976];
			else
				prot_offs = adie_prot_offs[ADIE_7975];
		} else if (offset >= EFUSE_BASE_OFFS_ADIE2 &&
			   offset < EFUSE_BASE_OFFS_ADIE1) {
			base = EFUSE_BASE_OFFS_ADIE2;
			prot_offs = adie_prot_offs[ADIE_7977];
		} else {
			base = EFUSE_BASE_OFFS_ADIE1;
			prot_offs = adie_prot_offs[ADIE_7977];
		}
		break;
	case MT7992_DEVICE_ID:
		/* block all the adie region in efuse for kite */
		if (offset >= EFUSE_BASE_OFFS_ADIE0)
			return false;
		base = EFUSE_BASE_OFFS_DDIE;
		prot_offs = ddie_prot_offs[DDIE_7992];
		break;
	case MT7990_DEVICE_ID:
		/* block all the adie region in efuse for griffin */
		if (offset >= EFUSE_BASE_OFFS_ADIE0)
			return false;
		base = EFUSE_BASE_OFFS_DDIE;
		prot_offs = ddie_prot_offs[DDIE_7990];
		break;
	default:
		return false;
	}

	/* check efuse protection */
	while (prot_offs[i].start != EFUSE_PROT_END_OFFSET) {
		if (offset >= prot_offs[i].start + base &&
		    offset <= prot_offs[i].end + base) {
			unsigned long prot_mask = prot_offs[i].prot_mask;
			int j;

			if (prot_mask == EFUSE_PROT_ALL_MASK)
				return false;

			for_each_set_bit(j, &prot_mask, MT76_TM_EEPROM_BLOCK_SIZE) {
				if (write_buf[j] != read_buf[j]) {
					write_buf[j] = read_buf[j];
					mt76_dbg(&dev->mt76, MT76_DBG_TEST,
						 "%s: offset %x is invalid to write\n",
						 __func__, offset + j);
				}
			}
			break;
		}
		i++;
	}

	if (!memcmp(read_buf, write_buf, MT76_TM_EEPROM_BLOCK_SIZE))
		return false;

	return true;
}

static int
mt7996_tm_write_back_to_efuse(struct mt7996_dev *dev)
{
	struct mt7996_mcu_eeprom_info req = {
		.tag = cpu_to_le16(UNI_EFUSE_ACCESS),
		.len = cpu_to_le16(sizeof(req) - 4 +
				   MT76_TM_EEPROM_BLOCK_SIZE),
	};
	int msg_len = sizeof(req) + MT76_TM_EEPROM_BLOCK_SIZE;
	u8 *eeprom = dev->mt76.eeprom.data;
	u8 write_buf[MT76_TM_EEPROM_BLOCK_SIZE];
	int i, ret = -EINVAL;

	/* prevent from damaging chip id in efuse */
	if (mt76_chip(&dev->mt76) != get_unaligned_le16(eeprom))
		return ret;

	for (i = 0; i < MT7996_EEPROM_SIZE; i += MT76_TM_EEPROM_BLOCK_SIZE) {
		struct sk_buff *skb;

		memset(write_buf, 0, MT76_TM_EEPROM_BLOCK_SIZE);
		if (!mt7996_tm_efuse_update_is_valid(dev, i, write_buf))
			continue;

		skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, msg_len);
		if (!skb)
			return -ENOMEM;

		req.addr = cpu_to_le32(i);
		skb_put_data(skb, &req, sizeof(req));
		skb_put_data(skb, write_buf, MT76_TM_EEPROM_BLOCK_SIZE);

		ret = mt76_mcu_skb_send_msg(&dev->mt76, skb,
					    MCU_WM_UNI_CMD(EFUSE_CTRL), true);
		if (ret)
			return ret;
	}

	return ret;
}

static int
mt7996_tm_set_eeprom(struct mt76_phy *mphy, u32 offset, u8 *val, u8 action)
{
	struct mt7996_phy *phy = mphy->priv;
	struct mt7996_dev *dev = phy->dev;
	u8 *eeprom = dev->mt76.eeprom.data;
	int ret = 0;

	if (offset >= MT7996_EEPROM_SIZE)
		return -EINVAL;

	switch (action) {
	case MT76_TM_EEPROM_ACTION_UPDATE_DATA:
		memcpy(eeprom + offset, val, MT76_TM_EEPROM_BLOCK_SIZE);
		break;
	case MT76_TM_EEPROM_ACTION_UPDATE_BUFFER_MODE:
		ret = mt7996_mcu_set_eeprom(dev);
		break;
	case MT76_TM_EEPROM_ACTION_WRITE_TO_EFUSE:
		ret = mt7996_tm_write_back_to_efuse(dev);
		break;
	case MT76_TM_EEPROM_ACTION_WRITE_TO_EXT_EEPROM:
		ret = mt7996_mcu_write_ext_eeprom(dev, 0, MT7996_EEPROM_SIZE, NULL);
		break;
	default:
		break;
	}

	return ret;
}

static int
mt7996_tm_dump_seg_list(struct mt7996_phy *phy)
{
	struct mt7996_dev *dev = phy->dev;
	struct mt76_list_mode_data *list;
	static const char * const testmode_tx_mode[] = {
		[MT76_TM_TX_MODE_CCK] = "cck",
		[MT76_TM_TX_MODE_OFDM] = "ofdm",
		[MT76_TM_TX_MODE_HT] = "ht",
		[MT76_TM_TX_MODE_VHT] = "vht",
		[MT76_TM_TX_MODE_HE_SU] = "he_su",
		[MT76_TM_TX_MODE_HE_EXT_SU] = "he_ext_su",
		[MT76_TM_TX_MODE_HE_TB] = "he_tb",
		[MT76_TM_TX_MODE_HE_MU] = "he_mu",
		[MT76_TM_TX_MODE_EHT_SU] = "eht_su",
		[MT76_TM_TX_MODE_EHT_TRIG] = "eht_tb",
		[MT76_TM_TX_MODE_EHT_MU] = "eht_mu",
	};
	int i, cbw, dbw;

	if (!phy->mt76->lists) {
		mt76_err(&dev->mt76, "%s: no available segment list\n", __func__);
		return 0;
	}

	mt76_dbg(&dev->mt76, MT76_DBG_TEST,
		 "Total Segment Number %d:\n", phy->mt76->seg_num);
	for (i = 0; i < phy->mt76->seg_num; i++) {
		list = &phy->mt76->lists[i];

		mt76_dbg(&dev->mt76, MT76_DBG_TEST, "%s Segment %d:\n",
			 list->seg_type == LM_SEG_TYPE_TX ? "TX" : "RX", i);
		mt76_dbg(&dev->mt76, MT76_DBG_TEST, "\tantenna swap: %d\n", list->ant_swap);
		mt76_dbg(&dev->mt76, MT76_DBG_TEST,
			 "\tsegment timeout: %d\n", list->seg_timeout);
		mt76_dbg(&dev->mt76, MT76_DBG_TEST,
			 "\ttx antenna mask: %d\n", list->tx_antenna_mask);
		mt76_dbg(&dev->mt76, MT76_DBG_TEST,
			 "\trx antenna mask: %d\n", list->rx_antenna_mask);
		mt76_dbg(&dev->mt76, MT76_DBG_TEST, "\tcenter ch1: %d\n", list->center_ch1);
		mt76_dbg(&dev->mt76, MT76_DBG_TEST, "\tcenter ch2: %d\n", list->center_ch2);
		cbw = mt7996_tm_bw_mapping(list->system_bw, BW_MAP_NL_TO_MHZ);
		dbw = mt7996_tm_bw_mapping(list->data_bw, BW_MAP_NL_TO_MHZ);
		mt76_dbg(&dev->mt76, MT76_DBG_TEST, "\tsystem bw: %d MH\n", cbw);
		mt76_dbg(&dev->mt76, MT76_DBG_TEST, "\tdata bw: %d MHz\n", dbw);
		mt76_dbg(&dev->mt76, MT76_DBG_TEST,
			 "\tprimary selection: %d\n", list->pri_sel);
		if (list->seg_type == LM_SEG_TYPE_TX) {
			mt76_dbg(&dev->mt76, MT76_DBG_TEST, "\tda: %pM\n", list->addr[0]);
			mt76_dbg(&dev->mt76, MT76_DBG_TEST, "\tsa: %pM\n", list->addr[1]);
			mt76_dbg(&dev->mt76, MT76_DBG_TEST, "\tbssid: %pM\n", list->addr[2]);
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "\ttx mpdu len: %d\n", list->tx_mpdu_len);
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "\ttx count: %d\n", list->tx_count);
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "\ttx power: %d\n", list->tx_power);
			mt76_dbg(&dev->mt76, MT76_DBG_TEST, "\ttx rate mode: %s\n",
				 testmode_tx_mode[list->tx_rate_mode]);
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "\ttx rate idx: %d\n", list->tx_rate_idx);
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "\ttx rate stbc: %d\n", list->tx_rate_stbc);
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "\ttx rate ldpc: %d\n", list->tx_rate_ldpc);
			mt76_dbg(&dev->mt76, MT76_DBG_TEST, "\ttx ipg: %d\n", list->tx_ipg);
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "\ttx rate sgi: %d\n", list->tx_rate_sgi);
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "\ttx rate nss: %d\n", list->tx_rate_nss);
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "\thw tx mode: %d\n", list->hw_tx_mode);
		} else {
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "\town addr: %pM\n", list->addr[0]);
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "\tsta idx: %d\n", list->sta_idx);
		}
		mt76_dbg(&dev->mt76, MT76_DBG_TEST, "\n");
	}

	return 0;
}

static int
mt7996_tm_get_list_mode_rx_stat(struct mt7996_dev *dev, int ext_id)
{
	struct mt7996_tm_list_req req = {
		.tag = cpu_to_le16(UNI_RF_TEST_LIST_MODE),
		.len = cpu_to_le16(sizeof(req.seg)),
		.seg.rx_stat.ext_id = cpu_to_le32(ext_id),
	};
	int seg_idx, total_seg, seg_read_num, ret;
	struct mt7996_tm_list_event *event;
	struct sk_buff *skb;

	for (seg_idx = 0; seg_idx < LIST_SEG_MAX_NUM;) {
		struct lm_rx_status *rx_stat;
		int i;

		req.seg.rx_stat.seg_start_idx = cpu_to_le32(seg_idx);
		ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_WM_UNI_CMD_QUERY(TESTMODE_CTRL),
						&req, sizeof(req), true, &skb);
		if (ret)
			break;

		event = (struct mt7996_tm_list_event *)skb->data;
		total_seg = le32_to_cpu(event->total_seg);
		seg_read_num = le32_to_cpu(event->seg_read_num);
		if (seg_idx == 0)
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "ext_id: %d, status: %d, total_seg: %d, seg_read_num: %d\n",
				 le32_to_cpu(event->ext_id), le16_to_cpu(event->status),
				 total_seg, seg_read_num);

		if (!seg_read_num)
			break;

		for (i = 0; i < seg_read_num; i++) {
			rx_stat = &event->rx_stats[i];
			mt76_dbg(&dev->mt76, MT76_DBG_TEST,
				 "seg_idx: %u, rx_ok: %u, fcs_err: %u\n",
				 seg_idx + i, le32_to_cpu(rx_stat->rx_ok),
				 le32_to_cpu(rx_stat->fcs_err));
			mt76_dbg(&dev->mt76, MT76_DBG_TEST, "rssi: %d, %d, %d, %d, %d\n",
				 le32_to_cpu(rx_stat->rssi0), le32_to_cpu(rx_stat->rssi1),
				 le32_to_cpu(rx_stat->rssi2), le32_to_cpu(rx_stat->rssi3),
				 le32_to_cpu(rx_stat->rssi4));
		}

		seg_idx += seg_read_num;
		if (seg_idx >= total_seg)
			break;
	}

	return 0;
}

static int
mt7996_tm_set_list_mode(struct mt76_phy *mphy, int seg_idx,
			enum mt76_testmode_list_act list_act)
{
	struct mt76_list_mode_data *list = &mphy->lists[seg_idx];
	struct mt7996_phy *phy = mphy->priv;
	struct mt7996_dev *dev = phy->dev;
	struct cfg80211_chan_def *chandef = &mphy->chandef;
	struct ieee80211_channel *chan = chandef->chan;
	struct mt7996_tm_list_req req = {
		.tag = cpu_to_le16(UNI_RF_TEST_LIST_MODE),
		.len = cpu_to_le16(sizeof(req.seg)),
	};
	static const u8 lm_ext_id[] = {
		[MT76_TM_LM_ACT_SET_TX_SEGMENT] = 16,
		[MT76_TM_LM_ACT_TX_START] = 17,
		[MT76_TM_LM_ACT_TX_STOP] = 19,
		[MT76_TM_LM_ACT_SET_RX_SEGMENT] = 20,
		[MT76_TM_LM_ACT_RX_START] = 21,
		[MT76_TM_LM_ACT_RX_STOP] = 23,
		[MT76_TM_LM_ACT_SWITCH_SEGMENT] = 25,
		[MT76_TM_LM_ACT_RX_STATUS] = 22,
		[MT76_TM_LM_ACT_DUT_STATUS] = 24,
	};
	static const char * const lm_state[] = {
		[LM_STATE_IDLE] = "idle",
		[LM_STATE_DPD_CAL] = "dpd cal",
		[LM_STATE_TX] = "tx ongoing",
		[LM_STATE_RX] = "rx ongoing",
	};
	int seg_param_num = sizeof(req.seg.tx_seg.rf) / sizeof(u32);
	int ret, state, band = mt7996_tm_band_mapping(chan->band);
	struct mt7996_tm_list_event *event;
	struct sk_buff *skb;
	u8 cbw, dbw;

	switch (list_act) {
	case MT76_TM_LM_ACT_SET_TX_SEGMENT:
		req.seg.tx_seg.hdr.ext_id = cpu_to_le32(lm_ext_id[list_act]);
		req.seg.tx_seg.hdr.frame_control = cpu_to_le32(0x8);
		req.seg.tx_seg.hdr.duration = cpu_to_le32(0);
		req.seg.tx_seg.hdr.seq_id = cpu_to_le32(0);
		req.seg.tx_seg.hdr.tx_mpdu_len = cpu_to_le32(list->tx_mpdu_len);
		memcpy(req.seg.tx_seg.hdr.da, list->addr[0], ETH_ALEN);
		memcpy(req.seg.tx_seg.hdr.sa, list->addr[1], ETH_ALEN);
		memcpy(req.seg.tx_seg.hdr.bssid, list->addr[2], ETH_ALEN);
		req.seg.tx_seg.hdr.tx_rate_stbc = cpu_to_le32(list->tx_rate_stbc);
		req.seg.tx_seg.hdr.seg_num = cpu_to_le32(1);
		seg_param_num += sizeof(req.seg.tx_seg.tx) / sizeof(u32);
		req.seg.tx_seg.hdr.seg_param_num = cpu_to_le32(seg_param_num);
		req.seg.tx_seg.rf.seg_idx = cpu_to_le32(seg_idx);
		req.seg.tx_seg.rf.band = cpu_to_le32(band);
		req.seg.tx_seg.rf.band_idx = cpu_to_le32(mphy->band_idx);
		req.seg.tx_seg.rf.tx_antenna_mask = cpu_to_le32(list->tx_antenna_mask);
		req.seg.tx_seg.rf.rx_antenna_mask = cpu_to_le32(list->rx_antenna_mask);
		req.seg.tx_seg.rf.center_ch1 = cpu_to_le32(list->center_ch1);
		req.seg.tx_seg.rf.center_ch2 = cpu_to_le32(list->center_ch2);
		cbw = mt7996_tm_bw_mapping(list->system_bw, BW_MAP_NL_TO_TM);
		dbw = mt7996_tm_bw_mapping(list->data_bw, BW_MAP_NL_TO_TM);
		req.seg.tx_seg.rf.system_bw = cpu_to_le32(cbw);
		req.seg.tx_seg.rf.data_bw = cpu_to_le32(dbw);
		req.seg.tx_seg.rf.pri_sel = cpu_to_le32(list->pri_sel);
		req.seg.tx_seg.tx.ch_band = cpu_to_le32(band);
		req.seg.tx_seg.tx.tx_mpdu_len = cpu_to_le32(list->tx_mpdu_len);
		req.seg.tx_seg.tx.tx_count = cpu_to_le32(list->tx_count);
		req.seg.tx_seg.tx.tx_power = cpu_to_le32(list->tx_power);
		req.seg.tx_seg.tx.tx_rate_mode = cpu_to_le32(list->tx_rate_mode);
		req.seg.tx_seg.tx.tx_rate_idx = cpu_to_le32(list->tx_rate_idx);
		req.seg.tx_seg.tx.tx_rate_ldpc = cpu_to_le32(list->tx_rate_ldpc);
		req.seg.tx_seg.tx.tx_ipg = cpu_to_le32(list->tx_ipg);
		req.seg.tx_seg.tx.tx_rate_sgi = cpu_to_le32(list->tx_rate_sgi);
		req.seg.tx_seg.tx.tx_rate_nss = cpu_to_le32(list->tx_rate_nss);
		req.seg.tx_seg.tx.hw_tx_mode = cpu_to_le32(0);
		req.seg.tx_seg.tx.ant_swap = cpu_to_le32(0);
		req.seg.tx_seg.tx.seg_timeout = cpu_to_le32(list->seg_timeout);
		break;
	case MT76_TM_LM_ACT_SET_RX_SEGMENT:
		req.seg.rx_seg.hdr.ext_id = cpu_to_le32(lm_ext_id[list_act]);
		memcpy(req.seg.rx_seg.hdr.addr, list->addr[0], ETH_ALEN);
		req.seg.rx_seg.hdr.seg_num = cpu_to_le32(1);
		seg_param_num += sizeof(req.seg.rx_seg.rx) / sizeof(u32);
		req.seg.rx_seg.hdr.seg_param_num = cpu_to_le32(seg_param_num);
		req.seg.rx_seg.rf.seg_idx = cpu_to_le32(seg_idx);
		req.seg.rx_seg.rf.band = cpu_to_le32(band);
		req.seg.rx_seg.rf.band_idx = cpu_to_le32(mphy->band_idx);
		req.seg.rx_seg.rf.tx_antenna_mask = cpu_to_le32(list->tx_antenna_mask);
		req.seg.rx_seg.rf.rx_antenna_mask = cpu_to_le32(list->rx_antenna_mask);
		req.seg.rx_seg.rf.center_ch1 = cpu_to_le32(list->center_ch1);
		req.seg.rx_seg.rf.center_ch2 = cpu_to_le32(list->center_ch2);
		cbw = mt7996_tm_bw_mapping(list->system_bw, BW_MAP_NL_TO_TM);
		dbw = mt7996_tm_bw_mapping(list->data_bw, BW_MAP_NL_TO_TM);
		req.seg.rx_seg.rf.system_bw = cpu_to_le32(cbw);
		req.seg.rx_seg.rf.data_bw = cpu_to_le32(dbw);
		req.seg.rx_seg.rf.pri_sel = cpu_to_le32(list->pri_sel);
		req.seg.rx_seg.rx.sta_idx = cpu_to_le32(list->sta_idx);
		req.seg.rx_seg.rx.ch_band = cpu_to_le32(band);
		req.seg.rx_seg.rx.ant_swap = cpu_to_le32(0);
		req.seg.rx_seg.rx.seg_timeout = cpu_to_le32(list->seg_timeout);
		break;
	case MT76_TM_LM_ACT_TX_START:
	case MT76_TM_LM_ACT_TX_STOP:
	case MT76_TM_LM_ACT_RX_START:
	case MT76_TM_LM_ACT_RX_STOP:
	case MT76_TM_LM_ACT_SWITCH_SEGMENT:
	case MT76_TM_LM_ACT_DUT_STATUS:
		req.seg.ext_id = cpu_to_le32(lm_ext_id[list_act]);
		break;
	case MT76_TM_LM_ACT_RX_STATUS:
		return mt7996_tm_get_list_mode_rx_stat(dev, lm_ext_id[list_act]);
	case MT76_TM_LM_ACT_CLEAR_SEGMENT:
		kfree(mphy->lists);
		mphy->lists = NULL;
		mphy->seg_num = 0;
		return 0;
	case MT76_TM_LM_ACT_DUMP_SEGMENT:
		return mt7996_tm_dump_seg_list(phy);
	default:
		return -EINVAL;
	}

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_WM_UNI_CMD_QUERY(TESTMODE_CTRL),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	event = (struct mt7996_tm_list_event *)skb->data;
	mt76_dbg(&dev->mt76, MT76_DBG_TEST,
		 "ext_id: %u, status: %u, total_seg: %u, seg_read_num: %u\n",
		 le32_to_cpu(event->ext_id), le16_to_cpu(event->status),
		 le32_to_cpu(event->total_seg), le32_to_cpu(event->seg_read_num));

	state = le32_to_cpu(event->event_state.state);
	if (list_act == MT76_TM_LM_ACT_DUT_STATUS && state < LM_STATE_NUM)
		mt76_dbg(&dev->mt76, MT76_DBG_TEST, "Event seg_idx: %u, state: %s\n",
			 le32_to_cpu(event->event_state.seg_idx), lm_state[state]);

	dev_kfree_skb(skb);

	return ret;
}

const struct mt76_testmode_ops mt7996_testmode_ops = {
	.set_state = mt7996_tm_set_state,
	.set_params = mt7996_tm_set_params,
	.dump_stats = mt7996_tm_dump_stats,
	.reset_rx_stats = mt7996_tm_reset_trx_stats,
	.tx_stop = mt7996_tm_tx_stop,
	.set_eeprom = mt7996_tm_set_eeprom,
	.dump_precal = mt7996_tm_dump_precal,
	.set_list_mode = mt7996_tm_set_list_mode,
};
