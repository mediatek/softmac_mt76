// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (C) 2026 MediaTek Inc.
 */

#include <linux/firmware.h>
#include "mt7999.h"
#include "eeprom.h"
#include <linux/moduleparam.h>

static bool testmode_enable;
module_param(testmode_enable, bool, 0644);
MODULE_PARM_DESC(testmode_enable, "Enable testmode");

static bool apply_cal;
module_param(apply_cal, bool, 0644);
MODULE_PARM_DESC(apply_cal, "Apply the calibrated data in eFuse");

static int mt7999_check_eeprom(struct mt7999_dev *dev)
{
	u8 *eeprom = dev->mt76.eeprom.data;
	u16 val = get_unaligned_le16(eeprom);

	switch (val) {
	case MT7999_DEVICE_ID:
		return is_mt7999(&dev->mt76) ? 0 : -EINVAL;
	default:
		return -EINVAL;
	}
}

const char *mt7999_eeprom_name(struct mt7999_dev *dev)
{
	switch (mt76_chip(&dev->mt76)) {
	case MT7999_DEVICE_ID:
	default:
		switch (dev->var.type) {
		case MT7999_VAR_TYPE_233:
			if (dev->var.fem == MT7999_FEM_INT)
				return MT7999_EEPROM_DEFAULT_233_INT;
			return MT7999_EEPROM_DEFAULT_233;
		case MT7999_VAR_TYPE_444:
		default:
			if (dev->var.fem == MT7999_FEM_INT)
				return MT7999_EEPROM_DEFAULT_INT;
			return MT7999_EEPROM_DEFAULT;
		}
	}
}

static void
mt7999_eeprom_parse_stream(const u8 *eeprom, u8 band_idx, u8 *path,
			   u8 *rx_path, u8 *nss)
{
	u32 offset = MT_EE_WIFI_CONF(band_idx);

	*path = u8_get_bits(eeprom[offset + 1], MT_EE_WIFI_CONF_TX_PATH);
	*rx_path = u8_get_bits(eeprom[offset + 1], MT_EE_WIFI_CONF_RX_PATH);
	*nss = u8_get_bits(eeprom[offset + 2], MT_EE_WIFI_CONF_STREAM_NUM);
}

static bool mt7999_eeprom_variant_valid(struct mt7999_dev *dev, const u8 *def)
{
#define FEM_INT	0
#define FEM_EXT	3
	u8 *eeprom = dev->mt76.eeprom.data, fem[2];
	int i;

	for (i = 0; i < 2; i++)
		fem[i] = u8_get_bits(eeprom[MT_EE_WIFI_CONF(i)],
				     MT_EE_WIFI_CONF_PA_LNA);

	if (dev->var.fem == MT7999_FEM_EXT &&
	    !(fem[0] == FEM_EXT && fem[1] == FEM_EXT))
		return false;
	else if (dev->var.fem == MT7999_FEM_INT &&
		 !(fem[0] == FEM_INT && fem[1] == FEM_INT))
		return false;

	for (i = 0; i < __MT_MAX_BAND; i++) {
		u8 path, rx_path, nss;
		u8 def_path, def_rx_path, def_nss;

		if (!dev->mt76.phys[i])
			continue;

		mt7999_eeprom_parse_stream(eeprom, i, &path, &rx_path, &nss);
		mt7999_eeprom_parse_stream(def, i, &def_path, &def_rx_path,
					   &def_nss);
		if (path > def_path || rx_path > def_rx_path || nss > def_nss)
			return false;
	}

	return true;
}

static int
mt7999_eeprom_check_or_use_default(struct mt7999_dev *dev)
{
	u8 *eeprom = dev->mt76.eeprom.data;
	const struct firmware *fw = NULL;
	int ret;

	ret = request_firmware(&fw, mt7999_eeprom_name(dev), dev->mt76.dev);
	if (ret)
		return ret;

	if (!fw || !fw->data) {
		dev_err(dev->mt76.dev, "Invalid default bin\n");
		ret = -EINVAL;
		goto out;
	}

	if (dev->eeprom_mode == EEPROM_MODE_FLASH ||
	    dev->eeprom_mode == EEPROM_MODE_BIN) {
		u8 eep_ver, cur_eep_ver;

		eep_ver = fw->data[MT_EE_VERSION];
		cur_eep_ver = eeprom[MT_EE_VERSION];
		WARN(eep_ver > cur_eep_ver,
		     "Current eeprom version %x is outdated. Please reapply calibrated data\n",
		     cur_eep_ver);
	}

	if (dev->eeprom_mode != EEPROM_MODE_DEFAULT_BIN &&
	    mt7999_eeprom_variant_valid(dev, fw->data))
		goto out;

	memcpy(eeprom, fw->data, MT7999_EEPROM_SIZE);
	dev->eeprom_mode = EEPROM_MODE_DEFAULT_BIN;
	eeprom[MT_EE_TESTMODE_EN] = dev->testmode_enable;

out:
	release_firmware(fw);

	return ret;
}

static int
mt7999_eeprom_load_bin(struct mt7999_dev *dev)
{
	u8 *eeprom = dev->mt76.eeprom.data;
	const struct firmware *fw = NULL;
	int ret;

	ret = request_firmware(&fw, dev->mt76.bin_file_name, dev->mt76.dev);
	if (ret)
		return ret;

	if (!fw || !fw->data) {
		dev_err(dev->mt76.dev, "Invalid bin %s\n", dev->mt76.bin_file_name);
		ret = -EINVAL;
		goto out;
	}

	memcpy(eeprom, fw->data, MT7999_EEPROM_SIZE);

out:
	release_firmware(fw);
	return ret;
}

static int mt7999_eeprom_load_flash(struct mt7999_dev *dev)
{
	bool bin_file_mode;
	int ret = 1;

	/* return > 0 for load success, return 0 for load failed, return < 0 for no memory */
	bin_file_mode = mt76_check_bin_file_mode(&dev->mt76);
	if (bin_file_mode) {
		dev->mt76.eeprom.size = MT7999_EEPROM_SIZE;
		dev->mt76.eeprom.data = devm_kzalloc(dev->mt76.dev, dev->mt76.eeprom.size,
						     GFP_KERNEL);
		if (!dev->mt76.eeprom.data)
			return -ENOMEM;

		if (mt7999_eeprom_load_bin(dev))
			return 0;
	} else {
		ret = mt76_eeprom_init(&dev->mt76, MT7999_EEPROM_SIZE);
	}

	if (mt7999_check_eeprom(dev))
		return 0;

	dev->eeprom_mode = bin_file_mode ? EEPROM_MODE_BIN : EEPROM_MODE_FLASH;

	return ret;
}

int mt7999_eeprom_check_fw_mode(struct mt7999_dev *dev)
{
	u8 *eeprom;
	int ret;

	dev->testmode_enable = testmode_enable;

	/* load eeprom in flash or bin file mode to determine fw mode */
	ret = mt7999_eeprom_load_flash(dev);
	if (ret <= 0)
		goto out;

	eeprom = dev->mt76.eeprom.data;
	/* testmode enable priority: eeprom field > module parameter */
	dev->testmode_enable = eeprom[MT_EE_TESTMODE_EN];

out:
	return ret;
}

static int mt7999_eeprom_load(struct mt7999_dev *dev)
{
	u8 *eeprom, buf[MT7999_EEPROM_BLOCK_SIZE];
	u8 offset = MT_EE_MAC_ADDR_BASE % MT7999_EEPROM_BLOCK_SIZE;
	u32 block_num;
	int i, ret;
	u16 adie_ofs = MT7999_EFUSE_BASE_OFFS_ADIE1 - MT7999_EFUSE_BASE_OFFS_ADIE0;

	mt7999_eeprom_check_or_use_default(dev);

	/* check if any calibrated data in efuse */
	ret = mt7999_mcu_get_eeprom(dev, MT_EE_WIFI_CONF_BASE, buf,
				    MT7999_EEPROM_BLOCK_SIZE, EEPROM_MODE_EFUSE);
	if (ret || !is_valid_ether_addr(buf + offset))
		return 0;

	eeprom = dev->mt76.eeprom.data;
	if (!apply_cal) {
		dev->apply_cal = !memcmp(buf + offset, eeprom + MT_EE_MAC_ADDR_BASE, ETH_ALEN);
		return 0;
	}

	/* load the calibrated data in efuse */
	block_num = DIV_ROUND_UP(MT7999_EEPROM_CAL_DATA_SIZE, MT7999_EEPROM_BLOCK_SIZE);
	for (i = 0; i < block_num; i++) {
		u32 efuse_offset = i * MT7999_EEPROM_BLOCK_SIZE;
		u32 len = MT7999_EEPROM_BLOCK_SIZE;

		if (i == block_num - 1)
			len = MT7999_EEPROM_CAL_DATA_SIZE % MT7999_EEPROM_BLOCK_SIZE ?: len;
		if (efuse_offset >= MT7999_EFUSE_BASE_OFFS_ADIE0)
			efuse_offset += adie_ofs;

		memset(buf, 0, MT7999_EEPROM_BLOCK_SIZE);
		ret = mt7999_mcu_get_eeprom(dev, efuse_offset,
					    buf, len, EEPROM_MODE_EFUSE);
		if (ret) {
			if (ret != -EINVAL)
				return ret;
			continue;
		}

		memcpy(eeprom + i * MT7999_EEPROM_BLOCK_SIZE, buf, MT7999_EEPROM_BLOCK_SIZE);
	}

	dev->apply_cal = true;
	if (dev->eeprom_mode == EEPROM_MODE_DEFAULT_BIN)
		dev->eeprom_mode = EEPROM_MODE_EFUSE;

	return 0;
}

static int mt7999_eeprom_parse_efuse_hw_cap(struct mt7999_phy *phy,
					    u8 *path, u8 *rx_path, u8 *nss)
{
#define MODE_EHT_ONLY		BIT(0)
#define WTBL_SIZE_GROUP		GENMASK(31, 28)
#define STREAM_CAP(_offs)	((cap >> (_offs)) & GENMASK(2, 0))
	struct mt7999_dev *dev = phy->dev;
	u32 cap = 0;
	int ret;

	ret = mt7999_mcu_get_chip_config(dev, &cap);
	if (ret)
		return ret;

	if (cap) {
		u8 band_offs = phy->mt76->band_idx * 3;

		dev->has_uhr = !(cap & MODE_EHT_ONLY);
		*nss = min_t(u8, *nss, STREAM_CAP(1 + band_offs));
		*path = min_t(u8, *path, STREAM_CAP(10 + band_offs));
		*rx_path = min_t(u8, *rx_path, STREAM_CAP(19 + band_offs));
		dev->wtbl_size_group = u32_get_bits(cap, WTBL_SIZE_GROUP);
	} else {
		/* for internal RFB without efuse hw cap */
		dev->has_uhr = true;
		dev->wtbl_size_group = 6;
	}

	return 0;
}

static int mt7999_eeprom_parse_band_config(struct mt7999_phy *phy)
{
	u8 *eeprom = phy->dev->mt76.eeprom.data;
	u32 val = eeprom[MT_EE_WIFI_CONF(phy->mt76->band_idx)];

	val = u32_get_bits(val, MT_EE_WIFI_CONF_BAND_SEL);
	phy->mt76->cap.sel = RADIO_SEL_FULL;
	switch (val) {
	case MT_EE_BAND_SEL_2GHZ:
		phy->mt76->cap.has_2ghz = true;
		break;
	case MT_EE_BAND_SEL_5GHZ_LOW:
	case MT_EE_BAND_SEL_5GHZ_HIGH:
		phy->mt76->cap.sel = val == MT_EE_BAND_SEL_5GHZ_HIGH ?
				     RADIO_SEL_HIGH : RADIO_SEL_LOW;
		fallthrough;
	case MT_EE_BAND_SEL_5GHZ:
		phy->mt76->cap.has_5ghz = true;
		break;
	case MT_EE_BAND_SEL_6GHZ_LOW:
	case MT_EE_BAND_SEL_6GHZ_HIGH:
		phy->mt76->cap.sel = val == MT_EE_BAND_SEL_6GHZ_HIGH ?
				     RADIO_SEL_HIGH : RADIO_SEL_LOW;
		fallthrough;
	case MT_EE_BAND_SEL_6GHZ:
		phy->mt76->cap.has_6ghz = true;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int mt7999_eeprom_parse_hw_cap(struct mt7999_dev *dev, struct mt7999_phy *phy)
{
	u8 path, rx_path, nss, band_idx = phy->mt76->band_idx;
	u8 *eeprom = dev->mt76.eeprom.data;
	struct mt76_phy *mphy = phy->mt76;
	int max_path = 5, max_nss = 4;
	int ret;

	mt7999_eeprom_parse_stream(eeprom, band_idx, &path, &rx_path, &nss);
	ret = mt7999_eeprom_parse_efuse_hw_cap(phy, &path, &rx_path, &nss);
	if (ret)
		return ret;

	if (!path || path > max_path)
		path = max_path;

	if (!nss || nss > max_nss)
		nss = max_nss;

	nss = min_t(u8, nss, path);

	if (path != rx_path)
		phy->has_aux_rx = true;

	mphy->antenna_mask = BIT(nss) - 1;
	phy->orig_antenna_mask = mphy->antenna_mask;
	mphy->chainmask = BIT(path) - 1;
	phy->orig_chainmask = mphy->chainmask;
	if (band_idx < MT_BAND2)
		dev->mt76.chainshift[band_idx + 1] = dev->mt76.chainshift[band_idx] +
						     hweight16(mphy->chainmask);

	return mt7999_eeprom_parse_band_config(phy);
}

int mt7999_eeprom_init(struct mt7999_dev *dev)
{
	int ret;

	ret = mt7999_eeprom_load(dev);
	if (ret)
		return ret;

	ret = mt7999_eeprom_parse_hw_cap(dev, &dev->phy);
	if (ret)
		return ret;

	memcpy(dev->mphy.macaddr, dev->mt76.eeprom.data + MT_EE_MAC_ADDR(0), ETH_ALEN);

	return mt76_eeprom_override(&dev->mphy);
}

static int
mt7999_eeprom_load_precal_binfile(struct mt7999_dev *dev, u32 offs, u32 size)
{
	const struct firmware *fw = NULL;
	int ret;

	ret = request_firmware(&fw, dev->mt76.bin_file_name, dev->mt76.dev);
	if (ret)
		return ret;

	if (!fw || !fw->data) {
		dev_err(dev->mt76.dev, "Invalid bin (bin file mode), load precal fail\n");
		ret = -EINVAL;
		goto out;
	}

	memcpy(dev->cal.data, fw->data + offs, size);

out:
	release_firmware(fw);

	return ret;
}

static int mt7999_eeprom_load_precal(struct mt7999_dev *dev)
{
	struct mt76_dev *mdev = &dev->mt76;
	u8 *eeprom = mdev->eeprom.data;
	u32 offs = MT_EE_PRECAL;
	int ret, i, val = 0;

	for (i = MT_BAND0; i < __MT_MAX_BAND; i++)
		val |= eeprom[MT_EE_DO_PRE_CAL(i)];
	if (!val)
		return 0;

	dev->cal.data = kvzalloc(dev->cal.size, GFP_KERNEL);
	if (!dev->cal.data) {
		ret = -ENOMEM;
		goto fail;
	}

	if (dev->eeprom_mode == EEPROM_MODE_BIN) {
		ret = mt7999_eeprom_load_precal_binfile(dev, offs, dev->cal.size);
		if (ret)
			goto fail;
		return 0;
	}

	ret = mt76_get_of_data_from_mtd(mdev, dev->cal.data, offs, dev->cal.size);
	if (!ret)
		return 0;

	ret = mt76_get_of_data_from_nvmem(mdev, dev->cal.data, "precal", dev->cal.size);
	if (!ret)
		return 0;

fail:
	dev_warn(dev->mt76.dev, "Failed to load precal data: %d\n", ret);
	kvfree(dev->cal.data);
	dev->cal.data = NULL;

	return ret;
}

int mt7999_eeprom_get_prek_category(struct mt7999_phy *phy, bool group)
{
	/* group prek for 5g/6g high & low are not supported yet */
	if (phy->mt76->cap.has_5ghz) {
		if (group)
			return PREK_CATEGORY_5G_GROUP_FULL;

		switch (phy->mt76->cap.sel) {
		case RADIO_SEL_LOW:
			return PREK_CATEGORY_5G_CH_LOW;
		case RADIO_SEL_HIGH:
			return PREK_CATEGORY_5G_CH_HIGH;
		case RADIO_SEL_FULL:
		default:
			return PREK_CATEGORY_5G_CH_FULL;
		}
	} else if (phy->mt76->cap.has_6ghz) {
		if (group)
			return PREK_CATEGORY_6G_GROUP_FULL;

		switch (phy->mt76->cap.sel) {
		case RADIO_SEL_LOW:
			return PREK_CATEGORY_6G_CH_LOW;
		case RADIO_SEL_HIGH:
			return PREK_CATEGORY_6G_CH_HIGH;
		case RADIO_SEL_FULL:
		default:
			return PREK_CATEGORY_6G_CH_FULL;
		}
	} else {
		if (group)
			return PREK_CATEGORY_2G_GROUP;
		return PREK_CATEGORY_2G_CH;
	}
}

int mt7999_eeprom_get_prek_block_header(struct mt7999_phy *phy, bool group)
{
	struct mt7999_dev *dev = phy->dev;
	struct prek_block_header *hdr;
	int category;
	u8 *p, *end;

	p = dev->cal.data;
	end = p + dev->cal.size;

	dev->prek.cur_hdr = (struct prek_block_header *)dev->cal.data;
	category = mt7999_eeprom_get_prek_category(phy, group);
	while (p < end) {
		u32 length;

		hdr = (struct prek_block_header *)p;
		length = le32_to_cpu(hdr->length);
		if (!length || hdr->category == category) {
			dev->prek.cur_hdr = hdr;
			if (!length)
				break;
			return hdr->status != PREK_STATUS_DONE ? -EINVAL : 0;
		}

		p += length;
	}

	return -ENODATA;
}

struct prek_dpd_ch_header *
mt7999_eeprom_get_dpd_ch_header(struct mt7999_phy *phy, u16 channel,
				enum dpd_ch_bw bw)
{
	struct mt7999_dev *dev = phy->dev;
	struct prek_block_header *hdr = dev->prek.cur_hdr;
	struct prek_dpd_ch_header *ch_hdr;
	u8 *p, *end;

	p = (u8 *)hdr + sizeof(*hdr);
	end = p + le32_to_cpu(hdr->data_offset);

	while (p < end) {
		ch_hdr = (struct prek_dpd_ch_header *)p;
		if (ch_hdr->bw == bw && le16_to_cpu(ch_hdr->channel) == channel)
			return ch_hdr;

		p += sizeof(*ch_hdr);
	}

	return NULL;
}

static void
mt7999_eeprom_parse_precal(struct mt7999_dev *dev)
{
	int band;

	if (!dev->cal.data)
		return;

	for (band = MT_BAND0; band < __MT_MAX_BAND; band++) {
		struct prek_dpd_ch_header *ch_hdr;
		struct mt7999_phy *phy;
		int bw;

		phy = __mt7999_phy(dev, band);
		if (!phy)
			continue;

		if (mt7999_eeprom_get_prek_block_header(phy, false))
			continue;

		for (bw = DPD_CH_BW_20; bw < DPD_CH_BW_NUM; bw++) {
			struct dpd_ch_info *ch_info = &dev->prek.ch_info[band][bw];
			struct dpd_ch *list = ch_info->list;
			int i;

			if (!list)
				break;

			for (i = 0; i < ch_info->ch_num; i++, ch_hdr++) {
				ch_hdr = mt7999_eeprom_get_dpd_ch_header(phy, list[i].chan, bw);
				list[i].valid = ch_hdr && ch_hdr->status == PREK_STATUS_DONE;
			}
		}
	}

	dev->prek.cur_hdr = NULL;
}

int mt7999_eeprom_init_precal(struct mt7999_dev *dev)
{
	struct dpd_ch dpd_2g_ch_list_bw20[] = {
		DPD_CH(1), DPD_CH(2), DPD_CH(3), DPD_CH(4),
		DPD_CH(5), DPD_CH(6), DPD_CH(7), DPD_CH(8),
		DPD_CH(9), DPD_CH(10), DPD_CH(11), DPD_CH(12),
		DPD_CH(13)
	};
	struct dpd_ch dpd_2g_ch_list_bw40[] = {
		DPD_CH(3), DPD_CH(4), DPD_CH(5), DPD_CH(6),
		DPD_CH(7), DPD_CH(8), DPD_CH(9), DPD_CH(10),
		DPD_CH(11)
	};
	struct dpd_ch dpd_5g_ch_list_bw20[] = {
		DPD_CH(8), DPD_CH(12), DPD_CH(16), DPD_CH(36),
		DPD_CH(40), DPD_CH(44), DPD_CH(48), DPD_CH(52),
		DPD_CH(56), DPD_CH(60), DPD_CH(64), DPD_CH(68),
		DPD_CH(72), DPD_CH(76), DPD_CH(80), DPD_CH(84),
		DPD_CH(88), DPD_CH(92), DPD_CH(96), DPD_CH(100),
		DPD_CH(104), DPD_CH(108), DPD_CH(112), DPD_CH(116),
		DPD_CH(120), DPD_CH(124), DPD_CH(128), DPD_CH(132),
		DPD_CH(136), DPD_CH(140), DPD_CH(144), DPD_CH(149),
		DPD_CH(153), DPD_CH(157), DPD_CH(161), DPD_CH(165),
		DPD_CH(169), DPD_CH(173), DPD_CH(177), DPD_CH(181),
		DPD_CH(184), DPD_CH(188), DPD_CH(192), DPD_CH(196)
	};
	struct dpd_ch dpd_5g_ch_list_bw40[] = {
		DPD_CH(10), DPD_CH(38), DPD_CH(46), DPD_CH(54),
		DPD_CH(62), DPD_CH(70), DPD_CH(78), DPD_CH(86),
		DPD_CH(94), DPD_CH(102), DPD_CH(110), DPD_CH(118),
		DPD_CH(126), DPD_CH(134), DPD_CH(142), DPD_CH(151),
		DPD_CH(159), DPD_CH(167), DPD_CH(175), DPD_CH(186),
		DPD_CH(194)
	};
	struct dpd_ch dpd_5g_ch_list_bw80[] = {
		DPD_CH(42), DPD_CH(58), DPD_CH(74), DPD_CH(90),
		DPD_CH(106), DPD_CH(122), DPD_CH(138), DPD_CH(155),
		DPD_CH(171)
	};
	struct dpd_ch dpd_5g_ch_list_bw160[] = {
		DPD_CH(50), DPD_CH(82), DPD_CH(114), DPD_CH(163)
	};
	struct dpd_ch dpd_5g_ch_list_bw320[] = {
		DPD_CH(130), DPD_CH(147)
	};
	struct dpd_ch dpd_6g_ch_list_bw20[] = {
		DPD_CH(1), DPD_CH(2), DPD_CH(5), DPD_CH(9),
		DPD_CH(13), DPD_CH(17), DPD_CH(21), DPD_CH(25),
		DPD_CH(29), DPD_CH(33), DPD_CH(37), DPD_CH(41),
		DPD_CH(45), DPD_CH(49), DPD_CH(53), DPD_CH(57),
		DPD_CH(61), DPD_CH(65), DPD_CH(69), DPD_CH(73),
		DPD_CH(77), DPD_CH(81), DPD_CH(85), DPD_CH(89),
		DPD_CH(93), DPD_CH(97), DPD_CH(101), DPD_CH(105),
		DPD_CH(109), DPD_CH(113), DPD_CH(117), DPD_CH(121),
		DPD_CH(125), DPD_CH(129), DPD_CH(133), DPD_CH(137),
		DPD_CH(141), DPD_CH(145), DPD_CH(149), DPD_CH(153),
		DPD_CH(157), DPD_CH(161), DPD_CH(165), DPD_CH(169),
		DPD_CH(173), DPD_CH(177), DPD_CH(181), DPD_CH(185),
		DPD_CH(189), DPD_CH(193), DPD_CH(197), DPD_CH(201),
		DPD_CH(205), DPD_CH(209), DPD_CH(213), DPD_CH(217),
		DPD_CH(221), DPD_CH(225), DPD_CH(229), DPD_CH(233),
		DPD_CH(237), DPD_CH(241), DPD_CH(245), DPD_CH(249),
		DPD_CH(253), DPD_CH(257)
	};
	struct dpd_ch dpd_6g_ch_list_bw40[] = {
		DPD_CH(3), DPD_CH(11), DPD_CH(19), DPD_CH(27),
		DPD_CH(35), DPD_CH(43), DPD_CH(51), DPD_CH(59),
		DPD_CH(67), DPD_CH(75), DPD_CH(83), DPD_CH(91),
		DPD_CH(99), DPD_CH(107), DPD_CH(115), DPD_CH(123),
		DPD_CH(131), DPD_CH(139), DPD_CH(147), DPD_CH(155),
		DPD_CH(163), DPD_CH(171), DPD_CH(179), DPD_CH(187),
		DPD_CH(195), DPD_CH(203), DPD_CH(211), DPD_CH(219),
		DPD_CH(227), DPD_CH(235), DPD_CH(243), DPD_CH(251)
	};
	struct dpd_ch dpd_6g_ch_list_bw80[] = {
		DPD_CH(7), DPD_CH(23), DPD_CH(39), DPD_CH(55),
		DPD_CH(71), DPD_CH(87), DPD_CH(103), DPD_CH(119),
		DPD_CH(135), DPD_CH(151), DPD_CH(167), DPD_CH(183),
		DPD_CH(199), DPD_CH(215), DPD_CH(231), DPD_CH(247)
	};
	struct dpd_ch dpd_6g_ch_list_bw160[] = {
		DPD_CH(15), DPD_CH(47), DPD_CH(79), DPD_CH(111),
		DPD_CH(143), DPD_CH(175), DPD_CH(207), DPD_CH(239)
	};
	struct dpd_ch dpd_6g_ch_list_bw320[] = {
		DPD_CH(31), DPD_CH(63), DPD_CH(95), DPD_CH(127),
		DPD_CH(159), DPD_CH(191), DPD_CH(223)
	};
	struct mt7999_phy *phy;
	int band;

	if (dev->eeprom_mode != EEPROM_MODE_FLASH &&
	    dev->eeprom_mode != EEPROM_MODE_BIN)
		return 0;

	switch (mt76_chip(&dev->mt76)) {
	case MT7999_DEVICE_ID:
	default:
		dev->prek.rev = mt7999_prek_rev;
		break;
	}

	dev->cal.size = 0;
	dev->cal.data = NULL;
	for (band = MT_BAND0; band < __MT_MAX_BAND; band++) {
		dev->prek.dpd_size[band] = 0;
		dev->prek.dpd_ch_num[band] = 0;
		phy = __mt7999_phy(dev, band);
		if (!phy)
			continue;

#define set_dpd_ch(_radio, _bw)									\
	do {											\
		struct dpd_ch *__ch_list = dpd_ ## _radio ## g_ch_list_bw ## _bw;		\
		int __ch_num = ARRAY_SIZE(dpd_ ## _radio ## g_ch_list_bw ## _bw);		\
		int __bw = DPD_CH_BW_ ## _bw;							\
												\
		dev->prek.ch_info[band][__bw].list =						\
			devm_kmemdup(dev->mt76.dev, __ch_list,					\
				     sizeof(*__ch_list) * __ch_num, GFP_KERNEL);		\
		if (!dev->prek.ch_info[band][__bw].list)					\
			return -ENOMEM;								\
		dev->prek.ch_info[band][__bw].ch_num = __ch_num;				\
		dev->prek.dpd_ch_num[band] += __ch_num;						\
		dev->prek.dpd_size[band] += __ch_num * PREK(DPD_PER_CH_SIZE);			\
	} while (0)
		if (phy->mt76->cap.has_5ghz) {
			dev->prek.group_size[band] = MT_EE_CAL_GROUP_SIZE(5);
			set_dpd_ch(5, 20);
			set_dpd_ch(5, 40);
			set_dpd_ch(5, 80);
			set_dpd_ch(5, 160);
			set_dpd_ch(5, 320);
		} else if (phy->mt76->cap.has_6ghz) {
			dev->prek.group_size[band] = MT_EE_CAL_GROUP_SIZE(6);
			set_dpd_ch(6, 20);
			set_dpd_ch(6, 40);
			set_dpd_ch(6, 80);
			set_dpd_ch(6, 160);
			set_dpd_ch(6, 320);
		} else {
			dev->prek.group_size[band] = MT_EE_CAL_GROUP_SIZE(2);
			set_dpd_ch(2, 20);
			set_dpd_ch(2, 40);
		}

		dev->cal.size += dev->prek.group_size[band] + dev->prek.dpd_size[band];
	}
#undef set_ch_list

	mt7999_eeprom_load_precal(dev);
	mt7999_eeprom_parse_precal(dev);

	return 0;
}

int mt7999_eeprom_get_target_power(struct mt7999_dev *dev,
				   struct ieee80211_channel *chan)
{
	u8 *eeprom = dev->mt76.eeprom.data;
	int target_power;

	if (chan->band == NL80211_BAND_5GHZ)
		target_power = eeprom[MT_EE_TX0_POWER_5G +
				      mt7999_get_channel_group_5g(chan->hw_value)];
	else if (chan->band == NL80211_BAND_6GHZ)
		target_power = eeprom[MT_EE_TX0_POWER_6G +
				      mt7999_get_channel_group_6g(chan->hw_value)];
	else
		target_power = eeprom[MT_EE_TX0_POWER_2G];

	return target_power;
}

s8 mt7999_eeprom_get_power_delta(struct mt7999_dev *dev, int band)
{
	u8 *eeprom = dev->mt76.eeprom.data;
	u32 val;
	s8 delta;

	if (band == NL80211_BAND_5GHZ)
		val = eeprom[MT_EE_RATE_DELTA_5G];
	else if (band == NL80211_BAND_6GHZ)
		val = eeprom[MT_EE_RATE_DELTA_6G];
	else
		val = eeprom[MT_EE_RATE_DELTA_2G];

	if (!(val & MT_EE_RATE_DELTA_EN))
		return 0;

	delta = FIELD_GET(MT_EE_RATE_DELTA_MASK, val);

	return val & MT_EE_RATE_DELTA_SIGN ? delta : -delta;
}

bool mt7999_eeprom_has_background_radar(struct mt7999_dev *dev)
{
	switch (mt76_chip(&dev->mt76)) {
	case MT7999_DEVICE_ID:
		if (dev->var.type == MT7999_VAR_TYPE_233)
			return false;
		break;
	default:
		return false;
	}

	return true;
}

const u8 mt7999_sku_group_len[] = {
	[SKU_CCK] = 4,
	[SKU_OFDM] = 8,
	[SKU_HT20] = 8,
	[SKU_HT40] = 9,
	[SKU_VHT20] = 12,
	[SKU_VHT40] = 12,
	[SKU_VHT80] = 12,
	[SKU_VHT160] = 12,
	[SKU_HE26] = 12,
	[SKU_HE52] = 12,
	[SKU_HE106] = 12,
	[SKU_HE242] = 12,
	[SKU_HE484] = 12,
	[SKU_HE996] = 12,
	[SKU_HE2x996] = 12,
	[SKU_EHT26] = 16,
	[SKU_EHT52] = 16,
	[SKU_EHT106] = 16,
	[SKU_EHT242] = 16,
	[SKU_EHT484] = 16,
	[SKU_EHT996] = 16,
	[SKU_EHT2x996] = 16,
	[SKU_EHT4x996] = 16,
	[SKU_EHT26_52] = 16,
	[SKU_EHT26_106] = 16,
	[SKU_EHT484_242] = 16,
	[SKU_EHT996_484] = 16,
	[SKU_EHT996_484_242] = 16,
	[SKU_EHT2x996_484] = 16,
	[SKU_EHT3x996] = 16,
	[SKU_EHT3x996_484] = 16,
	[SKU_UHR26] = 20,
	[SKU_UHR52] = 20,
	[SKU_UHR106] = 20,
	[SKU_UHR242] = 20,
	[SKU_UHR484] = 20,
	[SKU_UHR996] = 20,
	[SKU_UHR2x996] = 20,
	[SKU_UHR4x996] = 20,
	[SKU_UHR26_52] = 20,
	[SKU_UHR26_106] = 20,
	[SKU_UHR484_242] = 20,
	[SKU_UHR996_484] = 20,
	[SKU_UHR996_484_242] = 20,
	[SKU_UHR2x996_484] = 20,
	[SKU_UHR3x996] = 20,
	[SKU_UHR3x996_484] = 20,
};
