// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (C) 2026 MediaTek Inc.
 */

#include "mt7999.h"
#include "../dma.h"
#include "dma.h"
#include "mac.h"

int mt7999_init_tx_queues(struct mt7999_phy *phy, int idx, int n_desc,
			  int ring_base, struct mtk_wed_device *wed)
{
	struct mt7999_dev *dev = phy->dev;
	u32 flags = MT_QFLAG_CHECK_DDONE;

	if (mtk_wed_device_active(wed)) {
		ring_base += MT_TXQ_ID(0) * MT_RING_SIZE;
		idx -= MT_TXQ_ID(0);

		if (wed == &dev->mt76.mmio.wed_hif2)
			flags |= MT_WED_Q_TX(0);
		else
			flags |= MT_WED_Q_TX(idx);
	}

	if (mt76_npu_device_active(&dev->mt76))
		flags |= MT_NPU_Q_TX(phy->mt76->band_idx);

	return mt76_connac_init_tx_queues(phy->mt76, idx, n_desc,
					  ring_base, wed, flags);
}

static int mt7999_poll_tx(struct napi_struct *napi, int budget)
{
	struct mt7999_dev *dev;

	dev = container_of(napi, struct mt7999_dev, mt76.tx_napi);

	mt76_connac_tx_cleanup(&dev->mt76);
	if (napi_complete_done(napi, 0))
		mt7999_irq_enable(dev, MT_INT_TX_DONE_MCU);

	return 0;
}

static void mt7999_dma_config(struct mt7999_dev *dev)
{
	struct mtk_wed_device *wed = &dev->mt76.mmio.wed;
#define Q_CONFIG(q, int, id) do {			\
	dev->q_int_mask[(q)] = int;			\
	dev->q_id[(q)] = id;				\
	dev->q_valid[(q)] = true;			\
} while (0)

#define MCUQ_CONFIG(q, int, id)	Q_CONFIG(q, (int), (id))
#define RXQ_CONFIG(q, int, id)	Q_CONFIG(__RXQ(q), (int), (id))
#define TXQ_CONFIG(q, int, id)	Q_CONFIG(__TXQ(q), (int), (id))

	/* rx queue */
	RXQ_CONFIG(MT_RXQ_MCU, MT_INT_RX_DONE_WM, MT7999_RXQ_MCU_WM);
	RXQ_CONFIG(MT_RXQ_MCU_WA, MT_INT_RX_DONE_SDO, MT7999_RXQ_MCU_SDO);

	RXQ_CONFIG(MT_RXQ_MAIN, MT_INT_RX_DONE_BAND0, MT7999_RXQ_BAND0);
	RXQ_CONFIG(MT_RXQ_BAND2, MT_INT_RX_DONE_BAND2, MT7999_RXQ_BAND2);

	RXQ_CONFIG(MT_RXQ_TXFREE_SRC0, MT_INT_RX_TXFREE_SRC0, MT7999_RXQ_TXFREE0); /* WED0 */
	RXQ_CONFIG(MT_RXQ_TXFREE_SRC1, MT_INT_RX_TXFREE_SRC1, MT7999_RXQ_TXFREE1); /* WED1 */
	RXQ_CONFIG(MT_RXQ_TXFREE_SRC2, MT_INT_RX_TXFREE_SRC2, MT7999_RXQ_TXFREE2); /* Host Drv */
	RXQ_CONFIG(MT_RXQ_ICS_DATA, MT_INT_RX_DONE_ICS, MT7999_RXQ_ICS);

	/* data tx queue */
	TXQ_CONFIG(0, MT_INT_TX_DONE_BAND0, MT7999_TXQ_BAND0);
	if (dev->hif2) {
		TXQ_CONFIG(1, MT_INT_TX_DONE_BAND1, MT7999_TXQ_BAND1);
		TXQ_CONFIG(2, MT_INT_TX_DONE_BAND2, MT7999_TXQ_BAND2);
	} else {
		TXQ_CONFIG(2, MT_INT_TX_DONE_BAND1, MT7999_TXQ_BAND1);
	}
	/* mcu tx queue */
	MCUQ_CONFIG(MT_MCUQ_FWDL, MT_INT_TX_DONE_FWDL, MT7999_TXQ_FWDL);
	MCUQ_CONFIG(MT_MCUQ_WM, MT_INT_TX_DONE_MCU_WM, MT7999_TXQ_MCU_WM);
	MCUQ_CONFIG(MT_MCUQ_WM_VIP, MT_INT_TX_DONE_MCU_WM_VIP, MT7999_TXQ_MCU_WM_VIP);

	/* WED queues */
	if (mtk_wed_device_active(wed) && mt76_wed_get_ver(wed) == MTK_WED_HW_V3_1)
		RXQ_CONFIG(MT_RXQ_WED_RX_DATA, MT_INT_RX_DONE_WED_RX_DATA,
			   MT7999_TXQ_WED_RX);

	/* RRO queues */
	if (!mt7999_has_hwrro(dev))
		return;

	RXQ_CONFIG(MT_RXQ_RRO_BAND0, MT_INT_RX_DONE_RRO_BAND0, MT7999_RXQ_RRO_BAND0);
	RXQ_CONFIG(MT_RXQ_RRO_BAND2, MT_INT_RX_DONE_RRO_BAND2, MT7999_RXQ_RRO_BAND2);
	if (dev->mt76.hwrro_mode == MT76_HWRRO_V3) {
		RXQ_CONFIG(MT_RXQ_MSDU_PAGE_BAND0, MT_INT_RX_DONE_MSDU_PG_BAND0,
			   MT7999_RXQ_MSDU_PG_BAND0);
		RXQ_CONFIG(MT_RXQ_RRO_IND, MT_INT_RX_DONE_RRO_IND, MT7999_RXQ_RRO_IND);
	} else {
		RXQ_CONFIG(MT_RXQ_RRO_RXDMAD_C, MT_INT_RX_DONE_RRO_RXDMAD_C,
			   MT7999_RXQ_RRO_RXDMAD_C);
	}
}

void mt7999_dma_prefetch(struct mt7999_dev *dev)
{
	const struct __prefetch_config *config_pcie0, *config_pcie1;
	u32 hif1_ofs = MT_WFDMA0_PCIE1(0) - MT_WFDMA0(0);
	u8 size_pcie0, size_pcie1;
	int i;

	switch (mt76_chip(&dev->mt76)) {
	case MT7999_DEVICE_ID:
	default:
		config_pcie0 = mt7999_prefetch_config;
		size_pcie0 = ARRAY_SIZE(mt7999_prefetch_config);
		config_pcie1 = mt7999_prefetch_config_pcie1;
		size_pcie1 = ARRAY_SIZE(mt7999_prefetch_config_pcie1);
		break;
	}


	for (i = 0; i < size_pcie0; i++)
		mt76_wr(dev, config_pcie0[i].base, config_pcie0[i].val);

	if (!dev->hif2)
		return;

	for (i = 0; i < size_pcie1; i++)
		mt76_wr(dev, config_pcie1[i].base + hif1_ofs, config_pcie1[i].val);
}

static void mt7999_dma_disable(struct mt7999_dev *dev, bool reset)
{
	/* disable */
	mt76_clear(dev, MT_WFDMA0_GLO_CFG,
		   MT_WFDMA0_GLO_CFG_TX_DMA_EN |
		   MT_WFDMA0_GLO_CFG_RX_DMA_EN);

	if (dev->hif2) {
		u32 hif1_ofs = MT_WFDMA0_PCIE1(0) - MT_WFDMA0(0);

		mt76_clear(dev, MT_WFDMA0_GLO_CFG + hif1_ofs,
			   MT_WFDMA0_GLO_CFG_TX_DMA_EN |
			   MT_WFDMA0_GLO_CFG_RX_DMA_EN);
	}
	mt7999_irq_disable(dev, ~0);
}

void mt7999_dma_start(struct mt7999_dev *dev, bool reset, bool wed_reset)
{
	struct mtk_wed_device *wed = &dev->mt76.mmio.wed;
	u32 hif1_ofs = 0, irq_mask = 0;
	u8 i;

	if (dev->hif2)
		hif1_ofs = MT_WFDMA0_PCIE1(0) - MT_WFDMA0(0);

	/* enable WFDMA Tx/Rx */
	if (!reset) {
		if (mtk_wed_device_active(wed) && mtk_wed_get_rx_capa(wed))
			mt76_set(dev, MT_WFDMA0_GLO_CFG,
				 MT_WFDMA0_GLO_CFG_TX_DMA_EN);
		else
			mt76_set(dev, MT_WFDMA0_GLO_CFG,
				 MT_WFDMA0_GLO_CFG_TX_DMA_EN |
				 MT_WFDMA0_GLO_CFG_RX_DMA_EN);

		if (dev->hif2)
			mt76_set(dev, MT_WFDMA0_GLO_CFG + hif1_ofs,
				 MT_WFDMA0_GLO_CFG_TX_DMA_EN |
				 MT_WFDMA0_GLO_CFG_RX_DMA_EN);
	}

	for (i = 0; i < MT7999_MAX_QUEUE; i++) {
		if (!dev->q_valid[i] || (dev->q_int_mask[i] & MT_INT_RRO_RX_SKIP))
			continue;

		irq_mask |= dev->q_int_mask[i];
	}

	irq_mask |= MT_INT_MCU_CMD;
	dev->def_irq_mask = irq_mask;

	if (mtk_wed_device_active(wed) && wed_reset) {
		mt76_wr(dev, MT_INT_MASK_CSR, irq_mask);
		mtk_wed_device_start(wed, irq_mask);
	}

	irq_mask = reset ? MT_INT_MCU_CMD : irq_mask;

	mt7999_irq_enable(dev, irq_mask);
	mt7999_irq_disable(dev, 0);
}

static void mt7999_dma_enable(struct mt7999_dev *dev, bool reset)
{
	u32 hif1_ofs = 0;

	if (dev->hif2)
		hif1_ofs = MT_WFDMA0_PCIE1(0) - MT_WFDMA0(0);

	/* Decide pdma addr */
	mt76_clear(dev, MT_WFDMA_HOST_CONFIG, MT_WFDMA_HOST_CONFIG_TXD_48BIT);
	mt76_set(dev, MT_WFDMA_HOST_CONFIG, MT_WFDMA_HOST_CONFIG_TXD_36BIT);

	/* Config pdma outstanding */
	mt76_rmw(dev, MT_WFDMA0_GLO_CFG,
		 MT_WFDMA0_GLO_CFG_WR_OUTSTAND | MT_WFDMA0_GLO_CFG_RD_OUTSTAND,
		 u32_encode_bits(8, MT_WFDMA0_GLO_CFG_WR_OUTSTAND) |
		 u32_encode_bits(50, MT_WFDMA0_GLO_CFG_RD_OUTSTAND));

	/* WFDMA rx threshold */
	mt76_rmw(dev, MT_WFDMA0_RX_DMAD_SDL_45,
		 MT_WFDMA0_RX_DMAD_SDL_LOW | MT_WFDMA0_RX_DMAD_SDL_HIGH,
		 u32_encode_bits(MT7999_RX_DATA_BUF_SIZE, MT_WFDMA0_RX_DMAD_SDL_LOW) |
		 u32_encode_bits(MT7999_RX_DATA_BUF_SIZE, MT_WFDMA0_RX_DMAD_SDL_HIGH));
	mt76_rmw(dev, MT_WFDMA0_RX_DMAD_SDL_67,
		 MT_WFDMA0_RX_DMAD_SDL_LOW | MT_WFDMA0_RX_DMAD_SDL_HIGH,
		 u32_encode_bits(MT7999_RX_TXFREE_BUF_SIZE, MT_WFDMA0_RX_DMAD_SDL_LOW) |
		 u32_encode_bits(MT7999_RX_TXFREE_BUF_SIZE, MT_WFDMA0_RX_DMAD_SDL_HIGH));
	mt76_rmw(dev, MT_WFDMA0_RX_DMAD_SDL_89,
		 MT_WFDMA0_RX_DMAD_SDL_LOW | MT_WFDMA0_RX_DMAD_SDL_HIGH,
		 u32_encode_bits(MT7999_RX_DATA_BUF_SIZE, MT_WFDMA0_RX_DMAD_SDL_LOW) |
		 u32_encode_bits(MT7999_RX_DATA_BUF_SIZE, MT_WFDMA0_RX_DMAD_SDL_HIGH));
	mt76_rmw(dev, MT_WFDMA0_RX_DMAD_SDL_1011,
		 MT_WFDMA0_RX_DMAD_SDL_LOW | MT_WFDMA0_RX_DMAD_SDL_HIGH,
		 u32_encode_bits(MT7999_RX_MSDU_PAGE_BUF_SIZE, MT_WFDMA0_RX_DMAD_SDL_LOW) |
		 u32_encode_bits(MT7999_RX_DATA_BUF_SIZE, MT_WFDMA0_RX_DMAD_SDL_HIGH));
	mt76_rmw(dev, MT_WFDMA0_RX_DMAD_SDL_1213, MT_WFDMA0_RX_DMAD_SDL_LOW,
		 u32_encode_bits(MT7999_RX_TXFREE_BUF_SIZE, MT_WFDMA0_RX_DMAD_SDL_LOW));

	/* configure perfetch settings */
	mt7999_dma_prefetch(dev);

	/*  Config dmy CR for pdma */
	mt76_clear(dev, MT_WFDMA_TRINFO_P0_DMY_CTRL, MT_WFDMA_TRINFO_DMY_AXI_SEL);
	mt76_wr(dev, MT_WFDMA_TRINFO_P0_DMY_AXI_READ, dev->dummy_buff[0].phy_addr);

	/* WFDMA irq priority select */
	mt76_wr(dev, MT_WFDMA0_INT_TX_PRI_SEL, ~0);
	mt76_wr(dev, MT_WFDMA0_INT_RX_PRI_SEL, ~0);
	mt76_set(dev, MT_WFDMA_MSI_CONFIG1, MT_WFDMA_MSI_CONFIG1_DEASSERT);

	/* Clear IRQ status */
	mt76_wr(dev, MT_INT_SOURCE_CSR, ~0);

	if (dev->hif2) {
		mt76_set(dev, MT_WFDMA_HOST_CONFIG,
			 MT_WFDMA_HOST_CONFIG_PDMA_BAND |
			 MT_WFDMA_HOST_CONFIG_BAND2_PCIE1);
		mt76_clear(dev, MT_WFDMA_HOST_CONFIG,
			   MT_WFDMA_HOST_CONFIG_BAND0_PCIE1 |
			   MT_WFDMA_HOST_CONFIG_BAND1_PCIE1);

		mt76_clear(dev, MT_WFDMA_TRINFO_P1_DMY_CTRL,
			   MT_WFDMA_TRINFO_DMY_AXI_SEL);
		mt76_wr(dev, MT_WFDMA_TRINFO_P1_DMY_AXI_READ,
			dev->dummy_buff[1].phy_addr);
		if (mtk_wed_device_active(&dev->mt76.mmio.wed) &&
		    mt7999_has_hwrro(dev)) {
			u32 intr = MT_WFDMA_RX_INT_SEL_RING9 |
				   MT_WFDMA_RX_INT_SEL_RING5;

			mt76_set(dev, MT_WFDMA_RX_INT_PCIE_SEL, intr);
		}
		mt76_rmw(dev, MT_WFDMA0_GLO_CFG + hif1_ofs,
			MT_WFDMA0_GLO_CFG_WR_OUTSTAND | MT_WFDMA0_GLO_CFG_RD_OUTSTAND,
			u32_encode_bits(8, MT_WFDMA0_GLO_CFG_WR_OUTSTAND) |
			u32_encode_bits(50, MT_WFDMA0_GLO_CFG_RD_OUTSTAND));

		mt76_rmw(dev, MT_WFDMA0_RX_DMAD_SDL_45 + hif1_ofs,
			 MT_WFDMA0_RX_DMAD_SDL_HIGH,
			 u32_encode_bits(MT7999_RX_DATA_BUF_SIZE,
					 MT_WFDMA0_RX_DMAD_SDL_HIGH));
		mt76_rmw(dev, MT_WFDMA0_RX_DMAD_SDL_67 + hif1_ofs,
			 MT_WFDMA0_RX_DMAD_SDL_HIGH,
			 u32_encode_bits(MT7999_RX_TXFREE_BUF_SIZE,
					 MT_WFDMA0_RX_DMAD_SDL_HIGH));
		mt76_rmw(dev, MT_WFDMA0_RX_DMAD_SDL_89 + hif1_ofs,
			 MT_WFDMA0_RX_DMAD_SDL_HIGH,
			 u32_encode_bits(MT7999_RX_DATA_BUF_SIZE,
					 MT_WFDMA0_RX_DMAD_SDL_HIGH));
		/* WFDMA irq priority select */
		mt76_wr(dev, MT_WFDMA0_INT_TX_PRI_SEL + hif1_ofs, ~0);
		mt76_wr(dev, MT_WFDMA0_INT_RX_PRI_SEL + hif1_ofs, ~0);
		/* Clear IRQ status */
		mt76_wr(dev, MT_INT1_SOURCE_CSR, ~0);
	}
	mt7999_dma_start(dev, reset, true);
}

void mt7999_dma_rro_start(struct mt7999_dev *dev)
{
	if (mtk_wed_device_active(&dev->mt76.mmio.wed)) {
		mt76_wr(dev, MT_INT_MASK_CSR, dev->def_irq_mask);
		mtk_wed_device_start_hw_rro(&dev->mt76.mmio.wed, dev->def_irq_mask,
					    false);
		mt7999_irq_enable(dev, dev->def_irq_mask);
		return;
	}

	mt76_queue_rx_init(dev, MT_RXQ_RRO_BAND0, mt76_dma_rx_poll);
	mt76_queue_rx_init(dev, MT_RXQ_RRO_BAND2, mt76_dma_rx_poll);
	if (dev->mt76.hwrro_mode == MT76_HWRRO_V3_1) {
		mt76_queue_rx_init(dev, MT_RXQ_RRO_RXDMAD_C,
				   mt76_dma_rx_poll);
	} else {
		mt76_queue_rx_init(dev, MT_RXQ_RRO_IND,
				   mt76_dma_rx_poll);
		mt76_queue_rx_init(dev, MT_RXQ_MSDU_PAGE_BAND0,
				   mt76_dma_rx_poll);
	}

	if (!mt76_npu_device_active(&dev->mt76))
		mt7999_irq_enable(dev, MT_INT_RRO_RX_DONE);
}

static void mt7999_dma_wed_config(struct mt7999_dev *dev)
{
	struct mtk_wed_device *wed = &dev->mt76.mmio.wed;
	struct mtk_wed_device *wed_hif2 = &dev->mt76.mmio.wed_hif2;

	if (mtk_wed_device_active(wed)) {
		dev->mt76.q_rx[MT_RXQ_TXFREE_SRC0].flags = MT_WED_Q_TXFREE;
		dev->mt76.q_rx[MT_RXQ_TXFREE_SRC0].wed = wed;
	}

	if (mtk_wed_device_active(wed_hif2)) {
		dev->mt76.q_rx[MT_RXQ_TXFREE_SRC1].flags = MT_WED_Q_TXFREE;
		dev->mt76.q_rx[MT_RXQ_TXFREE_SRC1].wed = wed_hif2;
	}

	if (!mt7999_has_hwrro(dev))
		return;

	/* wed to host rx data queue */
	if (mtk_wed_device_active(wed)) {
		u8 qid = (mt76_wed_get_ver(wed) == MTK_WED_HW_V3_1) ?
			 MT_RXQ_WED_RX_DATA : MT_RXQ_MAIN;

		dev->mt76.q_rx[qid].flags = MT_WED_Q_RX(0);
		dev->mt76.q_rx[qid].wed = wed;
		if (mt76_wed_get_ver(wed) == MTK_WED_HW_V3) {
			dev->mt76.q_rx[MT_RXQ_BAND2].flags = MT_WED_Q_RX(1);
			dev->mt76.q_rx[MT_RXQ_BAND2].wed = wed;
		}
	}

	/* rx rro data queue for band0 */
	dev->mt76.q_rx[MT_RXQ_RRO_BAND0].flags =
		MT_WED_RRO_Q_DATA(0) | MT_QFLAG_WED_RRO_EN;
	if (mtk_wed_device_active(wed))
		dev->mt76.q_rx[MT_RXQ_RRO_BAND0].wed = wed;

	if (mt7999_band_valid(dev, MT_BAND2)) {
		/* rx rro data queue for band2 */
		dev->mt76.q_rx[MT_RXQ_RRO_BAND2].flags =
			MT_WED_RRO_Q_DATA(1) | MT_QFLAG_WED_RRO_EN;
		if (mtk_wed_device_active(wed))
			dev->mt76.q_rx[MT_RXQ_RRO_BAND2].wed = wed;
	}

	if (dev->mt76.hwrro_mode == MT76_HWRRO_V3_1) {
		/* rxdmad_c */
		dev->mt76.q_rx[MT_RXQ_RRO_RXDMAD_C].flags =
			MT_WED_RRO_Q_RXDMAD_C;
		if (mtk_wed_device_active(wed))
			dev->mt76.q_rx[MT_RXQ_RRO_RXDMAD_C].wed = wed;
		else if (!mt76_npu_device_active(&dev->mt76))
			dev->mt76.q_rx[MT_RXQ_RRO_RXDMAD_C].flags |= MT_QFLAG_EMI_EN;
	}

	/* ind cmd */
	dev->mt76.q_rx[MT_RXQ_RRO_IND].flags = MT_WED_RRO_Q_IND;
	if (mtk_wed_device_active(wed))
		dev->mt76.q_rx[MT_RXQ_RRO_IND].wed = wed;

	/* rx msdu page queue for band0 */
	dev->mt76.q_rx[MT_RXQ_MSDU_PAGE_BAND0].flags =
		MT_WED_RRO_Q_MSDU_PG(0) | MT_QFLAG_WED_RRO_EN;
	if (mtk_wed_device_active(wed))
		dev->mt76.q_rx[MT_RXQ_MSDU_PAGE_BAND0].wed = wed;
}

static void mt7999_dma_dmy_cleanup(struct mt7999_dev *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dev->dummy_buff); i++) {
		if (!dev->dummy_buff[i].ptr)
			continue;

		dmam_free_coherent(dev->mt76.dma_dev,
				   MT7999_DUMMY_BUFF_SIZE,
				   dev->dummy_buff[i].ptr,
				   dev->dummy_buff[i].phy_addr);

		dev->dummy_buff[i].ptr = NULL;
	}
}

static int mt7999_dma_dmy_init(struct mt7999_dev *dev)
{
	void *ptr;
	int i;

	for (i = 0; i < ARRAY_SIZE(dev->dummy_buff); i++) {
		ptr = dmam_alloc_coherent(dev->mt76.dma_dev,
					  MT7999_DUMMY_BUFF_SIZE,
					  &dev->dummy_buff[i].phy_addr,
					  GFP_KERNEL);
		if (!ptr)
			goto out;
		dev->dummy_buff[i].ptr = ptr;
	}
	return 0;

out:
	mt7999_dma_dmy_cleanup(dev);
	return -ENOMEM;
}

#define INIT_TXQ(qid, hif1_ofs) do {						\
	if (dev->q_valid[__TXQ(qid)]) {						\
		ret = mt7999_init_tx_queues(&dev->phy,				\
					    MT_TXQ_ID(qid),			\
					    MT7999_TX_RING_SIZE,		\
					    MT_TXQ_RING_BASE + (hif1_ofs),	\
					    wed);				\
		if (ret)							\
			return ret;						\
	}									\
} while (0)
#define INIT_MCUQ(qid, buf) do {						\
	if (dev->q_valid[qid]) {						\
		ret = mt76_init_mcu_queue(&dev->mt76, qid,			\
					  MT_MCUQ_ID(qid),			\
					  buf, MT_MCUQ_RING_BASE);		\
		if (ret)							\
			return ret;						\
	}									\
} while (0)
#define INIT_RXQ(qid, n_desc, bufsize, ringbase, hif1_ofs) do {			\
	if (dev->q_valid[__RXQ(qid)]) {						\
		ret = mt76_queue_alloc(dev,					\
				       &dev->mt76.q_rx[qid],			\
				       MT_RXQ_ID(qid),				\
				       n_desc,					\
				       bufsize + SKB_DATA_ALIGN(sizeof(struct skb_shared_info)), \
				       (ringbase) + (hif1_ofs));		\
		if (ret)							\
			return ret;						\
	}									\
} while (0)

int mt7999_dma_init(struct mt7999_dev *dev)
{
	struct mtk_wed_device *wed = &dev->mt76.mmio.wed;
	u32 hif1_ofs = 0;
	int ret;

	mt7999_dma_config(dev);
	mt7999_dma_wed_config(dev);
	ret = mt7999_dma_dmy_init(dev);
	if (ret)
		return ret;

	mt76_dma_attach(&dev->mt76);

	if (dev->hif2)
		hif1_ofs = MT_WFDMA0_PCIE1(0) - MT_WFDMA0(0);

	mt7999_dma_disable(dev, true);

	INIT_TXQ(dev->mphy.band_idx, 0);
	INIT_MCUQ(MT_MCUQ_WM, MT7999_TX_MCU_RING_SIZE);
	INIT_MCUQ(MT_MCUQ_WM_VIP, MT7999_TX_MCU_RING_SIZE);
	INIT_MCUQ(MT_MCUQ_FWDL, MT7999_TX_FWDL_RING_SIZE);
	INIT_RXQ(MT_RXQ_MCU, MT7999_RX_MCU_RING_SIZE,
		 MT7999_RX_MCU_BUF_SIZE, MT_RXQ_RING_BASE, 0);
	INIT_RXQ(MT_RXQ_MCU_WA, MT7999_RX_MCU_RING_SIZE_WA,
		 MT7999_RX_MCU_BUF_SIZE, MT_RXQ_RING_BASE, 0);
	INIT_RXQ(MT_RXQ_ICS_DATA, MT7999_RX_RING_SIZE,
		 MT7999_RX_MCU_BUF_SIZE, MT_RXQ_RING_BASE, 0);

	/* Rx Data Ring */
	INIT_RXQ(MT_RXQ_MAIN, MT7999_RX_RING_SIZE,
		 MT7999_RX_DATA_BUF_SIZE, MT_RXQ_RING_BASE, 0);
	INIT_RXQ(MT_RXQ_BAND2, MT7999_RX_RING_SIZE,
		 MT7999_RX_DATA_BUF_SIZE, MT_RXQ_RING_BASE, hif1_ofs);

	/* TxFree Ring */
	INIT_RXQ(MT_RXQ_TXFREE_SRC0, MT7999_RX_MCU_RING_SIZE,
		 MT7999_RX_TXFREE_BUF_SIZE, MT_RXQ_RING_BASE, 0);
	INIT_RXQ(MT_RXQ_TXFREE_SRC1, MT7999_RX_MCU_RING_SIZE,
		 MT7999_RX_TXFREE_BUF_SIZE, MT_RXQ_RING_BASE,
		 mt76_npu_device_active(&dev->mt76) ? 0 : hif1_ofs);
	INIT_RXQ(MT_RXQ_TXFREE_SRC2, MT7999_RX_MCU_RING_SIZE,
		 MT7999_RX_TXFREE_BUF_SIZE, MT_RXQ_RING_BASE, 0);

	/* RRO Data Ring */
	INIT_RXQ(MT_RXQ_RRO_BAND0, MT7999_RX_RING_SIZE,
		 MT7999_RX_DATA_BUF_SIZE, MT_RXQ_RING_BASE, 0);
	INIT_RXQ(MT_RXQ_RRO_BAND2, MT7999_RX_RING_SIZE,
		 MT7999_RX_DATA_BUF_SIZE, MT_RXQ_RING_BASE, hif1_ofs);

	/* WED Rx Ring */
	INIT_RXQ(MT_RXQ_WED_RX_DATA, MT7999_RX_RING_SIZE,
		 MT7999_RX_DATA_BUF_SIZE, MT_TXQ_RING_BASE, 0);

	ret = mt76_init_queues(dev, mt76_dma_rx_poll);
	if (ret < 0)
		return ret;

	ret = mt7999_npu_rx_queues_init(dev);
	if (ret)
		return ret;

	netif_napi_add_tx(dev->mt76.tx_napi_dev, &dev->mt76.tx_napi,
			  mt7999_poll_tx);
	napi_enable(&dev->mt76.tx_napi);

	mt7999_dma_enable(dev, false);

	return 0;
}

int mt7999_dma_rro_init(struct mt7999_dev *dev)
{
	struct mt76_dev *mdev = &dev->mt76;
	int ret;

	/* ind cmd */
	INIT_RXQ(MT_RXQ_RRO_IND, MT7999_RX_RING_SIZE,
		 0, MT_RXQ_RRO_IND_RING_BASE, 0);

	/* rx msdu page queue for band0 */
	INIT_RXQ(MT_RXQ_MSDU_PAGE_BAND0, MT7999_RX_RING_SIZE,
		 MT7999_RX_MSDU_PAGE_BUF_SIZE, MT_RXQ_RING_BASE, 0);

	/* rxdmad_c */
	INIT_RXQ(MT_RXQ_RRO_RXDMAD_C, MT7999_RX_RING_SIZE,
		 MT7999_RX_DATA_BUF_SIZE, MT_RXQ_RRO_AP_RING_BASE, 0);

	if (!mtk_wed_device_active(&mdev->mmio.wed) &&
	    dev->mt76.hwrro_mode == MT76_HWRRO_V3_1) {
		/* We need to set cpu idx pointer before resetting the
		 * EMI queues.
		 */
		mdev->q_rx[MT_RXQ_RRO_RXDMAD_C].emi_cpu_idx =
			&dev->wed_rro.emi_rings_cpu.ptr->ring[0].idx;
		mt76_queue_reset(dev, &mdev->q_rx[MT_RXQ_RRO_RXDMAD_C], true);
	}

	return 0;
}

#undef INIT_TXQ
#undef INIT_RXQ
#undef INIT_MCUQ

void mt7999_dma_reset(struct mt7999_dev *dev, bool force)
{
	struct mt76_phy *phy2 = dev->mt76.phys[MT_BAND1];
	struct mt76_phy *phy3 = dev->mt76.phys[MT_BAND2];
	u32 hif1_ofs = MT_WFDMA0_PCIE1(0) - MT_WFDMA0(0);
	int i;

	mt76_clear(dev, MT_WFDMA0_GLO_CFG,
		   MT_WFDMA0_GLO_CFG_TX_DMA_EN |
		   MT_WFDMA0_GLO_CFG_RX_DMA_EN);

	if (dev->hif2)
		mt76_clear(dev, MT_WFDMA0_GLO_CFG + hif1_ofs,
			   MT_WFDMA0_GLO_CFG_TX_DMA_EN |
			   MT_WFDMA0_GLO_CFG_RX_DMA_EN);

	usleep_range(1000, 2000);

	for (i = 0; i < __MT_TXQ_MAX; i++) {
		mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[i], true);
		if (phy2)
			mt76_queue_tx_cleanup(dev, phy2->q_tx[i], true);
		if (phy3)
			mt76_queue_tx_cleanup(dev, phy3->q_tx[i], true);
	}

	for (i = 0; i < __MT_MCUQ_MAX; i++)
		mt76_queue_tx_cleanup(dev, dev->mt76.q_mcu[i], true);

	if (!force)
		dev_info(dev->mt76.dev,"%s L1 SER tx queue clean up done.",
			 wiphy_name(dev->mt76.hw->wiphy));

	mt76_for_each_q_rx(&dev->mt76, i)
		mt76_queue_rx_cleanup(dev, &dev->mt76.q_rx[i]);

	if (!force)
		dev_info(dev->mt76.dev,"%s L1 SER rx queue clean up done.",
			 wiphy_name(dev->mt76.hw->wiphy));

	mt76_tx_status_check(&dev->mt76, true);

	if (mt7999_has_hwrro(dev) &&
	    !mtk_wed_device_active(&dev->mt76.mmio.wed))
		mt7999_rro_msdu_page_map_free(dev);

	if (dev->hif2 && mtk_wed_device_active(&dev->mt76.mmio.wed_hif2))
		mtk_wed_device_dma_reset(&dev->mt76.mmio.wed_hif2);

	if (mtk_wed_device_active(&dev->mt76.mmio.wed))
		mtk_wed_device_dma_reset(&dev->mt76.mmio.wed);

	mt76_npu_disable_irqs(&dev->mt76);

	if (!force)
		dev_info(dev->mt76.dev,"%s L1 SER wed dma reset done.",
			 wiphy_name(dev->mt76.hw->wiphy));

	mt7999_dma_disable(dev, force);

	if (!force)
		dev_info(dev->mt76.dev,"%s L1 SER dma disable done.",
			 wiphy_name(dev->mt76.hw->wiphy));
	mt76_wed_dma_reset(&dev->mt76);

	/* reset hw queues */
	for (i = 0; i < __MT_TXQ_MAX; i++) {
		mt76_dma_reset_tx_queue(&dev->mt76, dev->mphy.q_tx[i]);
		if (phy2)
			mt76_dma_reset_tx_queue(&dev->mt76, phy2->q_tx[i]);
		if (phy3)
			mt76_dma_reset_tx_queue(&dev->mt76, phy3->q_tx[i]);
	}

	if (!force)
		dev_info(dev->mt76.dev,"%s L1 SER dma tx queue reset done.",
			 wiphy_name(dev->mt76.hw->wiphy));

	for (i = 0; i < __MT_MCUQ_MAX; i++)
		mt76_queue_reset(dev, dev->mt76.q_mcu[i], true);

	if (!force)
		dev_info(dev->mt76.dev,"%s L1 SER mcu queue reset done.",
			 wiphy_name(dev->mt76.hw->wiphy));
	mt76_for_each_q_rx(&dev->mt76, i) {
		struct mt76_queue *q = &dev->mt76.q_rx[i];

		if (mtk_wed_device_active(&dev->mt76.mmio.wed)) {
			if (mt76_queue_is_wed_rro(q) ||
			    mt76_queue_is_wed_tx_free(q)) {
				if (force && mt76_queue_is_wed_rro_data(q))
					mt76_queue_reset(dev, q, false);
				continue;
			}
		}
		mt76_queue_reset(dev, q, true);
	}

	if (!force)
		dev_info(dev->mt76.dev,"%s L1 SER rx queue reset done.",
			 wiphy_name(dev->mt76.hw->wiphy));

	mt76_tx_status_check(&dev->mt76, true);

	mt76_for_each_q_rx(&dev->mt76, i) {
		if (mtk_wed_device_active(&dev->mt76.mmio.wed) && force &&
		    (mt76_queue_is_wed_rro_ind(&dev->mt76.q_rx[i]) ||
		     mt76_queue_is_wed_rro_msdu_pg(&dev->mt76.q_rx[i]) ||
		     mt76_queue_is_wed_rro_rxdmad_c(&dev->mt76.q_rx[i])))
			continue;

		mt76_queue_rx_reset(dev, i);
	}

	mt7999_dma_enable(dev, !force);

	if (!force)
		dev_info(dev->mt76.dev,"%s L1 SER dma enable done.",
			 wiphy_name(dev->mt76.hw->wiphy));
}

void mt7999_dma_cleanup(struct mt7999_dev *dev)
{
	mt7999_dma_disable(dev, true);

	mt76_dma_cleanup(&dev->mt76);
	mt7999_dma_dmy_cleanup(dev);

}
