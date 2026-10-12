/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Copyright (C) 2020-2026 MediaTek Inc. */

#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/inet.h>
#include "mt7915.h"
#include "mcu.h"
#include "mac.h"
#include "mtk_mcu.h"

const struct sw_radar_type radar_type[] = {
	[RADAR_TYPE_FCC_1_JP_1] = {0, 1, 0, 8, 32, 28, 0, 13, 508, 3076, 1, 1, 0, 0, 0, 0},
	[RADAR_TYPE_FCC_2] = {0, 1, 0, 12, 32, 28, 0, 17, 140, 240, 1, 1, 0, 0, 0, 0},
	[RADAR_TYPE_FCC_3] = {0, 1, 0, 8, 32, 28, 0, 22, 190, 510, 1, 1, 0, 0, 0, 0},
	[RADAR_TYPE_FCC_4] = {0, 1, 0, 6, 32, 28, 0, 32, 190, 510, 1, 1, 0, 0, 0, 0},
	[RADAR_TYPE_FCC_6] = {0, 1, 0, 9, 255, 28, 0, 13, 323, 343, 1, 32, 0, 0, 0, 0},
	[RADAR_TYPE_ETSI_1] = {0, 1, 0, 6, 32, 28, 0, 17, 990, 5010, 1, 1, 0, 0, 0, 0},
	[RADAR_TYPE_ETSI_2] = {0, 1, 0, 9, 32, 28, 0, 27, 615, 5010, 1, 1, 0, 0, 0, 0},
	[RADAR_TYPE_ETSI_3] = {0, 1, 0, 8, 32, 28, 0, 27, 245, 445, 1, 1, 0, 0, 0, 0},
	[RADAR_TYPE_ETSI_4] = {0, 1, 0, 6, 32, 28, 0, 42, 245, 510, 1, 1, 0, 0, 0, 0},
	[RADAR_TYPE_ETSI_5_2PRI] = {0, 1, 1, 0, 0, 0, 0, 14, 2490, 3343, 0, 0, 12, 32, 28, 126},
	[RADAR_TYPE_ETSI_5_3PRI] = {0, 1, 1, 0, 0, 0, 0, 14, 2490, 3343, 0, 0, 15, 32, 24, 126},
	[RADAR_TYPE_ETSI_6_2PRI] = {0, 1, 1, 0, 0, 0, 0, 14, 823, 2510, 0, 0, 18, 32, 28, 54},
	[RADAR_TYPE_ETSI_6_3PRI] = {0, 1, 1, 0, 0, 0, 0, 14, 823, 2510, 0, 0, 27, 32, 24, 54},
	[RADAR_TYPE_JP_2] = {0, 1, 0, 7, 32, 28, 0, 14, 3836, 3856, 1, 1, 0, 0, 0, 0},
	[RADAR_TYPE_JP_3] = {0, 1, 0, 6, 32, 22, 0, 110, 615, 5010, 1, 1, 0, 0, 0, 0},
	[RADAR_TYPE_JP_4] = {0, 1, 1, 0, 0, 0, 0, 110, 15, 5010, 0, 0, 12, 32, 28, 0},
	[RADAR_TYPE_KR_1] = {0, 1, 0, 8, 32, 28, 0, 14, 1419, 1439, 1, 1, 0, 0, 0, 0},
	[RADAR_TYPE_KR_2] = {0, 1, 0, 4, 32, 28, 0, 14, 546, 566, 1, 1, 0, 0, 0, 0},
	[RADAR_TYPE_KR_3] = {0, 1, 0, 9, 32, 28, 0, 14, 3020, 3040, 1, 1, 0, 0, 0, 0},
	[RADAR_TYPE_NUM] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
};

int mt7915_mcu_set_txpower_level(struct mt7915_phy *phy, u8 drop_level)
{
	struct mt7915_dev *dev = phy->dev;
	struct mt7915_sku_val {
		u8 format_id;
		u8 val;
		u8 band;
		u8 _rsv;
	} __packed req = {
		.format_id = 1,
		.band = phy->mt76->band_idx,
		.val = !!drop_level,
	};
	int ret;

	ret = mt76_mcu_send_msg(&dev->mt76,
				MCU_EXT_CMD(TX_POWER_FEATURE_CTRL), &req,
				sizeof(req), true);
	if (ret)
		return ret;

	req.format_id = 2;
	if ((drop_level > 90 && drop_level < 100) || !drop_level)
		req.val = 0;
	else if (drop_level > 60 && drop_level <= 90)
		/* reduce Pwr for 1 dB. */
		req.val = 2;
	else if (drop_level > 30 && drop_level <= 60)
		/* reduce Pwr for 3 dB. */
		req.val = 6;
	else if (drop_level > 15 && drop_level <= 30)
		/* reduce Pwr for 6 dB. */
		req.val = 12;
	else if (drop_level > 9 && drop_level <= 15)
		/* reduce Pwr for 9 dB. */
		req.val = 18;
	else if (drop_level > 0 && drop_level <= 9)
		/* reduce Pwr for 12 dB. */
		req.val = 24;

	return mt76_mcu_send_msg(&dev->mt76,
				 MCU_EXT_CMD(TX_POWER_FEATURE_CTRL), &req,
				 sizeof(req), true);
}

static void
mt7915_mcu_rx_sr_swsd(struct mt7915_dev *dev, struct sk_buff *skb)
{
#define SR_SCENE_DETECTION_TIMER_PERIOD_MS 500
	struct mt7915_mcu_sr_swsd_event *event;
	static const char * const rules[] = {"NO CONNECTED", "NO CONGESTION",
					     "NO INTERFERENCE", "MESH ILLEGAL APCLI",
					     "SR ON"};
	u8 idx, rule;

	event = (struct mt7915_mcu_sr_swsd_event *)skb->data;
	idx = event->basic.band_idx;

	rule = event->rule[idx];
	if (rule >= ARRAY_SIZE(rules)) {
		dev_info(dev->mt76.dev, "Invalid SR rule %u\n", rule);
		return;
	}

	dev_info(dev->mt76.dev, "Band index = %u\n", idx);
	dev_info(dev->mt76.dev, "Hit Rule = %s\n", rules[rule]);
	dev_info(dev->mt76.dev, "Timer Period = %d(us)\n"
		 "Congestion Ratio  = %d.%1d%%\n",
		 SR_SCENE_DETECTION_TIMER_PERIOD_MS * 1000,
		 le32_to_cpu(event->total_airtime_ratio[idx]) / 10,
		 le32_to_cpu(event->total_airtime_ratio[idx]) % 10);
	dev_info(dev->mt76.dev,
		 "Total Airtime = %d(us)\n"
		 "ChBusy = %d\n"
		 "SrTx = %d\n"
		 "OBSS = %d\n"
		 "MyTx = %d\n"
		 "MyRx = %d\n"
		 "Interference Ratio = %d.%1d%%\n",
		 le32_to_cpu(event->total_airtime[idx]),
		 le32_to_cpu(event->channel_busy_time[idx]),
		 le32_to_cpu(event->sr_tx_airtime[idx]),
		 le32_to_cpu(event->obss_airtime[idx]),
		 le32_to_cpu(event->my_tx_airtime[idx]),
		 le32_to_cpu(event->my_rx_airtime[idx]),
		 le32_to_cpu(event->obss_airtime_ratio[idx]) / 10,
		 le32_to_cpu(event->obss_airtime_ratio[idx]) % 10);
}

static void
mt7915_mcu_rx_sr_hw_indicator(struct mt7915_dev *dev, struct sk_buff *skb)
{
	struct mt7915_mcu_sr_hw_ind_event *event;

	event = (struct mt7915_mcu_sr_hw_ind_event *)skb->data;

	dev_info(dev->mt76.dev, "Inter PPDU Count = %u\n",
		 le16_to_cpu(event->inter_bss_ppdu_cnt));
	dev_info(dev->mt76.dev, "SR Valid Count = %u\n",
		 le16_to_cpu(event->non_srg_valid_cnt));
	dev_info(dev->mt76.dev, "SR Tx Count = %u\n",
		 le32_to_cpu(event->sr_ampdu_mpdu_cnt));
	dev_info(dev->mt76.dev, "SR Tx Acked Count = %u\n",
		 le32_to_cpu(event->sr_ampdu_mpdu_acked_cnt));
}

void mt7915_mcu_rx_sr_event(struct mt7915_dev *dev, struct sk_buff *skb)
{
	struct mt7915_mcu_sr_basic_event *event;
	struct mt7915_phy *phy;

	event = (struct mt7915_mcu_sr_basic_event *)skb->data;

	if (event->band_idx > MT_BAND1) {
		dev_info(dev->mt76.dev, "Invalid band idx %d\n", event->band_idx);
		return;
	}

	switch (event->sub_event_id) {
	case SPR_EVENT_CFG_SR_ENABLE:
		phy = dev->mt76.phys[event->band_idx]->priv;
		if (phy)
			phy->sr_enable = le32_to_cpu(event->value);
		break;
	case SPR_EVENT_SR_SW_SD:
		mt7915_mcu_rx_sr_swsd(dev, skb);
		break;
	case SPR_EVENT_SR_HW_IND:
		mt7915_mcu_rx_sr_hw_indicator(dev, skb);
		break;
	default:
		dev_info(dev->mt76.dev, "Unknown SR event tag %d\n",
			 event->sub_event_id);
		break;
	}
}

int mt7915_dbg_mcu_wa_cmd(struct mt7915_dev *dev, int cmd, u32 a1, u32 a2, u32 a3, bool wait_resp)
{
	struct {
		__le32 args[3];
	} req = {
		.args = {
			cpu_to_le32(a1),
			cpu_to_le32(a2),
			cpu_to_le32(a3),
		},
	};

	return mt76_mcu_send_msg(&dev->mt76, cmd, &req, sizeof(req), wait_resp);
}

int mt7915_mcu_set_amsdu_algo(struct mt7915_dev *dev, u16 wcid, u8 enable)
{
	struct {
		__le16 action;
		u8 _rsv1[2];
		__le16 wcid;
		u8 enable;
		u8 _rsv2[5];
	} __packed req = {
		.action = cpu_to_le16(1),
		.wcid = cpu_to_le16(wcid),
		.enable = enable,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(MEC_CTRL), &req, sizeof(req), true);
}

int mt7915_mcu_set_rdd_log_config(struct mt7915_dev *dev, u8 val)
{
#define RDD_LOG_CONFIG_UPDATE	0x4
	struct {
		__le16 ctrl;
		u8 hw_rdd_log_en;
		u8 sw_rdd_log_en;
		u8 dump_log_cond;
	} __packed req = {
		.ctrl = cpu_to_le16(RDD_LOG_CONFIG_UPDATE),
		.hw_rdd_log_en = 1,
		.sw_rdd_log_en = 1,
		.dump_log_cond = val,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(SET_RDD_TH),
				 &req, sizeof(req), false);
}

static void
mt7915_mcu_dump_radar_pulse_buffer(struct mt7915_dev *dev, u8 pulse_num,
				   u8 pulse_type, struct pulse_buffer *buff)
{
#define MAX_TIME		BIT(26)				/* hw store radar time info in 26 bits */
#define TIME_TRANS(time)	(le_to_cpu(time) * 4 / 10)	/* hw time unit = 0.4us */
#define POWER_TRANS(power)	(le_to_cpu(power) / 4 - 256)
	static const char * const pulse_name[] = {
		[LONG_PULSE] = "long",
		[SHORT_PULSE] = "short",
		[HW_PULSE] = "hw",
	};
	u8 pidx;

	if (pulse_num > MAX_PULSE_BUFFER_SIZE) {
		dev_info(dev->mt76.dev, "Invalid pulse number: %d (max = %d)\n",
			 pulse_num, MAX_PULSE_BUFFER_SIZE);
		return;
	}

	dev_info(dev->mt76.dev,
		 "----------------------%s pulse buffer----------------------\n",
		 pulse_name[pulse_type]);
	dev_info(dev->mt76.dev,
		 "Index | Start Time (us) | Pulse Width (us) | Power (dBm) | %s\n",
		 pulse_type == HW_PULSE ?
		 "Stability Check Pass | SW RDD Reset | MAC Data Ready (MDRDY) | TX Active" :
		 "Pulse Repetition Interval (us)");

	for (pidx = 0; pidx < pulse_num; pidx++) {
		u32 pri_value = 0;
		char str[80];
		u32 diff;

		if (pulse_type != HW_PULSE) {
			if (pidx) {
				diff = le_to_cpu(buff[pidx].start) -
				       le_to_cpu(buff[pidx - 1].start);
				pri_value = TIME_TRANS((diff + MAX_TIME) % MAX_TIME);
			}
			snprintf(str, sizeof(str), "%30d", pri_value);
		} else {
			snprintf(str, sizeof(str), "%20s | %12s | %22u | %9u",
				 buff[pidx].type.hw.sc_pass ? "true" : "false",
				 buff[pidx].type.hw.sw_reset ? "true" : "false",
				 buff[pidx].type.hw.mdrdy_flag,
				 buff[pidx].type.hw.tx_active);
		}

		dev_info(dev->mt76.dev, "%5u | %15u | %16u | %11d | %s\n",
			 pidx, TIME_TRANS(buff[pidx].start),
			 TIME_TRANS(buff[pidx].pulse_width),
			 POWER_TRANS(buff[pidx].pulse_power), str);
	}

	dev_info(dev->mt76.dev,
		 "----------------------%s pulse raw data----------------------\n",
		 pulse_name[pulse_type]);
	dev_info(dev->mt76.dev, "Start time | Pulse Width | Power\n");
	for (pidx = 0; pidx < pulse_num; pidx++) {
		dev_info(dev->mt76.dev, "%10u | %11u | %5d",
			 buff[pidx].start,
			 buff[pidx].pulse_width,
			 buff[pidx].pulse_power);
	}
	dev_info(dev->mt76.dev,
		 "-----------------------------------------------------------\n");
}

void mt7915_mcu_dump_radar_pulse(struct mt7915_dev *dev, void *rdd_report)
{
	struct mt7915_mcu_rdd_report *r = (struct mt7915_mcu_rdd_report *)rdd_report;
	static const char * const radar_type_name[] = {
		[RADAR_TYPE_FCC_1_JP_1] = "FCC-1/JP-1",
		[RADAR_TYPE_FCC_2] = "FCC-2",
		[RADAR_TYPE_FCC_3] = "FCC-3",
		[RADAR_TYPE_FCC_4] = "FCC-4",
		[RADAR_TYPE_FCC_6] = "FCC-6",
		[RADAR_TYPE_ETSI_1] = "ETSI-1",
		[RADAR_TYPE_ETSI_2] = "ETSI-2",
		[RADAR_TYPE_ETSI_3] = "ETSI-3",
		[RADAR_TYPE_ETSI_4] = "ETSI-4",
		[RADAR_TYPE_ETSI_5_2PRI] = "ETSI-5 (2 PRI)",
		[RADAR_TYPE_ETSI_5_3PRI] = "ETSI-5 (3 PRI)",
		[RADAR_TYPE_ETSI_6_2PRI] = "ETSI-6 (2 PRI)",
		[RADAR_TYPE_ETSI_6_3PRI] = "ETSI-6 (3 PRI)",
		[RADAR_TYPE_JP_2] = "JP-2",
		[RADAR_TYPE_JP_3] = "JP-3",
		[RADAR_TYPE_JP_4] = "JP-4",
		[RADAR_TYPE_KR_1] = "KR-1",
		[RADAR_TYPE_KR_2] = "KR-2",
		[RADAR_TYPE_KR_3] = "KR-3",
	};
	const struct sw_radar_type *type;
	bool prf_radar_detect;

	if (!r || !dev->rdd_log_en)
		return;

	mt7915_mcu_dump_radar_pulse_buffer(dev, r->hw_pulse_num, HW_PULSE,
					   (struct pulse_buffer *)r->hw_pulse);

	prf_radar_detect = r->constant_prf_detected || r->staggered_prf_detected;
	if (!r->long_detected && !prf_radar_detect)
		return;

	dev_info(dev->mt76.dev, "RDD-%d: Long pulse radar is %s\n",
		 r->rdd_idx, r->long_detected ? "detected" : "NOT detected");
	dev_info(dev->mt76.dev,
		 "Long Pulse Number = %d (MIN FCC5 LPN = 8)\n", r->out_lpn);
	mt7915_mcu_dump_radar_pulse_buffer(dev, r->long_pulse_num, LONG_PULSE,
					   (struct pulse_buffer *)r->long_pulse);

	if (prf_radar_detect) {
		if (r->radar_type_idx >= RADAR_TYPE_NUM) {
			dev_err(dev->mt76.dev, "Invalid radar type: %d\n", r->radar_type_idx);
			return;
		}

		type = &radar_type[r->radar_type_idx];
		dev_info(dev->mt76.dev,
			 "RDD-%d: Periodic radar (RT-%d (%s), Staggered: %s) is detected\n",
			 r->rdd_idx, r->radar_type_idx, radar_type_name[r->radar_type_idx],
			 r->staggered_prf_detected ? "true" : "false");
	} else {
		type = &radar_type[RADAR_TYPE_NUM];
		dev_info(dev->mt76.dev, "RDD-%d: No Periodic radar is detected\n", r->rdd_idx);
	}
	dev_info(dev->mt76.dev, "Short Pulse Number = %d\n", r->out_spn);
	dev_info(dev->mt76.dev, "Constant PRF Radar: Pulse Number = %d (min = %d max = %d)\n",
		 r->out_crpn, type->crpn_min, type->crpn_min);
	dev_info(dev->mt76.dev, "Constant PRF Radar: Pulse Ratio = %d / %d (min = %d / 32)\n",
		 r->out_crpn, r->periodic_pulse_num, type->crpr_min);
	dev_info(dev->mt76.dev, "Constant PRF Radar: Pulse Width = %d (min = %d max = %d)\n",
		 r->out_crpw, type->pw_min, type->pw_max);
	dev_info(dev->mt76.dev, "Constant PRF Radar: Common PRI = %d (min = %d max = %d)\n",
		 r->out_pri_const, type->pri_min, type->pri_max);
	dev_info(dev->mt76.dev, "Constant PRF Radar: Burst Number = %d (min = %d max = %d)\n",
		 r->out_crbn, type->crbn_min, type->crbn_max);
	dev_info(dev->mt76.dev, "Staggered PRF Radar: Pulse Number = %d (min = %d max = %d)\n",
		 r->out_stgpn, type->stgpn_min, type->stgpn_max);
	dev_info(dev->mt76.dev, "Staggered PRF Radar: Pulse Ratio = %d / %d (min = %d / 32)\n",
		 r->out_stgpn, r->periodic_pulse_num, type->stgpr_min);
	dev_info(dev->mt76.dev, "Staggered PRF Radar: Pulse Width = %d (min = %d max = %d)\n",
		 r->out_stgpw, type->pw_min, type->pw_max);
	dev_info(dev->mt76.dev, "Staggered PRF Radar: 1st Common PRI = %d (min = %d max = %d)\n",
		 r->out_pri_stg[0], type->pri_min, type->pri_max * 3);
	dev_info(dev->mt76.dev, "Staggered PRF Radar: 2nd Common PRI = %d (min = %d max = %d)\n",
		 r->out_pri_stg[1], type->pri_min, type->pri_max * 3);
	dev_info(dev->mt76.dev, "Staggered PRF Radar: 3rd Common PRI = %d\n", r->out_pri_stg[2]);
	dev_info(dev->mt76.dev, "Staggered PRF Radar: PRI diff (2nd - 1st) = %d\n",
		 r->out_pri_stg_dmin);
	mt7915_mcu_dump_radar_pulse_buffer(dev, r->periodic_pulse_num, SHORT_PULSE,
					   (struct pulse_buffer *)r->periodic_pulse);
}
