/* g2d_bsp.h
 *
 * Copyright (c)	2016 Allwinnertech Co., Ltd.
 *					2016 gqs
 *
 * G2D driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 */

#ifndef __G2D_BSP_H
#define __G2D_BSP_H

#include "linux/kernel.h"
#include "linux/mm.h"
#include <asm/uaccess.h>
#include <asm/memory.h>
#include <asm/unistd.h>
#include "linux/semaphore.h"
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/fb.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include "asm-generic/int-ll64.h"
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_iommu.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/string.h>
#include <linux/g2d_driver.h>
#include <linux/dma-buf.h>
#include <linux/reset.h>

#define G2D_FINISH_IRQ		(1<<8)
#define G2D_ERROR_IRQ			(1<<9)

extern u32 dbg_info;

#define G2D_INFO_MSG(fmt, args...) \
	do {\
		if (dbg_info)\
		pr_info("[G2D-%s] line:%d: " fmt, __func__, __LINE__, ##args);\
	} while (0)

typedef struct {
	unsigned long g2d_base;
} g2d_init_para;

typedef struct {
	g2d_init_para init_para;
} g2d_dev_t;

typedef enum {
	G2D_RGB2YUV_709,
	G2D_YUV2RGB_709,
	G2D_RGB2YUV_601,
	G2D_YUV2RGB_601,
	G2D_RGB2YUV_2020,
	G2D_YUV2RGB_2020,
} g2d_csc_sel;

typedef enum {
	VSU_FORMAT_YUV422 = 0x00,
	VSU_FORMAT_YUV420 = 0x01,
	VSU_FORMAT_YUV411 = 0x02,
	VSU_FORMAT_RGB = 0x03,
	VSU_FORMAT_BUTT = 0x04,
} vsu_pixel_format;

#define VSU_ZOOM0_SIZE	1
#define VSU_ZOOM1_SIZE	8
#define VSU_ZOOM2_SIZE	4
#define VSU_ZOOM3_SIZE	1
#define VSU_ZOOM4_SIZE	1
#define VSU_ZOOM5_SIZE	1

#define VSU_PHASE_NUM            32
#define VSU_PHASE_FRAC_BITWIDTH  19
#define VSU_PHASE_FRAC_REG_SHIFT 1
#define VSU_FB_FRAC_BITWIDTH     32

#define VI_LAYER_NUMBER 1
#define UI_LAYER_NUMBER 3

__s32 g2d_bsp_open(void);
__s32 g2d_bsp_close(void);
__s32 g2d_bsp_reset(void);
__s32 mixer_irq_query(void);
__s32 rot_irq_query(void);
__s32 g2d_mixer_reset(void);
__s32 g2d_rot_reset(void);
__s32 g2d_bsp_bld(g2d_image_enh *, g2d_image_enh *, __u32, g2d_ck *);
__s32 g2d_fillrectangle(g2d_image_enh *dst, __u32 color_value);
__s32 g2d_bsp_maskblt(g2d_image_enh *src, g2d_image_enh *ptn,
		      g2d_image_enh *mask, g2d_image_enh *dst,
		      __u32 back_flag, __u32 fore_flag);
__s32 g2d_bsp_bitblt(g2d_image_enh *src, g2d_image_enh *dst, __u32 flag);
__s32 g2d_byte_cal(__u32 format, __u32 *ycnt, __u32 *ucnt, __u32 *vcnt);

extern int g2d_wait_cmd_finish(void);

__u32	mixer_reg_init(void);
__s32	mixer_blt(g2d_blt *para, enum g2d_scan_order scan_order);
__s32	mixer_fillrectangle(g2d_fillrect *para);
__s32	mixer_stretchblt(g2d_stretchblt *para, enum g2d_scan_order scan_order);
__s32	mixer_maskblt(g2d_maskblt *para);
__u32	mixer_set_palette(g2d_palette *para);
__u64	mixer_get_addr(__u32 buffer_addr, __u32 format,
			__u32 stride, __u32 x, __u32 y);
__u32	mixer_set_reg_base(unsigned long addr);
__u32	mixer_get_irq(void);
__u32	mixer_get_irq0(void);
__u32	mixer_clear_init(void);
__u32	mixer_clear_init0(void);
__s32	mixer_cmdq(__u32 addr);
__u32	mixer_premultiply_set(__u32 flag);
__u32	mixer_micro_block_set(g2d_blt *para);

__u32 g2d_ip_version(void);

#endif	/* __G2D_BSP_H */

