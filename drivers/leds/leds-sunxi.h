// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018 Allwinner Technology Limited. All rights reserved.
 * Albert Yu <yuxyun@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#ifndef __LINUX_LEDS_SUNXI_H
#define __LINUX_LEDS_SUNXI_H

#include <linux/device.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

#define HEXADECIMAL	(0x10)
#define REG_INTERVAL	(0x04)
#define REG_CL		(0x0c)

#define RESULT_COMPLETE	1
#define RESULT_ERR	2

#define SUNXI_LEDC_REG_BASE_ADDR 0x06700000

#define SUNXI_MAX_LED_COUNT 1024

#define SUNXI_DEFAULT_LED_COUNT 8

#define SUNXI_RESET_TIME_MIN_NS 84
#define SUNXI_RESET_TIME_MAX_NS 327000

#define SUNXI_T1H_MIN_NS 84
#define SUNXI_T1H_MAX_NS 2560

#define SUNXI_T1L_MIN_NS 84
#define SUNXI_T1L_MAX_NS 1280

#define SUNXI_T0H_MIN_NS 84
#define SUNXI_T0H_MAX_NS 1280

#define SUNXI_T0L_MIN_NS 84
#define SUNXI_T0L_MAX_NS 2560

#define SUNXI_WAIT_TIME0_MIN_NS 84
#define SUNXI_WAIT_TIME0_MAX_NS 10000

#define SUNXI_WAIT_TIME1_MIN_NS 84
#define SUNXI_WAIT_TIME1_MAX_NS 85000000000

#define SUNXI_WAIT_DATA_TIME_MIN_NS 84
#define SUNXI_WAIT_DATA_TIME_MAX_NS_IC 655000
#define SUNXI_WAIT_DATA_TIME_MAX_NS_FPGA 20000000

#define SUNXI_LEDC_FIFO_DEPTH 32 /* 32 * 4 bytes */
#define SUNXI_LEDC_FIFO_TRIG_LEVEL 15

#if defined(CONFIG_FPGA_V4_PLATFORM) || defined(CONFIG_FPGA_V7_PLATFORM)
#define SUNXI_FPGA_LEDC
#endif

enum sunxi_ledc_output_mode_val {
	SUNXI_OUTPUT_GRB = 0 << 6,
	SUNXI_OUTPUT_GBR = 1 << 6,
	SUNXI_OUTPUT_RGB = 2 << 6,
	SUNXI_OUTPUT_RBG = 3 << 6,
	SUNXI_OUTPUT_BGR = 4 << 6,
	SUNXI_OUTPUT_BRG = 5 << 6
};

struct sunxi_ledc_output_mode {
	char *str;
	enum sunxi_ledc_output_mode_val val;
};

enum sunxi_ledc_trans_mode_val {
	LEDC_TRANS_CPU_MODE,
	LEDC_TRANS_DMA_MODE
};

enum sunxi_ledc_reg {
	LEDC_CTRL_REG_OFFSET              = 0x00,
	LED_T01_TIMING_CTRL_REG_OFFSET    = 0x04,
	LEDC_DATA_FINISH_CNT_REG_OFFSET   = 0x08,
	LED_RESET_TIMING_CTRL_REG_OFFSET  = 0x0c,
	LEDC_WAIT_TIME0_CTRL_REG          = 0x10,
	LEDC_DATA_REG_OFFSET              = 0x14,
	LEDC_DMA_CTRL_REG                 = 0x18,
	LEDC_INT_CTRL_REG_OFFSET          = 0x1c,
	LEDC_INT_STS_REG_OFFSET           = 0x20,
	LEDC_WAIT_TIME1_CTRL_REG          = 0x28,
	LEDC_VER_NUM_REG                  = 0x2c,
	LEDC_FIFO_DATA                    = 0x30,
	LEDC_TOTAL_REG_SIZE = LEDC_FIFO_DATA + SUNXI_LEDC_FIFO_DEPTH
};

enum sunxi_ledc_irq_ctrl_reg {
	LEDC_TRANS_FINISH_INT_EN     = (1 << 0),
	LEDC_FIFO_CPUREQ_INT_EN      = (1 << 1),
	LEDC_WAITDATA_TIMEOUT_INT_EN = (1 << 3),
	LEDC_FIFO_OVERFLOW_INT_EN    = (1 << 4),
	LEDC_GLOBAL_INT_EN           = (1 << 5),
};

enum sunxi_ledc_irq_status_reg {
	LEDC_TRANS_FINISH_INT     = (1 << 0),
	LEDC_FIFO_CPUREQ_INT      = (1 << 1),
	LEDC_WAITDATA_TIMEOUT_INT = (1 << 3),
	LEDC_FIFO_OVERFLOW_INT    = (1 << 4),
	LEDC_FIFO_FULL            = (1 << 16),
	LEDC_FIFO_EMPTY           = (1 << 17),
};

enum sunxi_led_type {
	LED_TYPE_R,
	LED_TYPE_G,
	LED_TYPE_B
};

struct sunxi_led_info {
	enum sunxi_led_type type;
	struct led_classdev cdev;
};

struct sunxi_led_classdev_group {
	u32 led_num;
	struct sunxi_led_info r;
	struct sunxi_led_info g;
	struct sunxi_led_info b;
};

struct sunxi_led {
	u32 reset_ns;
	u32 t1h_ns;
	u32 t1l_ns;
	u32 t0h_ns;
	u32 t0l_ns;
	u32 wait_time0_ns;
	unsigned long long wait_time1_ns;
	u32 wait_data_time_ns;
	u32 irqnum;
	u32 led_count;
	u32 *data;
	u32 length;
	u8 result;
	spinlock_t lock;
	struct device *dev;
	dma_addr_t src_dma;
	struct dma_chan *dma_chan;
	wait_queue_head_t wait;
	struct timespec64 start_time;
	struct clk *clk_ledc;
	struct clk *clk_cpuapb;
	struct pinctrl *pctrl;
	void __iomem *iomem_reg_base;
	struct resource	*res;
	struct sunxi_ledc_output_mode output_mode;
	struct sunxi_led_classdev_group *pcdev_group;
	struct dentry *debugfs_dir;
	char regulator_id[16];
	struct regulator *regulator;
	struct reset_control *reset;
};

enum {
	DEBUG_INIT    = 1U << 0,
	DEBUG_SUSPEND = 1U << 1,
	DEBUG_INFO    = 1U << 2,
	DEBUG_INFO1   = 1U << 3,
	DEBUG_INFO2   = 1U << 4,
};

#endif /* __LINUX_LEDS_SUNXI_H */
