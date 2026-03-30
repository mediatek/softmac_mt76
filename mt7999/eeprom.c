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

const struct ieee80211_channel dpd_2g_ch_list_bw20[] = {
	CHAN2G(3, 2422),
	CHAN2G(7, 2442),
	CHAN2G(11, 2462)
};

const struct ieee80211_channel dpd_5g_skip_ch_list[] = {
	CHAN5G(68, 5340),
	CHAN5G(72, 5360),
	CHAN5G(76, 5380),
	CHAN5G(80, 5400),
	CHAN5G(84, 5420),
	CHAN5G(88, 5440),
	CHAN5G(92, 5460),
	CHAN5G(96, 5480)
};

const struct ieee80211_channel dpd_5g_ch_list_bw80[] = {
	CHAN5G(42, 5210),
	CHAN5G(58, 5290),
	CHAN5G(106, 5530),
	CHAN5G(122, 5610),
	CHAN5G(138, 5690),
	CHAN5G(155, 5775),
	CHAN5G(171, 5855)
};

const struct ieee80211_channel dpd_5g_ch_list_bw160[] = {
	CHAN5G(50, 5250),
	CHAN5G(114, 5570),
	CHAN5G(163, 5815)
};

const struct ieee80211_channel dpd_6g_ch_list_bw80[] = {
	CHAN6G(7, 5985),
	CHAN6G(23, 6065),
	CHAN6G(39, 6145),
	CHAN6G(55, 6225),
	CHAN6G(71, 6305),
	CHAN6G(87, 6385),
	CHAN6G(103, 6465),
	CHAN6G(119, 6545),
	CHAN6G(135, 6625),
	CHAN6G(151, 6705),
	CHAN6G(167, 6785),
	CHAN6G(183, 6865),
	CHAN6G(199, 6945),
	CHAN6G(215, 7025)
};

const struct ieee80211_channel dpd_6g_ch_list_bw160[] = {
	CHAN6G(15, 6025),
	CHAN6G(47, 6185),
	CHAN6G(79, 6345),
	CHAN6G(111, 6505),
	CHAN6G(143, 6665),
	CHAN6G(175, 6825),
	CHAN6G(207, 6985)
};

const struct ieee80211_channel dpd_6g_ch_list_bw320[] = {
	CHAN6G(31, 6105),
	CHAN6G(63, 6265),
	CHAN6G(95, 6425),
	CHAN6G(127, 6585),
	CHAN6G(159, 6745),
	CHAN6G(191, 6905)
};

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

	if (!apply_cal)
		return 0;

	/* check if any calibrated data in efuse */
	ret = mt7999_mcu_get_eeprom(dev, MT_EE_WIFI_CONF_BASE, buf,
				    MT7999_EEPROM_BLOCK_SIZE, EEPROM_MODE_EFUSE);
	if (ret || !is_valid_ether_addr(buf + offset))
		return 0;

	/* load the calibrated data in efuse */
	eeprom = dev->mt76.eeprom.data;
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

	/* FIXME: remove it when uhr is supported */
	dev->has_uhr = false;

	return 0;
}

static int mt7999_eeprom_parse_band_config(struct mt7999_phy *phy)
{
	u8 *eeprom = phy->dev->mt76.eeprom.data;
	u32 val = eeprom[MT_EE_WIFI_CONF(phy->mt76->band_idx)];

	val = u32_get_bits(val, MT_EE_WIFI_CONF_BAND_SEL);
	switch (val) {
	case MT_EE_BAND_SEL_2GHZ:
		phy->mt76->cap.has_2ghz = true;
		break;
	case MT_EE_BAND_SEL_5GHZ:
		phy->mt76->cap.has_5ghz = true;
		break;
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

static void mt7999_eeprom_init_precal(struct mt7999_dev *dev)
{
#define MT76_CHANNELS_5GHZ_SIZE		36	/* ARRAY_SIZE(mt76_channels_5ghz) */
#define MT76_CHANNELS_6GHZ_SIZE		59	/* ARRAY_SIZE(mt76_channels_6ghz) */

	dev->prek.dpd_ch_num[DPD_CH_NUM_BW20_2G] = ARRAY_SIZE(dpd_2g_ch_list_bw20);
	dev->prek.dpd_ch_num[DPD_CH_NUM_BW20_5G_SKIP] = ARRAY_SIZE(dpd_5g_skip_ch_list);
	dev->prek.dpd_ch_num[DPD_CH_NUM_BW20_5G] = MT76_CHANNELS_5GHZ_SIZE -
						   DPD_CH_NUM(BW20_5G_SKIP);
	dev->prek.dpd_ch_num[DPD_CH_NUM_BW160_5G] = ARRAY_SIZE(dpd_5g_ch_list_bw160);
	dev->prek.dpd_ch_num[DPD_CH_NUM_BW20_6G] = MT76_CHANNELS_6GHZ_SIZE;
	dev->prek.dpd_ch_num[DPD_CH_NUM_BW160_6G] = ARRAY_SIZE(dpd_6g_ch_list_bw160);

	switch (mt76_chip(&dev->mt76)) {
	case MT7999_DEVICE_ID:
		dev->prek.rev = mt7999_prek_rev;
		if (dev->var.type == MT7999_VAR_TYPE_233)
			dev->prek.rev = mt7999_prek_rev_233;
		/* 5g & 6g bw 80 dpd channel list is not used */
		dev->prek.dpd_ch_num[DPD_CH_NUM_BW320_6G] = ARRAY_SIZE(dpd_6g_ch_list_bw320);
		break;
	default:
		dev->prek.rev = mt7999_prek_rev;
		break;
	}
}

static int mt7999_eeprom_load_precal(struct mt7999_dev *dev)
{
	struct mt76_dev *mdev = &dev->mt76;
	u8 *eeprom = mdev->eeprom.data;
	u32 val = eeprom[MT_EE_DO_PRE_CAL];
	u32 offs = MT_EE_PRECAL;
	int ret;

	if ((dev->eeprom_mode != EEPROM_MODE_FLASH &&
	     dev->eeprom_mode != EEPROM_MODE_BIN) || !val)
		return 0;

	mt7999_eeprom_init_precal(dev);

	dev->cal.size = MT_EE_CAL_GROUP_SIZE + MT_EE_CAL_DPD_SIZE;
	dev->cal.data = devm_kzalloc(mdev->dev, dev->cal.size, GFP_KERNEL);
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
	devm_kfree(dev->mt76.dev, dev->cal.data);
	dev->cal.data = NULL;
	dev->cal.size = 0;

	return ret;
}

int mt7999_eeprom_init(struct mt7999_dev *dev)
{
	int ret;

	ret = mt7999_eeprom_load(dev);
	if (ret)
		return ret;

	mt7999_eeprom_load_precal(dev);

	ret = mt7999_eeprom_parse_hw_cap(dev, &dev->phy);
	if (ret < 0)
		return ret;

	memcpy(dev->mphy.macaddr, dev->mt76.eeprom.data + MT_EE_MAC_ADDR(0), ETH_ALEN);

	return mt76_eeprom_override(&dev->mphy);
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
