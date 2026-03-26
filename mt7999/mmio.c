// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (C) 2026 MediaTek Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/rtnetlink.h>

#include "mt7999.h"
#include "mac.h"
#include "mcu.h"
#include "../trace.h"
#include "../dma.h"

static bool wed_enable = true;
module_param(wed_enable, bool, 0644);

#define INVALID_REG_ADDR	0xffffffff

static const struct __base mt7999_reg_base[] = {
	[WF_AGG_BASE]		= { { 0x820e2000, 0x820f2000, 0x830e2000 } },
	[WF_ARB_BASE]		= { { 0x820e3000, 0x820f3000, 0x830e3000 } },
	[WF_TMAC_BASE]		= { { 0x820e4000, 0x820f4000, 0x830e4000 } },
	[WF_RMAC_BASE]		= { { 0x820e5000, 0x820f5000, 0x830e5000 } },
	[WF_DMA_BASE]		= { { 0x820e7000, 0x820f7000, 0x830e7000 } },
	[WF_WTBLOFF_BASE]	= { { 0x820e9000, 0x820f9000, 0x830e9000 } },
	[WF_ETBF_BASE]		= { { 0x820ea000, 0x820fa000, 0x830ea000 } },
	[WF_LPON_BASE]		= { { 0x820eb000, 0x820fb000, 0x830eb000 } },
	[WF_MIB_BASE]		= { { 0x820ed000, 0x820fd000, 0x830ed000 } },
	[WF_RATE_BASE]		= { { 0x820ee000, 0x820fe000, 0x830ee000 } },
	[WF_AIRTIME_BASE]	= { { 0x84068400, 0x84069400, 0x8406a400 } },
};

static const u32 mt7999_offs[] = {
	[MIB_SDR27]		= 0x080,
	[MIB_SDR28]		= 0x084,
	[MIB_TRARC0]		= 0x0b0,
	[MIB_BFTFCR]		= 0x5d0,
	[MIB_BTSCR0]		= 0x5e0,
	[MIB_TSCR0]		= 0x750,
	[MIB_TSCR1]		= 0x754,
	[MIB_TSCR2]		= 0x758,
	[MIB_TSCR3]		= 0x75c,
	[MIB_TSCR4]		= 0x760,
	[MIB_TSCR5]		= 0x764,
	[MIB_TSCR6]		= 0x768,
	[MIB_TSCR7]		= 0x770,
	[MIB_RVSR0]		= 0x800,
	[MIB_RVSR1]		= 0x804,
	[MIB_BTSCR5]		= 0x868,
	[MIB_BTSCR6]		= 0x878,
	[MIB_RSCR1]		= 0x890,
	[MIB_RSCR27]		= 0xa38,
	[MIB_RSCR28]		= 0xa3c,
	[MIB_RSCR29]		= 0xa40,
	[MIB_RSCR30]		= 0xa44,
	[MIB_RSCR31]		= 0xa48,
	[MIB_RSCR33]		= 0xa50,
	[MIB_RSCR35]		= 0xa58,
	[MIB_RSCR36]		= 0xa5c,
	[MIB_BSCR0]		= 0xbb8,
	[MIB_BSCR1]		= 0xbbc,
	[MIB_BSCR2]		= 0xbc0,
	[MIB_BSCR3]		= 0xbc4,
	[MIB_BSCR4]		= 0xbc8,
	[MIB_BSCR5]		= 0xbcc,
	[MIB_BSCR6]		= 0xbd0,
	[MIB_BSCR7]		= 0xbd4,
	[MIB_BSCR17]		= 0xbfc,
	[MIB_TRDR1]		= 0xc14,
	[HIF_REMAP_L1]		= 0x24,
	[HIF_REMAP_BASE_L1]	= 0x130000,
	[HIF_REMAP_L2]		= 0x1b4,
	[HIF_REMAP_BASE_L2]	= 0x1000,
	[WTBLON_WDUCR]		= 0x400,
	[WTBL_UPDATE]		= 0x380,
	[WTBL_ITCR]		= 0x3b0,
	[WTBL_ITCR0]		= 0x3b8,
	[WTBL_ITCR1]		= 0x3bc,
	[FWDL_BASE]		= 0x50000,
	[FWDL_SYNC_INFO]	= 0x0,
	[FWDL_TIMEOUT]		= 0x4,
	[FWDL_MSG]		= 0x8,
	[FWDL_RADIO_TYPE]	= 0xc,
	[FWDL_HEADER_SIZE]	= 0x18,
	[FWDL_SLOT_TBL_ADDR_31_0] = 0x1c,
	[FWDL_SLOT_TBL_ADDR_63_32] = 0x20,
	[FWDL_SLOT_TBL_SIZE]	= 0x24,
	[FWDL_SLOT_ADDR]	= 0x28,
	[FWDL_SLOT_READY]	= 0x2c,
	[FWDL_NOTIF_DONE]	= 0x30,
	[FWDL_SLOT_DONE]	= 0x110,
	[FWDL_MSG_DONE]		= 0x114,
	[FWDL_NOTIF]		= 0x118,
	[FWDL_RESP]		= 0x11c,
	[FWDL_DL]		= 0x120,
	[FWDL_ERROR_CODE]	= 0x124,
	[FWDL_COMM_STATE]	= 0x128,
	[FWDL_SW_INIT_DONE]	= 0x1a4,
	[FWDL_FW_STATE]		= 0x1a8,
};

static const struct __map mt7999_reg_map[] = {
	{ 0x54000000, 0x02000, 0x1000 }, /* WFDMA_0 (PCIE0 MCU DMA0) */
	{ 0x55000000, 0x03000, 0x1000 }, /* WFDMA_1 (PCIE0 MCU DMA1) */
	{ 0x56000000, 0x04000, 0x1000 }, /* WFDMA reserved */
	{ 0x57000000, 0x05000, 0x1000 }, /* WFDMA MCU wrap CR */
	{ 0x58000000, 0x06000, 0x1000 }, /* WFDMA PCIE1 MCU DMA0 (MEM_DMA) */
	{ 0x59000000, 0x07000, 0x1000 }, /* WFDMA PCIE1 MCU DMA1 */
	{ 0x820c0000, 0x08000, 0x4000 }, /* WF_UMAC_TOP (PLE) */
	{ 0x820c8000, 0x0c000, 0x2000 }, /* WF_UMAC_TOP (PSE) */
	{ 0x820cc000, 0x0e000, 0x2000 }, /* WF_UMAC_TOP (PP) */
	{ 0x820e0000, 0x20000, 0x0400 }, /* WF_LMAC_TOP BN0 (WF_CFG) */
	{ 0x820e1000, 0x20400, 0x0200 }, /* WF_LMAC_TOP BN0 (WF_TRB) */
	{ 0x820e2000, 0x20800, 0x0400 }, /* WF_LMAC_TOP BN0 (WF_AGG) */
	{ 0x820e3000, 0x20c00, 0x0400 }, /* WF_LMAC_TOP BN0 (WF_ARB) */
	{ 0x820e4000, 0x21000, 0x0400 }, /* WF_LMAC_TOP BN0 (WF_TMAC) */
	{ 0x820e5000, 0x21400, 0x0800 }, /* WF_LMAC_TOP BN0 (WF_RMAC) */
	{ 0x820ce000, 0x21c00, 0x0200 }, /* WF_LMAC_TOP (WF_SEC) */
	{ 0x820e7000, 0x21e00, 0x0200 }, /* WF_LMAC_TOP BN0 (WF_DMA) */
	{ 0x820cf000, 0x22000, 0x1000 }, /* WF_LMAC_TOP (WF_PF) */
	{ 0x820e9000, 0x23400, 0x0200 }, /* WF_LMAC_TOP BN0 (WF_WTBLOFF) */
	{ 0x820ea000, 0x24000, 0x0200 }, /* WF_LMAC_TOP BN0 (WF_ETBF) */
	{ 0x820eb000, 0x24200, 0x0400 }, /* WF_LMAC_TOP BN0 (WF_LPON) */
	{ 0x820ec000, 0x24600, 0x0200 }, /* WF_LMAC_TOP BN0 (WF_INT) */
	{ 0x820ed000, 0x24800, 0x0800 }, /* WF_LMAC_TOP BN0 (WF_MIB) */
	{ 0x820ca000, 0x26000, 0x2000 }, /* WF_LMAC_TOP BN0 (WF_MUCOP) */
	{ 0x820d0000, 0x30000, 0x10000 }, /* WF_LMAC_TOP (WF_WTBLON) */
	{ 0x40000000, 0x70000, 0x10000 }, /* WF_UMAC_SYSRAM */
	{ 0x00400000, 0x80000, 0x10000 }, /* WF_MCU_SYSRAM */
	{ 0x00410000, 0x90000, 0x10000 }, /* WF_MCU_SYSRAM (configure register) */
	{ 0x820f0000, 0xa0000, 0x0400 }, /* WF_LMAC_TOP BN1 (WF_CFG) */
	{ 0x820f1000, 0xa0600, 0x0200 }, /* WF_LMAC_TOP BN1 (WF_TRB) */
	{ 0x820f2000, 0xa0800, 0x0400 }, /* WF_LMAC_TOP BN1 (WF_AGG) */
	{ 0x820f3000, 0xa0c00, 0x0400 }, /* WF_LMAC_TOP BN1 (WF_ARB) */
	{ 0x820f4000, 0xa1000, 0x0400 }, /* WF_LMAC_TOP BN1 (WF_TMAC) */
	{ 0x820f5000, 0xa1400, 0x0800 }, /* WF_LMAC_TOP BN1 (WF_RMAC) */
	{ 0x820f7000, 0xa1e00, 0x0200 }, /* WF_LMAC_TOP BN1 (WF_DMA) */
	{ 0x820f9000, 0xa3400, 0x0200 }, /* WF_LMAC_TOP BN1 (WF_WTBLOFF) */
	{ 0x820fa000, 0xa4000, 0x0200 }, /* WF_LMAC_TOP BN1 (WF_ETBF) */
	{ 0x820fb000, 0xa4200, 0x0400 }, /* WF_LMAC_TOP BN1 (WF_LPON) */
	{ 0x820fc000, 0xa4600, 0x0200 }, /* WF_LMAC_TOP BN1 (WF_INT) */
	{ 0x820fd000, 0xa4800, 0x0800 }, /* WF_LMAC_TOP BN1 (WF_MIB) */
	{ 0x820cc000, 0xa5000, 0x2000 }, /* WF_LMAC_TOP BN1 (WF_MUCOP) */
	{ 0x820c4000, 0xa8000, 0x4000 }, /* WF_LMAC_TOP BN1 (WF_UWTBL) */
	{ 0x820b0000, 0xae000, 0x1000 }, /* [APB2] WFSYS_ON */
	{ 0x80020000, 0xb0000, 0x10000 }, /* WF_TOP_MISC_OFF */
	{ 0x81020000, 0xc0000, 0x10000 }, /* WF_TOP_MISC_ON */
	{ 0x20020000, 0xd0000, 0x10000 }, /* CONN_INFRA, wfdma */
	{ 0x20060000, 0xe0000, 0x10000 }, /* CONN_INFRA, conn_host_csr_top */
	{ 0x20000000, 0xf0000, 0x10000 }, /* CONN_INFRA */
	{ 0x0, 0x0, 0x0 }, /* imply end of search */
};

static u32 mt7999_reg_map_l1(struct mt7999_dev *dev, u32 addr)
{
	u32 offset = FIELD_GET(MT_REG_OFFSET, addr);
	u32 base = FIELD_GET(MT_REG_BASE, addr);

	dev->bus_ops->rmw(&dev->mt76, MT_HIF_REMAP_L1,
			  MT_HIF_REMAP_L1_MASK,
			  FIELD_PREP(MT_HIF_REMAP_L1_MASK, base));
	/* use read to push write */
	dev->bus_ops->rr(&dev->mt76, MT_HIF_REMAP_L1);

	return MT_HIF_REMAP_BASE_L1 + offset;
}

static u32 mt7999_reg_map_l2(struct mt7999_dev *dev, u32 addr)
{
	u32 offset = FIELD_GET(MT_REG_OFFSET, addr);
	u32 base = FIELD_GET(MT_REG_BASE, addr);

	dev->bus_ops->rmw(&dev->mt76, MT_HIF_REMAP_L2,
			  MT_HIF_REMAP_L2_MASK,
			  FIELD_PREP(MT_HIF_REMAP_L2_MASK, base));
	/* use read to push write */
	dev->bus_ops->rr(&dev->mt76, MT_HIF_REMAP_L2);

	return MT_HIF_REMAP_BASE_L2 + offset;
}

static u32 mt7999_reg_map_cbtop(struct mt7999_dev *dev, u32 addr)
{
	u32 offset = FIELD_GET(MT_REG_OFFSET, addr);
	u32 base = FIELD_GET(MT_REG_BASE, addr);

	dev->bus_ops->rmw(&dev->mt76, MT_HIF_REMAP_CBTOP,
			  MT_HIF_REMAP_CBTOP_MASK,
			  FIELD_PREP(MT_HIF_REMAP_CBTOP_MASK, base));
	/* use read to push write */
	dev->bus_ops->rr(&dev->mt76, MT_HIF_REMAP_CBTOP);

	return MT_HIF_REMAP_BASE_CBTOP + offset;
}

static u32 __mt7999_reg_addr(struct mt7999_dev *dev, u32 addr)
{
	int i;

	if (addr < 0x200000)
		return addr;

	for (i = 0; i < dev->reg.map_size; i++) {
		u32 ofs;

		if (addr < dev->reg.map[i].phys)
			continue;

		ofs = addr - dev->reg.map[i].phys;
		if (ofs >= dev->reg.map[i].size)
			continue;

		return dev->reg.map[i].mapped + ofs;
	}

	return INVALID_REG_ADDR;
}

static u32 __mt7999_reg_remap_addr(struct mt7999_dev *dev, u32 addr)
{
	if ((addr >= MT_INFRA_BASE && addr < MT_WFSYS0_PHY_START) ||
	    (addr >= MT_WFSYS0_PHY_START && addr < MT_WFSYS1_PHY_START) ||
	    (addr >= MT_WFSYS1_PHY_START && addr <= MT_WFSYS1_PHY_END))
		return mt7999_reg_map_l1(dev, addr);

	/* CONN_INFRA: covert to phyiscal addr and use layer 1 remap */
	if (addr >= MT_INFRA_MCU_START && addr <= MT_INFRA_MCU_END) {
		addr = addr - MT_INFRA_MCU_START + MT_INFRA_BASE;
		return mt7999_reg_map_l1(dev, addr);
	}

	if ((addr >= MT_CBTOP1_PHY_START && addr <= MT_CBTOP1_PHY_END) ||
	    addr >= MT_CBTOP2_PHY_START)
		return mt7999_reg_map_cbtop(dev, addr);

	return mt7999_reg_map_l2(dev, addr);
}

void mt7999_memcpy_fromio(struct mt7999_dev *dev, void *buf, u32 offset,
			  size_t len)
{
	u32 addr = __mt7999_reg_addr(dev, offset);

	if (addr != INVALID_REG_ADDR) {
		memcpy_fromio(buf, dev->mt76.mmio.regs + addr, len);
		return;
	}

	spin_lock_bh(&dev->reg_lock);
	memcpy_fromio(buf, dev->mt76.mmio.regs +
			   __mt7999_reg_remap_addr(dev, offset), len);
	spin_unlock_bh(&dev->reg_lock);
}

static u32 mt7999_rr(struct mt76_dev *mdev, u32 offset)
{
	struct mt7999_dev *dev = container_of(mdev, struct mt7999_dev, mt76);
	u32 addr = __mt7999_reg_addr(dev, offset), val;

	if (addr != INVALID_REG_ADDR)
		return dev->bus_ops->rr(mdev, addr);

	spin_lock_bh(&dev->reg_lock);
	val = dev->bus_ops->rr(mdev, __mt7999_reg_remap_addr(dev, offset));
	spin_unlock_bh(&dev->reg_lock);

	return val;
}

static void mt7999_wr(struct mt76_dev *mdev, u32 offset, u32 val)
{
	struct mt7999_dev *dev = container_of(mdev, struct mt7999_dev, mt76);
	u32 addr = __mt7999_reg_addr(dev, offset);

	if (addr != INVALID_REG_ADDR) {
		dev->bus_ops->wr(mdev, addr, val);
		return;
	}

	spin_lock_bh(&dev->reg_lock);
	dev->bus_ops->wr(mdev, __mt7999_reg_remap_addr(dev, offset), val);
	spin_unlock_bh(&dev->reg_lock);
}

static u32 mt7999_rmw(struct mt76_dev *mdev, u32 offset, u32 mask, u32 val)
{
	struct mt7999_dev *dev = container_of(mdev, struct mt7999_dev, mt76);
	u32 addr = __mt7999_reg_addr(dev, offset);

	if (addr != INVALID_REG_ADDR)
		return dev->bus_ops->rmw(mdev, addr, mask, val);

	spin_lock_bh(&dev->reg_lock);
	val = dev->bus_ops->rmw(mdev, __mt7999_reg_remap_addr(dev, offset), mask, val);
	spin_unlock_bh(&dev->reg_lock);

	return val;
}

#ifdef CONFIG_NET_MEDIATEK_SOC_WED
static int mt7999_mmio_wed_reset(struct mtk_wed_device *wed)
{
	struct mt76_dev *mdev = container_of(wed, struct mt76_dev, mmio.wed);
	struct mt7999_dev *dev = container_of(mdev, struct mt7999_dev, mt76);
	struct mt76_phy *mphy = &dev->mphy;
	int ret;

	ASSERT_RTNL();

	if (test_and_set_bit(MT76_STATE_WED_RESET, &mphy->state))
		return -EBUSY;

	ret = mt7999_mcu_set_ser(dev, UNI_SER_TRIGGER, SER_METHOD_RECOVER_FROM_ETH,
				 mphy->band_idx);
	if (ret)
		goto out;

	rtnl_unlock();
	if (!wait_for_completion_timeout(&mdev->mmio.wed_reset, 20 * HZ)) {
		dev_err(mdev->dev, "wed reset timeout\n");
		ret = -ETIMEDOUT;
	}
	rtnl_lock();
out:
	clear_bit(MT76_STATE_WED_RESET, &mphy->state);

	return ret;
}
#endif

/*
 *			MTK_WED_HW_V3	MTK_WED_HW_V3_1
 *			(MT7988)	(MT7987)
 * ----------------------------------------------------
 * RRO	|MT7999		MT76_HWRRO_V3	MT76_HWRRO_V3_1
 */
int mt7999_mmio_wed_init(struct mt7999_dev *dev, void *pdev_ptr,
			 bool hif2, int *irq)
{
#ifdef CONFIG_NET_MEDIATEK_SOC_WED
	struct mtk_wed_device *wed = &dev->mt76.mmio.wed;
	struct pci_dev *pci_dev = pdev_ptr;
	u32 hif1_ofs;
	u16 tx_token_size, rx_token_size;
	int wed_hw_ver;

	if (!wed_enable) {
		dev->mt76.rx_token_size = mt7999_has_hwrro(dev) ? 32768 : 0;
		return 0;
	}

	if (hif2)
		wed = &dev->mt76.mmio.wed_hif2;

	if (mtk_wed_device_active(wed)) {
		*irq = wed->irq;
		return 1;
	}

	wed_hw_ver = mtk_wed_device_get_hw_version();
	tx_token_size = MT7999_WED_TOKEN_SIZE;

	switch (wed_hw_ver) {
	case MTK_WED_HW_V3:
		dev->mt76.hwrro_mode = MT76_HWRRO_V3;
		rx_token_size = dev->hif2 ? 32768 : 24576;
		break;
	case MTK_WED_HW_V3_1:
		dev->mt76.hwrro_mode = MT76_HWRRO_V3_1;
		rx_token_size = 24576;
		break;
	default:
		wed_enable = false;
		dev->mt76.hwrro_mode = MT76_HWRRO_OFF;
		dev_err(dev->mt76.dev, "wed version %d not support\n", wed_hw_ver);
		return 0;
	}

	hif1_ofs = dev->hif2 ? MT_WFDMA0_PCIE1(0) - MT_WFDMA0(0) : 0;

	wed->wlan.pci_dev = pci_dev;
	wed->wlan.bus_type = MTK_WED_BUS_PCIE;

	wed->wlan.base = devm_ioremap(dev->mt76.dev,
				      pci_resource_start(pci_dev, 0),
				      pci_resource_len(pci_dev, 0));
	if (!wed->wlan.base)
		return -ENOMEM;

	wed->wlan.phy_base = pci_resource_start(pci_dev, 0);

	if (hif2) {
		wed->wlan.wpdma_int = wed->wlan.phy_base + MT_INT1_SOURCE_CSR;
		wed->wlan.wpdma_mask = wed->wlan.phy_base + MT_INT1_MASK_CSR;

		wed->wlan.wpdma_tx[0] = wed->wlan.phy_base + hif1_ofs +
					MT_TXQ_RING_BASE +
					MT7999_TXQ_BAND2 * MT_RING_SIZE;
		wed->wlan.wpdma_txfree = wed->wlan.phy_base + hif1_ofs +
					 MT_RXQ_RING_BASE +
					 MT7999_RXQ_TXFREE1 * MT_RING_SIZE;
		wed->wlan.txfree_tbit = ffs(MT_INT_RX_TXFREE_SRC1) - 1;

		wed->wlan.fc_disable = true;
		wed->wlan.tx_tbit[0] = ffs(MT_INT_TX_DONE_BAND2) - 1;
	} else {
		wed->wlan.hw_rro = (enum mtk_wed_hwrro_mode)dev->mt76.hwrro_mode; /* default on */
		wed->wlan.wpdma_int = wed->wlan.phy_base + MT_INT_SOURCE_CSR;
		wed->wlan.wpdma_mask = wed->wlan.phy_base + MT_INT_MASK_CSR;

		wed->wlan.wpdma_tx[0] = wed->wlan.phy_base + MT_TXQ_RING_BASE +
					MT7999_TXQ_BAND0 * MT_RING_SIZE;
		wed->wlan.wpdma_tx[1] = wed->wlan.phy_base + MT_TXQ_RING_BASE +
					MT7999_TXQ_BAND1 * MT_RING_SIZE;

		wed->wlan.wpdma_rx_glo = wed->wlan.phy_base + MT_WFDMA0_GLO_CFG;

		if (wed_hw_ver == MTK_WED_HW_V3)
			wed->wlan.wpdma_rx[0] = wed->wlan.phy_base +
						MT_RXQ_RING_BASE +
						MT7999_RXQ_BAND0 * MT_RING_SIZE;

		wed->wlan.wpdma_rx_rro[0] = wed->wlan.phy_base +
					    MT_RXQ_RING_BASE +
					    MT7999_RXQ_RRO_BAND0 * MT_RING_SIZE;

		wed->wlan.wpdma_rx_rro[1] = wed->wlan.phy_base + hif1_ofs +
					    MT_RXQ_RING_BASE +
					    MT7999_RXQ_RRO_BAND2 * MT_RING_SIZE;

		wed->wlan.wpdma_rx[1] = wed->wlan.phy_base + hif1_ofs +
					MT_RXQ_RING_BASE +
					MT7999_RXQ_BAND2 * MT_RING_SIZE;

		if (dev->mt76.hwrro_mode == MT76_HWRRO_V3)
			wed->wlan.wpdma_rx_pg = wed->wlan.phy_base +
						MT_RXQ_RING_BASE +
						MT7999_RXQ_MSDU_PG_BAND0 * MT_RING_SIZE;
		else
			wed->wlan.wpdma_rro_3_1_rx = wed->wlan.phy_base +
						     MT_RXQ_RRO_AP_RING_BASE +
						     MT7999_RXQ_RRO_RXDMAD_C * MT_RING_SIZE;

		wed->wlan.rx_nbuf = 65536;
		wed->wlan.rx_npkt = rx_token_size;
		wed->wlan.rx_size = MT7999_RX_DATA_BUF_SIZE;

		if (wed_hw_ver == MTK_WED_HW_V3)
			wed->wlan.rx_tbit[0] = ffs(MT_INT_RX_DONE_BAND0) - 1;
		else
			wed->wlan.rx_tbit[0] = ffs(MT_INT_RX_DONE_WED_RX_DATA) - 1;
		wed->wlan.rro_rx_tbit[0] = ffs(MT_INT_RX_DONE_RRO_BAND0) - 1;
		wed->wlan.rx_tbit[1] = ffs(MT_INT_RX_DONE_BAND2) - 1;
		wed->wlan.rro_rx_tbit[1] = ffs(MT_INT_RX_DONE_RRO_BAND2) - 1;

		if (dev->mt76.hwrro_mode == MT76_HWRRO_V3) {
			wed->wlan.rx_pg_tbit[0] = ffs(MT_INT_RX_DONE_MSDU_PG_BAND0) - 1;
			wed->wlan.pg_ring_num = 1;
		} else {
			wed->wlan.rro_3_1_rx_tbit = ffs(MT_INT_RX_DONE_RRO_RXDMAD_C) - 1;
			wed->wlan.pg_ring_num = 0;
		}

		wed->wlan.tx_tbit[0] = ffs(MT_INT_TX_DONE_BAND0) - 1;
		wed->wlan.tx_tbit[1] = ffs(MT_INT_TX_DONE_BAND1) - 1;
		wed->wlan.wpdma_txfree = wed->wlan.phy_base +
					 MT_RXQ_RING_BASE +
					 MT7999_RXQ_TXFREE0 * MT_RING_SIZE;
		wed->wlan.txfree_tbit = ffs(MT_INT_RX_TXFREE_SRC0) - 1;

		dev->mt76.rx_token_size = tx_token_size + rx_token_size;
	}

	wed->wlan.nbuf = tx_token_size;
	wed->wlan.token_start = 0;
	wed->wlan.hif2 = hif2;

	wed->wlan.amsdu_max_subframes = 8;
	wed->wlan.amsdu_max_len = 1536;

	wed->wlan.init_buf = mt7999_wed_init_buf;
	wed->wlan.init_rx_buf = mt76_wed_init_rx_buf;
	wed->wlan.release_rx_buf = mt76_wed_release_rx_buf;
	wed->wlan.offload_enable = mt76_wed_offload_enable;
	wed->wlan.offload_disable = mt76_wed_offload_disable;
	if (!hif2) {
		wed->wlan.reset = mt7999_mmio_wed_reset;
		wed->wlan.reset_complete = mt76_wed_reset_complete;
	}

	if (mtk_wed_device_attach(wed)) {
		dev->mt76.hwrro_mode = MT76_HWRRO_OFF;
		wed_enable = false;
		return 0;
	}

	dev->mt76.token_start = wed->wlan.nbuf;

	*irq = wed->irq;
	dev->mt76.dma_dev = wed->dev;

	return 1;
#else
	return 0;
#endif
}

static int mt7999_mmio_init(struct mt76_dev *mdev,
			    void __iomem *mem_base,
			    u32 device_id)
{
	struct mt76_bus_ops *bus_ops;
	struct mt7999_dev *dev;

	dev = container_of(mdev, struct mt7999_dev, mt76);
	mt76_mmio_init(&dev->mt76, mem_base);
	spin_lock_init(&dev->reg_lock);

	switch (device_id) {
	case MT7999_DEVICE_ID:
		dev->reg.base = mt7999_reg_base;
		dev->reg.offs_rev = mt7999_offs;
		dev->reg.map = mt7999_reg_map;
		dev->reg.map_size = ARRAY_SIZE(mt7999_reg_map);
		break;
	default:
		return -EINVAL;
	}

	dev->bus_ops = dev->mt76.bus;
	bus_ops = devm_kmemdup(dev->mt76.dev, dev->bus_ops, sizeof(*bus_ops),
			       GFP_KERNEL);
	if (!bus_ops)
		return -ENOMEM;

	bus_ops->rr = mt7999_rr;
	bus_ops->wr = mt7999_wr;
	bus_ops->rmw = mt7999_rmw;
	dev->mt76.bus = bus_ops;

	mdev->rev = (device_id << 16) | (mt76_rr(dev, MT_HW_REV) & 0xff);

	dev_dbg(mdev->dev, "ASIC revision: %04x\n", mdev->rev);

	return 0;
}

void mt7999_dual_hif_set_irq_mask(struct mt7999_dev *dev, bool write_reg,
				  u32 clear, u32 set)
{
	struct mt76_dev *mdev = &dev->mt76;
	unsigned long flags;

	spin_lock_irqsave(&mdev->mmio.irq_lock, flags);

	mdev->mmio.irqmask &= ~clear;
	mdev->mmio.irqmask |= set;

	if (write_reg) {
		if (mtk_wed_device_active(&mdev->mmio.wed)) {
			mtk_wed_device_irq_set_mask(&mdev->mmio.wed,
						    mdev->mmio.irqmask);
			if (mtk_wed_device_active(&mdev->mmio.wed_hif2))
				mtk_wed_device_irq_set_mask(&mdev->mmio.wed_hif2,
							    mdev->mmio.irqmask);
		} else {
			mt76_wr(dev, MT_INT_MASK_CSR, mdev->mmio.irqmask);
			mt76_wr(dev, MT_INT1_MASK_CSR, mdev->mmio.irqmask);
		}
	}

	spin_unlock_irqrestore(&mdev->mmio.irq_lock, flags);
}

static void mt7999_rx_poll_complete(struct mt76_dev *mdev,
				    enum mt76_rxq_id q)
{
	if (q == MT_RXQ_NPU0 || q == MT_RXQ_NPU1) {
		struct airoha_npu *npu;

		npu = rcu_dereference(mdev->mmio.npu);
		if (npu)
			airoha_npu_wlan_enable_irq(npu, q - MT_RXQ_NPU0);
	} else {
		struct mt7999_dev *dev = container_of(mdev, struct mt7999_dev,
						      mt76);

		mt7999_irq_enable(dev, MT_INT_RX(q));
	}
}

/* TODO: support 2/4/6/8 MSI-X vectors */
static void mt7999_irq_tasklet(struct tasklet_struct *t)
{
	struct mt7999_dev *dev = from_tasklet(dev, t, mt76.irq_tasklet);
	struct mtk_wed_device *wed = &dev->mt76.mmio.wed;
	struct mtk_wed_device *wed_hif2 = &dev->mt76.mmio.wed_hif2;
	u32 i, intr, mask, intr1 = 0;

	if (dev->hif2 && mtk_wed_device_active(wed_hif2)) {
		mtk_wed_device_irq_set_mask(wed_hif2, 0);
		intr1 = mtk_wed_device_irq_get(wed_hif2,
					       dev->mt76.mmio.irqmask);

		if (intr1 & MT_INT_RX_TXFREE_SRC1)
			napi_schedule(&dev->mt76.napi[MT_RXQ_TXFREE_SRC1]);

	}

	if (mtk_wed_device_active(wed)) {
		mtk_wed_device_irq_set_mask(wed, 0);
		intr = mtk_wed_device_irq_get(wed, dev->mt76.mmio.irqmask);
		intr |= (intr1 & ~MT_INT_TX_RX_DONE_EXT);
	} else {
		mt76_wr(dev, MT_INT_MASK_CSR, 0);
		if (dev->hif2)
			mt76_wr(dev, MT_INT1_MASK_CSR, 0);

		intr = mt76_rr(dev, MT_INT_SOURCE_CSR);
		intr &= dev->mt76.mmio.irqmask;
		mt76_wr(dev, MT_INT_SOURCE_CSR, intr);
		if (dev->hif2) {
			intr1 = mt76_rr(dev, MT_INT1_SOURCE_CSR);
			intr1 &= dev->mt76.mmio.irqmask;
			mt76_wr(dev, MT_INT1_SOURCE_CSR, intr1);
			intr |= intr1;
		}
	}

	trace_dev_irq(&dev->mt76, intr, dev->mt76.mmio.irqmask);

	mask = intr & MT_INT_RX_DONE_ALL;
	if (intr & MT_INT_TX_DONE_MCU)
		mask |= MT_INT_TX_DONE_MCU;
	mt7999_irq_disable(dev, mask);

	if (intr & MT_INT_TX_DONE_MCU)
		napi_schedule(&dev->mt76.tx_napi);

	for (i = 0; i < __MT_RXQ_MAX; i++) {
		if ((intr & MT_INT_RX(i)))
			napi_schedule(&dev->mt76.napi[i]);
	}

	if (intr & MT_INT_MCU_CMD) {
		u32 val = mt76_rr(dev, MT_MCU_CMD);

		mt76_clear(dev, MT_WFDMA0_MCU_HOST_INT_ENA,
			   val & MT_WFDMA0_MCU_HOST_SW_INT_ENA);
		mt76_wr(dev, MT_MCU_CMD, val);
		mt76_set(dev, MT_WFDMA0_MCU_HOST_INT_ENA,
			 val & MT_WFDMA0_MCU_HOST_SW_INT_ENA);
		if (val & (MT_MCU_CMD_ERROR_MASK | MT_MCU_CMD_WDT_MASK)) {
			dev->recovery.state = val;
			mt7999_reset(dev);
		}
	}
}

static void mt7999_irq_fwdl_set(struct mt7999_dev *dev, u32 set, u32 clear)
{
	struct mt7999_firmware_download *fwdl = dev->fwdl;
	unsigned long flags;

	spin_lock_irqsave(&fwdl->irq_lock, flags);
	fwdl->irq_mask |= set;
	fwdl->irq_mask &= ~clear;
	mt76_rmw(dev, MT_MAC_IREG_IMASK_ADDR, MT_MAC_IREG_IRQ_ALL, fwdl->irq_mask);
	spin_unlock_irqrestore(&fwdl->irq_lock, flags);
}

void mt7999_irq_fwdl_enable(struct mt7999_dev *dev, u32 mask)
{
	mt7999_irq_fwdl_set(dev, mask, 0);
}

void mt7999_irq_fwdl_disable(struct mt7999_dev *dev, u32 mask)
{
	mt7999_irq_fwdl_set(dev, 0, mask);
	mt76_wr(dev, MT_MAC_IREG_INT_CLR, mask);
}

static void mt7999_handle_fwdl_resp(struct mt7999_dev *dev)
{
	struct mt7999_firmware_download *fwdl = dev->fwdl;
	u32 dl, state, resp = mt76_rr(dev, MT_FWDL_RESP);
	u16 dl_type;

	switch (resp) {
	case MT_FWDL_RESP_FAILURE:
		fwdl->errno = u32_get_bits(mt76_rr(dev, MT_FWDL_ERROR_CODE),
					   MT_FWDL_ERROR_CODE_MASK);
		set_bit(FWDL_FAILED, &fwdl->action);
		break;
	case MT_FWDL_RESP_REQUEST_DL:
		state = mt76_rr(dev, MT_FWDL_COMM_STATE);
		if (state != MT_FWDL_COMM_STATE_MSG)
			dev_err(dev->mt76.dev,
				"Invalid communicate fwdl state (%d)\n", state);

		dl = mt76_rr(dev, MT_FWDL_DL);
		dl_type = u32_get_bits(dl, MT_FWDL_DL_TYPE);
		if (dl_type != MT_FWDL_RADIO_TYPE_WIFI)
			dev_err(dev->mt76.dev, "Invalid fwdl type (%d)\n", dl_type);

		fwdl->block_id = u32_get_bits(dl, MT_FWDL_DL_BLK);
		set_bit(FWDL_REQUEST_DL, &fwdl->action);
		break;
	default:
		dev_err(dev->mt76.dev, "Invalid fwdl resp (%d)\n", resp);
		break;
	}
}

static void mt7999_handle_fwdl_notify(struct mt7999_dev *dev)
{
	struct mt7999_firmware_download *fwdl = dev->fwdl;
	u32 notif = mt76_rr(dev, MT_FWDL_NOTIF);
	u16 id = u32_get_bits(notif, MT_FWDL_NOTIF_ID);
	u16 sn = u32_get_bits(notif, MT_FWDL_NOTIF_SN);
	u16 dl_type;

	switch (id) {
	case MT_FWDL_NOTIF_ID_RESP:
		mt7999_handle_fwdl_resp(dev);
		break;
	case MT_FWDL_NOTIF_ID_DL_DONE:
		dl_type = u32_get_bits(mt76_rr(dev, MT_FWDL_DL), MT_FWDL_DL_TYPE);

		if (dl_type != MT_FWDL_RADIO_TYPE_WIFI)
			dev_err(dev->mt76.dev, "Invalid fwdl type (%d)\n", dl_type);
		set_bit(FWDL_DONE, &fwdl->action);
		break;
	default:
		dev_err(dev->mt76.dev, "Invalid fwdl id (%d)\n", id);
		return;
	}

	mt76_rmw_field(dev, MT_FWDL_NOTIF_DONE, MT_FWDL_NOTIF_DONE_MASK, sn);
	mt76_wr(dev, MT_MAC_IREG_FWDL_DOORBELL, MT_MAC_IREG_FWDL_DOORBELL_MSG_ACK);
	complete(&fwdl->wait);
}

void mt7999_irq_tasklet_fwdl(struct tasklet_struct *t)
{
	struct mt7999_firmware_download *fwdl = from_tasklet(fwdl, t, irq_tasklet);
	struct mt7999_dev *dev = fwdl->dev;
	u32 intr = mt76_rr(dev, MT_MAC_IREG_ISTATUS);

	mt7999_irq_fwdl_disable(dev, intr & MT_MAC_IREG_IRQ_ALL);

	if (intr & MT_MAC_IREG_ISTATUS_EVENT) {
		mt7999_handle_fwdl_notify(dev);
		mt7999_irq_fwdl_enable(dev, MT_MAC_IREG_ISTATUS_EVENT);
	}

	if (intr & MT_MAC_IREG_ISTATUS_SLOT) {
		u16 sn = u32_get_bits(mt76_rr(dev, MT_FWDL_NOTIF), MT_FWDL_NOTIF_SN);

		fwdl->slot_done = mt76_rr(dev, MT_FWDL_SLOT_DONE);
		mt76_rmw_field(dev, MT_FWDL_NOTIF_DONE, MT_FWDL_NOTIF_DONE_MASK, sn);
		mt7999_irq_fwdl_enable(dev, MT_MAC_IREG_ISTATUS_SLOT);
	}

	if (intr & MT_MAC_IREG_ISTATUS_MSG_ACK) {
		u16 msg_sn = u32_get_bits(mt76_rr(dev, MT_FWDL_MSG_DONE),
					  MT_FWDL_MSG_DONE_SN);

		if (msg_sn != fwdl->msg_sn)
			dev_err(dev->mt76.dev,
				"Invalid SN. msg_sn = %d, msg_sn_done = %d\n",
				msg_sn, fwdl->msg_sn);

		fwdl->wait_ack = false;
		mt7999_irq_fwdl_enable(dev, MT_MAC_IREG_ISTATUS_MSG_ACK);
	}

	/* Read back FWDL interrupt status to avoid invalid interrupt */
	mt76_rr(dev, MT_MAC_IREG_ISTATUS);
}

irqreturn_t mt7999_irq_handler(int irq, void *dev_instance)
{
	struct mt7999_dev *dev = dev_instance;

	if (test_bit(MT76_STATE_MCU_FWDL, &dev->mphy.state)) {
		mt76_rmw(dev, MT_MAC_IREG_IMASK_ADDR, MT_MAC_IREG_IRQ_ALL, 0);
		tasklet_schedule(&dev->fwdl->irq_tasklet);

		return IRQ_HANDLED;
	}

	if (mtk_wed_device_active(&dev->mt76.mmio.wed))
		mtk_wed_device_irq_set_mask(&dev->mt76.mmio.wed, 0);
	else
		mt76_wr(dev, MT_INT_MASK_CSR, 0);

	if (dev->hif2) {
		if (mtk_wed_device_active(&dev->mt76.mmio.wed_hif2))
			mtk_wed_device_irq_set_mask(&dev->mt76.mmio.wed_hif2, 0);
		else
			mt76_wr(dev, MT_INT1_MASK_CSR, 0);
	}

	if (!test_bit(MT76_STATE_INITIALIZED, &dev->mphy.state))
		return IRQ_NONE;

	tasklet_schedule(&dev->mt76.irq_tasklet);

	return IRQ_HANDLED;
}

struct mt7999_dev *mt7999_mmio_probe(struct device *pdev,
				     void __iomem *mem_base, u32 device_id)
{
	static const struct mt76_driver_ops drv_ops = {
		/* txwi_size = txd size + txp size */
		.txwi_size = MT_TXD_SIZE + sizeof(struct mt76_connac_fw_txp),
		.link_data_size = sizeof(struct mt7999_vif_link),
		.drv_flags = MT_DRV_TXWI_NO_FREE |
			     MT_DRV_AMSDU_OFFLOAD |
			     MT_DRV_HW_MGMT_TXQ,
		.survey_flags = SURVEY_INFO_TIME_TX |
				SURVEY_INFO_TIME_RX |
				SURVEY_INFO_TIME_BSS_RX,
		.token_size = MT7999_SW_TOKEN_SIZE,
		.tx_prepare_skb = mt7999_tx_prepare_skb,
		.tx_complete_skb = mt76_connac_tx_complete_skb,
		.rx_skb = mt7999_queue_rx_skb,
		.rx_check = mt7999_rx_check,
		.rx_poll_complete = mt7999_rx_poll_complete,
		.rx_rro_ind_process = mt7999_rro_rx_process,
		.rx_rro_add_msdu_page = mt7999_rro_msdu_page_add,
		.update_survey = mt7999_update_channel,
		.set_channel = mt7999_set_channel,
		.vif_link_add = mt7999_vif_link_add,
		.vif_link_remove = mt7999_vif_link_remove,
	};
	struct mt7999_dev *dev;
	struct mt76_dev *mdev;
	int ret;

	mdev = mt76_alloc_device(pdev, sizeof(*dev), &mt7999_ops, &drv_ops);
	if (!mdev)
		return ERR_PTR(-ENOMEM);

	dev = container_of(mdev, struct mt7999_dev, mt76);

	ret = mt7999_mmio_init(mdev, mem_base, device_id);
	if (ret)
		goto error;

	tasklet_setup(&mdev->irq_tasklet, mt7999_irq_tasklet);

	mt76_wr(dev, MT_INT_MASK_CSR, 0);

	return dev;

error:
	mt76_free_device(&dev->mt76);

	return ERR_PTR(ret);
}

static int __init mt7999_init(void)
{
	int ret;

	ret = pci_register_driver(&mt7999_hif_driver);
	if (ret)
		return ret;

	ret = pci_register_driver(&mt7999_pci_driver);
	if (ret)
		pci_unregister_driver(&mt7999_hif_driver);

	return ret;
}

static void __exit mt7999_exit(void)
{
	pci_unregister_driver(&mt7999_pci_driver);
	pci_unregister_driver(&mt7999_hif_driver);
}

module_init(mt7999_init);
module_exit(mt7999_exit);
MODULE_DESCRIPTION("MediaTek MT7999 MMIO helpers");
MODULE_LICENSE("Dual BSD/GPL");
