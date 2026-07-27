// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (C) 2026 MediaTek Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include "mt7999.h"
#include "mac.h"
#include "../trace.h"

static bool hif2_enable = false;
module_param(hif2_enable, bool, 0644);

static int rro_mode = MT76_HWRRO_OFF;
module_param(rro_mode, int, 0644);

static LIST_HEAD(hif_list);
static DEFINE_SPINLOCK(hif_lock);
static u32 hif_idx;

static const struct pci_device_id mt7999_pci_device_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, MT7999_DEVICE_ID) },
	{ },
};

static const struct pci_device_id mt7999_hif_device_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, MT7999_DEVICE_ID_2) },
	{ },
};

static struct mt7999_hif *mt7999_pci_get_hif2(u32 idx)
{
	struct mt7999_hif *hif;
	u32 val;

	spin_lock_bh(&hif_lock);

	list_for_each_entry(hif, &hif_list, list) {
		val = readl(hif->regs + MT_PCIE_RECOG_ID);
		val &= MT_PCIE_RECOG_ID_MASK;
		if (val != idx)
			continue;

		get_device(hif->dev);
		goto out;
	}
	hif = NULL;

out:
	spin_unlock_bh(&hif_lock);

	return hif;
}

static void mt7999_put_hif2(struct mt7999_hif *hif)
{
	if (!hif)
		return;

	put_device(hif->dev);
}

static struct mt7999_hif *mt7999_pci_init_hif2(struct pci_dev *pdev)
{
	const struct pci_device_id *id;
	struct pci_dev *tmp_pdev = NULL;

	hif_idx++;

	if (!hif2_enable)
		return NULL;

	for (id = mt7999_hif_device_table; id->vendor; id++) {
		tmp_pdev = pci_get_device(id->vendor, id->device, NULL);
		if (tmp_pdev)
			break;
	}
	if (!tmp_pdev)
		return NULL;

	pci_dev_put(tmp_pdev);

	writel(u32_encode_bits(hif_idx, MT_PCIE_RECOG_ID_MASK),
	       pcim_iomap_table(pdev)[0] + MT_PCIE_RECOG_ID);

	return mt7999_pci_get_hif2(hif_idx);
}

static int mt7999_pci_hif2_probe(struct pci_dev *pdev)
{
	struct mt7999_hif *hif;

	if (!hif2_enable)
		return 0;

	hif = devm_kzalloc(&pdev->dev, sizeof(*hif), GFP_KERNEL);
	if (!hif)
		return -ENOMEM;

	hif->dev = &pdev->dev;
	hif->regs = pcim_iomap_table(pdev)[0];
	hif->irq = pdev->irq;
	pcie_bandwidth_available(pdev, NULL, &hif->speed, &hif->width);
	spin_lock_bh(&hif_lock);
	list_add(&hif->list, &hif_list);
	spin_unlock_bh(&hif_lock);
	pci_set_drvdata(pdev, hif);

	return 0;
}

static int mt7999_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	struct pci_dev *pdev_hif2;
	struct mt7999_dev *dev;
	struct mt76_dev *mdev;
	int ret;

	hif2_enable = (id->device == MT7999_DEVICE_ID ||
		       id->device == MT7999_DEVICE_ID_2);

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = pcim_iomap_regions(pdev, BIT(0), pci_name(pdev));
	if (ret)
		return ret;

	pci_set_master(pdev);

	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(36));
	if (ret)
		return ret;

	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	mt76_pci_disable_aspm(pdev);

	if (id->device == MT7999_DEVICE_ID_2)
		return mt7999_pci_hif2_probe(pdev);

	dev = mt7999_mmio_probe(&pdev->dev, pcim_iomap_table(pdev)[0],
				id->device);
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	mdev = &dev->mt76;
	mdev->hwrro_mode = rro_mode > MT76_HWRRO_V3_1 ? MT76_HWRRO_OFF : rro_mode;

	mt7999_wfsys_reset(dev);

	mt76_npu_init(mdev, pci_resource_start(pdev, 0),
		      pdev->bus && pci_domain_nr(pdev->bus) ? 3 : 2);

	/* Get IRQ and set handler */
	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_INTX);
	if (ret < 0)
		goto free_device;

	dev->irq = pci_irq_vector(pdev, 0);
	ret = devm_request_irq(mdev->dev, dev->irq, mt7999_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME, dev);
	if (ret)
		goto free_irq_vector;

	mt76_wr(dev, MT_INT_MASK_CSR, 0);
	/* master switch of PCIe interrupt enable */
	mt76_wr(dev, MT_PCIE_MAC_INT_ENABLE, 0xff);

	dev->hif2 = mt7999_pci_init_hif2(pdev);
	if (dev->hif2) {
		pdev_hif2 = container_of(dev->hif2->dev, struct pci_dev, dev);
		/* Get IRQ and set handler */
		ret = pci_alloc_irq_vectors(pdev_hif2, 1, 1, PCI_IRQ_INTX);
		if (ret < 0)
			goto free_irq;

		dev->hif2->irq = pci_irq_vector(pdev_hif2, 0);
		ret = devm_request_irq(mdev->dev, dev->hif2->irq,
				       mt7999_irq_handler, IRQF_SHARED,
				       KBUILD_MODNAME "-hif", dev);
		if (ret)
			goto free_hif2_irq_vector;

		mt76_wr(dev, MT_INT1_MASK_CSR, 0);
		/* master switch of PCIe tnterrupt enable */
		mt76_wr(dev, MT_PCIE1_MAC_INT_ENABLE, 0xff);
	}

	ret = mt7999_register_device(dev);
	if (ret)
		goto free_hif2_irq;

	return 0;

free_hif2_irq:
	if (dev->hif2)
		devm_free_irq(mdev->dev, dev->hif2->irq, dev);
free_hif2_irq_vector:
	if (dev->hif2)
		pci_free_irq_vectors(pdev_hif2);
free_irq:
	if (dev->hif2)
		put_device(dev->hif2->dev);
	devm_free_irq(mdev->dev, dev->irq, dev);
free_irq_vector:
	pci_free_irq_vectors(pdev);
free_device:
	mt76_free_device(&dev->mt76);

	return ret;
}

static void mt7999_hif_remove(struct pci_dev *pdev)
{
	struct mt7999_hif *hif = pci_get_drvdata(pdev);

	list_del(&hif->list);
}

static void mt7999_pci_remove(struct pci_dev *pdev)
{
	struct mt76_dev *mdev;
	struct mt7999_dev *dev;

	mdev = pci_get_drvdata(pdev);
	dev = container_of(mdev, struct mt7999_dev, mt76);
	mt7999_unregister_device(dev);

	if (dev->hif2) {
		struct pci_dev *pdev_hif2 =
			container_of(dev->hif2->dev, struct pci_dev, dev);

		devm_free_irq(mdev->dev, dev->hif2->irq, dev);
		pci_free_irq_vectors(pdev_hif2);
		if (dev->hif2->wed_irq)
			devm_free_irq(mdev->dev, dev->hif2->wed_irq, dev);
		mt7999_put_hif2(dev->hif2);
	}
	devm_free_irq(mdev->dev, dev->irq, dev);
	pci_free_irq_vectors(pdev);
	if (dev->wed_irq)
		devm_free_irq(mdev->dev, dev->wed_irq, dev);
	mt76_free_device(&dev->mt76);
}

struct pci_driver mt7999_hif_driver = {
	.name		= KBUILD_MODNAME "_hif",
	.id_table	= mt7999_hif_device_table,
	.probe		= mt7999_pci_probe,
	.remove		= mt7999_hif_remove,
};

struct pci_driver mt7999_pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= mt7999_pci_device_table,
	.probe		= mt7999_pci_probe,
	.remove		= mt7999_pci_remove,
};

MODULE_DEVICE_TABLE(pci, mt7999_pci_device_table);
MODULE_DEVICE_TABLE(pci, mt7999_hif_device_table);
MODULE_FIRMWARE(MT7999_FIRMWARE);
