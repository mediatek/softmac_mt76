// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (C) 2026 MediaTek Inc.
 */

#include "mcu.h"
#include "mt7996.h"
#include "mtk_debug.h"
#include "muldf.h"

enum ring_type {
	RING_TYPE_H2SDO = 0,
	RING_TYPE_H2WM,
	RING_TYPE_MAC2H,
	RING_TYPE_WM2H,
	RING_TYPE_SDO2H,
	RING_TYPE_SDO2MAC,
	RING_TYPE_WM2MAC,
	RING_TYPE_MAC2WM,
	RING_TYPE_UNKNOWN
};

struct ring_desc {
	const char *name;
	u32 addr;
	enum ring_type type;
};

static const struct ring_desc host_p0_ring_desc[] = {
	{ "T16:FWDL Both",		 WF_WFDMA_HOST_DMA0_WPDMA_TX_RING16_CTRL0_ADDR, RING_TYPE_H2WM },
	{ "T17:Cmd(H2WM) Both",		 WF_WFDMA_HOST_DMA0_WPDMA_TX_RING17_CTRL0_ADDR, RING_TYPE_H2WM },
#ifdef TCSUPPORT_NPU_WIFI_OFFLOAD
	{ "T18:TXD0(H2SDO) AP",		 WF_WFDMA_EXT_WRAP_CSR_WED_TX0_CTRL0_ADDR,	RING_TYPE_H2SDO },
	{ "T19:TXD1(H2SDO) AP",		 WF_WFDMA_EXT_WRAP_CSR_WED_TX1_CTRL0_ADDR,	RING_TYPE_H2SDO },
#else
	{ "T18:TXD0(H2SDO) AP",		 WF_WFDMA_HOST_DMA0_WPDMA_TX_RING18_CTRL0_ADDR, RING_TYPE_H2SDO },
	{ "T19:TXD1(H2SDO) AP",		 WF_WFDMA_HOST_DMA0_WPDMA_TX_RING19_CTRL0_ADDR, RING_TYPE_H2SDO },
#endif
	{ "R0:Event(WM2H) Both",	 WF_WFDMA_HOST_DMA0_WPDMA_RX_RING0_CTRL0_ADDR,  RING_TYPE_WM2H },
	{ "R1:Event(SDO2H) AP",		 WF_WFDMA_HOST_DMA0_WPDMA_RX_RING1_CTRL0_ADDR,  RING_TYPE_SDO2H },
	{ "R4:Data0(MAC2H) Both",	 WF_WFDMA_HOST_DMA0_WPDMA_RX_RING4_CTRL0_ADDR,  RING_TYPE_MAC2H },
	{ "R5:Data1(MAC2H) Both",	 WF_WFDMA_HOST_DMA0_WPDMA_RX_RING5_CTRL0_ADDR,  RING_TYPE_MAC2H },
	{ "R6:TxDone0(MAC2H) Both",	 WF_WFDMA_HOST_DMA0_WPDMA_RX_RING6_CTRL0_ADDR,  RING_TYPE_MAC2H },
	{ "R8:BUF0(MAC2H) Both",	 WF_WFDMA_HOST_DMA0_WPDMA_RX_RING8_CTRL0_ADDR,  RING_TYPE_MAC2H },
	{ "R9:BUF1(MAC2H) Both",	 WF_WFDMA_HOST_DMA0_WPDMA_RX_RING9_CTRL0_ADDR,  RING_TYPE_MAC2H },
	{ "R10:MSDU_PG(MAC2H) Both",	 WF_WFDMA_HOST_DMA0_WPDMA_RX_RING10_CTRL0_ADDR, RING_TYPE_MAC2H },
	{ "RRO:IND_CMD(MAC2H) Both",	 WF_RRO_TOP_IND_CMD_0_CTRL0_ADDR,		RING_TYPE_MAC2H },
	{ "RRO:Data0(MAC2H) Both",	 WF_RRO_TOP_RX_RING_AP_0_CTRL0_ADDR,		RING_TYPE_MAC2H },
	{ "RRO:Data1(MAC2H) Both",	 WF_RRO_TOP_RX_RING_AP_1_CTRL0_ADDR,		RING_TYPE_MAC2H },
	{ "RRO:Data2(MAC2H) Both",	 WF_RRO_TOP_RX_RING_AP_2_CTRL0_ADDR,		RING_TYPE_MAC2H },
	{ "RRO:Data3(MAC2H) Both",	 WF_RRO_TOP_RX_RING_AP_3_CTRL0_ADDR,		RING_TYPE_MAC2H },
};

static const struct ring_desc host_p1_ring_desc[] = {
	{ "T21:TXD1(H2SDO) AP",		WF_WFDMA_HOST_DMA0_PCIE1_WPDMA_TX_RING21_CTRL0_ADDR, RING_TYPE_H2SDO },
	{ "T22:TXD?(H2SDO) AP",		WF_WFDMA_HOST_DMA0_PCIE1_WPDMA_TX_RING22_CTRL0_ADDR, RING_TYPE_H2SDO },
	{ "R5:Data1(MAC2H) Both",	WF_WFDMA_HOST_DMA0_PCIE1_WPDMA_RX_RING5_CTRL0_ADDR,  RING_TYPE_MAC2H },
	{ "R7:TxDone1(MAC2H) Both",	WF_WFDMA_HOST_DMA0_PCIE1_WPDMA_RX_RING7_CTRL0_ADDR,  RING_TYPE_MAC2H },
	{ "R9:RRO Data1(MAC2H) Both",	WF_WFDMA_HOST_DMA0_PCIE1_WPDMA_RX_RING9_CTRL0_ADDR,  RING_TYPE_MAC2H },
};

static const struct ring_desc mcu_p0_ring_desc[] = {
	{ "T0:Event(WM2H) Both",	WF_WFDMA_MCU_DMA0_WPDMA_TX_RING0_CTRL0_ADDR,  RING_TYPE_WM2H },
	{ "T1:Event(SDO2H) AP",		WF_WFDMA_MCU_DMA0_WPDMA_TX_RING1_CTRL0_ADDR,  RING_TYPE_SDO2H },
	{ "T4:TXD(WM2MAC) Both",	WF_WFDMA_MCU_DMA0_WPDMA_TX_RING4_CTRL0_ADDR,  RING_TYPE_WM2MAC },
	{ "T5:TXCMD(WM2MAC) Both",	WF_WFDMA_MCU_DMA0_WPDMA_TX_RING5_CTRL0_ADDR,  RING_TYPE_WM2MAC },
	{ "T6:TXD(SDO2MAC) AP",		WF_WFDMA_MCU_DMA0_WPDMA_TX_RING6_CTRL0_ADDR,  RING_TYPE_SDO2MAC },
	{ "R0:FWDL Both",		WF_WFDMA_MCU_DMA0_WPDMA_RX_RING1_CTRL0_ADDR,  RING_TYPE_H2WM },
	{ "R1:Cmd(H2WM) Both",		WF_WFDMA_MCU_DMA0_WPDMA_RX_RING1_CTRL0_ADDR,  RING_TYPE_H2WM },
	{ "R2:TXD0(H2SDO) AP",		WF_WFDMA_MCU_DMA0_WPDMA_RX_RING2_CTRL0_ADDR,  RING_TYPE_H2SDO },
	{ "R3:TXD1(H2SDO) AP",		WF_WFDMA_MCU_DMA0_WPDMA_RX_RING3_CTRL0_ADDR,  RING_TYPE_H2SDO },
	{ "R5:Data0(MAC2WM) Both",	WF_WFDMA_MCU_DMA0_WPDMA_RX_RING5_CTRL0_ADDR,  RING_TYPE_MAC2WM },
	{ "R6:TxDone(MAC2WM) Both",	WF_WFDMA_MCU_DMA0_WPDMA_RX_RING6_CTRL0_ADDR,  RING_TYPE_MAC2WM },
	{ "R7:SPL/RPT(MAC2WM) Both",	WF_WFDMA_MCU_DMA0_WPDMA_RX_RING7_CTRL0_ADDR,  RING_TYPE_MAC2WM },
	{ "R9:Data1(MAC2WM) Both",	WF_WFDMA_MCU_DMA0_WPDMA_RX_RING9_CTRL0_ADDR,  RING_TYPE_MAC2WM },
};

#define CHECK_IDX(idx, arr) \
	do { \
		if ((idx) >= ARRAY_SIZE(arr)) { \
			mt76_mul_dbg(&dev->mt76,"index out of bound for " #arr); \
			continue; \
		} \
	} while (0)

static void
fill_dma_ring_info(struct mt7996_dev *dev, const char *name, u32 base,
		   struct queue_info *queue, bool is_tx)
{
	u32 addr, cnt, cidx, didx;

	addr = mt76_rr(dev, base);
	cnt = mt76_get_field(dev, base + 4, GENMASK(11, 0));
	cidx = mt76_get_field(dev, base + 8, GENMASK(11, 0));
	didx = mt76_get_field(dev, base + 12, GENMASK(11, 0));

	strscpy(queue->name, name, sizeof(queue->name));
	queue->tail = cidx;
	queue->head = didx;
	if (is_tx)
		queue->pkt_num = (addr == 0) ? 0 :
			(cidx >= didx) ? (cidx - didx) : (cidx - didx + cnt);
	else
		queue->pkt_num = (addr == 0) ? 0 :
			(didx > cidx) ? (didx - cidx - 1) : (didx - cidx + cnt - 1);
}

static void
fill_hostdma_rings(struct mt7996_dev *dev, struct consys_hostdma *hostdma,
		   const struct ring_desc *desc, size_t count)
{
	u8 h2sdo_idx = 0, h2wm_idx = 0, mac2h_idx = 0, wm2h_idx = 0, sdo2h_idx = 0;
	size_t j = 0;

	for (j = 0; j < count; j++) {
		struct queue_info *queue_info = NULL;

		switch (desc[j].type) {
		case RING_TYPE_H2SDO:
			CHECK_IDX(h2sdo_idx, hostdma->h2sdo_txq);
			queue_info = &hostdma->h2sdo_txq[h2sdo_idx++];
			fill_dma_ring_info(dev, desc[j].name, desc[j].addr, queue_info, true);
			break;
		case RING_TYPE_H2WM:
			CHECK_IDX(h2wm_idx, hostdma->h2wm_txq);
			queue_info = &hostdma->h2wm_txq[h2wm_idx++];
			fill_dma_ring_info(dev, desc[j].name, desc[j].addr, queue_info, true);
			break;
		case RING_TYPE_MAC2H:
			CHECK_IDX(mac2h_idx, hostdma->mac2h_rxq);
			queue_info = &hostdma->mac2h_rxq[mac2h_idx++];
			fill_dma_ring_info(dev, desc[j].name, desc[j].addr, queue_info, false);
			break;
		case RING_TYPE_SDO2H:
			CHECK_IDX(sdo2h_idx, hostdma->sdo2h_rxq);
			queue_info = &hostdma->sdo2h_rxq[sdo2h_idx++];
			fill_dma_ring_info(dev, desc[j].name, desc[j].addr, queue_info, false);
			break;
		case RING_TYPE_WM2H:
			CHECK_IDX(wm2h_idx, hostdma->wm2h_rxq);
			queue_info = &hostdma->wm2h_rxq[wm2h_idx++];
			fill_dma_ring_info(dev, desc[j].name, desc[j].addr, queue_info, false);
			break;
		default:
			mt76_mul_dbg(&dev->mt76, "Unknown ring type for %s", desc[j].name);
			break;
		}
	}
}

static void
fill_mcudma_rings(struct mt7996_dev *dev, struct consys_mcudma *mcudma,
		  const struct ring_desc *desc, size_t count)
{
	u8 sdo2h_idx = 0, sdo2mac_idx = 0, wm2h_idx = 0, wm2mac_idx = 0;
	u8 mac2wm_idx = 0, h2sdo_idx = 0, h2wm_idx = 0;
	size_t j = 0;

	for (j = 0; j < count; j++) {
		struct queue_info *queue_info = NULL;

		switch (desc[j].type) {
		case RING_TYPE_SDO2H:
			CHECK_IDX(sdo2h_idx, mcudma->sdo2h_txq);
			queue_info = &mcudma->sdo2h_txq[sdo2h_idx++];
			fill_dma_ring_info(dev, desc[j].name, desc[j].addr, queue_info, true);
			break;
		case RING_TYPE_SDO2MAC:
			CHECK_IDX(sdo2mac_idx, mcudma->sdo2mac_txq);
			queue_info = &mcudma->sdo2mac_txq[sdo2mac_idx++];
			fill_dma_ring_info(dev, desc[j].name, desc[j].addr, queue_info, true);
			break;
		case RING_TYPE_WM2H:
			CHECK_IDX(wm2h_idx, mcudma->wm2h_txq);
			queue_info = &mcudma->wm2h_txq[wm2h_idx++];
			fill_dma_ring_info(dev, desc[j].name, desc[j].addr, queue_info, true);
			break;
		case RING_TYPE_WM2MAC:
			CHECK_IDX(wm2mac_idx, mcudma->wm2mac_txq);
			queue_info = &mcudma->wm2mac_txq[wm2mac_idx++];
			fill_dma_ring_info(dev, desc[j].name, desc[j].addr, queue_info, true);
			break;
		case RING_TYPE_MAC2WM:
			CHECK_IDX(mac2wm_idx, mcudma->mac2wm_rxq);
			queue_info = &mcudma->mac2wm_rxq[mac2wm_idx++];
			fill_dma_ring_info(dev, desc[j].name, desc[j].addr, queue_info, false);
			break;
		case RING_TYPE_H2SDO:
			CHECK_IDX(h2sdo_idx, mcudma->h2sdo_rxq);
			queue_info = &mcudma->h2sdo_rxq[h2sdo_idx++];
			fill_dma_ring_info(dev, desc[j].name, desc[j].addr, queue_info, false);
			break;
		case RING_TYPE_H2WM:
			CHECK_IDX(h2wm_idx, mcudma->h2wm_rxq);
			queue_info = &mcudma->h2wm_rxq[h2wm_idx++];
			fill_dma_ring_info(dev, desc[j].name, desc[j].addr, queue_info, false);
			break;
		default:
			mt76_mul_dbg(&dev->mt76, "Unknown ring type for %s", desc[j].name);
			break;
		}
	}
}

static int
mt7996_muldf_wpdma_info_tlv(struct muldf_query_data *data, struct mt7996_dev *dev)
{
	struct consys_wpdma_info *wpdma_info;
	struct muldf_tlv *tlv;
	const struct ring_desc *ring_desc = NULL;
	size_t ring_count = 0, i = 0;

	tlv = mt76_muldf_query_add_tlv(data, CONSYS_WPDMA_INFO, sizeof(*wpdma_info));
	if (!tlv)
		return -ENOMEM;

	wpdma_info = (struct consys_wpdma_info *)tlv;
	/* Fill Host WPDMA info structure with DMA ring information */
	for (i = 0; i < ARRAY_SIZE(wpdma_info->hostdma); i++) {
		struct consys_hostdma *hostdma = &wpdma_info->hostdma[i];

		switch (i) {
		case 0:
			ring_desc = host_p0_ring_desc;
			ring_count = ARRAY_SIZE(host_p0_ring_desc);
			break;
		case 1:
			ring_desc = host_p1_ring_desc;
			ring_count = ARRAY_SIZE(host_p1_ring_desc);
			break;
		default:
			continue;
		}

		fill_hostdma_rings(dev, hostdma, ring_desc, ring_count);
	}

	/* Fill MCU WPDMA info structure with DMA ring information */
	for (i = 0; i < ARRAY_SIZE(wpdma_info->mcudma); i++) {
		struct consys_mcudma *mcudma = &wpdma_info->mcudma[i];

		switch (i) {
		case 0:
			ring_desc = mcu_p0_ring_desc;
			ring_count = ARRAY_SIZE(mcu_p0_ring_desc);
			break;
		default:
			continue;
		}

		fill_mcudma_rings(dev, mcudma, ring_desc, ring_count);
	}

	return 0;
}

static int
mt7996_muldf_phy_info_tlv(struct muldf_query_data *data, struct mt7996_dev *dev)
{
	struct consys_phy_info *phy_info;
	struct mt7996_phy *phy;
	struct muldf_tlv *tlv;

	tlv = mt76_muldf_query_add_tlv(data, CONSYS_PHY_INFO, sizeof(*phy_info));
	if (!tlv)
		return -ENOMEM;

	phy_info = (struct consys_phy_info *)tlv;
	mt7996_for_each_phy(dev, phy) {
		struct mt76_mib_stats *mib = &phy->mib;
		struct consys_phy_cnt *phy_cnt;

		phy_cnt = &phy_info->phy_cnt[phy->mt76->band_idx];
		phy_cnt->channel_idle_cnt = mib->channel_idle_cnt;
		phy_cnt->rx_mpdu_cnt = mib->rx_mpdu_cnt;
		phy_cnt->rx_fcs_err_cnt = mib->fcs_err_cnt;
		phy_cnt->rx_fifo_full_cnt = mib->rx_fifo_full_cnt;
	}

	return 0;
}

static int
mt7996_muldf_query(struct mt76_dev *mdev, struct muldf_query_data *data)
{
	struct mt7996_dev *dev = container_of(mdev, struct mt7996_dev, mt76);
	int ret;

	ret = mt7996_muldf_wpdma_info_tlv(data, dev);
	if (ret)
		return ret;

	ret = mt7996_muldf_phy_info_tlv(data, dev);
	if (ret)
		return ret;

	return 0;
}

int mt7996_muldf_init(struct mt7996_dev *dev)
{
	/* currently support event monitor only on mt7990 chipsets */
	if (is_mt7990(&dev->mt76))
		dev->mt76.muldf.query = mt7996_muldf_query;

	return mt76_muldf_init(&dev->mt76, dev->debugfs_dir);
}

void mt7996_muldf_canary_monitor_event(struct mt7996_dev *dev,
				       struct sk_buff *skb)
{
	struct tlv *tlv;
	int len, i;

	/* skip mcu rxd and fixed length */
	skb_pull(skb, 44 + sizeof(struct canary_monitor_event));
	tlv = (struct tlv *)skb->data;
	len = skb->len;

	while (len > 0 && le16_to_cpu(tlv->len) <= len) {
		u16 tag = le16_to_cpu(tlv->tag);
		u16 tag_len = le16_to_cpu(tlv->len);

		switch (tag) {
		case UNI_CANARY_MONITOR_TXPWR_RPT:
			mt76_mul_dbg(&dev->mt76, "[CM] UniEvent txpwr report");
			break;
		case UNI_CANARY_MONITOR_COMMON_INFO: {
			struct cm_common_info *common_info;

			common_info = (void *)tlv;
			mt76_mul_dbg(&dev->mt76, "[CM] UniEvent common Info - len(%u),"
				     "errcode(%u), systime(%u), module(%u), sec(%u),"
				     "usec(%u), seq(%u), tlvpresent(%u)",
				     tag_len, common_info->err_code,
				     common_info->cur_systime, common_info->mod,
				     common_info->utc_sec, common_info->utc_usec,
				     common_info->seq_num, common_info->tlv_present);
			break;
		}
		case UNI_CANARY_MONITOR_TASK_INFO:
			mt76_mul_dbg(&dev->mt76, "[CM] UniEvent task Info");
			break;
		case UNI_CANARY_MONITOR_SEC_INFO:
			mt76_mul_dbg(&dev->mt76, "[CM] UniEvent security info");
			break;
		case UNI_CANARY_MONITOR_MAC_QUEUE_INFO: {
			struct canary_monitor_mac_queue_info_event *queue_info;
			struct cm_mac_queue_info *entry;

			queue_info = (void *)tlv;
			for (i = 0; i < le16_to_cpu(queue_info->count); i++) {
				entry = &queue_info->entry[i];
				mt76_mul_dbg(&dev->mt76, "[CM] UniEvent mac queue info - len(%u),"
					     "ucQueType(%u), ucQueIdx(%u), ucBand(%u),"
					     "ucBssIdx(%u), u2HeadFid(%u)",
					     tag_len, entry->q_type, entry->q_idx,
					     entry->band, entry->bss_idx,
					     le16_to_cpu(entry->head_fid));

				mt76_muldf_monitor_mac_queue_check(&dev->mt76, entry);
			}
			break;
		}
		case UNI_CANARY_MONITOR_STATE_MACHINE_INFO: {
			struct canary_monitor_state_info_event *state_info;

			state_info = (void *)tlv;
			mt76_mul_dbg(&dev->mt76, "[CM] UniEvent state machine info - len(%u),"
				     "ucCrIdx(%u), ucBssIdx(%u), u4StateMachineVal(%u)",
				     tag_len, state_info->cr_idx,
				     state_info->bss_idx, state_info->val);
			/* TBD */
			break;
		}
		case UNI_CANARY_MONITOR_STATE_MAC_PAUSE_INFO: {
			struct canary_monitor_mac_pause_info_event *pause_info;

			pause_info = (void *)tlv;
			for (i = 0; i < le16_to_cpu(pause_info->count); i++) {
				struct cm_mac_pause_info *entry = &pause_info->entry[i];

				mt76_mul_dbg(&dev->mt76, "[CM] UniEvent state mac pause info -"
					     "len(%u), u2Wcid(%u), u2PauseReason(%u), ucACNum(%u)",
					     le16_to_cpu(pause_info->len),
					     le16_to_cpu(entry->wcid),
					     le16_to_cpu(entry->reason), entry->ac_num);
			}
			/* TBD */
			break;
		}
		default:
			mt76_mul_dbg(&dev->mt76, "[CM] invalid event tag = %d", tag);
			break;
		}

		len -= tag_len;
		tlv = (struct tlv *)((u8 *)(tlv) + tag_len);
	}
}
