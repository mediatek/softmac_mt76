// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2023 MediaTek Inc.
 */

#include <linux/firmware.h>
#include <linux/fs.h>
#include "mt7996.h"
#include "mcu.h"
#include "mac.h"
#include "mtk_mcu.h"

#ifdef CONFIG_MTK_DEBUG

static struct tlv *
__mt7996_mcu_add_uni_tlv(struct sk_buff *skb, u16 tag, u16 len)
{
	struct tlv *ptlv, tlv = {
		.tag = cpu_to_le16(tag),
		.len = cpu_to_le16(len),
	};

	ptlv = skb_put(skb, len);
	memcpy(ptlv, &tlv, sizeof(tlv));

	return ptlv;
}

int mt7996_mcu_set_txbf_internal(struct mt7996_phy *phy, u8 action, int idx, bool bfer)
{
	struct mt7996_dev *dev = phy->dev;
#define MT7996_MTK_BF_MAX_SIZE	sizeof(struct bf_starec_read)
	struct uni_header hdr;
	struct sk_buff *skb;
	struct tlv *tlv;
	int len = sizeof(hdr) + MT7996_MTK_BF_MAX_SIZE;

	memset(&hdr, 0, sizeof(hdr));

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));

	switch (action) {
	case BF_PFMU_TAG_READ: {
		struct bf_pfmu_tag *req;

		tlv = __mt7996_mcu_add_uni_tlv(skb, action, sizeof(*req));
		req = (struct bf_pfmu_tag *)tlv;
		req->pfmu_id = idx;
		req->bfer = bfer;
		req->band_idx = phy->mt76->band_idx;
		break;
	}
	case BF_STA_REC_READ: {
		struct bf_starec_read *req;

		tlv = __mt7996_mcu_add_uni_tlv(skb, action, sizeof(*req));
		req = (struct bf_starec_read *)tlv;
		req->wlan_idx = idx;
		break;
	}
	case BF_FBRPT_DBG_INFO_READ: {
		struct bf_fbk_rpt_info *req;

		if (idx != 0) {
			dev_info(dev->mt76.dev, "Invalid input");
			return 0;
		}

		tlv = __mt7996_mcu_add_uni_tlv(skb, action, sizeof(*req));
		req = (struct bf_fbk_rpt_info *)tlv;
		req->action = idx;
		req->band_idx = phy->mt76->band_idx;
		break;
	}
	default:
		return -EINVAL;
	}

	return mt76_mcu_skb_send_msg(&phy->dev->mt76, skb, MCU_WM_UNI_CMD(BF), false);
}

int mt7996_mcu_set_txbf_snd_info(struct mt7996_dev *dev, void *para)
{
	char *buf = (char *)para;
	__le16 input[5] = {0};
	u8 recv_arg = 0;
	struct bf_txsnd_info *req;
	struct uni_header hdr;
	struct sk_buff *skb;
	struct tlv *tlv;
	int len = sizeof(hdr) + MT7996_MTK_BF_MAX_SIZE;

	memset(&hdr, 0, sizeof(hdr));

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, len);
	if (!skb)
		return -ENOMEM;

	skb_put_data(skb, &hdr, sizeof(hdr));

	recv_arg = sscanf(buf, "%hx:%hx:%hx:%hx:%hx", &input[0], &input[1], &input[2],
						      &input[3], &input[4]);

	if (!recv_arg)
		return -EINVAL;

	tlv = __mt7996_mcu_add_uni_tlv(skb, BF_TXSND_INFO, sizeof(*req));
	req = (struct bf_txsnd_info *)tlv;
	req->action = input[0];

	switch (req->action) {
	case BF_SND_READ_INFO: {
		req->read_clr = input[1];
		break;
	}
	case BF_SND_CFG_OPT: {
		req->vht_opt = input[1];
		req->he_opt = input[2];
		req->glo_opt = input[3];
		break;
	}
	case BF_SND_CFG_INTV: {
		req->wlan_idx = input[1];
		req->snd_intv = input[2];
		break;
	}
	case BF_SND_STA_STOP: {
		req->wlan_idx = input[1];
		req->snd_stop = input[2];
		break;
	}
	case BF_SND_CFG_MAX_STA: {
		req->max_snd_stas = input[1];
		break;
	}
	case BF_SND_CFG_BFRP: {
		req->man = input[1];
		req->tx_time = input[2];
		req->mcs = input[3];
		req->ldpc = input[4];
		break;
	}
	case BF_SND_CFG_INF: {
		req->inf = input[1];
		break;
	}
	case BF_SND_CFG_TXOP_SND: {
		req->man = input[1];
		req->ac_queue = input[2];
		req->sxn_protect = input[3];
		req->direct_fbk = input[4];
		break;
	}
	default:
		return -EINVAL;
	}

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, MCU_WM_UNI_CMD(BF), false);
}

static inline void
mt7996_ibf_phase_assign(struct mt7996_dev *dev,
			struct mt7996_ibf_cal_info *cal,
			struct mt7996_txbf_phase *phase)
{
	/* fw return ibf calibrated data with
	 * the mt7996_txbf_phase_info_5g struct for both 2G and 5G.
	 * (return struct mt7992_txbf_phase_info_5g for ibf 2.0)
	 * Therefore, memcpy cannot be used here.
	 */
	if (get_ibf_version(dev) != IBF_VER_2) {
		phase_assign(cal->group, v1, m_t0_h, true);
		phase_assign(cal->group, v1, m_t1_h, true);
		phase_assign(cal->group, v1, m_t2_h, true);
		phase_assign(cal->group, v1, m_t2_h_sx2, false);
		phase_assign_rx_v1(cal->group, v1, r0);
		phase_assign_rx_v1(cal->group, v1, r1);
		phase_assign_rx_v1(cal->group, v1, r2);
		phase_assign_rx_v1(cal->group, v1, r3);
		phase_assign_rx(cal->group, v1, r2_sx2, false);
		phase_assign_rx(cal->group, v1, r3_sx2, false);
		phase_assign(cal->group, v1, r0_reserved, false);
		phase_assign(cal->group, v1, r1_reserved, false);
		phase_assign(cal->group, v1, r2_reserved, false);
		phase_assign(cal->group, v1, r3_reserved, false);
		phase_assign(cal->group, v1, r2_sx2_reserved, false);
		phase_assign(cal->group, v1, r3_sx2_reserved, false);
	} else {
		phase_assign(cal->group, v2, m_t0_h, true);
		phase_assign(cal->group, v2, m_t1_h, true);
		phase_assign(cal->group, v2, m_t2_h, true);
		if (cal->group) {
			phase->v2.phase_5g.m_t3_h = cal->v2.phase_5g.m_t3_h;
			mt76_tm_dbg(&dev->mt76, "m_t3_h = %d", phase->v2.phase_5g.m_t3_h);
		}
		phase_assign_rx_ext(cal->group, v2, r0, true);
		phase_assign_rx_ext(cal->group, v2, r1, true);
		phase_assign_rx_ext(cal->group, v2, r2, true);
		phase_assign_rx_ext(cal->group, v2, r3, true);
		if (cal->group) {
			memcpy(&phase->v2.phase_5g.r4, &cal->v2.phase_5g.r4,
			       sizeof(struct txbf_rx_phase_ext));
			mt76_tm_dbg(&dev->mt76, "r4.rx_uh = %d", phase->v2.phase_5g.r4.rx_uh);
			mt76_tm_dbg(&dev->mt76, "r4.rx_h = %d", phase->v2.phase_5g.r4.rx_h);
			mt76_tm_dbg(&dev->mt76, "r4.rx_mh = %d", phase->v2.phase_5g.r4.rx_mh);
			mt76_tm_dbg(&dev->mt76, "r4.rx_m = %d", phase->v2.phase_5g.r4.rx_m);
			mt76_tm_dbg(&dev->mt76, "r4.rx_l = %d", phase->v2.phase_5g.r4.rx_l);
			mt76_tm_dbg(&dev->mt76, "r4.rx_ul = %d", phase->v2.phase_5g.r4.rx_ul);
		}
	}
}

void
mt7996_mcu_rx_bf_event(struct mt7996_dev *dev, struct sk_buff *skb)
{
	struct mt7996_mcu_bf_basic_event *event;

	event = (struct mt7996_mcu_bf_basic_event *)skb->data;

	dev_info(dev->mt76.dev, " bf_event tag = %d\n", event->tag);

	switch (event->tag) {
	case UNI_EVENT_BF_PFMU_TAG: {

		struct mt7996_pfmu_tag_event *tag;
		u32 *raw_t1, *raw_t2;

		tag = (struct mt7996_pfmu_tag_event *) skb->data;

		raw_t1 = (u32 *)&tag->t1;
		raw_t2 = (u32 *)&tag->t2;

		dev_info(dev->mt76.dev, "=================== TXBf Profile Tag1 Info ==================\n");
		dev_info(dev->mt76.dev,
			 "DW0 = 0x%08x, DW1 = 0x%08x, DW2 = 0x%08x\n",
			 raw_t1[0], raw_t1[1], raw_t1[2]);
		dev_info(dev->mt76.dev,
			 "DW4 = 0x%08x, DW5 = 0x%08x, DW6 = 0x%08x\n\n",
			 raw_t1[3], raw_t1[4], raw_t1[5]);
		dev_info(dev->mt76.dev, "PFMU ID = %d              Invalid status = %d\n",
			 tag->t1.pfmu_idx, tag->t1.invalid_prof);
		dev_info(dev->mt76.dev, "iBf/eBf = %d\n\n", tag->t1.ebf);
		dev_info(dev->mt76.dev, "DBW   = %d\n", tag->t1.data_bw);
		dev_info(dev->mt76.dev, "SU/MU = %d\n", tag->t1.is_mu);
		dev_info(dev->mt76.dev,
			 "nrow = %d, ncol = %d, ng = %d, LM = %d, CodeBook = %d MobCalEn = %d\n",
			 tag->t1.nr, tag->t1.nc, tag->t1.ngroup, tag->t1.lm, tag->t1.codebook,
			 tag->t1.mob_cal_en);

		if (tag->t1.lm <= BF_LM_HE)
			dev_info(dev->mt76.dev, "RU start = %d, RU end = %d\n",
				 tag->t1.field.ru_start_id, tag->t1.field.ru_end_id);
		else
			dev_info(dev->mt76.dev, "PartialBW = %d\n",
				 tag->t1.bw_info.partial_bw_info);

		dev_info(dev->mt76.dev, "Mem Col1 = %d, Mem Row1 = %d, Mem Col2 = %d, Mem Row2 = %d\n",
			 tag->t1.col_id1, tag->t1.row_id1, tag->t1.col_id2, tag->t1.row_id2);
		dev_info(dev->mt76.dev, "Mem Col3 = %d, Mem Row3 = %d, Mem Col4 = %d, Mem Row4 = %d\n\n",
			 tag->t1.col_id3, tag->t1.row_id3, tag->t1.col_id4, tag->t1.row_id4);
		dev_info(dev->mt76.dev,
			 "STS0_SNR = 0x%02x, STS1_SNR = 0x%02x, STS2_SNR = 0x%02x, STS3_SNR = 0x%02x\n",
			 tag->t1.snr_sts0, tag->t1.snr_sts1, tag->t1.snr_sts2, tag->t1.snr_sts3);
		dev_info(dev->mt76.dev,
			 "STS4_SNR = 0x%02x, STS5_SNR = 0x%02x, STS6_SNR = 0x%02x, STS7_SNR = 0x%02x\n",
			 tag->t1.snr_sts4, tag->t1.snr_sts5, tag->t1.snr_sts6, tag->t1.snr_sts7);
		dev_info(dev->mt76.dev, "=============================================================\n");

		dev_info(dev->mt76.dev, "=================== TXBf Profile Tag2 Info ==================\n");
		dev_info(dev->mt76.dev,
			 "DW0 = 0x%08x, DW1 = 0x%08x, DW2 = 0x%08x\n",
			 raw_t2[0], raw_t2[1], raw_t2[2]);
		dev_info(dev->mt76.dev,
			 "DW3 = 0x%08x, DW4 = 0x%08x, DW5 = 0x%08x\n\n",
			 raw_t2[3], raw_t2[4], raw_t2[5]);
		dev_info(dev->mt76.dev, "Smart antenna ID = 0x%x,  SE index = %d\n",
			 tag->t2.smart_ant, tag->t2.se_idx);
		dev_info(dev->mt76.dev, "Timeout = 0x%x\n", tag->t2.ibf_timeout);
		dev_info(dev->mt76.dev, "Desired BW = %d, Desired Ncol = %d, Desired Nrow = %d\n",
			 tag->t2.ibf_data_bw, tag->t2.ibf_nc, tag->t2.ibf_nr);
		dev_info(dev->mt76.dev, "Desired RU Allocation = %d\n", tag->t2.ibf_ru);
		dev_info(dev->mt76.dev, "Mobility DeltaT = %d, Mobility LQ = %d\n",
			 tag->t2.mob_delta_t, tag->t2.mob_lq_result);
		dev_info(dev->mt76.dev, "=============================================================\n");
		break;
	}
	case UNI_EVENT_BF_STAREC: {

		struct mt7996_mcu_bf_starec_read *r;

		r = (struct mt7996_mcu_bf_starec_read *)skb->data;
		dev_info(dev->mt76.dev, "=================== BF StaRec ===================\n"
					"rStaRecBf.u2PfmuId      = %d\n"
					"rStaRecBf.fgSU_MU       = %d\n"
					"rStaRecBf.u1TxBfCap     = %d\n"
					"rStaRecBf.ucSoundingPhy = %d\n"
					"rStaRecBf.ucNdpaRate    = %d\n"
					"rStaRecBf.ucNdpRate     = %d\n"
					"rStaRecBf.ucReptPollRate= %d\n"
					"rStaRecBf.ucTxMode      = %d\n"
					"rStaRecBf.ucNc          = %d\n"
					"rStaRecBf.ucNr          = %d\n"
					"rStaRecBf.ucCBW         = %d\n"
					"rStaRecBf.ucMemRequire20M = %d\n"
					"rStaRecBf.ucMemRow0     = %d\n"
					"rStaRecBf.ucMemCol0     = %d\n"
					"rStaRecBf.ucMemRow1     = %d\n"
					"rStaRecBf.ucMemCol1     = %d\n"
					"rStaRecBf.ucMemRow2     = %d\n"
					"rStaRecBf.ucMemCol2     = %d\n"
					"rStaRecBf.ucMemRow3     = %d\n"
					"rStaRecBf.ucMemCol3     = %d\n",
					r->pfmu_id,
					r->is_su_mu,
					r->txbf_cap,
					r->sounding_phy,
					r->ndpa_rate,
					r->ndp_rate,
					r->rpt_poll_rate,
					r->tx_mode,
					r->nc,
					r->nr,
					r->bw,
					r->mem_require_20m,
					r->mem_row0,
					r->mem_col0,
					r->mem_row1,
					r->mem_col1,
					r->mem_row2,
					r->mem_col2,
					r->mem_row3,
					r->mem_col3);

		dev_info(dev->mt76.dev, "rStaRecBf.u2SmartAnt    = 0x%x\n"
					"rStaRecBf.ucSEIdx       = %d\n"
					"rStaRecBf.uciBfTimeOut  = 0x%x\n"
					"rStaRecBf.uciBfDBW      = %d\n"
					"rStaRecBf.uciBfNcol     = %d\n"
					"rStaRecBf.uciBfNrow     = %d\n"
					"rStaRecBf.nr_bw160      = %d\n"
					"rStaRecBf.nc_bw160 	  = %d\n"
					"rStaRecBf.ru_start_idx  = %d\n"
					"rStaRecBf.ru_end_idx 	  = %d\n"
					"rStaRecBf.trigger_su 	  = %d\n"
					"rStaRecBf.trigger_mu 	  = %d\n"
					"rStaRecBf.ng16_su 	  = %d\n"
					"rStaRecBf.ng16_mu 	  = %d\n"
					"rStaRecBf.codebook42_su = %d\n"
					"rStaRecBf.codebook75_mu = %d\n"
					"rStaRecBf.he_ltf 	      = %d\n"
					"======================================\n",
					r->smart_ant,
					r->se_idx,
					r->bf_timeout,
					r->bf_dbw,
					r->bf_ncol,
					r->bf_nrow,
					r->nr_lt_bw80,
					r->nc_lt_bw80,
					r->ru_start_idx,
					r->ru_end_idx,
					r->trigger_su,
					r->trigger_mu,
					r->ng16_su,
					r->ng16_mu,
					r->codebook42_su,
					r->codebook75_mu,
					r->he_ltf);
		break;
	}
	case UNI_EVENT_BF_FBK_INFO: {
		struct mt7996_mcu_txbf_fbk_info *info;
		__le32 total, i;

		info = (struct mt7996_mcu_txbf_fbk_info *)skb->data;

		total = info->u4PFMUWRDoneCnt + info->u4PFMUWRFailCnt;
		total += info->u4PFMUWRTimeoutFreeCnt + info->u4FbRptPktDropCnt;

		dev_info(dev->mt76.dev, "\n");
		dev_info(dev->mt76.dev, "\x1b[32m =================================\x1b[m\n");
		dev_info(dev->mt76.dev, "\x1b[32m PFMUWRDoneCnt              = %u\x1b[m\n",
			info->u4PFMUWRDoneCnt);
		dev_info(dev->mt76.dev, "\x1b[32m PFMUWRFailCnt              = %u\x1b[m\n",
			info->u4PFMUWRFailCnt);
		dev_info(dev->mt76.dev, "\x1b[32m PFMUWRTimeOutCnt           = %u\x1b[m\n",
			info->u4PFMUWRTimeOutCnt);
		dev_info(dev->mt76.dev, "\x1b[32m PFMUWRTimeoutFreeCnt       = %u\x1b[m\n",
			info->u4PFMUWRTimeoutFreeCnt);
		dev_info(dev->mt76.dev, "\x1b[32m FbRptPktDropCnt            = %u\x1b[m\n",
			info->u4FbRptPktDropCnt);
		dev_info(dev->mt76.dev, "\x1b[32m TotalFbRptPkt              = %u\x1b[m\n", total);
		dev_info(dev->mt76.dev, "\x1b[32m PollPFMUIntrStatTimeOut    = %u(micro-sec)\x1b[m\n",
			info->u4PollPFMUIntrStatTimeOut);
		dev_info(dev->mt76.dev, "\x1b[32m FbRptDeQInterval           = %u(milli-sec)\x1b[m\n",
			info->u4DeQInterval);
		dev_info(dev->mt76.dev, "\x1b[32m PktCntInFbRptTimeOutQ      = %u\x1b[m\n",
			info->u4RptPktTimeOutListNum);
		dev_info(dev->mt76.dev, "\x1b[32m PktCntInFbRptQ             = %u\x1b[m\n",
			info->u4RptPktListNum);

		// [ToDo] Check if it is valid entry
		for (i = 0; ((i < 5) && (i < CFG_BF_STA_REC_NUM)); i++) {

			// [ToDo] AID needs to be refined
			dev_info(dev->mt76.dev,"\x1b[32m AID%u  RxFbRptCnt           = %u\x1b[m\n"
				, i, info->au4RxPerStaFbRptCnt[i]);
		}

		break;
	}
	case UNI_EVENT_BF_TXSND_INFO: {
		struct mt7996_mcu_tx_snd_info *info;
		struct uni_event_bf_txsnd_sta_info *snd_sta_info;
		int Idx;
		int max_wtbl_size = mt7996_wtbl_size(dev);

		info = (struct mt7996_mcu_tx_snd_info *)skb->data;
		dev_info(dev->mt76.dev, "=================== Global Setting ===================\n");

		dev_info(dev->mt76.dev, "VhtOpt = 0x%02X, HeOpt = 0x%02X, GloOpt = 0x%02X\n",
			info->vht_opt, info->he_opt, info->glo_opt);

		for (Idx = 0; Idx < BF_SND_CTRL_STA_DWORD_CNT; Idx++) {
			dev_info(dev->mt76.dev, "SuSta[%d] = 0x%08X,", Idx,
				 info->snd_rec_su_sta[Idx]);
			if ((Idx & 0x03) == 0x03)
				dev_info(dev->mt76.dev, "\n");
		}

		if ((Idx & 0x03) != 0x03)
			dev_info(dev->mt76.dev, "\n");


		for (Idx = 0; Idx < BF_SND_CTRL_STA_DWORD_CNT; Idx++) {
			dev_info(dev->mt76.dev, "VhtMuSta[%d] = 0x%08X,", Idx, info->snd_rec_vht_mu_sta[Idx]);
			if ((Idx & 0x03) == 0x03)
				dev_info(dev->mt76.dev, "\n");
		}

		if ((Idx & 0x03) != 0x03)
			dev_info(dev->mt76.dev, "\n");

		for (Idx = 0; Idx < BF_SND_CTRL_STA_DWORD_CNT; Idx++) {
			dev_info(dev->mt76.dev, "HeTBSta[%d] = 0x%08X,", Idx, info->snd_rec_he_tb_sta[Idx]);
			if ((Idx & 0x03) == 0x03)
				dev_info(dev->mt76.dev, "\n");
		}

		if ((Idx & 0x03) != 0x03)
			dev_info(dev->mt76.dev, "\n");

		for (Idx = 0; Idx < BF_SND_CTRL_STA_DWORD_CNT; Idx++) {
			dev_info(dev->mt76.dev, "EhtTBSta[%d] = 0x%08X,", Idx, info->snd_rec_eht_tb_sta[Idx]);
			if ((Idx & 0x03) == 0x03)
				dev_info(dev->mt76.dev, "\n");
		}

		if ((Idx & 0x03) != 0x03)
			dev_info(dev->mt76.dev, "\n");

		for (Idx = 0; Idx < CFG_WIFI_RAM_BAND_NUM; Idx++) {
			dev_info(dev->mt76.dev, "Band%u:\n", Idx);
			dev_info(dev->mt76.dev, "	 Wlan Idx For VHT MC Sounding = %u\n", info->wlan_idx_for_mc_snd[Idx]);
			dev_info(dev->mt76.dev, "	 Wlan Idx For HE TB Sounding = %u\n", info->wlan_idx_for_he_tb_snd[Idx]);
			dev_info(dev->mt76.dev, "	 Wlan Idx For EHT TB Sounding = %u\n", info->wlan_idx_for_eht_tb_snd[Idx]);
		}

		dev_info(dev->mt76.dev, "ULLen = %d, ULMcs = %d, ULLDCP = %d\n",
			info->ul_length, info->mcs, info->ldpc);

		dev_info(dev->mt76.dev, "=================== STA Info ===================\n");

		for (Idx = 1; (Idx < 5 && (Idx < CFG_BF_STA_REC_NUM)); Idx++) {
			snd_sta_info = &info->snd_sta_info[Idx];
			dev_info(dev->mt76.dev, "Idx%2u Interval = %d, interval counter = %d, TxCnt = %d, StopReason = 0x%02X\n",
				Idx,
				snd_sta_info->snd_intv,
				snd_sta_info->snd_intv_cnt,
				snd_sta_info->snd_tx_cnt,
				snd_sta_info->snd_stop_reason);
		}

		dev_info(dev->mt76.dev, "=================== STA Info Connected ===================\n");
		// [ToDo] How to iterate and get AID info of station
		// Check UniEventBFCtrlTxSndHandle() on Logan

		//hardcode max_wtbl_size as 5
		max_wtbl_size = 5;
		for (Idx = 1; ((Idx < max_wtbl_size) && (Idx < CFG_BF_STA_REC_NUM)); Idx++) {

			// [ToDo] We do not show AID info here
			snd_sta_info = &info->snd_sta_info[Idx];
			dev_info(dev->mt76.dev, " Interval = %d (%u ms), interval counter = %d (%u ms), TxCnt = %d, StopReason = 0x%02X\n",
				snd_sta_info->snd_intv,
				snd_sta_info->snd_intv * 10,
				snd_sta_info->snd_intv_cnt,
				snd_sta_info->snd_intv_cnt * 10,
				snd_sta_info->snd_tx_cnt,
				snd_sta_info->snd_stop_reason);
		}

		dev_info(dev->mt76.dev, "======================================\n");

		break;
	}
	case UNI_EVENT_BF_CAL_PHASE: {
		struct mt7996_ibf_cal_info *cal;
		struct mt7996_txbf_phase *phase;
		union {
			struct mt7996_txbf_phase_out v1;
			struct mt7992_txbf_phase_out v2;
		} phase_out;
		int phase_out_size = sizeof(struct mt7996_txbf_phase_out);

		cal = (struct mt7996_ibf_cal_info *)skb->data;
		phase = (struct mt7996_txbf_phase *)dev->test.txbf_phase_cal;
		if (get_ibf_version(dev) == IBF_VER_2)
			phase_out_size = sizeof(struct mt7992_txbf_phase_out);
		memcpy(&phase_out, &cal->buf, phase_out_size);
		switch (cal->cal_type) {
		case IBF_PHASE_CAL_NORMAL:
		case IBF_PHASE_CAL_NORMAL_INSTRUMENT:
			/* Only calibrate group M */
			if (cal->group_l_m_n != GROUP_M)
				break;
			phase = &phase[cal->group];
			phase->status = cal->status;
			mt76_tm_dbg(&dev->mt76, "Calibrated result = %d", phase->status);
			mt76_tm_dbg(&dev->mt76, "Group %d and Group M", cal->group);
			mt7996_ibf_phase_assign(dev, cal, phase);
			break;
		case IBF_PHASE_CAL_VERIFY:
		case IBF_PHASE_CAL_VERIFY_INSTRUMENT:
			mt76_tm_dbg(&dev->mt76, "Verification result = %d", cal->status);
			break;
		default:
			break;
		}

		if (get_ibf_version(dev) == IBF_VER_2) {
			mt76_tm_dbg(&dev->mt76, "c0_uh = %d, c1_uh = %d, c2_uh = %d, c3_uh = %d c4_uh = %d",
				    phase_out.v2.c0_uh, phase_out.v2.c1_uh, phase_out.v2.c2_uh,
				    phase_out.v2.c3_uh, phase_out.v2.c4_uh);
			mt76_tm_dbg(&dev->mt76, "c0_h = %d, c1_h = %d, c2_h = %d, c3_h = %d c4_h = %d",
				    phase_out.v2.c0_h, phase_out.v2.c1_h, phase_out.v2.c2_h,
				    phase_out.v2.c3_h, phase_out.v2.c4_h);
			mt76_tm_dbg(&dev->mt76, "c0_mh = %d, c1_mh = %d, c2_mh = %d, c3_mh = %d c4_mh = %d",
				    phase_out.v2.c0_mh, phase_out.v2.c1_mh, phase_out.v2.c2_mh,
				    phase_out.v2.c3_mh, phase_out.v2.c4_mh);
			mt76_tm_dbg(&dev->mt76, "c0_m = %d, c1_m = %d, c2_m = %d, c3_m = %d c4_m = %d",
				    phase_out.v2.c0_m, phase_out.v2.c1_m, phase_out.v2.c2_m,
				    phase_out.v2.c3_m, phase_out.v2.c4_m);
			mt76_tm_dbg(&dev->mt76, "c0_l = %d, c1_l = %d, c2_l = %d, c3_l = %d c4_l = %d",
				    phase_out.v2.c0_l, phase_out.v2.c1_l, phase_out.v2.c2_l,
				    phase_out.v2.c3_l, phase_out.v2.c4_l);
		} else {
			mt76_tm_dbg(&dev->mt76, "c0_uh = %d, c1_uh = %d, c2_uh = %d, c3_uh = %d",
				    phase_out.v1.c0_uh, phase_out.v1.c1_uh,
				    phase_out.v1.c2_uh, phase_out.v1.c3_uh);
			mt76_tm_dbg(&dev->mt76, "c0_h = %d, c1_h = %d, c2_h = %d, c3_h = %d",
				    phase_out.v1.c0_h, phase_out.v1.c1_h,
				    phase_out.v1.c2_h, phase_out.v1.c3_h);
			mt76_tm_dbg(&dev->mt76, "c0_mh = %d, c1_mh = %d, c2_mh = %d, c3_mh = %d",
				    phase_out.v1.c0_mh, phase_out.v1.c1_mh,
				    phase_out.v1.c2_mh, phase_out.v1.c3_mh);
			mt76_tm_dbg(&dev->mt76, "c0_m = %d, c1_m = %d, c2_m = %d, c3_m = %d",
				    phase_out.v1.c0_m, phase_out.v1.c1_m,
				    phase_out.v1.c2_m, phase_out.v1.c3_m);
			mt76_tm_dbg(&dev->mt76, "c0_l = %d, c1_l = %d, c2_l = %d, c3_l = %d",
				    phase_out.v1.c0_l, phase_out.v1.c1_l,
				    phase_out.v1.c2_l, phase_out.v1.c3_l);
		}

		break;
	}
	default:
		dev_info(dev->mt76.dev, "%s: unknown bf event tag %d\n",
			 __func__, event->tag);
	}

	wake_up(&dev->mt76.mcu.wait);
}

#endif
