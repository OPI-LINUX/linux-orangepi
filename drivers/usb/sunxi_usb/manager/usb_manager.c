/*
 * drivers/usb/sunxi_usb/manager/usb_manager.c
 * (C) Copyright 2010-2015
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * javen, 2011-4-14, create this file
 *
 * usb manager.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/kthread.h>

#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/power_supply.h>

#include  "../include/sunxi_usb_config.h"
#include  "usb_manager.h"
#include  "usbc_platform.h"
#include  "usb_hw_scan.h"
#include  "usb_msg_center.h"

struct usb_cfg g_usb_cfg;
int thread_id_irq_run_flag;
int thread_device_run_flag;
int thread_host_run_flag;
int thread_pmu_run_flag;

__u32 thread_run_flag = 1;
int thread_stopped_flag = 1;
atomic_t thread_suspend_flag;

#if defined(CONFIG_TYPEC)
static int sunxi_dr_set(struct typec_port *p, enum typec_data_role data)
{
	return 0;
}
static int sunxi_pr_set(struct typec_port *p, enum typec_role data)
{
	return 0;
}

static const struct typec_operations sunxi_usb_ops = {
	.dr_set = sunxi_dr_set,
	.pr_set = sunxi_pr_set,
};

#endif

#if IS_ENABLED(CONFIG_DUAL_ROLE_USB_INTF)
static enum dual_role_property sunxi_usb_dr_properties[] = {
	DUAL_ROLE_PROP_SUPPORTED_MODES,
	DUAL_ROLE_PROP_MODE,
	DUAL_ROLE_PROP_PR,
	DUAL_ROLE_PROP_DR,
};

static int sunxi_dr_get_property(struct dual_role_phy_instance *dual_role,
			enum dual_role_property prop, unsigned int *val)
{
	enum usb_role role = USB_ROLE_NULL;
	int mode, pr, dr;

	/*
	 * FIXME: e.g
	 * 1.mutex_lock is needed ?
	 * 2.synchronize current status before updated role ?
	 * ...
	 */

	role = get_usb_role();

	if (role == USB_ROLE_HOST) {
		DMSG_DEBUG("mode is HOST(DFP)\n");
		mode = DUAL_ROLE_PROP_MODE_DFP;
		pr = DUAL_ROLE_PROP_PR_SRC;
		dr = DUAL_ROLE_PROP_DR_HOST;
	} else if (role == USB_ROLE_DEVICE) {
		DMSG_DEBUG("mode is DEVICE(UFP)\n");
		mode = DUAL_ROLE_PROP_MODE_UFP;
		pr = DUAL_ROLE_PROP_PR_SNK;
		dr = DUAL_ROLE_PROP_DR_DEVICE;
	} else {
		DMSG_DEBUG("mode is NULL(NONE)\n");
		mode = DUAL_ROLE_PROP_MODE_NONE;
		pr = DUAL_ROLE_PROP_PR_NONE;
		dr = DUAL_ROLE_PROP_DR_NONE;
	}

	switch (prop) {
	case DUAL_ROLE_PROP_MODE:
		*val = mode;
		break;
	case DUAL_ROLE_PROP_PR:
		*val = pr;
		break;
	case DUAL_ROLE_PROP_DR:
		*val = dr;
		break;
	default:
		DMSG_PANIC("unsupported property %d\n", prop);
		return -EINVAL;
	}

	return 0;
}
#endif

static int usb_device_scan_thread(void *pArg)
{

	while (thread_device_run_flag) {

		msleep(1000);  /* 1s */
		hw_rmmod_usb_host();
		hw_rmmod_usb_device();
		usb_msg_center(&g_usb_cfg);

		hw_insmod_usb_device();
		usb_msg_center(&g_usb_cfg);
		thread_device_run_flag = 0;
		DMSG_INFO("device_chose finished %d!\n", __LINE__);
	}

	return 0;
}

static int usb_host_scan_thread(void *pArg)
{

	while (thread_host_run_flag) {

		msleep(1000);  /* 1s */
		hw_rmmod_usb_host();
		hw_rmmod_usb_device();
		usb_msg_center(&g_usb_cfg);

		hw_insmod_usb_host();
		usb_msg_center(&g_usb_cfg);
		thread_host_run_flag = 0;
		DMSG_INFO("host_chose finished %d!\n", __LINE__);
	}

	return 0;
}

static int usb_pmu_scan_thread(void *pArg)
{
	struct usb_cfg *cfg = pArg;

	while (thread_pmu_run_flag) {
		msleep(1000);  /* 1s */

		if (atomic_read(&thread_suspend_flag))
			continue;
		usb_hw_scan(cfg);
		usb_msg_center(cfg);
	}

	thread_stopped_flag = 1;

	return 0;
}

static int usb_hardware_scan_thread(void *pArg)
{
	struct usb_cfg *cfg = pArg;

	while (thread_run_flag) {
		msleep(1000);  /* 1s */

		if (atomic_read(&thread_suspend_flag))
			continue;
		usb_hw_scan(cfg);
		usb_msg_center(cfg);
	}

	thread_stopped_flag = 1;
	return 0;
}

static irqreturn_t usb_id_irq(int irq, void *parg)
{
	struct usb_cfg *cfg = parg;

	mdelay(1000);

	/**
	 * rmmod usb device/host driver first,
	 * then insmod usb host/device driver.
	 */
	usb_hw_scan(cfg);
	usb_msg_center(cfg);

	usb_hw_scan(cfg);
	usb_msg_center(cfg);

	return IRQ_HANDLED;
}

static int usb_id_irq_thread(void *parg)
{
	struct usb_cfg *cfg = parg;
	int id_irq_num = 0;
	unsigned long irq_flags = 0;
	int ret = 0;

	/* delay for udc & hcd ready */
	msleep(3000);

	while (thread_id_irq_run_flag) {
		msleep(1000);
		hw_rmmod_usb_host();
		hw_rmmod_usb_device();
		usb_msg_center(cfg);

		hw_insmod_usb_device();
		usb_msg_center(cfg);

		if (cfg->port.id.valid) {
			id_irq_num = gpio_to_irq(cfg->port.id.gpio);
			if (IS_ERR_VALUE((unsigned long)id_irq_num)) {
				DMSG_PANIC("ERR: map usb id gpio to virq failed, err %d\n",
					   id_irq_num);
				return -EINVAL;
			}

			irq_flags = IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING |
				    IRQF_ONESHOT;
			ret = request_threaded_irq(id_irq_num, NULL, usb_id_irq,
						   irq_flags, "usb_id", cfg);
			if (IS_ERR_VALUE((unsigned long)ret)) {
				DMSG_PANIC("ERR: request usb id virq %d failed, err %d\n",
					 id_irq_num, ret);
				return -EINVAL;
			}
			cfg->port.id_irq_num = id_irq_num;
		}

		thread_id_irq_run_flag = 0;
	}

	return 0;
}

static __s32 usb_script_parse(struct device_node *np, struct usb_cfg *cfg)
{
	struct device_node *usbc_np = NULL;
	int ret = -1;
	const char  *used_status;

	usbc_np = of_find_node_by_type(NULL, SET_USB0);

	/* usbc enable */
	ret = of_property_read_string(usbc_np, "status", &used_status);
	if (ret) {
		DMSG_INFO("get usb_used is fail, %d\n", ret);
		cfg->port.enable = 0;
	} else if (!strcmp(used_status, "okay")) {
		cfg->port.enable = 1;
	} else {
		cfg->port.enable = 0;
	}

	/* usbc port type */
	ret = of_property_read_u32(usbc_np,
					KEY_USB_PORT_TYPE,
					&cfg->port.port_type);
	if (ret)
		DMSG_INFO("get usb_port_type is fail, %d\n", ret);

	/* usbc det mode */
	ret = of_property_read_u32(usbc_np,
					KEY_USB_DET_MODE,
					&cfg->port.detect_mode);
	if (ret)
		DMSG_INFO("get usb_detect_mode is fail, %d\n", ret);

	/* usbc det_vbus */
	ret = of_property_read_string(usbc_np,
					KEY_USB_DETVBUS_GPIO,
					&cfg->port.det_vbus_name);
	if (ret) {
		DMSG_INFO("get det_vbus is fail, %d\n", ret);
		cfg->port.det_vbus.valid = 0;
	} else {
		if (strncmp(cfg->port.det_vbus_name, "axp_ctrl", 8) == 0) {
			cfg->port.det_vbus_type = USB_DET_VBUS_TYPE_AXP;
			cfg->port.det_vbus.valid = 0;
		} else {
			/*get det vbus gpio */
			cfg->port.det_vbus.gpio = of_get_named_gpio(usbc_np, KEY_USB_DETVBUS_GPIO, 0);
			if (gpio_is_valid(cfg->port.det_vbus.gpio)) {
				cfg->port.det_vbus.valid = 1;
				cfg->port.det_vbus_type = USB_DET_VBUS_TYPE_GPIO;
			} else {
				cfg->port.det_vbus.valid = 0;
			}
		}
	}


	/* usbc det type */
	ret = of_property_read_u32(usbc_np,
					KEY_USB_DET_TYPE,
					&cfg->port.detect_type);
	if (ret) {
		DMSG_INFO("get usb_detect_type is fail, %d\n", ret);
	}

	/* usbc id  */
	ret = of_property_read_string(usbc_np,
					KEY_USB_ID_GPIO,
					&cfg->port.id_name);
	if (ret) {
		DMSG_INFO("get id is fail, %d\n", ret);
		cfg->port.id.valid = 0;
	} else {
		if (strncmp(cfg->port.id_name, "axp_ctrl", 8) == 0) {
			cfg->port.id_type = USB_ID_TYPE_AXP;
			cfg->port.id.valid = 0;
		} else {
			/*get id gpio */
			cfg->port.id.gpio = of_get_named_gpio(usbc_np, KEY_USB_ID_GPIO, 0);
			if (gpio_is_valid(cfg->port.id.gpio)) {
				cfg->port.id.valid = 1;
				cfg->port.id_type = USB_ID_TYPE_GPIO;
			} else {
				cfg->port.id.valid = 0;
			}
		}
	}

	return 0;
}

int usb_otg_id_status(void)
{
	struct usb_cfg *cfg = NULL;
	int id_status = -1;

	cfg = &g_usb_cfg;
	if (cfg == NULL)
		return -1;

	if (cfg->port.port_type == USB_PORT_TYPE_DEVICE)
		return 1;

	if (cfg->port.port_type == USB_PORT_TYPE_OTG) {
		if (cfg->port.detect_type == USB_DETECT_TYPE_VBUS_ID) {
			if (cfg->port.id.valid)
				id_status = __gpio_get_value(
						cfg->port.id.gpio);
		} else if (cfg->port.detect_type == USB_DETECT_TYPE_VBUS_PMU) {
			id_status = get_usb_role() - 1;
		}
	}

	return id_status;
}
EXPORT_SYMBOL(usb_otg_id_status);

static int sunxi_otg_manager_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct task_struct *device_th = NULL;
	struct task_struct *host_th = NULL;
	struct task_struct *th = NULL;
	struct task_struct *id_irq_th = NULL;
	struct task_struct *pmu_th = NULL;
	int ret = -1;

	memset(&g_usb_cfg, 0, sizeof(struct usb_cfg));
	g_usb_cfg.usb_global_enable = 1;
	g_usb_cfg.pdev = pdev;
	usb_msg_center_init();

	ret = usb_script_parse(np, &g_usb_cfg);
	if (ret != 0) {
		DMSG_PANIC("ERR: get_usb_cfg failed\n");
		return -1;
	}

	if (g_usb_cfg.port.enable == 0) {
		DMSG_PANIC("wrn: usb0 is disable\n");
		return 0;
	}

	create_node_file(pdev);

	if (g_usb_cfg.port.port_type == USB_PORT_TYPE_DEVICE) {
		thread_device_run_flag = 1;
		device_th = kthread_create(usb_device_scan_thread,
						NULL,
						"usb_device_chose");
		if (IS_ERR(device_th)) {
			DMSG_PANIC("ERR: device kthread_create failed\n");
			return -1;
		}

		wake_up_process(device_th);
	}

	if (g_usb_cfg.port.port_type == USB_PORT_TYPE_HOST) {

		set_usb_role_ex(USB_ROLE_HOST);

		thread_host_run_flag = 0;
		host_th = kthread_create(usb_host_scan_thread,
						NULL,
						"usb_host_chose");
		if (IS_ERR(host_th)) {
			DMSG_PANIC("ERR: host kthread_create failed\n");
			return -1;
		}

		wake_up_process(host_th);
	}

	if (g_usb_cfg.port.port_type == USB_PORT_TYPE_OTG) {
		if (g_usb_cfg.port.detect_type == USB_DETECT_TYPE_VBUS_ID) {
			usb_hw_scan_init(&g_usb_cfg);

			if (g_usb_cfg.port.detect_mode == USB_DETECT_MODE_THREAD) {
				atomic_set(&thread_suspend_flag, 0);
				thread_run_flag = 1;
				thread_stopped_flag = 0;

				th = kthread_create(usb_hardware_scan_thread,
							&g_usb_cfg,
							"usb-hardware-scan");
				if (IS_ERR(th)) {
					DMSG_PANIC("ERR: kthread_create failed\n");
					return -1;
				}

				wake_up_process(th);
			} else if (g_usb_cfg.port.detect_mode == USB_DETECT_MODE_INTR) {
				thread_id_irq_run_flag = 1;
				id_irq_th = kthread_create(usb_id_irq_thread,
								&g_usb_cfg,
								"usb_id_irq");
				if (IS_ERR(id_irq_th)) {
					DMSG_PANIC("ERR: id_irq kthread_create failed\n");
					return -1;
				}

				wake_up_process(id_irq_th);
			} else {
				DMSG_PANIC("ERR: usb detect mode isn't supported\n");
				return -1;
			}
		} else if (g_usb_cfg.port.detect_type == USB_DETECT_TYPE_VBUS_PMU) {
#if defined(CONFIG_POWER_SUPPLY)

			if (of_find_property(np, "det_vbus_supply", NULL))
				g_usb_cfg.port.pmu_psy = devm_power_supply_get_by_phandle(&pdev->dev,
											"det_vbus_supply");
			if (!g_usb_cfg.port.pmu_psy  || IS_ERR(g_usb_cfg.port.pmu_psy)) {
				DMSG_PANIC("%s()%d WARN: get power supply failed\n", __func__, __LINE__);
				return -1;
			}
			usb_hw_scan_init(&g_usb_cfg);
			thread_pmu_run_flag = 1;
			thread_stopped_flag = 0;
			pmu_th = kthread_create(usb_pmu_scan_thread,
						&g_usb_cfg,
						"usb_pmu_scan");
			if (IS_ERR(pmu_th)) {
				DMSG_PANIC("ERR:pmu_scan kthread_create failed\n");
				return -1;
			}
			wake_up_process(pmu_th);
#endif
		}

#if IS_ENABLED(CONFIG_DUAL_ROLE_USB_INTF)
		g_usb_cfg.port.dr_desc.name = "dr_usbc0";
		g_usb_cfg.port.dr_desc.supported_modes = DUAL_ROLE_SUPPORTED_MODES_DFP_AND_UFP;
		g_usb_cfg.port.dr_desc.properties = sunxi_usb_dr_properties;
		g_usb_cfg.port.dr_desc.num_properties = ARRAY_SIZE(sunxi_usb_dr_properties);
		g_usb_cfg.port.dr_desc.get_property = sunxi_dr_get_property;
		g_usb_cfg.port.dr_desc.set_property = NULL;
		g_usb_cfg.port.dr_desc.property_is_writeable = NULL;

		g_usb_cfg.port.dual_role = devm_dual_role_instance_register(&pdev->dev, &g_usb_cfg.port.dr_desc);
		if (IS_ERR(g_usb_cfg.port.dual_role))
			DMSG_PANIC("ERR: failed to register dual_role_class device\n");
#endif

#if defined(CONFIG_TYPEC)
		g_usb_cfg.port.typec_caps.type = TYPEC_PORT_SNK;
		g_usb_cfg.port.typec_caps.ops = &sunxi_usb_ops;
		g_usb_cfg.port.typec_port = typec_register_port(&pdev->dev, &g_usb_cfg.port.typec_caps);
#endif
	}

	return 0;
}

static int sunxi_otg_manager_remove(struct platform_device *pdev)
{

#if IS_ENABLED(CONFIG_DUAL_ROLE_USB_INTF)
	struct dual_role_phy_instance *dual_role = g_usb_cfg.port.dual_role;
#endif

	if (g_usb_cfg.port.enable == 0) {
		DMSG_PANIC("wrn: usb0 is disable\n");
		return 0;
	}

	if (g_usb_cfg.port.port_type == USB_PORT_TYPE_OTG) {
#if IS_ENABLED(CONFIG_DUAL_ROLE_USB_INTF)
		devm_dual_role_instance_unregister(&pdev->dev, dual_role);
		dual_role = NULL;
#endif

		thread_run_flag = 0;
		while (!thread_stopped_flag) {
			DMSG_INFO("waitting for usb_hardware_scan_thread stop\n");
			msleep(20);
		}
		if (g_usb_cfg.port.detect_mode == USB_DETECT_MODE_INTR)
			if (g_usb_cfg.port.id.valid
					&& g_usb_cfg.port.id_irq_num)
				free_irq(g_usb_cfg.port.id_irq_num, &g_usb_cfg);
		usb_hw_scan_exit(&g_usb_cfg);
	}

	remove_node_file(pdev);

	/* Remove host and device driver before manager exit. */
	hw_rmmod_usb_host();
	hw_rmmod_usb_device();
	usb_msg_center(&g_usb_cfg);

	return 0;
}

#if IS_ENABLED(CONFIG_PM)
static int sunxi_otg_manager_suspend(struct device *dev)
{
	device_insmod_delay = 0;
	atomic_set(&thread_suspend_flag, 1);
	return 0;
}

static int sunxi_otg_manager_resume(struct device *dev)
{
	device_insmod_delay = 0;
	atomic_set(&thread_suspend_flag, 0);
	return 0;
}

static const struct dev_pm_ops sunxi_otg_manager_pm_ops = {
	.suspend = sunxi_otg_manager_suspend,
	.resume = sunxi_otg_manager_resume,
};
#define OTG_MANAGER_PM_OPS        (&sunxi_otg_manager_pm_ops)

#else /* !CONFIG_PM_SLEEP */

#define OTG_MANAGER_PM_OPS        NULL
#endif /* CONFIG_PM_SLEEP */

static const struct of_device_id sunxi_otg_manager_match[] = {
	{.compatible = "allwinner,sunxi-otg-manager", },
	{},
};
MODULE_DEVICE_TABLE(of, sunxi_otg_manager_match);

static struct platform_driver sunxi_otg_manager_platform_driver = {
	.probe  = sunxi_otg_manager_probe,
	.remove = sunxi_otg_manager_remove,
	.driver = {
		.name  = "otg manager",
		.pm    = OTG_MANAGER_PM_OPS,
		.owner = THIS_MODULE,
		.of_match_table = sunxi_otg_manager_match,
	},
};

static int __init usb_manager_init(void)
{
	return platform_driver_register(&sunxi_otg_manager_platform_driver);
}

static void __exit usb_manager_exit(void)
{
	return platform_driver_unregister(&sunxi_otg_manager_platform_driver);
}

late_initcall(usb_manager_init);
module_exit(usb_manager_exit);

MODULE_AUTHOR("wangjx<wangjx@allwinnertech.com>");
MODULE_DESCRIPTION("Driver for Allwinner usb otg manager");
MODULE_ALIAS("platform: usb manager for host and udc");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0.10");
