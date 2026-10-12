/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/* Copyright (C) 2020-2026 MediaTek Inc. */

#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/inet.h>
#include "mt7915.h"
#include "mcu.h"
#include "mac.h"
#include "mtk_mcu.h"

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
