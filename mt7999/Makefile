# SPDX-License-Identifier: BSD-3-Clause-Clear
EXTRA_CFLAGS += -Werror -std=gnu11
EXTRA_CFLAGS += -DCONFIG_MT76_LEDS
EXTRA_CFLAGS += -DCONFIG_MTK_DEBUG
EXTRA_CFLAGS += -DCONFIG_MTK_VENDOR
EXTRA_CFLAGS += $(ADDITIONAL_CFLAGS)

obj-$(CONFIG_MT7999E) += mt7999e.o

mt7999e-y := pci.o init.o dma.o eeprom.o main.o mcu.o mac.o \
	     debugfs.o mmio.o vendor.o

mt7999e-$(CONFIG_MT7999_NPU) += npu.o
mt7999e-$(CONFIG_DEV_COREDUMP) += coredump.o
mt7999e-$(CONFIG_NL80211_TESTMODE) += testmode.o

mt7999e-y += mtk_mcu.o trace.o mtk_debugfs.o
