/*
 * Based on drivers/input/keyboard/sunxi-keyboard.c
 *
 * Copyright (C) 2015 Allwinnertech Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/keyboard.h>
#include <linux/ioport.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <linux/timer.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>

#include <linux/iio/iio.h>
#include <linux/iio/machine.h>
#include <linux/iio/driver.h>
#include <linux/reset.h>

#if IS_ENABLED(CONFIG_PM)
#include <linux/pm.h>
#endif
#include "sunxi-keyboard.h"

static unsigned char keypad_mapindex[64] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0,		/* key 1, 0-8 */
	1, 1, 1, 1, 1,				/* key 2, 9-13 */
	2, 2, 2, 2, 2, 2,			/* key 3, 14-19 */
	3, 3, 3, 3, 3, 3,			/* key 4, 20-25 */
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,	/* key 5, 26-36 */
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,	/* key 6, 37-39 */
	6, 6, 6, 6, 6, 6, 6, 6, 6,		/* key 7, 40-49 */
	7, 7, 7, 7, 7, 7, 7			/* key 8, 50-63 */
};

#define INITIAL_VALUE (0xff)
#define VOL_NUM KEY_MAX_CNT

/*
 * MAX_KEYPRESS: The driver can recognize how many keys are pressed at the same time
 * MAX_KEYPRESS can be 1, 2, 4;
 */
#define MAX_KEYPRESS 2

struct sunxi_key_data {
	struct platform_device	*pdev;
	struct clk *mclk;
	struct clk *pclk;
	struct reset_control	*rst_clk;
	struct input_dev *input_dev;
	struct sunxi_adc_disc *disc;
	spinlock_t		lock; /* syn */
	void __iomem *reg_base;
	u32 scankeycodes[KEY_MAX_CNT];
	int irq_num;
	u32 key_val;
	u32 before_code;
	unsigned char compare_later;
	unsigned char compare_before;
	u8 key_code;
	u8 last_key_code;
	char key_name[16];
	u8 key_cnt;
	int wakeup;
};

static struct sunxi_adc_disc disc_1350 = {
	.measure = 1350,
	.resol = 21,
};

static struct sunxi_adc_disc disc_1200 = {
	.measure = 1200,
	.resol = 19,
};

static struct sunxi_adc_disc disc_2000 = {
	.measure = 2000,
	.resol = 31,
};

#if IS_ENABLED(CONFIG_OF)
/*
 * Translate OpenFirmware node properties into platform_data
 */
static struct of_device_id const sunxi_keyboard_of_match[] = {
	{ .compatible = "allwinner,keyboard_1350mv",
		.data = &disc_1350 },
	{ .compatible = "allwinner,keyboard_1200mv",
		.data = &disc_1200 },
	{ .compatible = "allwinner,keyboard_2000mv",
		.data = &disc_2000 },
	{ },
};
MODULE_DEVICE_TABLE(of, sunxi_keyboard_of_match);
#else /* !CONFIG_OF */
#endif

static void sunxi_keyboard_ctrl_set(void __iomem *reg_base,
					enum key_mode key_mode, u32 para)
{
	u32 ctrl_reg = 0;

	if (para != 0)
		ctrl_reg = readl(reg_base + LRADC_CTRL);
	if (CONCERT_DLY_SET & key_mode)
		ctrl_reg |= (FIRST_CONCERT_DLY & para);
	if (ADC_CHAN_SET & key_mode)
		ctrl_reg |= (ADC_CHAN_SELECT & para);
	if (KEY_MODE_SET & key_mode)
		ctrl_reg |= (KEY_MODE_SELECT & para);
	if (LRADC_HOLD_SET & key_mode)
		ctrl_reg |= (LRADC_HOLD_EN & para);
	if (LEVELB_VOL_SET & key_mode) {
		ctrl_reg |= (LEVELB_VOL & para);
#if IS_ENABLED(CONFIG_ARCH_SUN8IW18)
		ctrl_reg &= ~(u32)(3 << 4);
#endif
	}
	if (LRADC_SAMPLE_SET & key_mode)
		ctrl_reg |= (LRADC_SAMPLE_250HZ & para);
	if (LRADC_EN_SET & key_mode)
		ctrl_reg |= (LRADC_EN & para);

	writel(ctrl_reg, reg_base + LRADC_CTRL);
}

static void sunxi_keyboard_int_set(void __iomem *reg_base,
					enum int_mode int_mode, u32 para)
{
	u32 ctrl_reg = 0;

	if (para != 0)
		ctrl_reg = readl(reg_base + LRADC_INTC);

	if (ADC0_DOWN_INT_SET & int_mode)
		ctrl_reg |= (LRADC_ADC0_DOWN_EN & para);
	if (ADC0_UP_INT_SET & int_mode)
		ctrl_reg |= (LRADC_ADC0_UP_EN & para);
	if (ADC0_DATA_INT_SET & int_mode)
		ctrl_reg |= (LRADC_ADC0_DATA_EN & para);

	writel(ctrl_reg, reg_base + LRADC_INTC);
}

static u32 sunxi_keyboard_read_ints(void __iomem *reg_base)
{
	u32 reg_val;

	reg_val  = readl(reg_base + LRADC_INT_STA);

	return reg_val;
}

static void sunxi_keyboard_clr_ints(void __iomem *reg_base, u32 reg_val)
{
	writel(reg_val, reg_base + LRADC_INT_STA);
}

static u32 sunxi_keyboard_read_data(void __iomem *reg_base)
{
	u32 reg_val;

	reg_val = readl(reg_base + LRADC_DATA0);

	return reg_val;
}

#if IS_ENABLED(CONFIG_PM)
static int sunxi_keyboard_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sunxi_key_data *key_data = platform_get_drvdata(pdev);

	pr_debug("[%s] enter standby\n", __func__);

	if (device_may_wakeup(dev)) {
		if (key_data->wakeup)
			enable_irq_wake(key_data->irq_num);
	} else {
		disable_irq_nosync(key_data->irq_num);

		sunxi_keyboard_ctrl_set(key_data->reg_base, 0, 0);

		if (IS_ERR_OR_NULL(key_data->mclk))
			pr_warn("%s apb1_keyadc mclk handle is invalid!\n",
				__func__);
		else
			clk_disable_unprepare(key_data->mclk);
	}

	return 0;
}

static int sunxi_keyboard_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sunxi_key_data *key_data = platform_get_drvdata(pdev);
	unsigned long mode, para;

	pr_debug("[%s] return from standby\n", __func__);

	if (device_may_wakeup(dev)) {
		if (key_data->wakeup)
			disable_irq_wake(key_data->irq_num);
	} else {
		if (IS_ERR_OR_NULL(key_data->mclk))
			pr_warn("%s apb1_keyadc mclk handle is invalid!\n",
				__func__);
		else
			clk_prepare_enable(key_data->mclk);

		mode = ADC0_DOWN_INT_SET | ADC0_UP_INT_SET | ADC0_DATA_INT_SET;
		para = LRADC_ADC0_DOWN_EN | LRADC_ADC0_UP_EN
			| LRADC_ADC0_DATA_EN;
		sunxi_keyboard_int_set(key_data->reg_base, mode, para);
		mode = CONCERT_DLY_SET | ADC_CHAN_SET | KEY_MODE_SET
			| LRADC_HOLD_SET | LEVELB_VOL_SET
			| LRADC_SAMPLE_SET | LRADC_EN_SET;
		para = FIRST_CONCERT_DLY | LEVELB_VOL|KEY_MODE_SELECT
			| LRADC_HOLD_EN	| ADC_CHAN_SELECT
			| LRADC_SAMPLE_250HZ|LRADC_EN;
		sunxi_keyboard_ctrl_set(key_data->reg_base, mode, para);

		enable_irq(key_data->irq_num);
	}

	return 0;
}
#endif

#define KEY_NOCHANGED         0
#define KEY_CHANGED_DOWN      1
#define KEY_CHANGED_UP        2
/* before:last keycode.
 * now: now changed keycode.
 * key:now change key index.
 * report: which key report.
 * return: 0: no changed, 1, key down, 2, key up.
 */
static int isKeyChange(u32 before, u32 now, u32 key, u32 *report)
{
	int i, ret;
	int keycode = (now >> (8 * key)) & 0xFF;

	if (keycode == 0) {
		for (i = 0; i < MAX_KEYPRESS; i++) {
			keycode = (before >> (8 * i)) & 0xFF;
			if (keycode == 0)
				continue;
			ret = isKeyChange(now, before, i, report);
			if (ret == KEY_CHANGED_DOWN) {
				*report = keycode;
				return KEY_CHANGED_UP;
			}
		}
		return KEY_NOCHANGED;
	}

	for (i = 0; i < MAX_KEYPRESS; i++) {
		int key_b = (before >> (8 * i)) & 0xFF;
		if (key_b == keycode)
			return KEY_NOCHANGED;
	}
	*report = keycode;
	return KEY_CHANGED_DOWN;
}

static void sunxi_report_key_down_event(struct sunxi_key_data *key_data)
{
	u32 scancode = 0;
	int i = 0;

	key_data->compare_later = key_data->compare_before;
	scancode = key_data->scankeycodes[key_data->key_code];
	if (key_data->before_code != scancode) {
		int key;
		int changed;

		for (i = 0; i < MAX_KEYPRESS; i++) {
			key = 0;
			changed = isKeyChange(key_data->before_code, scancode, i, &key);
			if (changed == KEY_CHANGED_DOWN) {
				pr_debug("before : %d, scancode : %d, key : %d, down : 1\n", key_data->before_code, scancode, key);
				input_report_key(key_data->input_dev, key, 1);
				input_sync(key_data->input_dev);
			} else if (changed == KEY_CHANGED_UP) {
				pr_debug("before : %d, scancode : %d, key : %d, down : 0\n", key_data->before_code, scancode, key);
				input_report_key(key_data->input_dev, key, 0);
				input_sync(key_data->input_dev);
			}
		}
	}
	key_data->before_code = scancode;
	key_data->key_cnt = 0;
}

static irqreturn_t sunxi_isr_key(int irq, void *dummy)
{
	struct sunxi_key_data *key_data = (struct sunxi_key_data *)dummy;
	unsigned long flags = 0;
	u32 reg_val = 0;
	u32 key_val = 0;

	pr_debug("Key Interrupt\n");
	spin_lock_irqsave(&key_data->lock, flags);
	reg_val = sunxi_keyboard_read_ints(key_data->reg_base);
	sunxi_keyboard_clr_ints(key_data->reg_base, reg_val);

	if (reg_val & LRADC_ADC0_DOWNPEND)
		pr_debug("key down\n");

	if (reg_val & LRADC_ADC0_DATAPEND) {
		key_data->key_cnt++;
		key_val = sunxi_keyboard_read_data(key_data->reg_base);
		key_data->compare_before = key_val & 0x3f;
		if (key_data->compare_before == key_data->compare_later) {
			key_data->key_code = keypad_mapindex[key_val & 0x3f];
			sunxi_report_key_down_event(key_data);
		}
		if (key_data->key_cnt == 2) {
			key_data->compare_later = key_data->compare_before;
			key_data->key_cnt = 0;
		}
	}

	if (reg_val & LRADC_ADC0_UPPEND) {
		int i;

		if (key_data->wakeup)
			pm_wakeup_event(key_data->input_dev->dev.parent, 0);
		for (i = 0; i < MAX_KEYPRESS; i++) {
			int key = (key_data->before_code >> (8 * i)) & 0xFF;
			if (key > 0) {
				pr_debug("report : %d, key : %d\n", key_data->before_code, key);
				input_report_key(key_data->input_dev, key, 0);
				input_sync(key_data->input_dev);
			}
		}
		pr_debug("key up\n");
		key_data->key_cnt = 0;
		key_data->compare_later = 0;
		key_data->before_code = 0;
		key_data->last_key_code = INITIAL_VALUE;
	}

	spin_unlock_irqrestore(&key_data->lock, flags);
	return IRQ_HANDLED;
}

static int sunxi_keyboard_startup(struct sunxi_key_data *key_data,
				struct platform_device *pdev)
{
	struct device_node *np = NULL;
	int ret = 0;

	np = pdev->dev.of_node;
	if (!of_device_is_available(np)) {
		pr_err("%s: sunxi keyboard is disable\n", __func__);
		return -EPERM;
	}
	key_data->reg_base = of_iomap(np, 0);
	if (key_data->reg_base == 0) {
		pr_err("%s:Failed to ioremap() io memory region.\n", __func__);
		ret = -EBUSY;
	} else
		pr_debug("key base: %p !\n", key_data->reg_base);

	key_data->irq_num = irq_of_parse_and_map(np, 0);
	if (key_data->irq_num == 0) {
		pr_err("%s:Failed to map irq.\n", __func__);
		ret = -EBUSY;
	} else
		pr_debug("ir irq num: %d !\n", key_data->irq_num);

	/* some IC will use clock gating while others HW use 24MHZ, So just try
	 * to get the clock, if it doesn't exist, give warning instead of error
	 */
	key_data->rst_clk = devm_reset_control_get(&pdev->dev, NULL);
	if (IS_ERR_OR_NULL(key_data->rst_clk)) {
		pr_debug("%s: keyboard has no reset clk.\n", __func__);
	} else {
		if (reset_control_deassert(key_data->rst_clk)) {
			pr_err("%s enable apb1_keyadc clock failed!\n",
							__func__);
			return -EINVAL;
		}
	}
	key_data->mclk = of_clk_get(np, 0);
	if (IS_ERR_OR_NULL(key_data->mclk)) {
		pr_debug("%s: keyboard has no clk.\n", __func__);
	} else {
		if (clk_prepare_enable(key_data->mclk)) {
			pr_err("%s enable apb1_keyadc clock failed!\n",
							__func__);
			return -EINVAL;
		}
	}

	return ret;
}

static int sunxikbd_key_init(struct sunxi_key_data *key_data,
			struct platform_device *pdev)
{
	struct device_node *np = NULL;
	const struct of_device_id *match;
	struct sunxi_adc_disc *disc;
	int i, j = 0;
	u32 val[2] = {0, 0};
	u32 key_num = 0;
	u32 key_vol[VOL_NUM];

	np = pdev->dev.of_node;
	match = of_match_node(sunxi_keyboard_of_match, np);
	disc = (struct sunxi_adc_disc *)match->data;
	key_data->disc = disc;
	if (of_property_read_u32(np, "key_cnt", &key_num)) {
		pr_err("%s: get key count failed", __func__);
		return -EBUSY;
	}
	pr_debug("%s key number = %d.\n", __func__, key_num);
	if (key_num < 1 || key_num > VOL_NUM) {
		pr_err("incorrect key number.\n");
		return -1;
	}
	for (i = 0; i < key_num; i++) {
		sprintf(key_data->key_name, "key%d", i);
		if (of_property_read_u32_array(np, key_data->key_name,
						val, ARRAY_SIZE(val))) {
			pr_err("%s:get%s err!\n", __func__, key_data->key_name);
			return -EBUSY;
		}
		key_vol[i] = val[0];
		key_data->scankeycodes[i] = val[1];
		pr_debug("%s: key%d vol= %d code= %d\n", __func__, i,
				key_vol[i], key_data->scankeycodes[i]);
	}
	key_vol[key_num] = disc->measure;
	for (i = 0; i < key_num; i++)
		key_vol[i] += (key_vol[i+1] - key_vol[i])/2;

	for (i = 0; i < 64; i++) {
		if (i * disc->resol > key_vol[j])
			j++;
		keypad_mapindex[i] = j;
	}

	key_data->wakeup = of_property_read_bool(np, "wakeup-source");
	device_init_wakeup(&pdev->dev, key_data->wakeup);

	key_data->last_key_code = INITIAL_VALUE;

	return 0;
}

#if IS_ENABLED(CONFIG_IIO)
struct sunxi_lradc_iio {
	struct sunxi_key_data *key_data;
};

static const struct iio_chan_spec sunxi_lradc_channels[] = {
	{
		.indexed = 1,
		.type = IIO_VOLTAGE,
		.channel = 0,
		.datasheet_name = "LRADC",
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	},
};

/* default maps used by iio consumer (axp charger driver) */
static struct iio_map sunxi_lradc_default_iio_maps[] = {
	{
		.consumer_dev_name = "axp-charger",
		.consumer_channel = "axp-battery-lradc",
		.adc_channel_label = "LRADC",
	},
	{ }
};

static int sunxi_lradc_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int *val, int *val2, long mask)
{
	int ret = 0;
	int key_val, id_vol;
	struct sunxi_lradc_iio *info = iio_priv(indio_dev);
	struct sunxi_key_data *key_data = info->key_data;
	struct sunxi_adc_disc *disc = key_data->disc;

	mutex_lock(&indio_dev->mlock);
	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		key_val = sunxi_keyboard_read_data(key_data->reg_base) & 0x3f;
		id_vol = key_val * disc->resol;
		*val = id_vol;
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static const struct iio_info sunxi_lradc_iio_info = {
	.read_raw = &sunxi_lradc_read_raw,
};

static void sunxi_lradc_remove_iio(void *_data)
{
	struct iio_dev *indio_dev = _data;

	if (IS_ERR_OR_NULL(indio_dev)) {
		pr_err("indio_dev is null\n");
	} else {
		iio_device_unregister(indio_dev);
		iio_map_array_unregister(indio_dev);
	}
}

static int sunxi_keyboard_iio_init(struct platform_device *pdev)
{
	int ret;
	struct iio_dev *indio_dev;
	struct sunxi_lradc_iio *info;
	struct sunxi_key_data *key_data = platform_get_drvdata(pdev);

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*info));
	if (!indio_dev)
		return -ENOMEM;

	info = iio_priv(indio_dev);
	info->key_data = key_data;

	indio_dev->dev.parent = &pdev->dev;
	indio_dev->name = pdev->name;
	indio_dev->channels = sunxi_lradc_channels;
	indio_dev->num_channels = ARRAY_SIZE(sunxi_lradc_channels);
	indio_dev->info = &sunxi_lradc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	ret = iio_map_array_register(indio_dev, sunxi_lradc_default_iio_maps);
	if (ret < 0)
		return ret;

	ret = iio_device_register(indio_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "unable to register iio device\n");
		goto err_array_unregister;
	}

	ret = devm_add_action(&pdev->dev,
				sunxi_lradc_remove_iio, indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "unable to add iio cleanup action\n");
		goto err_iio_unregister;
	}

	return 0;

err_iio_unregister:
	iio_device_unregister(indio_dev);

err_array_unregister:
	iio_map_array_unregister(indio_dev);

	return ret;
}
#else
static inline int sunxi_keyboard_iio_init(struct platform_device *pdev)
{
	return -ENODEV;
}
#endif

static int sunxi_keyboard_probe(struct platform_device *pdev)
{
	static struct input_dev *sunxikbd_dev;
	struct sunxi_key_data *key_data;
	unsigned long mode, para;
	u32 reg_val = 0;
	int i;
	int err = 0;

	key_data = kzalloc(sizeof(*key_data), GFP_KERNEL);
	if (IS_ERR_OR_NULL(key_data)) {
		pr_err("key_data: not enough memory for key data\n");
		return -ENOMEM;
	}

	pr_debug("sunxikbd_init\n");
	if (pdev->dev.of_node) {
		/* get dt and sysconfig */
		err = sunxi_keyboard_startup(key_data, pdev);
	} else {
		pr_err("sunxi keyboard device tree err!\n");
		return -EBUSY;
	}

	if (err < 0)
		goto fail1;

	if (sunxikbd_key_init(key_data, pdev)) {
		err = -EFAULT;
		goto fail1;
	}

	sunxikbd_dev = input_allocate_device();
	if (!sunxikbd_dev) {
		pr_err("sunxikbd: not enough memory for input device\n");
		err = -ENOMEM;
		goto fail1;
	}

	sunxikbd_dev->name = INPUT_DEV_NAME;
	sunxikbd_dev->phys = "sunxikbd/input0";
	sunxikbd_dev->id.bustype = BUS_HOST;
	sunxikbd_dev->id.vendor = 0x0001;
	sunxikbd_dev->id.product = 0x0001;
	sunxikbd_dev->id.version = 0x0100;

#ifdef REPORT_REPEAT_KEY_BY_INPUT_CORE
	sunxikbd_dev->evbit[0] = BIT_MASK(EV_KEY)|BIT_MASK(EV_REP);
	pr_info("support report repeat key value.\n");
#else
	sunxikbd_dev->evbit[0] = BIT_MASK(EV_KEY);
#endif

	for (i = 0; i < KEY_MAX_CNT; i++) {
		if (key_data->scankeycodes[i] < KEY_MAX)
			set_bit(key_data->scankeycodes[i], sunxikbd_dev->keybit);
	}
	key_data->input_dev = sunxikbd_dev;
	platform_set_drvdata(pdev, key_data);
#ifdef ONE_CHANNEL
	mode = ADC0_DOWN_INT_SET | ADC0_UP_INT_SET | ADC0_DATA_INT_SET;
	para = LRADC_ADC0_DOWN_EN | LRADC_ADC0_UP_EN | LRADC_ADC0_DATA_EN;
	sunxi_keyboard_int_set(key_data->reg_base, mode, para);
	mode = CONCERT_DLY_SET | ADC_CHAN_SET | KEY_MODE_SET
		| LRADC_HOLD_SET | LEVELB_VOL_SET
		| LRADC_SAMPLE_SET | LRADC_EN_SET;
	para = FIRST_CONCERT_DLY|LEVELB_VOL|KEY_MODE_SELECT
		|LRADC_HOLD_EN|ADC_CHAN_SELECT
		|LRADC_SAMPLE_250HZ|LRADC_EN;
	sunxi_keyboard_ctrl_set(key_data->reg_base, mode, para);
#else
#endif
	if (request_irq(key_data->irq_num, sunxi_isr_key, 0,
					"sunxikbd", key_data)) {
		err = -EBUSY;
		pr_err("request irq failure.\n");
		goto fail2;
	}

	err = input_register_device(key_data->input_dev);
	if (err)
		goto fail3;

	reg_val = sunxi_keyboard_read_ints(key_data->reg_base);
	sunxi_keyboard_clr_ints(key_data->reg_base, reg_val);

	sunxi_keyboard_iio_init(pdev);

	pr_debug("sunxikbd_init end\n");
	return 0;

fail3:
	free_irq(key_data->irq_num, NULL);
fail2:
	input_free_device(key_data->input_dev);
fail1:
	kfree(key_data);
	pr_err("sunxikbd_init failed.\n");

	return err;
}

static int sunxi_keyboard_remove(struct platform_device *pdev)
{
	struct sunxi_key_data *key_data = platform_get_drvdata(pdev);

	free_irq(key_data->irq_num, key_data);
	input_unregister_device(key_data->input_dev);
	device_init_wakeup(&pdev->dev, 0);
	clk_disable_unprepare(key_data->mclk);
	reset_control_assert(key_data->rst_clk);
	kfree(key_data);
	return 0;
}

#if IS_ENABLED(CONFIG_PM)
static const struct dev_pm_ops sunxi_keyboard_pm_ops = {
	.suspend = sunxi_keyboard_suspend,
	.resume = sunxi_keyboard_resume,
};

#define SUNXI_KEYBOARD_PM_OPS (&sunxi_keyboard_pm_ops)
#endif

static struct platform_driver sunxi_keyboard_driver = {
	.probe  = sunxi_keyboard_probe,
	.remove = sunxi_keyboard_remove,
	.driver = {
		.name   = "sunxi-keyboard",
		.owner  = THIS_MODULE,
#if IS_ENABLED(CONFIG_PM)
		.pm	= SUNXI_KEYBOARD_PM_OPS,
#endif
		.of_match_table = of_match_ptr(sunxi_keyboard_of_match),
	},
};

static int __init sunxi_keyboard_init(void)
{
	int ret;

	ret = platform_driver_register(&sunxi_keyboard_driver);

	return ret;
}

static void __exit sunxi_keyboard_exit(void)
{
	platform_driver_unregister(&sunxi_keyboard_driver);
}

subsys_initcall_sync(sunxi_keyboard_init);
module_exit(sunxi_keyboard_exit);

MODULE_AUTHOR(" Qin");
MODULE_DESCRIPTION("sunxi-keyboard driver");
MODULE_LICENSE("GPL");
