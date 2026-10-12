/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (C) 2026 MediaTek Inc.
 */

#include "mt7999.h"
#include "mcu.h"
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
	{"T16:FWDL Both",		 WF_P0_WFDMA_WPDMA_TX_RING16_CTRL0_ADDR, RING_TYPE_H2WM},
	{"T17:Cmd(H2WM) Both",		 WF_P0_WFDMA_WPDMA_TX_RING17_CTRL0_ADDR, RING_TYPE_H2WM},
	{"T18:TXD0(H2SDO) AP",		 WF_P0_WFDMA_WPDMA_TX_RING18_CTRL0_ADDR, RING_TYPE_H2SDO},
	{"T19:TXD1(H2SDO) AP",		 WF_P0_WFDMA_WPDMA_TX_RING19_CTRL0_ADDR, RING_TYPE_H2SDO},
	{"R0:Event(WM2H) Both",		 WF_P0_WFDMA_WPDMA_RX_RING0_CTRL0_ADDR,  RING_TYPE_WM2H},
	{"R1:Event(SDO2H) AP",		 WF_P0_WFDMA_WPDMA_RX_RING1_CTRL0_ADDR,  RING_TYPE_SDO2H},
	{"R4:Data0(MAC2H) Both",	 WF_P0_WFDMA_WPDMA_RX_RING4_CTRL0_ADDR,  RING_TYPE_MAC2H},
	{"R5:Data1(MAC2H) Both",	 WF_P0_WFDMA_WPDMA_RX_RING5_CTRL0_ADDR,  RING_TYPE_MAC2H},
	{"R6:TxDone0(MAC2H) Both",	 WF_P0_WFDMA_WPDMA_RX_RING6_CTRL0_ADDR,  RING_TYPE_MAC2H},
	{"R7:TxDone1(MAC2H) Both",	 WF_P0_WFDMA_WPDMA_RX_RING7_CTRL0_ADDR,  RING_TYPE_MAC2H},
	{"R8:RRO Data0(MAC2H) Both",	 WF_P0_WFDMA_WPDMA_RX_RING8_CTRL0_ADDR,  RING_TYPE_MAC2H},
	{"R9:RRO Data1(MAC2H) Both",	 WF_P0_WFDMA_WPDMA_RX_RING9_CTRL0_ADDR,  RING_TYPE_MAC2H},
	{"R11:ICS(MAC2H) Both",		 WF_P0_WFDMA_WPDMA_RX_RING11_CTRL0_ADDR, RING_TYPE_MAC2H},
};

static const struct ring_desc host_p1_ring_desc[] = {
	{"T21:TXD1(H2SDO) AP",		WF_P1_WFDMA_WPDMA_TX_RING21_CTRL0_ADDR, RING_TYPE_H2SDO},
	{"T22:TXD?(H2SDO) AP",		WF_P1_WFDMA_WPDMA_TX_RING22_CTRL0_ADDR, RING_TYPE_H2SDO},
	{"R5:Data1(MAC2H) Both",	WF_P1_WFDMA_WPDMA_RX_RING5_CTRL0_ADDR,  RING_TYPE_MAC2H},
	{"R7:TxDone1(MAC2H) Both",	WF_P1_WFDMA_WPDMA_RX_RING7_CTRL0_ADDR,  RING_TYPE_MAC2H},
	{"R9:RRO Data1(MAC2H) Both",	WF_P1_WFDMA_WPDMA_RX_RING9_CTRL0_ADDR,  RING_TYPE_MAC2H},
};

static const struct ring_desc mcu_p0_ring_desc[] = {
	{"T0:Event(WM2H) Both",		WF_M0_WFDMA_WPDMA_TX_RING0_CTRL0_ADDR,  RING_TYPE_WM2H},
	{"T1:Event(SDO2H) AP",		WF_M0_WFDMA_WPDMA_TX_RING1_CTRL0_ADDR,  RING_TYPE_SDO2H},
	{"T4:TXD(WM2MAC) Both",		WF_M0_WFDMA_WPDMA_TX_RING4_CTRL0_ADDR,  RING_TYPE_WM2MAC},
	{"T5:TXCMD(WM2MAC) Both",	WF_M0_WFDMA_WPDMA_TX_RING5_CTRL0_ADDR,  RING_TYPE_WM2MAC},
	{"T6:TXD(SDO2MAC) AP",		WF_M0_WFDMA_WPDMA_TX_RING6_CTRL0_ADDR,  RING_TYPE_SDO2MAC},
	{"R0:FWDL Both",		WF_M0_WFDMA_WPDMA_RX_RING0_CTRL0_ADDR,  RING_TYPE_H2WM},
	{"R1:Cmd(H2WM) Both",		WF_M0_WFDMA_WPDMA_RX_RING1_CTRL0_ADDR,  RING_TYPE_H2WM},
	{"R2:TXD0(H2SDO) AP",		WF_M0_WFDMA_WPDMA_RX_RING2_CTRL0_ADDR,  RING_TYPE_H2SDO},
	{"R3:TXD1(H2SDO) AP",		WF_M0_WFDMA_WPDMA_RX_RING3_CTRL0_ADDR,  RING_TYPE_H2SDO},
	{"R5:Data0(MAC2WM) Both",	WF_M0_WFDMA_WPDMA_RX_RING5_CTRL0_ADDR,  RING_TYPE_MAC2WM},
	{"R6:TxDone(MAC2WM) Both",	WF_M0_WFDMA_WPDMA_RX_RING6_CTRL0_ADDR,  RING_TYPE_MAC2WM},
	{"R7:SPL/RPT(MAC2WM) Both",	WF_M0_WFDMA_WPDMA_RX_RING7_CTRL0_ADDR,  RING_TYPE_MAC2WM},
	{"R8:Data1(MAC2WM) Both",	WF_M0_WFDMA_WPDMA_RX_RING8_CTRL0_ADDR,  RING_TYPE_MAC2WM},
};

#define CHECK_IDX(idx, arr) \
	do { \
		if ((idx) >= ARRAY_SIZE(arr)) { \
			mt76_mul_dbg(&dev->mt76,"index out of bound for " #arr); \
			continue; \
		} \
	} while (0)

static void
fill_dma_ring_info(struct mt7999_dev *dev, const char *name, u32 base,
		   struct queue_info *queue)
{
	u32 addr, ctrl2, ctrl3;

	addr = mt76_rr(dev, base);
	ctrl2 = mt76_get_field(dev, base + 8, GENMASK(11, 0));
	ctrl3 = mt76_get_field(dev, base + 12, GENMASK(11, 0));

	strscpy(queue->name, name, sizeof(queue->name));
	queue->tail = FIELD_GET(GENMASK(11, 0), ctrl2);
	queue->head = FIELD_GET(GENMASK(11, 0), ctrl3);
	queue->pkt_num = FIELD_GET(GENMASK(27, 16), ctrl3);
}

static void
fill_hostdma_rings(struct mt7999_dev *dev, struct consys_hostdma *hostdma,
		   const struct ring_desc *desc, size_t count)
{
	u8 h2sdo_idx = 0, h2wm_idx = 0, mac2h_idx = 0, wm2h_idx = 0, sdo2h_idx = 0;
	size_t j;

	for (j = 0; j < count; j++) {
		struct queue_info *queue_info = NULL;

		switch (desc[j].type) {
		case RING_TYPE_H2SDO:
			CHECK_IDX(h2sdo_idx, hostdma->h2sdo_txq);
			queue_info = &hostdma->h2sdo_txq[h2sdo_idx++];
			break;
		case RING_TYPE_H2WM:
			CHECK_IDX(h2wm_idx, hostdma->h2wm_txq);
			queue_info = &hostdma->h2wm_txq[h2wm_idx++];
			break;
		case RING_TYPE_MAC2H:
			CHECK_IDX(mac2h_idx, hostdma->mac2h_rxq);
			queue_info = &hostdma->mac2h_rxq[mac2h_idx++];
			break;
		case RING_TYPE_SDO2H:
			CHECK_IDX(sdo2h_idx, hostdma->sdo2h_rxq);
			queue_info = &hostdma->sdo2h_rxq[sdo2h_idx++];
			break;
		case RING_TYPE_WM2H:
			CHECK_IDX(wm2h_idx, hostdma->wm2h_rxq);
			queue_info = &hostdma->wm2h_rxq[wm2h_idx++];
			break;
		default:
			mt76_mul_dbg(&dev->mt76, "Unknown ring type for %s", desc[j].name);
			continue;
		}
		fill_dma_ring_info(dev, desc[j].name, desc[j].addr, queue_info);
	}
}

static void
fill_mcudma_rings(struct mt7999_dev *dev, struct consys_mcudma *mcudma,
		  const struct ring_desc *desc, size_t count)
{
	u8 sdo2h_idx = 0, sdo2mac_idx = 0, wm2h_idx = 0, wm2mac_idx = 0;
	u8 mac2wm_idx = 0, h2sdo_idx = 0, h2wm_idx = 0;
	size_t j;

	for (j = 0; j < count; j++) {
		struct queue_info *queue_info = NULL;

		switch (desc[j].type) {
		case RING_TYPE_SDO2H:
			CHECK_IDX(sdo2h_idx, mcudma->sdo2h_txq);
			queue_info = &mcudma->sdo2h_txq[sdo2h_idx++];
			break;
		case RING_TYPE_SDO2MAC:
			CHECK_IDX(sdo2mac_idx, mcudma->sdo2mac_txq);
			queue_info = &mcudma->sdo2mac_txq[sdo2mac_idx++];
			break;
		case RING_TYPE_WM2H:
			CHECK_IDX(wm2h_idx, mcudma->wm2h_txq);
			queue_info = &mcudma->wm2h_txq[wm2h_idx++];
			break;
		case RING_TYPE_WM2MAC:
			CHECK_IDX(wm2mac_idx, mcudma->wm2mac_txq);
			queue_info = &mcudma->wm2mac_txq[wm2mac_idx++];
			break;
		case RING_TYPE_MAC2WM:
			CHECK_IDX(mac2wm_idx, mcudma->mac2wm_rxq);
			queue_info = &mcudma->mac2wm_rxq[mac2wm_idx++];
			break;
		case RING_TYPE_H2SDO:
			CHECK_IDX(h2sdo_idx, mcudma->h2sdo_rxq);
			queue_info = &mcudma->h2sdo_rxq[h2sdo_idx++];
			break;
		case RING_TYPE_H2WM:
			CHECK_IDX(h2wm_idx, mcudma->h2wm_rxq);
			queue_info = &mcudma->h2wm_rxq[h2wm_idx++];
			break;
		default:
			mt76_mul_dbg(&dev->mt76, "Unknown ring type for %s", desc[j].name);
			continue;
		}
		fill_dma_ring_info(dev, desc[j].name, desc[j].addr, queue_info);
	}
}

static int
mt7999_muldf_wpdma_info_tlv(struct muldf_query_data *data, struct mt7999_dev *dev)
{
	struct consys_wpdma_info *wpdma_info;
	struct muldf_tlv *tlv;
	const struct ring_desc *ring_desc;
	size_t ring_count, i;

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
mt7999_muldf_phy_info_tlv(struct muldf_query_data *data, struct mt7999_dev *dev)
{
	struct consys_phy_info *phy_info;
	struct mt7999_phy *phy;
	struct muldf_tlv *tlv;

	tlv = mt76_muldf_query_add_tlv(data, CONSYS_PHY_INFO, sizeof(*phy_info));
	if (!tlv)
		return -ENOMEM;

	phy_info = (struct consys_phy_info *)tlv;
	mt7999_for_each_phy(dev, phy) {
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
mt7999_muldf_query(struct mt76_dev *mdev, struct muldf_query_data *data)
{
	struct mt7999_dev *dev = container_of(mdev, struct mt7999_dev, mt76);
	int ret;

	ret = mt7999_muldf_wpdma_info_tlv(data, dev);
	if (ret)
		return ret;

	ret = mt7999_muldf_phy_info_tlv(data, dev);
	if (ret)
		return ret;

	return 0;
}

int mt7999_muldf_init(struct mt7999_dev *dev)
{
	dev->mt76.muldf.query = mt7999_muldf_query;

	return mt76_muldf_init(&dev->mt76, dev->debugfs_dir);
}

static void
cm_handle_common_tlv(struct mt7999_dev *dev, struct tlv *tlv)
{
	struct cm_common_info *info = (struct cm_common_info *)tlv;

	mt76_mul_dbg(&dev->mt76, "[CM] Common info - Mod(0x%x) Sub(0x%x) Err(0x%x)"
		     " Time(%u) Sec(%u) USec(%u) Seq(%u) Tlv(%u)",
		     le32_to_cpu(info->mod), le32_to_cpu(info->sub_mod),
		     le32_to_cpu(info->err_code), le32_to_cpu(info->cur_systime),
		     le32_to_cpu(info->utc_sec), le32_to_cpu(info->utc_usec),
		     le32_to_cpu(info->seq_num), info->tlv_present);
}

static void
cm_handle_mac_tlv(struct mt7999_dev *dev, struct tlv *tlv)
{
	struct cm_tlv *ctlv = (struct cm_tlv *)tlv;
	u16 sub_mod = le16_to_cpu(ctlv->fail_type);
	int i;

	switch (sub_mod) {
	case CM_SUBMOD_MAC_QUEUE: {
		struct cm_mac_queue_info_batch *batch = (void *)tlv->data;

		for (i = 0; i < le16_to_cpu(batch->count); i++) {
			struct cm_mac_queue_info *entry = &batch->entry[i];

			mt76_mul_dbg(&dev->mt76, "[CM] MAC queue - QType(0x%x) QIdx(0x%x)"
				     " Bn(0x%x) Bss(0x%x) Head(0x%x) TxdAcHead(0x%x)",
				     entry->q_type, entry->q_idx,
				     entry->band, entry->bss_idx,
				     le16_to_cpu(entry->head_fid),
				     le16_to_cpu(entry->txd_ac_head_fid));

			mt76_muldf_monitor_mac_queue_check(&dev->mt76, entry);
		}
		break;
	}
	case CM_SUBMOD_MAC_STATE_MACHINE: {
		struct cm_mac_sm_info *sm = (void *)ctlv->data;

		mt76_mul_dbg(&dev->mt76, "[CM] MAC state machine - CrIdx(%x) BssIdx(%x)"
			     " DetectedModule(%x) Val(%x)",
			     sm->cr_idx, sm->bss_idx,
			     sm->detected_module, le32_to_cpu(sm->val));

		/* TODO: state machine check */
		break;
	}
	case CM_SUBMOD_MAC_PAUSE: {
		struct cm_mac_pause_info_batch *batch = (void *)ctlv->data;

		for (i = 0; i < le16_to_cpu(batch->count); i++) {
			struct cm_mac_pause_info *entry = &batch->entry[i];

			mt76_mul_dbg(&dev->mt76, "[CM] MAC pause - Wcid(0x%x)"
				     " PauseReason(0x%x) ACNum(0x%x)",
				     le16_to_cpu(entry->wcid), le16_to_cpu(entry->reason),
				     entry->ac_num);

			/* TODO: mac pause check */
		}
		break;
	}
	default:
		mt76_mul_dbg(&dev->mt76, "[CM] MAC unknown submodule(%u)", sub_mod);
		break;
	}
}

static void
cm_handle_phy_tlv(struct mt7999_dev *dev, struct tlv *tlv)
{
	struct cm_tlv *ctlv = (struct cm_tlv *)tlv;
	u16 sub_mod = le16_to_cpu(ctlv->fail_type);

	switch (sub_mod) {
	case CM_SUBMOD_RA: {
		struct cm_phy_ra_info *ra = (void *)ctlv->data;

		mt76_mul_dbg(&dev->mt76, "[CM] PHY RA - Wcid(%u) FailReason(%u)",
			     le16_to_cpu(ra->wcid), le16_to_cpu(ra->fail_reason));
		break;
	}
	default:
		mt76_mul_dbg(&dev->mt76, "[CM] PHY unknown submodule(%u)", sub_mod);
		break;
	}
}

static void
cm_handle_dma_tlv(struct mt7999_dev *dev, struct tlv *tlv)
{
	struct cm_tlv *ctlv = (struct cm_tlv *)tlv;
	u16 sub_mod = le16_to_cpu(ctlv->fail_type);

	switch (sub_mod) {
	case CM_SUBMOD_WFDMA:
		mt76_mul_dbg(&dev->mt76, "[CM] DMA WFDMA event");
		break;
	default:
		mt76_mul_dbg(&dev->mt76, "[CM] DMA unknown submodule(%u)", sub_mod);
		break;
	}
}

void mt7999_muldf_canary_monitor_event(struct mt7999_dev *dev,
				       struct sk_buff *skb)
{
	struct tlv *tlv;
	int len;

	/* skip mcu rxd and fixed length */
	skb_pull(skb, 44 + sizeof(struct canary_monitor_event));
	tlv = (struct tlv *)skb->data;
	len = skb->len;

	while (len > 0 && le16_to_cpu(tlv->len) <= len) {
		u16 tag = le16_to_cpu(tlv->tag);
		u16 tag_len = le16_to_cpu(tlv->len);

		switch (tag) {
		case CM_MOD_COMMON:
			cm_handle_common_tlv(dev, tlv);
			break;
		case CM_MOD_MAC:
			cm_handle_mac_tlv(dev, tlv);
			break;
		case CM_MOD_PHY:
			cm_handle_phy_tlv(dev, tlv);
			break;
		case CM_MOD_DMA:
			cm_handle_dma_tlv(dev, tlv);
			break;
		default:
			mt76_mul_dbg(&dev->mt76, "[CM] unhandled module(0x%x)", tag);
			break;
		}

		len -= tag_len;
		tlv = (struct tlv *)((u8 *)(tlv) + tag_len);
	}
}
