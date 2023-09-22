/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * (C) Copyright 2015 Google, Inc
 */

#ifndef __CONFIG_RK3288_COMMON_H
#define __CONFIG_RK3288_COMMON_H

#include <asm/arch-rockchip/hardware.h>
#include "rockchip-common.h"

#define CONFIG_SYS_HZ_CLOCK		24000000

#define CONFIG_IRAM_BASE		0xff700000

/* RAW SD card / eMMC locations. */

#define CONFIG_SYS_SDRAM_BASE		0
#define SDRAM_BANK_SIZE			(2UL << 30)
#define SDRAM_MAX_SIZE			0xfe000000

#define CONFIG_SYS_MONITOR_LEN (600 * 1024)

#define ENV_MEM_LAYOUT_SETTINGS \
	"scriptaddr=0x00000000\0" \
	"pxefile_addr_r=0x00100000\0" \
	"fdt_addr_r=0x01f00000\0" \
	"kernel_addr_r=0x02000000\0" \
	"ramdisk_addr_r=0x04000000\0"

#include <config_distro_bootcmd.h>

/* Linux fails to load the fdt if it's loaded above 256M on a Rock 2 board, so
 * limit the fdt reallocation to that */
#define CONFIG_EXTRA_ENV_SETTINGS \
	"bootr=" \
		"tftpboot 0x04000000 rootfs.squashfs;" \
		"run bootk\0" \
	"bootk=" \
		"tftpboot 0x02008000 zImage;" \
		"tftpboot 0x08300000 rk3288-firefly.dtb;" \
		"bootz 0x02008000 - 0x08300000\0" \
	"mmcdev="__stringify(CONFIG_SYS_MMC_ENV_DEV)"\0" \
	"mmcpart=2\0" \
	"mmcbootr=" \
		"fatload mmc ${mmcdev}:${mmcpart} 0x04000000 rootfs.squashfs;" \
		"run mmcboot\0" \
	"mmcboot=" \
		"fatload mmc ${mmcdev}:${mmcpart} 0x02008000 zImage;" \
		"fatload mmc ${mmcdev}:${mmcpart} 0x08300000 rk3288-firefly.dtb;" \
		"bootz 0x02008000 - 0x08300000\0" \
	"ethaddr=00:26:22:D9:FA:25\0" \
	"tftptimeout=0\0" \
	"fdt_high=0x0fffffff\0" \
	"initrd_high=0x0fffffff\0" \
	"fdtfile=" CONFIG_DEFAULT_FDT_FILE "\0" \
	ENV_MEM_LAYOUT_SETTINGS \
	ROCKCHIP_DEVICE_SETTINGS

#define CONFIG_NETMASK		255.255.255.0
#define CONFIG_IPADDR		192.168.2.11
#define CONFIG_SERVERIP		192.168.2.125
#define CONFIG_GATEWAYIP	192.168.2.1

#endif
