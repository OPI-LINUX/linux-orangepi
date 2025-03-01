/*
 * AXP20x regulators driver.
 *
 * Copyright (C) 2013 Carlo Caione <carlo@caione.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/axp2101.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

#define AXP20X_IO_ENABLED		0x03
#define AXP20X_IO_DISABLED		0x07

#define AXP22X_IO_ENABLED		0x03
#define AXP22X_IO_DISABLED		0x04

#define AXP20X_WORKMODE_DCDC2_MASK	BIT(2)
#define AXP20X_WORKMODE_DCDC3_MASK	BIT(1)
#define AXP22X_WORKMODE_DCDCX_MASK(x)	BIT(x)

#define AXP20X_FREQ_DCDC_MASK		0x0f

#define AXP22X_MISC_N_VBUSEN_FUNC	BIT(4)

#define AXP803_MISC_N_VBUSEN_FUNC	BIT(4)

#define AXP2202_MISC_N_RBFETEN_FUNC	BIT(0)

#define AXP_DESC_IO(_family, _id, _match, _supply, _min, _max, _step, _vreg,	\
		    _vmask, _ereg, _emask, _enable_val, _disable_val)		\
	[_family##_##_id] = {							\
		.name		= (_match),					\
		.supply_name	= (_supply),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= _family##_##_id,				\
		.n_voltages	= (((_max) - (_min)) / (_step) + 1),		\
		.owner		= THIS_MODULE,					\
		.min_uV		= (_min) * 1000,				\
		.uV_step	= (_step) * 1000,				\
		.vsel_reg	= (_vreg),					\
		.vsel_mask	= (_vmask),					\
		.enable_reg	= (_ereg),					\
		.enable_mask	= (_emask),					\
		.enable_val	= (_enable_val),				\
		.disable_val	= (_disable_val),				\
		.ops		= &axp20x_ops,					\
	}

#define AXP_DESC(_family, _id, _match, _supply, _min, _max, _step, _vreg,	\
		 _vmask, _ereg, _emask) 					\
	[_family##_##_id] = {							\
		.name		= (_match),					\
		.supply_name	= (_supply),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= _family##_##_id,				\
		.n_voltages	= (((_max) - (_min)) / (_step) + 1),		\
		.owner		= THIS_MODULE,					\
		.min_uV		= (_min) * 1000,				\
		.uV_step	= (_step) * 1000,				\
		.vsel_reg	= (_vreg),					\
		.vsel_mask	= (_vmask),					\
		.enable_reg	= (_ereg),					\
		.enable_mask	= (_emask),					\
		.ops		= &axp20x_ops,					\
	}

#define AXP_DESC_SW(_family, _id, _match, _supply, _ereg, _emask)		\
	[_family##_##_id] = {							\
		.name		= (_match),					\
		.supply_name	= (_supply),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= _family##_##_id,				\
		.owner		= THIS_MODULE,					\
		.enable_reg	= (_ereg),					\
		.enable_mask	= (_emask),					\
		.ops		= &axp20x_ops_sw,				\
	}

#define AXP_DESC_FIXED(_family, _id, _match, _supply, _volt)			\
	[_family##_##_id] = {							\
		.name		= (_match),					\
		.supply_name	= (_supply),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= _family##_##_id,				\
		.n_voltages	= 1,						\
		.owner		= THIS_MODULE,					\
		.min_uV		= (_volt) * 1000,				\
		.ops		= &axp20x_ops_fixed				\
	}

#define AXP_DESC_RANGES(_family, _id, _match, _supply, _ranges, _n_voltages,	\
			_vreg, _vmask, _ereg, _emask)				\
	[_family##_##_id] = {							\
		.name		= (_match),					\
		.supply_name	= (_supply),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= _family##_##_id,				\
		.n_voltages	= (_n_voltages),				\
		.owner		= THIS_MODULE,					\
		.vsel_reg	= (_vreg),					\
		.vsel_mask	= (_vmask),					\
		.enable_reg	= (_ereg),					\
		.enable_mask	= (_emask),					\
		.linear_ranges	= (_ranges),					\
		.n_linear_ranges = ARRAY_SIZE(_ranges),				\
		.ops		= &axp20x_ops_range,				\
	}

#define AXP_DESC_RANGES_VOL_DELAY(_family, _id, _match, _supply, _ranges, _n_voltages,	\
			_vreg, _vmask, _ereg, _emask)				\
	[_family##_##_id] = {							\
		.name		= (_match),					\
		.supply_name	= (_supply),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= _family##_##_id,				\
		.n_voltages	= (_n_voltages),				\
		.owner		= THIS_MODULE,					\
		.vsel_reg	= (_vreg),					\
		.vsel_mask	= (_vmask),					\
		.enable_reg	= (_ereg),					\
		.enable_mask	= (_emask),					\
		.linear_ranges	= (_ranges),					\
		.n_linear_ranges = ARRAY_SIZE(_ranges),				\
		.ops		= &axp20x_ops_range_vol_delay,			\
	}

struct regulator_delay {
	u32 step;
	u32 final;
};

/* use for axp2202 which need to control boost_en */
/* add a extra reg_write to set/reset reg19[4]*/
int regulator_is_enabled_regmap_axp2202(struct regulator_dev *rdev)
{
	unsigned int val[2];
	int ret;

	ret = regmap_read(rdev->regmap, rdev->desc->enable_reg, &val[0]);
	if (ret != 0)
		return ret;

	ret = regmap_read(rdev->regmap, rdev->desc->vsel_reg, &val[1]);
	if (ret != 0)
		return ret;

	val[0] &= rdev->desc->enable_mask;
	val[1] &= rdev->desc->vsel_mask;

	if (rdev->desc->enable_is_inverted) {
		if (rdev->desc->enable_val)
			return (val[0] != rdev->desc->enable_val) && (val[1] != rdev->desc->vsel_mask);
		return (val[0] == 0) && (val[1] == 0);
	} else {
		if (rdev->desc->enable_val)
			return (val[0] == rdev->desc->enable_val) && (val[1] == rdev->desc->vsel_mask);
		return (val[0] != 0) && (val[1] != 0);
	}
}
EXPORT_SYMBOL_GPL(regulator_is_enabled_regmap_axp2202);

int regulator_enable_regmap_axp2202(struct regulator_dev *rdev)
{
	unsigned int val;
	int ret;

	printk("%s %s %d \n", __FILE__, __FUNCTION__, __LINE__);
	val = rdev->desc->enable_mask;

	ret = regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				rdev->desc->enable_mask, val);
	if (ret != 0)
		return ret;

	val = rdev->desc->vsel_mask;
	ret = regmap_update_bits(rdev->regmap, rdev->desc->vsel_reg,
				rdev->desc->vsel_mask, val);
	if (ret != 0)
		return ret;
	printk("%s %s %d \n", __FILE__, __FUNCTION__, __LINE__);

	return 0;
}
EXPORT_SYMBOL_GPL(regulator_enable_regmap_axp2202);

int regulator_disable_regmap_axp2202(struct regulator_dev *rdev)
{
	unsigned int val;
	int ret;

	printk("%s %s %d \n", __FILE__, __FUNCTION__, __LINE__);
	val = 0;

	ret = regmap_update_bits(rdev->regmap, rdev->desc->enable_reg,
				  rdev->desc->enable_mask, val);
	if (ret != 0)
		return ret;

	val = 0;
	ret = regmap_update_bits(rdev->regmap, rdev->desc->vsel_reg,
				  rdev->desc->vsel_mask, val);
	if (ret != 0)
		return ret;
	printk("%s %s %d \n", __FILE__, __FUNCTION__, __LINE__);

	return 0;

}
EXPORT_SYMBOL_GPL(regulator_disable_regmap_axp2202);


static int axp2101_set_voltage_time_sel(struct regulator_dev *rdev,
		unsigned int old_selector, unsigned int new_selector)
{
	struct regulator_delay *delay = (struct regulator_delay *)rdev->reg_data;

	return abs(new_selector - old_selector) * delay->step + delay->final;
};

static struct regulator_ops axp20x_ops_fixed = {
	.list_voltage		= regulator_list_voltage_linear,
};

static struct regulator_ops axp20x_ops_range = {
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear_range,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_voltage_time_sel = axp2101_set_voltage_time_sel,
};

static struct regulator_ops axp20x_ops_range_vol_delay = {
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear_range,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

static struct regulator_ops axp20x_ops = {
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_voltage_time_sel = axp2101_set_voltage_time_sel,
};

static struct regulator_ops axp20x_ops_sw = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

static struct regulator_ops axp2202_ops_sw = {
	.enable			= regulator_enable_regmap_axp2202,
	.disable		= regulator_disable_regmap_axp2202,
	.is_enabled		= regulator_is_enabled_regmap_axp2202,
};

static const struct regulator_linear_range axp152_dcdc1_ranges[] = {
	REGULATOR_LINEAR_RANGE(1700000, 0x0, 0x4, 100000),
	REGULATOR_LINEAR_RANGE(2400000, 0x5, 0x9, 100000),
	REGULATOR_LINEAR_RANGE(3000000, 0xa, 0xf, 100000),
};

static const struct regulator_linear_range axp152_aldo1_ranges[] = {
	REGULATOR_LINEAR_RANGE(1200000, 0x0, 0x8, 100000),
	REGULATOR_LINEAR_RANGE(2500000, 0x9, 0x9, 0),
	REGULATOR_LINEAR_RANGE(2700000, 0xa, 0xb, 100000),
	REGULATOR_LINEAR_RANGE(3000000, 0xc, 0xf, 100000),
};

static const struct regulator_linear_range axp152_aldo2_ranges[] = {
	REGULATOR_LINEAR_RANGE(1200000, 0x0, 0x8, 100000),
	REGULATOR_LINEAR_RANGE(2500000, 0x9, 0x9, 0),
	REGULATOR_LINEAR_RANGE(2700000, 0xa, 0xb, 100000),
	REGULATOR_LINEAR_RANGE(3000000, 0xc, 0xf, 100000),
};

static const struct regulator_linear_range axp152_ldo0_ranges[] = {
	REGULATOR_LINEAR_RANGE(5000000, 0x0, 0x0, 0),
	REGULATOR_LINEAR_RANGE(3300000, 0x1, 0x1, 0),
	REGULATOR_LINEAR_RANGE(2800000, 0x2, 0x2, 0),
	REGULATOR_LINEAR_RANGE(2500000, 0x3, 0x3, 0),
};

static const struct regulator_desc axp152_regulators[] = {
	AXP_DESC_RANGES(AXP152, DCDC1, "dcdc1", "vin1", axp152_dcdc1_ranges,
			0x10, AXP152_DCDC1_V_OUT, 0xf, AXP152_LDO3456_DC1234_CTRL, BIT(7)),
	AXP_DESC(AXP152, DCDC2, "dcdc2", "vin2", 700, 2275, 25,
			AXP152_DCDC2_V_OUT, 0x3f, AXP152_LDO3456_DC1234_CTRL, BIT(6)),
	AXP_DESC(AXP152, DCDC3, "dcdc3", "vin3", 700, 3500, 50,
			AXP152_DCDC3_V_OUT, 0x3f, AXP152_LDO3456_DC1234_CTRL, BIT(5)),
	AXP_DESC(AXP152, DCDC4, "dcdc4", "vin4", 700, 3500, 25,
			AXP152_DCDC4_V_OUT, 0x7f, AXP152_LDO3456_DC1234_CTRL, BIT(4)),
	AXP_DESC_RANGES(AXP152, ALDO1, "aldo1", "aldoin", axp152_aldo1_ranges,
			0x10, AXP152_ALDO12_V_OUT, 0xf0, AXP152_LDO3456_DC1234_CTRL, BIT(3)),
	AXP_DESC_RANGES(AXP152, ALDO2, "aldo2", "aldoin", axp152_aldo2_ranges,
			0x10, AXP152_ALDO12_V_OUT, 0xf, AXP152_LDO3456_DC1234_CTRL, BIT(2)),
	AXP_DESC(AXP152, DLDO1, "dldo1", "dldoin", 700, 3500, 100,
			AXP152_DLDO1_V_OUT, 0x1f, AXP152_LDO3456_DC1234_CTRL, BIT(1)),
	AXP_DESC(AXP152, DLDO2, "dldo2", "dldoin", 700, 3500, 100,
			AXP152_DLDO2_V_OUT, 0x1f, AXP152_LDO3456_DC1234_CTRL, BIT(0)),
	AXP_DESC_RANGES(AXP152, LDO0, "ldo0", "ldoin", axp152_ldo0_ranges,
			0x4, AXP152_LDO0_CTRL, 0x30, AXP152_LDO0_CTRL, BIT(7)),
	AXP_DESC_IO(AXP152, GPIO2_LDO, "gpio2_ldo", "gpio_ldo", 1800, 3300, 100,
			AXP152_LDOGPIO2_V_OUT, 0xf, AXP152_GPIO2_CTRL, 0x7, 0x2, 0x7),
	AXP_DESC_FIXED(AXP152, RTC13, "rtcldo13", "rtcldo13in", 1300),
	AXP_DESC_FIXED(AXP152, RTC18, "rtcldo18", "rtcldo18in", 1800),
};

static const struct regulator_linear_range axp20x_ldo4_ranges[] = {
	REGULATOR_LINEAR_RANGE(1250000, 0x0, 0x0, 0),
	REGULATOR_LINEAR_RANGE(1300000, 0x1, 0x8, 100000),
	REGULATOR_LINEAR_RANGE(2500000, 0x9, 0x9, 0),
	REGULATOR_LINEAR_RANGE(2700000, 0xa, 0xb, 100000),
	REGULATOR_LINEAR_RANGE(3000000, 0xc, 0xf, 100000),
};

static const struct regulator_desc axp20x_regulators[] = {
	AXP_DESC(AXP20X, DCDC2, "dcdc2", "vin2", 700, 2275, 25,
		 AXP20X_DCDC2_V_OUT, 0x3f, AXP20X_PWR_OUT_CTRL, 0x10),
	AXP_DESC(AXP20X, DCDC3, "dcdc3", "vin3", 700, 3500, 25,
		 AXP20X_DCDC3_V_OUT, 0x7f, AXP20X_PWR_OUT_CTRL, 0x02),
	AXP_DESC_FIXED(AXP20X, LDO1, "ldo1", "acin", 1300),
	AXP_DESC(AXP20X, LDO2, "ldo2", "ldo24in", 1800, 3300, 100,
		 AXP20X_LDO24_V_OUT, 0xf0, AXP20X_PWR_OUT_CTRL, 0x04),
	AXP_DESC(AXP20X, LDO3, "ldo3", "ldo3in", 700, 3500, 25,
		 AXP20X_LDO3_V_OUT, 0x7f, AXP20X_PWR_OUT_CTRL, 0x40),
	AXP_DESC_RANGES(AXP20X, LDO4, "ldo4", "ldo24in", axp20x_ldo4_ranges,
			16, AXP20X_LDO24_V_OUT, 0x0f, AXP20X_PWR_OUT_CTRL,
			0x08),
	AXP_DESC_IO(AXP20X, LDO5, "ldo5", "ldo5in", 1800, 3300, 100,
		    AXP20X_LDO5_V_OUT, 0xf0, AXP20X_GPIO0_CTRL, 0x07,
		    AXP20X_IO_ENABLED, AXP20X_IO_DISABLED),
};

static const struct regulator_desc axp22x_regulators[] = {
	AXP_DESC(AXP22X, DCDC1, "dcdc1", "vin1", 1600, 3400, 100,
		 AXP22X_DCDC1_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL1, BIT(1)),
	AXP_DESC(AXP22X, DCDC2, "dcdc2", "vin2", 600, 1540, 20,
		 AXP22X_DCDC2_V_OUT, 0x3f, AXP22X_PWR_OUT_CTRL1, BIT(2)),
	AXP_DESC(AXP22X, DCDC3, "dcdc3", "vin3", 600, 1860, 20,
		 AXP22X_DCDC3_V_OUT, 0x3f, AXP22X_PWR_OUT_CTRL1, BIT(3)),
	AXP_DESC(AXP22X, DCDC4, "dcdc4", "vin4", 600, 1540, 20,
		 AXP22X_DCDC4_V_OUT, 0x3f, AXP22X_PWR_OUT_CTRL1, BIT(4)),
	AXP_DESC(AXP22X, DCDC5, "dcdc5", "vin5", 1000, 2550, 50,
		 AXP22X_DCDC5_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL1, BIT(5)),
	/* secondary switchable output of DCDC1 */
	AXP_DESC_SW(AXP22X, DC1SW, "dc1sw", NULL, AXP22X_PWR_OUT_CTRL2,
		    BIT(7)),
	/* LDO regulator internally chained to DCDC5 */
	AXP_DESC(AXP22X, DC5LDO, "dc5ldo", NULL, 700, 1400, 100,
		 AXP22X_DC5LDO_V_OUT, 0x7, AXP22X_PWR_OUT_CTRL1, BIT(0)),
	AXP_DESC(AXP22X, ALDO1, "aldo1", "aldoin", 700, 3300, 100,
		 AXP22X_ALDO1_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL1, BIT(6)),
	AXP_DESC(AXP22X, ALDO2, "aldo2", "aldoin", 700, 3300, 100,
		 AXP22X_ALDO2_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL1, BIT(7)),
	AXP_DESC(AXP22X, ALDO3, "aldo3", "aldoin", 700, 3300, 100,
		 AXP22X_ALDO3_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL3, BIT(7)),
	AXP_DESC(AXP22X, DLDO1, "dldo1", "dldoin", 700, 3300, 100,
		 AXP22X_DLDO1_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2, BIT(3)),
	AXP_DESC(AXP22X, DLDO2, "dldo2", "dldoin", 700, 3300, 100,
		 AXP22X_DLDO2_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2, BIT(4)),
	AXP_DESC(AXP22X, DLDO3, "dldo3", "dldoin", 700, 3300, 100,
		 AXP22X_DLDO3_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2, BIT(5)),
	AXP_DESC(AXP22X, DLDO4, "dldo4", "dldoin", 700, 3300, 100,
		 AXP22X_DLDO4_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2, BIT(6)),
	AXP_DESC(AXP22X, ELDO1, "eldo1", "eldoin", 700, 3300, 100,
		 AXP22X_ELDO1_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2, BIT(0)),
	AXP_DESC(AXP22X, ELDO2, "eldo2", "eldoin", 700, 3300, 100,
		 AXP22X_ELDO2_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2, BIT(1)),
	AXP_DESC(AXP22X, ELDO3, "eldo3", "eldoin", 700, 3300, 100,
		 AXP22X_ELDO3_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2, BIT(2)),
	/* Note the datasheet only guarantees reliable operation up to
	 * 3.3V, this needs to be enforced via dts provided constraints */
	AXP_DESC_IO(AXP22X, LDO_IO0, "ldo_io0", "ips", 700, 3800, 100,
		    AXP22X_LDO_IO0_V_OUT, 0x1f, AXP20X_GPIO0_CTRL, 0x07,
		    AXP22X_IO_ENABLED, AXP22X_IO_DISABLED),
	/* Note the datasheet only guarantees reliable operation up to
	 * 3.3V, this needs to be enforced via dts provided constraints */
	AXP_DESC_IO(AXP22X, LDO_IO1, "ldo_io1", "ips", 700, 3800, 100,
		    AXP22X_LDO_IO1_V_OUT, 0x1f, AXP20X_GPIO1_CTRL, 0x07,
		    AXP22X_IO_ENABLED, AXP22X_IO_DISABLED),
	AXP_DESC_FIXED(AXP22X, RTC_LDO, "rtc_ldo", "ips", 3000),
};

static const struct regulator_desc axp22x_drivevbus_regulator = {
	.name		= "drivevbus",
	.supply_name	= "drivevbusin",
	.of_match	= of_match_ptr("drivevbus"),
	.regulators_node = of_match_ptr("regulators"),
	.type		= REGULATOR_VOLTAGE,
	.owner		= THIS_MODULE,
	.enable_reg	= AXP20X_VBUS_IPSOUT_MGMT,
	.enable_mask	= BIT(2),
	.ops		= &axp20x_ops_sw,
};

static const struct regulator_linear_range axp806_dcdca_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000, 0x0, 0x32, 10000),
	REGULATOR_LINEAR_RANGE(1120000, 0x33, 0x47, 20000),
};

static const struct regulator_linear_range axp806_dcdcd_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000, 0x0, 0x2d, 20000),
	REGULATOR_LINEAR_RANGE(1600000, 0x2e, 0x3f, 100000),
};

static const struct regulator_linear_range axp806_cldo2_ranges[] = {
	REGULATOR_LINEAR_RANGE(700000, 0x0, 0x1a, 100000),
	REGULATOR_LINEAR_RANGE(3400000, 0x1b, 0x1f, 200000),
};

static const struct regulator_desc axp806_regulators[] = {
	AXP_DESC_RANGES(AXP806, DCDCA, "dcdca", "vina", axp806_dcdca_ranges,
			72, AXP806_DCDCA_V_CTRL, 0x7f, AXP806_PWR_OUT_CTRL1,
			BIT(0)),
	AXP_DESC(AXP806, DCDCB, "dcdcb", "vinb", 1000, 2550, 50,
		 AXP806_DCDCB_V_CTRL, 0x1f, AXP806_PWR_OUT_CTRL1, BIT(1)),
	AXP_DESC_RANGES(AXP806, DCDCC, "dcdcc", "vinc", axp806_dcdca_ranges,
			72, AXP806_DCDCC_V_CTRL, 0x7f, AXP806_PWR_OUT_CTRL1,
			BIT(2)),
	AXP_DESC_RANGES(AXP806, DCDCD, "dcdcd", "vind", axp806_dcdcd_ranges,
			64, AXP806_DCDCD_V_CTRL, 0x3f, AXP806_PWR_OUT_CTRL1,
			BIT(3)),
	AXP_DESC(AXP806, DCDCE, "dcdce", "vine", 1100, 3400, 100,
		 AXP806_DCDCE_V_CTRL, 0x1f, AXP806_PWR_OUT_CTRL1, BIT(4)),
	AXP_DESC(AXP806, ALDO1, "aldo1", "aldoin", 700, 3300, 100,
		 AXP806_ALDO1_V_CTRL, 0x1f, AXP806_PWR_OUT_CTRL1, BIT(5)),
	AXP_DESC(AXP806, ALDO2, "aldo2", "aldoin", 700, 3400, 100,
		 AXP806_ALDO2_V_CTRL, 0x1f, AXP806_PWR_OUT_CTRL1, BIT(6)),
	AXP_DESC(AXP806, ALDO3, "aldo3", "aldoin", 700, 3300, 100,
		 AXP806_ALDO3_V_CTRL, 0x1f, AXP806_PWR_OUT_CTRL1, BIT(7)),
	AXP_DESC(AXP806, BLDO1, "bldo1", "bldoin", 700, 1900, 100,
		 AXP806_BLDO1_V_CTRL, 0x0f, AXP806_PWR_OUT_CTRL2, BIT(0)),
	AXP_DESC(AXP806, BLDO2, "bldo2", "bldoin", 700, 1900, 100,
		 AXP806_BLDO2_V_CTRL, 0x0f, AXP806_PWR_OUT_CTRL2, BIT(1)),
	AXP_DESC(AXP806, BLDO3, "bldo3", "bldoin", 700, 1900, 100,
		 AXP806_BLDO3_V_CTRL, 0x0f, AXP806_PWR_OUT_CTRL2, BIT(2)),
	AXP_DESC(AXP806, BLDO4, "bldo4", "bldoin", 700, 1900, 100,
		 AXP806_BLDO4_V_CTRL, 0x0f, AXP806_PWR_OUT_CTRL2, BIT(3)),
	AXP_DESC(AXP806, CLDO1, "cldo1", "cldoin", 700, 3300, 100,
		 AXP806_CLDO1_V_CTRL, 0x1f, AXP806_PWR_OUT_CTRL2, BIT(4)),
	AXP_DESC_RANGES(AXP806, CLDO2, "cldo2", "cldoin", axp806_cldo2_ranges,
			32, AXP806_CLDO2_V_CTRL, 0x1f, AXP806_PWR_OUT_CTRL2,
			BIT(5)),
	AXP_DESC(AXP806, CLDO3, "cldo3", "cldoin", 700, 3300, 100,
		 AXP806_CLDO3_V_CTRL, 0x1f, AXP806_PWR_OUT_CTRL2, BIT(6)),
	AXP_DESC_SW(AXP806, SW, "sw", "swin", AXP806_PWR_OUT_CTRL2, BIT(7)),
};

static const struct regulator_linear_range axp809_dcdc4_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000, 0x0, 0x2f, 20000),
	REGULATOR_LINEAR_RANGE(1800000, 0x30, 0x38, 100000),
};

static const struct regulator_desc axp809_regulators[] = {
	AXP_DESC(AXP809, DCDC1, "dcdc1", "vin1", 1600, 3400, 100,
		 AXP22X_DCDC1_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL1, BIT(1)),
	AXP_DESC(AXP809, DCDC2, "dcdc2", "vin2", 600, 1540, 20,
		 AXP22X_DCDC2_V_OUT, 0x3f, AXP22X_PWR_OUT_CTRL1, BIT(2)),
	AXP_DESC(AXP809, DCDC3, "dcdc3", "vin3", 600, 1860, 20,
		 AXP22X_DCDC3_V_OUT, 0x3f, AXP22X_PWR_OUT_CTRL1, BIT(3)),
	AXP_DESC_RANGES(AXP809, DCDC4, "dcdc4", "vin4", axp809_dcdc4_ranges,
			57, AXP22X_DCDC4_V_OUT, 0x3f, AXP22X_PWR_OUT_CTRL1,
			BIT(4)),
	AXP_DESC(AXP809, DCDC5, "dcdc5", "vin5", 1000, 2550, 50,
		 AXP22X_DCDC5_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL1, BIT(5)),
	/* secondary switchable output of DCDC1 */
	AXP_DESC_SW(AXP809, DC1SW, "dc1sw", NULL, AXP22X_PWR_OUT_CTRL2,
		    BIT(7)),
	/* LDO regulator internally chained to DCDC5 */
	AXP_DESC(AXP809, DC5LDO, "dc5ldo", NULL, 700, 1400, 100,
		 AXP22X_DC5LDO_V_OUT, 0x7, AXP22X_PWR_OUT_CTRL1, BIT(0)),
	AXP_DESC(AXP809, ALDO1, "aldo1", "aldoin", 700, 3300, 100,
		 AXP22X_ALDO1_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL1, BIT(6)),
	AXP_DESC(AXP809, ALDO2, "aldo2", "aldoin", 700, 3300, 100,
		 AXP22X_ALDO2_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL1, BIT(7)),
	AXP_DESC(AXP809, ALDO3, "aldo3", "aldoin", 700, 3300, 100,
		 AXP22X_ALDO3_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2, BIT(5)),
	AXP_DESC_RANGES(AXP809, DLDO1, "dldo1", "dldoin", axp806_cldo2_ranges,
			32, AXP22X_DLDO1_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2,
			BIT(3)),
	AXP_DESC(AXP809, DLDO2, "dldo2", "dldoin", 700, 3300, 100,
		 AXP22X_DLDO2_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2, BIT(4)),
	AXP_DESC(AXP809, ELDO1, "eldo1", "eldoin", 700, 3300, 100,
		 AXP22X_ELDO1_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2, BIT(0)),
	AXP_DESC(AXP809, ELDO2, "eldo2", "eldoin", 700, 3300, 100,
		 AXP22X_ELDO2_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2, BIT(1)),
	AXP_DESC(AXP809, ELDO3, "eldo3", "eldoin", 700, 3300, 100,
		 AXP22X_ELDO3_V_OUT, 0x1f, AXP22X_PWR_OUT_CTRL2, BIT(2)),
	/*
	 * Note the datasheet only guarantees reliable operation up to
	 * 3.3V, this needs to be enforced via dts provided constraints
	 */
	AXP_DESC_IO(AXP809, LDO_IO0, "ldo_io0", "ips", 700, 3800, 100,
		    AXP22X_LDO_IO0_V_OUT, 0x1f, AXP20X_GPIO0_CTRL, 0x07,
		    AXP22X_IO_ENABLED, AXP22X_IO_DISABLED),
	/*
	 * Note the datasheet only guarantees reliable operation up to
	 * 3.3V, this needs to be enforced via dts provided constraints
	 */
	AXP_DESC_IO(AXP809, LDO_IO1, "ldo_io1", "ips", 700, 3800, 100,
		    AXP22X_LDO_IO1_V_OUT, 0x1f, AXP20X_GPIO1_CTRL, 0x07,
		    AXP22X_IO_ENABLED, AXP22X_IO_DISABLED),
	AXP_DESC_FIXED(AXP809, RTC_LDO, "rtc_ldo", "ips", 1800),
	AXP_DESC_SW(AXP809, SW, "sw", "swin", AXP22X_PWR_OUT_CTRL2, BIT(6)),
};

static const struct regulator_linear_range axp2101_dcdc2_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x0, 0x46, 10000),
	REGULATOR_LINEAR_RANGE(1220000, 0x47, 0x57, 20000),
};

static const struct regulator_linear_range axp2101_dcdc3_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x0, 0x46, 10000),
	REGULATOR_LINEAR_RANGE(1220000, 0x47, 0x57, 20000),
	REGULATOR_LINEAR_RANGE(1600000, 0x58, 0x6a, 100000),
};

static const struct regulator_linear_range axp2101_dcdc4_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x0, 0x46, 10000),
	REGULATOR_LINEAR_RANGE(1220000, 0x47, 0x66, 20000),
};

static const struct regulator_linear_range axp2101_rtcldo_ranges[] = {
	REGULATOR_LINEAR_RANGE(1800000, 0x0, 0x0, 0),
	REGULATOR_LINEAR_RANGE(2500000, 0x1, 0x1, 0),
	REGULATOR_LINEAR_RANGE(2800000, 0x2, 0x2, 0),
	REGULATOR_LINEAR_RANGE(3300000, 0x3, 0x3, 0),
};

static const struct regulator_linear_range axp2101_dcdc5_ranges[] = {
	REGULATOR_LINEAR_RANGE(1400000, 0x0, 0x17, 100000),
	REGULATOR_LINEAR_RANGE(1200000, 0x19, 0x19, 0),
};

static const struct regulator_desc axp2101_regulators[] = {
	AXP_DESC(AXP2101, DCDC1, "dcdc1", "vin1", 1500, 3400, 100,
		 AXP2101_DCDC1_CFG, 0x1f, AXP2101_DCDC_CFG0, BIT(0)),
	AXP_DESC_RANGES(AXP2101, DCDC2, "dcdc2", "vin2", axp2101_dcdc2_ranges,
			0x58, AXP2101_DCDC2_CFG, 0x7f, AXP2101_DCDC_CFG0,
			BIT(1)),
	AXP_DESC_RANGES(AXP2101, DCDC3, "dcdc3", "vin3", axp2101_dcdc3_ranges,
			0x6b, AXP2101_DCDC3_CFG, 0x7f, AXP2101_DCDC_CFG0,
			BIT(2)),
	AXP_DESC_RANGES(AXP2101, DCDC4, "dcdc4", "vin4", axp2101_dcdc4_ranges,
			0x67, AXP2101_DCDC4_CFG, 0x7f, AXP2101_DCDC_CFG0,
			BIT(3)),
	AXP_DESC_RANGES(AXP2101, DCDC5, "dcdc5", "vin5", axp2101_dcdc5_ranges,
			0x19, AXP2101_DCDC5_CFG, 0x1f, AXP2101_DCDC_CFG0,
			BIT(4)),
	AXP_DESC_FIXED(AXP2101, LDO1, "rtcldo", "rtcldoin", 1800),
	AXP_DESC_FIXED(AXP2101, LDO2, "rtcldo1", "rtcldo1in", 1800),
	AXP_DESC(AXP2101, LDO3, "aldo1", "aldoin", 500, 3500, 100,
		 AXP2101_ALDO1_CFG, 0x1f, AXP2101_LDO_EN_CFG0, BIT(0)),
	AXP_DESC(AXP2101, LDO4, "aldo2", "aldoin", 500, 3500, 100,
		 AXP2101_ALDO2_CFG, 0x1f, AXP2101_LDO_EN_CFG0, BIT(1)),
	AXP_DESC(AXP2101, LDO5, "aldo3", "aldoin", 500, 3500, 100,
		 AXP2101_ALDO3_CFG, 0x1f, AXP2101_LDO_EN_CFG0, BIT(2)),
	AXP_DESC(AXP2101, LDO6, "aldo4", "aldoin", 500, 3500, 100,
		 AXP2101_ALDO4_CFG, 0x1f, AXP2101_LDO_EN_CFG0, BIT(3)),
	AXP_DESC(AXP2101, LDO7, "bldo1", "bldoin", 500, 3500, 100,
		 AXP2101_BLDO1_CFG, 0x1f, AXP2101_LDO_EN_CFG0, BIT(4)),
	AXP_DESC(AXP2101, LDO8, "bldo2", "bldoin", 500, 3500, 100,
		 AXP2101_BLDO2_CFG, 0x1f, AXP2101_LDO_EN_CFG0, BIT(5)),
	AXP_DESC(AXP2101, LDO9, "dldo1", "dldoin", 500, 3500, 100,
		 AXP2101_DLDO1_CFG, 0x1f, AXP2101_LDO_EN_CFG0, BIT(7)),
	AXP_DESC(AXP2101, LDO10, "dldo2", "dldoin", 500, 1400, 50,
		 AXP2101_DLDO2_CFG, 0x1f, AXP2101_LDO_EN_CFG1, BIT(0)),
	AXP_DESC(AXP2101, LDO11, "cpusldo", "cpusldoin", 500, 1400, 50,
		 AXP2101_CPUSLD_CFG, 0x1f, AXP2101_LDO_EN_CFG0, BIT(6)),
};

static const struct regulator_linear_range axp15_dcdc1_ranges[] = {
	REGULATOR_LINEAR_RANGE(1700000, 0x0, 0x4, 100000),
	REGULATOR_LINEAR_RANGE(2400000, 0x5, 0x9, 100000),
	REGULATOR_LINEAR_RANGE(3000000, 0xA, 0xF, 100000),
};

static const struct regulator_linear_range axp15_aldo2_ranges[] = {
	REGULATOR_LINEAR_RANGE(1200000, 0x0, 0x8, 100000),
	REGULATOR_LINEAR_RANGE(2500000, 0x9, 0x9, 0),
	REGULATOR_LINEAR_RANGE(2700000, 0xA, 0xB, 100000),
	REGULATOR_LINEAR_RANGE(3000000, 0xC, 0xF, 100000),
};

static const struct regulator_linear_range axp15_ldo0_ranges[] = {
	REGULATOR_LINEAR_RANGE(5000000, 0x0, 0x0, 0),
	REGULATOR_LINEAR_RANGE(3300000, 0x1, 0x1, 0),
	REGULATOR_LINEAR_RANGE(2800000, 0x2, 0x2, 0),
	REGULATOR_LINEAR_RANGE(2500000, 0x3, 0x3, 0),
};

static const struct regulator_desc axp15_regulators[] = {
	AXP_DESC_RANGES(AXP15, DCDC1, "dcdc1", "vin1", axp15_dcdc1_ranges,
			0x10, AXP15_DC1OUT_VOL, 0xf, AXP15_LDO3456_DC1234_CTL, BIT(7)),
	AXP_DESC(AXP15, DCDC2, "dcdc2", "vin2", 700, 2275, 25,
		AXP15_DC2OUT_VOL, 0x3f, AXP15_LDO3456_DC1234_CTL, BIT(6)),
	AXP_DESC(AXP15, DCDC3, "dcdc3", "vin3", 700, 3500, 25,
		AXP15_DC3OUT_VOL, 0x3f, AXP15_LDO3456_DC1234_CTL, BIT(5)),
	AXP_DESC(AXP15, DCDC4, "dcdc4", "vin4", 700, 3500, 50,
		AXP15_DC4OUT_VOL, 0x7f, AXP15_LDO3456_DC1234_CTL, BIT(4)),

	AXP_DESC_RANGES(AXP15, LDO1, "ldo0", "ldo0in", axp15_ldo0_ranges,
			0x4, AXP15_LDO0OUT_VOL, 0x30, AXP15_LDO0OUT_VOL, BIT(7)),
	AXP_DESC_FIXED(AXP15, LDO2, "rtcldo", "rtcldoin", 3100),
	AXP_DESC(AXP15, LDO3, "aldo1", "aldoin", 1200, 3300, 100,
		 AXP15_LDO34OUT_VOL, 0xf0, AXP15_LDO3456_DC1234_CTL, BIT(3)),
	AXP_DESC_RANGES(AXP15, LDO4, "aldo2", "aldoin", axp15_aldo2_ranges,
			0x10, AXP15_LDO34OUT_VOL, 0xf, AXP15_LDO3456_DC1234_CTL, BIT(3)),
	AXP_DESC(AXP15, LDO5, "dldo1", "dldoin", 700, 3500, 100,
		 AXP15_LDO5OUT_VOL, 0x1f, AXP15_LDO3456_DC1234_CTL, BIT(1)),
	AXP_DESC(AXP15, LDO6, "dldo2", "dldoin", 700, 3500, 100,
		 AXP15_LDO6OUT_VOL, 0x1f, AXP15_LDO3456_DC1234_CTL, BIT(0)),
	AXP_DESC_IO(AXP15, LDO7, "gpio", "gpioin", 1800, 3300, 100,
		 AXP15_GPIO0_VOL, 0xf, AXP15_GPIO2_CTL, 0x7, 0x2, 0x7),
};

static const struct regulator_linear_range axp1530_dcdc1_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x0, 0x46, 10000),
	REGULATOR_LINEAR_RANGE(1220000, 0x47, 0x57, 20000),
	REGULATOR_LINEAR_RANGE(1600000, 0x58, 0x6A, 100000),
};

static const struct regulator_linear_range axp1530_dcdc2_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x0, 0x46, 10000),
	REGULATOR_LINEAR_RANGE(1220000, 0x47, 0x57, 20000),
};

static const struct regulator_linear_range axp1530_dcdc3_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x0, 0x46, 10000),
	REGULATOR_LINEAR_RANGE(1220000, 0x47, 0x66, 20000),
};

static const struct regulator_desc axp1530_regulators[] = {
	AXP_DESC_RANGES(AXP1530, DCDC1, "dcdc1", "vin1", axp1530_dcdc1_ranges,
			0x6B, AXP1530_DCDC1_CONRTOL, 0x7f, AXP1530_OUTPUT_CONTROL, BIT(0)),
	AXP_DESC_RANGES(AXP1530, DCDC2, "dcdc2", "vin2", axp1530_dcdc2_ranges,
			0x58, AXP1530_DCDC2_CONRTOL, 0x7f, AXP1530_OUTPUT_CONTROL, BIT(1)),
	AXP_DESC_RANGES(AXP1530, DCDC3, "dcdc3", "vin3", axp1530_dcdc3_ranges,
			0x58, AXP1530_DCDC3_CONRTOL, 0x7f, AXP1530_OUTPUT_CONTROL, BIT(2)),
	AXP_DESC(AXP1530, LDO1, "ldo1", "ldo1in", 500, 3500, 100,
		AXP1530_ALDO1_CONRTOL, 0x1f, AXP1530_OUTPUT_CONTROL, BIT(3)),
	AXP_DESC(AXP1530, LDO2, "ldo2", "ldo2in", 500, 3500, 100,
		AXP1530_DLDO1_CONRTOL, 0x1f, AXP1530_OUTPUT_CONTROL, BIT(4)),
};

static const struct regulator_linear_range axp858_dcdc2_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x0, 0x46, 10000),
	REGULATOR_LINEAR_RANGE(1220000, 0x47, 0x57, 20000),
};

static const struct regulator_linear_range axp858_dcdc3_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x0, 0x46, 10000),
	REGULATOR_LINEAR_RANGE(1220000, 0x47, 0x57, 20000),
};

static const struct regulator_linear_range axp858_dcdc4_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x0, 0x46, 10000),
	REGULATOR_LINEAR_RANGE(1220000, 0x47, 0x57, 20000),
};

static const struct regulator_linear_range axp858_dcdc5_ranges[] = {
	REGULATOR_LINEAR_RANGE(800000, 0x0, 0x20, 10000),
	REGULATOR_LINEAR_RANGE(1140000, 0x21, 0x44, 20000),
};


static const struct regulator_desc axp858_regulators[] = {
	AXP_DESC(AXP858, DCDC1, "dcdc1", "vin1", 1500, 3400, 100,
		 AXP858_DCDC1_CONTROL, 0x1f, AXP858_OUTPUT_CONTROL1, BIT(0)),
	AXP_DESC_RANGES(AXP858, DCDC2, "dcdc2", "vin2", axp858_dcdc2_ranges,
			0x58, AXP858_DCDC2_CONTROL, 0x7f, AXP858_OUTPUT_CONTROL1, BIT(1)),
	AXP_DESC_RANGES(AXP858, DCDC3, "dcdc3", "vin3", axp858_dcdc3_ranges,
			0x58, AXP858_DCDC3_CONTROL, 0x7f, AXP858_OUTPUT_CONTROL1, BIT(2)),
	AXP_DESC_RANGES(AXP858, DCDC4, "dcdc4", "vin4", axp858_dcdc4_ranges,
			0x58, AXP858_DCDC4_CONTROL, 0x7f, AXP858_OUTPUT_CONTROL1, BIT(3)),
	AXP_DESC_RANGES(AXP858, DCDC5, "dcdc5", "vin5", axp858_dcdc5_ranges,
			0x45, AXP858_DCDC5_CONTROL, 0x7f, AXP858_OUTPUT_CONTROL1, BIT(4)),
	AXP_DESC(AXP858, DCDC6, "dcdc6", "vin6", 500, 3400, 100,
		 AXP858_DCDC6_CONTROL, 0x1f, AXP858_OUTPUT_CONTROL1, BIT(5)),
	AXP_DESC(AXP858, ALDO1, "aldo1", "aldoin", 700, 3300, 100,
		 AXP858_ALDO1_CONTROL, 0x1f, AXP858_OUTPUT_CONTROL2, BIT(0)),
	AXP_DESC(AXP858, ALDO2, "aldo2", "aldoin", 700, 3300, 100,
		 AXP858_ALDO2_CTL, 0x1f, AXP858_OUTPUT_CONTROL2, BIT(1)),
	AXP_DESC(AXP858, ALDO3, "aldo3", "aldoin", 700, 3300, 100,
		 AXP858_ALDO3_CTL, 0x1f, AXP858_OUTPUT_CONTROL2, BIT(2)),
	AXP_DESC(AXP858, ALDO4, "aldo4", "aldoin", 700, 3300, 100,
		 AXP858_ALDO4_CTL, 0x1f, AXP858_OUTPUT_CONTROL2, BIT(3)),
	AXP_DESC(AXP858, ALDO5, "aldo5", "aldoin", 700, 3300, 100,
		 AXP858_ALDO5_CTL, 0x1f, AXP858_OUTPUT_CONTROL2, BIT(4)),
	AXP_DESC(AXP858, BLDO1, "bldo1", "bldoin", 700, 3300, 100,
		 AXP858_BLDO1_CTL, 0x1f, AXP858_OUTPUT_CONTROL2, BIT(5)),
	AXP_DESC(AXP858, BLDO2, "bldo2", "bldoin", 700, 3300, 100,
		 AXP858_BLDO2_CTL, 0x1f, AXP858_OUTPUT_CONTROL2, BIT(6)),
	AXP_DESC(AXP858, BLDO3, "bldo3", "bldoin", 700, 3300, 100,
		 AXP858_BLDO3_CTL, 0x1f, AXP858_OUTPUT_CONTROL2, BIT(7)),
	AXP_DESC(AXP858, BLDO4, "bldo4", "bldoin", 700, 3300, 100,
		 AXP858_BLDO4_CTL, 0x1f, AXP858_OUTPUT_CONTROL3, BIT(0)),
	AXP_DESC(AXP858, BLDO5, "bldo5", "bldoin", 700, 3300, 100,
		 AXP858_BLDO5_CTL, 0x1f, AXP858_OUTPUT_CONTROL3, BIT(1)),
	AXP_DESC(AXP858, CLDO1, "cldo1", "cldoin", 700, 3300, 100,
		 AXP858_CLDO1_CTL, 0x1f, AXP858_OUTPUT_CONTROL3, BIT(2)),
	AXP_DESC(AXP858, CLDO2, "cldo2", "cldoin", 700, 3300, 100,
		 AXP858_CLDO2_CTL, 0x1f, AXP858_OUTPUT_CONTROL3, BIT(3)),
	AXP_DESC(AXP858, CLDO3, "cldo3", "cldoin", 700, 3300, 100,
		 AXP858_CLDO3_GPIO1_CTL, 0x1f, AXP858_OUTPUT_CONTROL3, BIT(4)),
	AXP_DESC(AXP858, CLDO4, "cldo4", "cldoin", 700, 4200, 100,
		 AXP858_CLDO4_CTL, 0x3f, AXP858_OUTPUT_CONTROL3, BIT(5)),
	AXP_DESC(AXP858, CPUSLDO, "cpusldo", "cpusldoin", 700, 1400, 50,
		 AXP858_CPUSLDO_CTL, 0x1f, AXP858_OUTPUT_CONTROL3, BIT(6)),
	AXP_DESC_SW(AXP858, DC1SW, "dc1sw", "swin", AXP858_OUTPUT_CONTROL3, BIT(7)),
};

static const struct regulator_linear_range axp803_dcdc1_ranges[] = {
	REGULATOR_LINEAR_RANGE(1600000, 0x0, 0x12, 100000),
};

static const struct regulator_linear_range axp803_dcdc2_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x0, 0x46, 10000),
	REGULATOR_LINEAR_RANGE(1220000, 0x47, 0x4b, 20000),
};

static const struct regulator_linear_range axp803_dcdc3_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x0, 0x46, 10000),
	REGULATOR_LINEAR_RANGE(1220000, 0x47, 0x4b, 20000),
};

static const struct regulator_linear_range axp803_dcdc4_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x0, 0x46, 10000),
	REGULATOR_LINEAR_RANGE(1220000, 0x47, 0x4b, 20000),
};

static const struct regulator_linear_range axp803_dcdc5_ranges[] = {
	REGULATOR_LINEAR_RANGE(800000, 0x0, 0x20, 10000),
	REGULATOR_LINEAR_RANGE(1140000, 0x21, 0x44, 20000),
};

static const struct regulator_linear_range axp803_dcdc6_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000, 0x0, 0x32, 10000),
	REGULATOR_LINEAR_RANGE(1120000, 0x33, 0x47, 20000),
};

static const struct regulator_linear_range axp803_dcdc7_ranges[] = {
	REGULATOR_LINEAR_RANGE(600000, 0x0, 0x32, 10000),
	REGULATOR_LINEAR_RANGE(1120000, 0x33, 0x47, 20000),
};

static const struct regulator_linear_range axp803_aldo3_ranges[] = {
	REGULATOR_LINEAR_RANGE(700000, 0x0, 0x1a, 100000),
	REGULATOR_LINEAR_RANGE(3300000, 0x1b, 0x1f, 0),
};

static const struct regulator_linear_range axp803_dldo2_ranges[] = {
	REGULATOR_LINEAR_RANGE(700000, 0x0, 0x1b, 100000),
	REGULATOR_LINEAR_RANGE(3600000, 0x1c, 0x1f, 200000),
};

static const struct regulator_desc axp803_regulators[] = {
	AXP_DESC_RANGES_VOL_DELAY
		(AXP803, DCDC1, "dcdc1", "vin1", axp803_dcdc1_ranges,
		 0x13, AXP803_DC1OUT_VOL, 0x1f, AXP803_LDO_DC_EN1, BIT(0)),
	AXP_DESC_RANGES_VOL_DELAY
		(AXP803, DCDC2, "dcdc2", "vin2", axp803_dcdc2_ranges,
		 0x4c, AXP803_DC2OUT_VOL, 0x7f, AXP803_LDO_DC_EN1, BIT(1)),
	AXP_DESC_RANGES_VOL_DELAY
		(AXP803, DCDC3, "dcdc3", "vin3", axp803_dcdc3_ranges,
		0x4c, AXP803_DC3OUT_VOL, 0x7f, AXP803_LDO_DC_EN1, BIT(2)),
	AXP_DESC_RANGES_VOL_DELAY
		(AXP803, DCDC4, "dcdc4", "vin4", axp803_dcdc4_ranges,
		 0x4c, AXP803_DC4OUT_VOL, 0x7f, AXP803_LDO_DC_EN1, BIT(3)),
	AXP_DESC_RANGES_VOL_DELAY
		(AXP803, DCDC5, "dcdc5", "vin5", axp803_dcdc5_ranges,
		0x45, AXP803_DC5OUT_VOL, 0x7f, AXP803_LDO_DC_EN1, BIT(4)),
	AXP_DESC_RANGES_VOL_DELAY
		(AXP803, DCDC6, "dcdc6", "vin6", axp803_dcdc6_ranges,
		0x48, AXP803_DC6OUT_VOL, 0x7f, AXP803_LDO_DC_EN1, BIT(5)),
	AXP_DESC_RANGES_VOL_DELAY
		(AXP803, DCDC7, "dcdc7", "vin7", axp803_dcdc5_ranges,
		0x48, AXP803_DC7OUT_VOL, 0x7f, AXP803_LDO_DC_EN1, BIT(6)),
	AXP_DESC_FIXED(AXP803, RTCLDO, "rtcldo", "rtcldoin", 1800),
	AXP_DESC(AXP803, ALDO1, "aldo1", "aldoin", 700, 3300, 100,
		AXP803_ALDO1OUT_VOL, 0x1f, AXP803_LDO_DC_EN3, BIT(5)),
	AXP_DESC(AXP803, ALDO2, "aldo2", "aldoin", 700, 3300, 100,
		AXP803_ALDO2OUT_VOL, 0x1f, AXP803_LDO_DC_EN3, BIT(6)),
	AXP_DESC_RANGES(AXP803, ALDO3, "aldo3", "aldoin", axp803_aldo3_ranges,
			0x20, AXP803_ALDO3OUT_VOL, 0x1f, AXP803_LDO_DC_EN3, BIT(7)),
	AXP_DESC(AXP803, DLDO1, "dldo1", "dldoin", 700, 3300, 100,
		AXP803_DLDO1OUT_VOL, 0x1f, AXP803_LDO_DC_EN2, BIT(3)),
	AXP_DESC_RANGES(AXP803, DLDO2, "dldo2", "dldoin", axp803_dldo2_ranges,
			0x20, AXP803_DLDO2OUT_VOL, 0x1f, AXP803_LDO_DC_EN2, BIT(4)),
	AXP_DESC(AXP803, DLDO3, "dldo3", "dldoin", 700, 3300, 100,
		AXP803_DLDO3OUT_VOL, 0x1f, AXP803_LDO_DC_EN2, BIT(5)),
	AXP_DESC(AXP803, DLDO4, "dldo4", "dldoin", 700, 3300, 100,
		AXP803_DLDO4OUT_VOL, 0x1f, AXP803_LDO_DC_EN2, BIT(6)),
	AXP_DESC(AXP803, ELDO1, "eldo1", "eldoin", 700, 1900, 50,
		 AXP803_ELDO1OUT_VOL, 0x1f, AXP803_LDO_DC_EN2, BIT(0)),
	AXP_DESC(AXP803, ELDO2, "eldo2", "eldoin", 700, 1900, 50,
		 AXP803_ELDO2OUT_VOL, 0x1f, AXP803_LDO_DC_EN2, BIT(1)),
	AXP_DESC(AXP803, ELDO3, "eldo3", "eldoin", 700, 1900, 50,
		 AXP803_ELDO3OUT_VOL, 0x1f, AXP803_LDO_DC_EN2, BIT(2)),
	AXP_DESC(AXP803, FLDO1, "fldo1", "fldoin", 700, 1450, 50,
		 AXP803_FLDO1OUT_VOL, 0x0f, AXP803_LDO_DC_EN3, BIT(2)),
	AXP_DESC(AXP803, FLDO2, "fldo2", "fldoin", 700, 1450, 50,
		 AXP803_FLDO2OUT_VOL, 0x0f, AXP803_LDO_DC_EN3, BIT(3)),
	AXP_DESC_IO(AXP803, LDOIO0, "ldoio0", "ips", 700, 3300, 100,
		    AXP803_GPIO0LDOOUT_VOL, 0x1f, AXP803_GPIO0_CTL, 0x07,
		    0x3, 0x4),
	AXP_DESC_IO(AXP803, LDOIO1, "ldoio1", "ips", 700, 3300, 100,
		    AXP803_GPIO1LDOOUT_VOL, 0x1f, AXP803_GPIO1_CTL, 0x07,
		    0x3, 0x4),
	AXP_DESC_SW(AXP803, DC1SW, "dc1sw", "swin", AXP803_LDO_DC_EN2, BIT(7)),
};

static struct regulator_linear_range axp2202_dcdc1_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000, 0x0, 0x46, 10000),
	REGULATOR_LINEAR_RANGE(1220000, 0x47, 0x57, 20000),
};

static struct regulator_linear_range axp2202_dcdc2_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000, 0, 0x46, 10000),
	REGULATOR_LINEAR_RANGE(1220000, 0x47, 0x57, 20000),
	REGULATOR_LINEAR_RANGE(1600000, 0x58, 0x6b, 100000),
};

static struct regulator_linear_range axp2202_dcdc3_ranges[] = {
	REGULATOR_LINEAR_RANGE(500000, 0, 0x46, 10000),
	REGULATOR_LINEAR_RANGE(1220000, 0x47, 0x66, 20000),
};

static const struct regulator_desc axp2202_regulators[] = {
	AXP_DESC_RANGES_VOL_DELAY
			(AXP2202, DCDC1, "dcdc1", "vin-ps", axp2202_dcdc1_ranges,
			0x58, AXP2202_DCDC1_CFG, GENMASK(6, 0),
			AXP2202_DCDC_CFG0, BIT(0)),
	AXP_DESC_RANGES_VOL_DELAY
			(AXP2202, DCDC2, "dcdc2", "vin-ps", axp2202_dcdc2_ranges,
			0x6c, AXP2202_DCDC2_CFG, GENMASK(6, 0),
			AXP2202_DCDC_CFG0, BIT(1)),
	AXP_DESC_RANGES_VOL_DELAY
			(AXP2202, DCDC3, "dcdc3", "vin-ps", axp2202_dcdc3_ranges,
			0x67, AXP2202_DCDC3_CFG, GENMASK(6, 0),
			AXP2202_DCDC_CFG0, BIT(2)),
	AXP_DESC(AXP2202, DCDC4, "dcdc4", "vin-ps", 1000, 3700, 100,
			AXP2202_DCDC4_CFG, GENMASK(4, 0), AXP2202_DCDC_CFG0,
			BIT(3)),
	AXP_DESC(AXP2202, ALDO1, "aldo1", "aldo", 500, 3500, 100,
		 AXP2202_ALDO1_CFG, GENMASK(4, 0), AXP2202_LDO_EN_CFG0,
		 BIT(0)),
	AXP_DESC(AXP2202, ALDO2, "aldo2", "aldo", 500, 3500, 100,
		 AXP2202_ALDO2_CFG, GENMASK(4, 0), AXP2202_LDO_EN_CFG0,
		 BIT(1)),
	AXP_DESC(AXP2202, ALDO3, "aldo3", "aldo", 500, 3500, 100,
		 AXP2202_ALDO3_CFG, GENMASK(4, 0), AXP2202_LDO_EN_CFG0,
		 BIT(2)),
	AXP_DESC(AXP2202, ALDO4, "aldo4", "aldo", 500, 3500, 100,
		 AXP2202_ALDO4_CFG, GENMASK(4, 0), AXP2202_LDO_EN_CFG0,
		 BIT(3)),
	AXP_DESC(AXP2202, BLDO1, "bldo1", "bldo", 500, 3500, 100,
		 AXP2202_BLDO1_CFG, GENMASK(4, 0), AXP2202_LDO_EN_CFG0,
		 BIT(4)),
	AXP_DESC(AXP2202, BLDO2, "bldo2", "bldo", 500, 3500, 100,
		 AXP2202_BLDO2_CFG, GENMASK(4, 0), AXP2202_LDO_EN_CFG0,
		 BIT(5)),
	AXP_DESC(AXP2202, BLDO3, "bldo3", "bldo", 500, 3500, 100,
		 AXP2202_BLDO3_CFG, GENMASK(4, 0), AXP2202_LDO_EN_CFG0,
		 BIT(6)),
	AXP_DESC(AXP2202, BLDO4, "bldo4", "bldo", 500, 3500, 100,
		 AXP2202_BLDO4_CFG, GENMASK(4, 0), AXP2202_LDO_EN_CFG0,
		 BIT(7)),
	AXP_DESC(AXP2202, CLDO1, "cldo1", "cldo", 500, 3500, 100,
		 AXP2202_CLDO1_CFG, GENMASK(4, 0), AXP2202_LDO_EN_CFG1,
		 BIT(0)),
	AXP_DESC(AXP2202, CLDO2, "cldo2", "cldo", 500, 3500, 100,
		 AXP2202_CLDO2_CFG, GENMASK(4, 0), AXP2202_LDO_EN_CFG1,
		 BIT(1)),
	AXP_DESC(AXP2202, CLDO3, "cldo3", "cldo", 500, 3500, 100,
		 AXP2202_CLDO3_CFG, GENMASK(4, 0), AXP2202_LDO_EN_CFG1,
		 BIT(2)),
	AXP_DESC(AXP2202, CLDO4, "cldo4", "cldo", 500, 3500, 100,
		 AXP2202_CLDO4_CFG, GENMASK(4, 0), AXP2202_LDO_EN_CFG1,
		 BIT(3)),
	AXP_DESC_FIXED(AXP2202, RTCLDO, "rtcldo", "vin-ps", 1800),
	AXP_DESC(AXP2202, CPUSLDO, "cpusldo", "vin-ps", 500, 1400, 50,
		AXP2202_CPUSLDO_CFG, GENMASK(4, 0), AXP2202_LDO_EN_CFG1,
		BIT(4)),
};

static const struct regulator_desc axp803_drivevbus_regulator = {
	.name		= "drivevbus",
	.supply_name	= "drivevbusin",
	.of_match	= of_match_ptr("drivevbus"),
	.regulators_node = of_match_ptr("regulators"),
	.type		= REGULATOR_VOLTAGE,
	.owner		= THIS_MODULE,
	.enable_reg	= AXP803_IPS_SET,
	.enable_mask	= BIT(2),
	.ops		= &axp20x_ops_sw,
};

static const struct regulator_desc axp2202_drivevbus_regulator = {
	.name		= "drivevbus",
	.supply_name	= "drivevbusin",
	.of_match	= of_match_ptr("drivevbus"),
	.regulators_node = of_match_ptr("regulators"),
	.type		= REGULATOR_VOLTAGE,
	.owner		= THIS_MODULE,
	.enable_reg	= AXP2202_RBFET_CTRL,
	.enable_mask	= BIT(0),
	.ops		= &axp20x_ops_sw,
};

static const struct regulator_desc axp2202_A_drivevbus_regulator = {
	.name		= "drivevbus",
	.supply_name	= "drivevbusin",
	.of_match	= of_match_ptr("drivevbus"),
	.regulators_node = of_match_ptr("regulators"),
	.type		= REGULATOR_VOLTAGE,
	.owner		= THIS_MODULE,
	.enable_reg	= AXP2202_RBFET_CTRL,
	.enable_mask	= BIT(0),
	.vsel_reg	= AXP2202_MODULE_EN,
	.vsel_mask	= BIT(4),
	.ops		= &axp2202_ops_sw,
};

static int axp20x_set_dcdc_freq(struct platform_device *pdev, u32 dcdcfreq)
{
	struct axp20x_dev *axp20x = dev_get_drvdata(pdev->dev.parent);
	unsigned int reg = AXP20X_DCDC_FREQ;
	u32 min, max, def, step;

	switch (axp20x->variant) {
	case AXP202_ID:
	case AXP209_ID:
		min = 750;
		max = 1875;
		def = 1500;
		step = 75;
		break;
	case AXP806_ID:
		/*
		 * AXP806 DCDC work frequency setting has the same range and
		 * step as AXP22X, but at a different register.
		 * Fall through to the check below.
		 * (See include/linux/mfd/axp20x.h)
		 */
		reg = AXP806_DCDC_FREQ_CTRL;
	case AXP221_ID:
	case AXP223_ID:
	case AXP809_ID:
		min = 1800;
		max = 4050;
		def = 3000;
		step = 150;
		break;
	default:
		dev_err(&pdev->dev,
			"Setting DCDC frequency for unsupported AXP variant\n");
		return -EINVAL;
	}

	if (dcdcfreq == 0)
		dcdcfreq = def;

	if (dcdcfreq < min) {
		dcdcfreq = min;
		dev_warn(&pdev->dev, "DCDC frequency too low. Set to %ukHz\n",
			 min);
	}

	if (dcdcfreq > max) {
		dcdcfreq = max;
		dev_warn(&pdev->dev, "DCDC frequency too high. Set to %ukHz\n",
			 max);
	}

	dcdcfreq = (dcdcfreq - min) / step;

	return regmap_update_bits(axp20x->regmap, reg,
				  AXP20X_FREQ_DCDC_MASK, dcdcfreq);
}

static int axp20x_regulator_parse_dt(struct platform_device *pdev)
{
	struct device_node *np, *regulators;
	int ret;
	u32 dcdcfreq = 0;

	np = of_node_get(pdev->dev.parent->of_node);
	if (!np)
		return 0;

	regulators = of_get_child_by_name(np, "regulators");
	if (!regulators) {
		dev_warn(&pdev->dev, "regulators node not found\n");
	} else if (of_property_read_u32(regulators, "x-powers,dcdc-freq", &dcdcfreq) >= 0) {
		ret = axp20x_set_dcdc_freq(pdev, dcdcfreq);
		if (ret < 0) {
			dev_err(&pdev->dev, "Error setting dcdc frequency: %d\n", ret);
			return ret;
		}

		of_node_put(regulators);
	}

	return 0;
}

static int axp20x_set_dcdc_workmode(struct regulator_dev *rdev, int id, u32 workmode)
{
	struct axp20x_dev *axp20x = rdev_get_drvdata(rdev);
	unsigned int reg = AXP20X_DCDC_MODE;
	unsigned int mask;

	switch (axp20x->variant) {
	case AXP202_ID:
	case AXP209_ID:
		if ((id != AXP20X_DCDC2) && (id != AXP20X_DCDC3))
			return -EINVAL;

		mask = AXP20X_WORKMODE_DCDC2_MASK;
		if (id == AXP20X_DCDC3)
			mask = AXP20X_WORKMODE_DCDC3_MASK;

		workmode <<= ffs(mask) - 1;
		break;

	case AXP806_ID:
		reg = AXP806_DCDC_MODE_CTRL2;
		/*
		 * AXP806 DCDC regulator IDs have the same range as AXP22X.
		 * Fall through to the check below.
		 * (See include/linux/mfd/axp20x.h)
		 */
	case AXP221_ID:
	case AXP223_ID:
	case AXP809_ID:
		if (id < AXP22X_DCDC1 || id > AXP22X_DCDC5)
			return -EINVAL;

		mask = AXP22X_WORKMODE_DCDCX_MASK(id - AXP22X_DCDC1);
		workmode <<= id - AXP22X_DCDC1;
		break;

	default:
		/* should not happen */
		WARN_ON(1);
		return -EINVAL;
	}

	return regmap_update_bits(rdev->regmap, reg, mask, workmode);
}

/*
 * This function checks whether a regulator is part of a poly-phase
 * output setup based on the registers settings. Returns true if it is.
 */
static bool axp20x_is_polyphase_slave(struct axp20x_dev *axp20x, int id)
{
	u32 reg = 0;

	/* Only AXP806 has poly-phase outputs */
	if (axp20x->variant != AXP806_ID)
		return false;

	regmap_read(axp20x->regmap, AXP806_DCDC_MODE_CTRL2, &reg);

	switch (id) {
	case AXP806_DCDCB:
		return (((reg & GENMASK(7, 6)) == BIT(6)) ||
			((reg & GENMASK(7, 6)) == BIT(7)));
	case AXP806_DCDCC:
		return ((reg & GENMASK(7, 6)) == BIT(7));
	case AXP806_DCDCE:
		return !!(reg & BIT(5));
	}

	return false;
}

static int axp2101_regulator_probe(struct platform_device *pdev)
{
	struct regulator_dev *rdev;
	struct axp20x_dev *axp20x = dev_get_drvdata(pdev->dev.parent);
	const struct regulator_desc *regulators;
	struct regulator_config config = {
		.dev = pdev->dev.parent,
		.regmap = axp20x->regmap,
		.driver_data = axp20x,
	};
	int ret, i, nregulators;
	u32 workmode;
	const char *dcdc1_name = axp22x_regulators[AXP22X_DCDC1].name;
	const char *dcdc5_name = axp22x_regulators[AXP22X_DCDC5].name;
	bool drivevbus = false;
	u32 dval;
	struct regulator_delay *rdev_delay;

	switch (axp20x->variant) {
	case AXP152_ID:
		regulators = axp152_regulators;
		nregulators = AXP152_REG_ID_MAX;
		break;
	case AXP202_ID:
	case AXP209_ID:
		regulators = axp20x_regulators;
		nregulators = AXP20X_REG_ID_MAX;
		break;
	case AXP221_ID:
	case AXP223_ID:
		regulators = axp22x_regulators;
		nregulators = AXP22X_REG_ID_MAX;
		drivevbus = of_property_read_bool(pdev->dev.parent->of_node,
						  "x-powers,drive-vbus-en");
		break;
	case AXP806_ID:
		regulators = axp806_regulators;
		nregulators = AXP806_REG_ID_MAX;
		break;
	case AXP809_ID:
		regulators = axp809_regulators;
		nregulators = AXP809_REG_ID_MAX;
		break;
	case AXP2101_ID:
		regulators = axp2101_regulators;
		nregulators = AXP2101_REG_ID_MAX;
		break;
	case AXP15_ID:
		regulators = axp15_regulators;
		nregulators = AXP15_REG_ID_MAX;
		break;
	case AXP1530_ID:
		regulators = axp1530_regulators;
		nregulators = AXP1530_REG_ID_MAX;
		break;
	case AXP858_ID:
		regulators = axp858_regulators;
		nregulators = AXP858_REG_ID_MAX;
		break;
	case AXP803_ID:
		regulators = axp803_regulators;
		nregulators = AXP803_REG_ID_MAX;
		drivevbus = of_property_read_bool(pdev->dev.parent->of_node,
						  "x-powers,drive-vbus-en");
		break;
	case AXP2202_ID:
		regulators = axp2202_regulators;
		nregulators = AXP2202_REG_ID_MAX;
		drivevbus = of_property_read_bool(pdev->dev.parent->of_node,
						  "x-powers,drive-vbus-en");
	break;
	default:
		dev_err(&pdev->dev, "Unsupported AXP variant: %ld\n",
			axp20x->variant);
		return -EINVAL;
	}

	/* This only sets the dcdc freq. Ignore any errors */
	axp20x_regulator_parse_dt(pdev);

	for (i = 0; i < nregulators; i++) {
		const struct regulator_desc *desc = &regulators[i];
		struct regulator_desc *new_desc;

		/*
		 * If this regulator is a slave in a poly-phase setup,
		 * skip it, as its controls are bound to the master
		 * regulator and won't work.
		 */
		if (axp20x_is_polyphase_slave(axp20x, i))
			continue;

		/*
		 * Regulators DC1SW and DC5LDO are connected internally,
		 * so we have to handle their supply names separately.
		 *
		 * We always register the regulators in proper sequence,
		 * so the supply names are correctly read. See the last
		 * part of this loop to see where we save the DT defined
		 * name.
		 */
		if ((regulators == axp22x_regulators && i == AXP22X_DC1SW) ||
		    (regulators == axp809_regulators && i == AXP809_DC1SW)) {
			new_desc = devm_kzalloc(&pdev->dev, sizeof(*desc),
						GFP_KERNEL);
			*new_desc = regulators[i];
			new_desc->supply_name = dcdc1_name;
			desc = new_desc;
		}

		if ((regulators == axp22x_regulators && i == AXP22X_DC5LDO) ||
		    (regulators == axp809_regulators && i == AXP809_DC5LDO)) {
			new_desc = devm_kzalloc(&pdev->dev, sizeof(*desc),
						GFP_KERNEL);
			*new_desc = regulators[i];
			new_desc->supply_name = dcdc5_name;
			desc = new_desc;
		}

		rdev = devm_regulator_register(&pdev->dev, desc, &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "Failed to register %s\n",
				regulators[i].name);

			return PTR_ERR(rdev);
		}

		rdev_delay = devm_kzalloc(&pdev->dev, sizeof(*rdev_delay), GFP_KERNEL);
		if (!of_property_read_u32(rdev->dev.of_node,
				"regulator-step-delay-us", &dval))
			rdev_delay->step = dval;
		else
			rdev_delay->step = 0;

		if (!of_property_read_u32(rdev->dev.of_node,
				"regulator-final-delay-us", &dval))
			rdev_delay->final = dval;
		else
			rdev_delay->final = 0;

		rdev->reg_data = rdev_delay;

		ret = of_property_read_u32(rdev->dev.of_node,
					   "x-powers,dcdc-workmode",
					   &workmode);
		if (!ret) {
			if (axp20x_set_dcdc_workmode(rdev, i, workmode))
				dev_err(&pdev->dev, "Failed to set workmode on %s\n",
					rdev->desc->name);
		}

		/*
		 * Save AXP22X DCDC1 / DCDC5 regulator names for later.
		 */
		if ((regulators == axp22x_regulators && i == AXP22X_DCDC1) ||
		    (regulators == axp809_regulators && i == AXP809_DCDC1))
			of_property_read_string(rdev->dev.of_node,
						"regulator-name",
						&dcdc1_name);

		if ((regulators == axp22x_regulators && i == AXP22X_DCDC5) ||
		    (regulators == axp809_regulators && i == AXP809_DCDC5))
			of_property_read_string(rdev->dev.of_node,
						"regulator-name",
						&dcdc5_name);
	}

	if (drivevbus) {
		switch (axp20x->variant) {
		case AXP221_ID:
		case AXP223_ID:
			/* Change N_VBUSEN sense pin to DRIVEVBUS output pin */
			regmap_update_bits(axp20x->regmap, AXP20X_OVER_TMP,
					   AXP22X_MISC_N_VBUSEN_FUNC, 0);
			rdev = devm_regulator_register(&pdev->dev,
						       &axp22x_drivevbus_regulator,
						       &config);
			break;
		case AXP803_ID:
			/* Change N_VBUSEN sense pin to DRIVEVBUS output pin */
			regmap_update_bits(axp20x->regmap, AXP803_HOTOVER_CTL,
					   AXP803_MISC_N_VBUSEN_FUNC, 0);
			rdev = devm_regulator_register(&pdev->dev,
						       &axp803_drivevbus_regulator,
						       &config);
			break;
		case AXP2202_ID:
			regmap_read(axp20x->regmap, AXP2202_VBUS_TYPE, &ret);
			/* control two regs in a133b6 , compatible later */
			if (!ret) {
				rdev = devm_regulator_register(&pdev->dev,
						       &axp2202_A_drivevbus_regulator,
						       &config);
			} else {
				rdev = devm_regulator_register(&pdev->dev,
						       &axp2202_drivevbus_regulator,
						       &config);
			}
			break;
		default:
			dev_err(&pdev->dev, "AXP variant: %ld unsupported drivevbus\n",
				axp20x->variant);
			return -EINVAL;
		}

		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "Failed to register drivevbus\n");
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static int axp2101_regulator_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct axp20x_dev *axp20x = dev_get_drvdata(pdev->dev.parent);

	switch (axp20x->variant) {
	case AXP2202_ID:
		regmap_update_bits(axp20x->regmap, AXP2202_MODULE_EN, BIT(4), 0);
		break;
	default:
		break;
	}
	return 0;
}
static int axp2101_regulator_resume(struct platform_device *pdev)
{
	struct axp20x_dev *axp20x = dev_get_drvdata(pdev->dev.parent);

	switch (axp20x->variant) {
	case AXP2202_ID:
		regmap_update_bits(axp20x->regmap, AXP2202_MODULE_EN, BIT(4), BIT(4));
		break;
	default:
		break;
	}
	return 0;
}

static int axp2101_regulator_remove(struct platform_device *pdev)
{
	return 0;
}

static struct of_device_id axp_regulator_id_tab[] = {
	{ .compatible = "x-powers,axp2202-regulator" },
	{ /* sentinel */ },
};

static struct platform_driver axp2101_regulator_driver = {
	.probe	= axp2101_regulator_probe,
	.remove	= axp2101_regulator_remove,
	.driver	= {
		.of_match_table = axp_regulator_id_tab,
		.name		= "axp2101-regulator",
	},
	.suspend = axp2101_regulator_suspend,
	.resume = axp2101_regulator_resume,
};

static int __init axp2101_regulator_init(void)
{
	return platform_driver_register(&axp2101_regulator_driver);
}

static void __exit axp2101_regulator_exit(void)
{
	platform_driver_unregister(&axp2101_regulator_driver);
}

subsys_initcall(axp2101_regulator_init);
module_exit(axp2101_regulator_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Carlo Caione <carlo@caione.org>");
MODULE_DESCRIPTION("Regulator Driver for AXP20X PMIC");
MODULE_ALIAS("platform:axp20x-regulator");
