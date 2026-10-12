/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (C) 2026 MediaTek Inc.
 */

#include <linux/devcoredump.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/utsname.h>
#include "coredump.h"

static bool coredump_memdump = true;
module_param(coredump_memdump, bool, 0644);
MODULE_PARM_DESC(coredump_memdump, "Optional ability to dump firmware memory");

static const struct mt7999_mem_region mt7999_wm_mem_regions[] = {
	{
		.start = 0x00800000,
		.len = 0x0001bfff,
		.name = "ULM0",
	},
	{
		.start = 0x00900000,
		.len = 0x0006ffff,
		.name = "ULM1",
	},
	{
		.start = 0x02200000,
		.len = 0x0007ffff,
		.name = "ULM2",
	},
	{
		.start = 0x00400000,
		.len = 0x0002bfff,
		.name = "SRAM",
	},
	{
		.start = 0xe0000000,
		.len = 0x003fffff,
		.name = "CRAM",
	},
	{
		.start = 0x7c050000,
		.len = 0x00005fff,
		.name = "CONN_INFRA",
	},
};

const struct mt7999_mem_region*
mt7999_coredump_get_mem_layout(struct mt7999_dev *dev, u8 type, u32 *num)
{
	switch (mt76_chip(&dev->mt76)) {
	case MT7999_DEVICE_ID:
	case MT7999_DEVICE_ID_2:
		*num = ARRAY_SIZE(mt7999_wm_mem_regions);
		return &mt7999_wm_mem_regions[0];
	default:
		return NULL;
	}
}

static int mt7999_coredump_get_mem_size(struct mt7999_dev *dev, u8 type)
{
	const struct mt7999_mem_region *mem_region;
	size_t size = 0;
	u32 num;
	int i;

	mem_region = mt7999_coredump_get_mem_layout(dev, type, &num);
	if (!mem_region)
		return 0;

	for (i = 0; i < num; i++) {
		size += mem_region->len;
		mem_region++;
	}

	/* reserve space for the headers */
	size += num * sizeof(struct mt7999_mem_hdr);
	/* make sure it is aligned 4 bytes for debug message print out */
	size = ALIGN(size, 4);

	return size;
}

struct mt7999_crash_data *mt7999_coredump_new(struct mt7999_dev *dev, u8 type)
{
	struct mt7999_crash_data *crash_data = dev->coredump.crash_data[type];

	lockdep_assert_held(&dev->dump_mutex);

	if (!coredump_memdump || !crash_data->supported)
		return NULL;

	guid_gen(&crash_data->guid);
	ktime_get_real_ts64(&crash_data->timestamp);

	return crash_data;
}

static void
mt7999_coredump_fw_state(struct mt7999_dev *dev, u8 type, struct mt7999_coredump *dump,
			 bool *exception)
{
	u32 count, coredump_reg;

	switch (mt76_chip(&dev->mt76)) {
	case MT7999_DEVICE_ID:
	case MT7999_DEVICE_ID_2:
		coredump_reg = mt76_rr(dev, 0x7c0548d0) + 4;
		break;
	default:
		dev_info(dev->mt76.dev, "%s coredump not supported\n",
			 wiphy_name(dev->mt76.hw->wiphy));
		return;
	}

	count = mt76_rr(dev, coredump_reg);
	if (count == 0xdeadbeef)
		return;
	/* normal mode: driver can manually trigger assert for detail info */
	if (!count)
		strscpy(dump->fw_state, "normal", sizeof(dump->fw_state));
	else
		strscpy(dump->fw_state, "exception", sizeof(dump->fw_state));

	*exception = !!count;
}

static void
mt7999_coredump_fw_stack(struct mt7999_dev *dev, u8 type, struct mt7999_coredump *dump,
			 bool exception)
{
	u32 i, val;

	/* Disable mcu monflag */
	mt76_wr(dev, MT_CONN_HOST_CSR_MCU_OFF, 0x1);

	/* Select mcu debug mode */
	val = mt76_rr(dev, MT_CONN_HOST_CSR_MCU_SEL);
	mt76_wr(dev, MT_CONN_HOST_CSR_MCU_SEL, (val & 0x1f) | 0x3);

	/* Debug flag setting for PC */
	mt76_wr(dev, MT_CONN_DBG_CTL_MCU_DBG_EN, 0x1);
	val = mt76_rr(dev, MT_CONN_DBG_CTL_MCU_DBG_SEL);
	mt76_wr(dev, MT_CONN_DBG_CTL_MCU_DBG_SEL,
		(val & MT_CONN_DBG_CTL_MCU_DBG_SEL_FR) | 0x046890a0);

	/* choose 33th PC log buffer to read current PC index */
	mt76_wr(dev, MT_CONN_DBG_CTL_PC_LOG_SEL, 0x22);

	/* read current PC */
	for (i = 0; i < 10; i++)
		dump->pc_cur[i] = mt76_rr(dev, MT_CONN_DBG_CTL_MCU_DBG);

	/* read PC log */
	mt76_wr(dev, MT_CONN_DBG_CTL_PC_LOG_SEL, 0x20);
	dump->pc_dbg_ctrl = mt76_rr(dev, MT_CONN_DBG_CTL_MCU_DBG);

	for (i = 0; i < 32; i++) {
		mt76_wr(dev, MT_CONN_DBG_CTL_PC_LOG_SEL, i);
		dump->pc_stack[i] = mt76_rr(dev, MT_CONN_DBG_CTL_MCU_DBG);
	}

	/* Debug flag setting for LR */
	mt76_wr(dev, MT_CONN_DBG_CTL_MCU_DBG_EN, 0x1);
	val = mt76_rr(dev, MT_CONN_DBG_CTL_MCU_DBG_SEL);
	mt76_wr(dev, MT_CONN_DBG_CTL_MCU_DBG_SEL,
		(val & MT_CONN_DBG_CTL_MCU_DBG_SEL_FR) | 0x04e992a4);

	/* read current LR */
	mt76_wr(dev, MT_CONN_DBG_CTL_GPR_LOG_SEL, 0x22);
	dump->lr_cur_idx = mt76_rr(dev, MT_CONN_DBG_CTL_MCU_DBG);

	/* read LR log */
	mt76_wr(dev, MT_CONN_DBG_CTL_GPR_LOG_SEL, 0x20);
	dump->lr_dbg_ctrl = mt76_rr(dev, MT_CONN_DBG_CTL_MCU_DBG);

	for (i = 0; i < 32; i++) {
		mt76_wr(dev, MT_CONN_DBG_CTL_GPR_LOG_SEL, i);
		dump->lr_stack[i] = mt76_rr(dev, MT_CONN_DBG_CTL_MCU_DBG);
	}
}

struct mt7999_coredump *mt7999_coredump_build(struct mt7999_dev *dev, u8 type, bool full_dump)
{
	struct mt7999_crash_data *crash_data = dev->coredump.crash_data[type];
	struct mt7999_coredump *dump;
	struct mt7999_coredump_mem *dump_mem;
	size_t len, sofar = 0, hdr_len = sizeof(*dump);
	unsigned char *buf;
	bool exception;

	len = hdr_len;

	if (full_dump && coredump_memdump && crash_data->memdump_buf_len)
		len += sizeof(*dump_mem) + crash_data->memdump_buf_len;

	sofar += hdr_len;

	/* this is going to get big when we start dumping memory and such,
	 * so go ahead and use vmalloc.
	 */
	buf = vzalloc(len);
	if (!buf)
		return NULL;

	mutex_lock(&dev->dump_mutex);

	dump = (struct mt7999_coredump *)(buf);
	dump->len = len;
	dump->hdr_len = hdr_len;

	/* plain text */
	strscpy(dump->magic, "mt76-crash-dump", sizeof(dump->magic));
	strscpy(dump->kernel, init_utsname()->release, sizeof(dump->kernel));
	strscpy(dump->fw_type, "WM", sizeof(dump->fw_type));
	strscpy(dump->fw_ver, dev->mt76.hw->wiphy->fw_version,
		sizeof(dump->fw_ver));
	strscpy(dump->fw_ram_date[MT7999_RAM_TYPE_WM],
		dev->fw_build_date, MT7999_BUILD_TIME_LEN);

	guid_copy(&dump->guid, &crash_data->guid);
	dump->tv_sec = crash_data->timestamp.tv_sec;
	dump->tv_nsec = crash_data->timestamp.tv_nsec;
	dump->device_id = mt76_chip(&dev->mt76);

	mt7999_coredump_fw_state(dev, type, dump, &exception);
	mt7999_coredump_fw_stack(dev, type, dump, exception);

	if (!full_dump)
		goto skip_dump_mem;

	/* gather memory content */
	dump_mem = (struct mt7999_coredump_mem *)(buf + sofar);
	dump_mem->len = crash_data->memdump_buf_len;
	if (coredump_memdump && crash_data->memdump_buf_len)
		memcpy(dump_mem->data, crash_data->memdump_buf,
		       crash_data->memdump_buf_len);

skip_dump_mem:
	mutex_unlock(&dev->dump_mutex);

	return dump;
}

int mt7999_coredump_submit(struct mt7999_dev *dev, u8 type)
{
	struct mt7999_coredump *dump;

	dump = mt7999_coredump_build(dev, type, true);
	if (!dump) {
		dev_warn(dev->mt76.dev, "no crash dump data found\n");
		return -ENODATA;
	}

	dev_coredumpv(dev->mt76.dev, dump, dump->len, GFP_KERNEL);
	dev_info(dev->mt76.dev, "%s coredump completed\n",
		 wiphy_name(dev->mt76.hw->wiphy));

	return 0;
}

int mt7999_coredump_register(struct mt7999_dev *dev)
{
	struct mt7999_crash_data *crash_data;
	int i;

	for (i = 0; i < MT7999_COREDUMP_MAX; i++) {
		crash_data = vzalloc(sizeof(*dev->coredump.crash_data[i]));
		if (!crash_data)
			goto nomem;

		dev->coredump.crash_data[i] = crash_data;
		crash_data->supported = false;

		if (coredump_memdump) {
			crash_data->memdump_buf_len = mt7999_coredump_get_mem_size(dev, i);
			if (!crash_data->memdump_buf_len)
				/* no memory content */
				continue;

			crash_data->memdump_buf = vzalloc(crash_data->memdump_buf_len);
			if (!crash_data->memdump_buf)
				goto nomem;

			crash_data->supported = true;
		}
	}

	return 0;
nomem:
	mt7999_coredump_unregister(dev);
	return -ENOMEM;
}

void mt7999_coredump_unregister(struct mt7999_dev *dev)
{
	int i;
	struct mt7999_crash_data *crash_data;

	for (i = 0; i < MT7999_COREDUMP_MAX; i++) {
		crash_data = dev->coredump.crash_data[i];

		if (!crash_data)
			continue;

		if (crash_data->memdump_buf)
			vfree(crash_data->memdump_buf);

		vfree(crash_data);
	}
}

