/*
 * linux/drivers/video/backlight/pwm_bl.c
 *
 * simple PWM based backlight control, board code has to setup
 * 1) pin configuration so PWM waveforms can output
 * 2) platform_data being correctly configured
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/pwm.h>
#include <linux/pwm_backlight.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <mach/gpio.h>
#include <mach/sys_config.h>

struct pwm_bl_data {
	struct pwm_device	*pwm;
	struct device		*dev;
	unsigned int		period;
	unsigned int		lth_brightness;
	bool			enabled;
	struct regulator	*power_supply;
	u32 			enable_gpio_hd;
	u32				lcd_power_hd;
	unsigned int		scale;
	bool			legacy;
	int			(*notify)(struct device *,
					  int brightness);
	void			(*notify_after)(struct device *,
					int brightness);
	int			(*check_fb)(struct device *, struct fb_info *);
	void			(*exit)(struct device *);
};

static struct platform_device pwm_backlight_device = {
	.name		= "pwm-backlight",
};

static void pwm_backlight_power_on(struct pwm_bl_data *pb, int brightness)
{
	if (pb->enabled)
		return;
	
	gpio_set_value(pb->enable_gpio_hd, 1);

	pwm_enable(pb->pwm);
	pb->enabled = true;
}

static void pwm_backlight_power_off(struct pwm_bl_data *pb)
{
	if (!pb->enabled)
		return;

	pwm_config(pb->pwm, 0, pb->period);
	pwm_disable(pb->pwm);

	gpio_set_value(pb->enable_gpio_hd, 0);
	
	pb->enabled = false;
}

static int compute_duty_cycle(struct pwm_bl_data *pb, int brightness)
{
	unsigned int lth = pb->lth_brightness;
	int duty_cycle;

	duty_cycle = brightness;

	pr_info("%s, brightness = %d, duty_cycle = %d", __func__, brightness, duty_cycle);

	return (duty_cycle * (pb->period - lth) / pb->scale) + lth;
}

static int pwm_backlight_update_status(struct backlight_device *bl)
{
	struct pwm_bl_data *pb = bl_get_data(bl);
	int brightness = bl->props.brightness;
	int duty_cycle;

	pr_info("%s brightness = %d\n", __func__, brightness);

	if (bl->props.power != FB_BLANK_UNBLANK ||
	    bl->props.fb_blank != FB_BLANK_UNBLANK ||
	    bl->props.state & BL_CORE_FBBLANK)
		brightness = 0;

	if (pb->notify)
		brightness = pb->notify(pb->dev, brightness);

	if (brightness > 0) {
		duty_cycle = compute_duty_cycle(pb, brightness);
		pr_info("%s, duty_cycle = %d\n", __func__, duty_cycle);
		pwm_config(pb->pwm, duty_cycle, pb->period);
		pwm_backlight_power_on(pb, brightness);
	} else
		pwm_backlight_power_off(pb);

	if (pb->notify_after)
		pb->notify_after(pb->dev, brightness);

	return 0;
}

static int pwm_backlight_get_brightness(struct backlight_device *bl)
{
	return bl->props.brightness;
}

static int pwm_backlight_check_fb(struct backlight_device *bl,
				  struct fb_info *info)
{
	struct pwm_bl_data *pb = bl_get_data(bl);

	return !pb->check_fb || pb->check_fb(pb->dev, info);
}

static const struct backlight_ops pwm_backlight_ops = {
	.update_status	= pwm_backlight_update_status,
	.get_brightness	= pwm_backlight_get_brightness,
	.check_fb	= pwm_backlight_check_fb,
};

static int pwm_backlight_parse_sysconfig(struct device *dev,
				  struct platform_pwm_backlight_data *data)
{
	script_item_value_type_e type = 0;
	script_item_u item_temp;

	memset(data, 0, sizeof(*data));

	type = script_get_item("pwmbl_para", "pwm_ch", &item_temp);
	if(type == SCIRPT_ITEM_VALUE_TYPE_INT){
		data->pwm_id = item_temp.val;
	}else{
		dev_err(dev, "failed to get pwm_id\n");
		data->pwm_id = 0;
	}

	type = script_get_item("pwmbl_para", "pwm_pol", &item_temp);
	if(type == SCIRPT_ITEM_VALUE_TYPE_INT){
		data->polarity = item_temp.val;
	}else{
		dev_err(dev, "failed to get pwm_pol\n");
		data->polarity = 0;
	}

	type = script_get_item("pwmbl_para", "pwm_freq", &item_temp);
	if(type == SCIRPT_ITEM_VALUE_TYPE_INT){
		data->pwm_period_ns = 1000 * 1000 * 1000 / item_temp.val;
	}else{
		dev_err(dev, "failed to get pwm_freq\n");
		data->pwm_period_ns = 0;
	}

	type = script_get_item("pwmbl_para", "lth_brightness", &item_temp);
	if(type == SCIRPT_ITEM_VALUE_TYPE_INT){
		data->lth_brightness = item_temp.val;
	}else{
		dev_err(dev, "failed to get lth_brightness\n");
		data->lth_brightness = 0;
	}

	type = script_get_item("pwmbl_para", "dft_brightness", &item_temp);
	if(type == SCIRPT_ITEM_VALUE_TYPE_INT){
		data->dft_brightness = item_temp.val;
	}else{
		dev_err(dev, "failed to get dft_brightness\n");
		data->dft_brightness = 0;
	}

	/* lcd_power gpio */
	type = script_get_item("pwmbl_para", "lcd_power", &item_temp);
	if(type == SCIRPT_ITEM_VALUE_TYPE_PIO){
		data->lcd_power = item_temp.gpio.gpio;
	}else{
		dev_err(dev, "failed to get lcd_power gpio\n");
		data->lcd_power = 0;
	}

	/* bl_enable gpio */
	type = script_get_item("pwmbl_para", "bl_enable", &item_temp);
	if(type == SCIRPT_ITEM_VALUE_TYPE_PIO){
		data->enable_gpio = item_temp.gpio.gpio;
	}else{
		dev_err(dev, "failed to get bl_enable gpio\n");
		data->enable_gpio = 0;
	}

	data->max_brightness = 255;

	pr_info("pwm[%d] polarity[%d] max_brightness[%d] dft_brightness[%d] lth_brightness[%d] pwm_period_ns[%d]\n", 
			data->pwm_id, data->polarity, data->max_brightness, data->dft_brightness, data->lth_brightness, data->pwm_period_ns);
	
	return 0;
}

static int pwm_backlight_probe(struct platform_device *pdev)
{
	struct platform_pwm_backlight_data *data = dev_get_platdata(&pdev->dev);
	struct platform_pwm_backlight_data defdata;
	struct backlight_properties props;
	struct backlight_device *bl;
	struct pwm_bl_data *pb;
	int ret;

	dev_info(&pdev->dev, "pwm-backlight probe\n");

	if (!data) {
		ret = pwm_backlight_parse_sysconfig(&pdev->dev, &defdata);
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to find platform data\n");
			return ret;
		}

		data = &defdata;
	}

	if (data->init) {
		ret = data->init(&pdev->dev);
		if (ret < 0)
			return ret;
	}

	pb = devm_kzalloc(&pdev->dev, sizeof(*pb), GFP_KERNEL);
	if (!pb) {
		ret = -ENOMEM;
		goto err_alloc;
	}

	pb->scale = data->max_brightness;
	pb->notify = data->notify;
	pb->notify_after = data->notify_after;
	pb->check_fb = data->check_fb;
	pb->exit = data->exit;
	pb->dev = &pdev->dev;
	pb->enabled = false;

	/* power gpio */
	if (gpio_is_valid(data->lcd_power)) {
		ret = gpio_request(data->lcd_power, "lcd_power");
		if (ret != 0) {
			dev_err(&pdev->dev, "failed to request GPIO#%d: %d\n",
				data->lcd_power, ret);
			goto err_alloc;
		}

		pb->lcd_power_hd = data->lcd_power;

		/* set lcd power on */
		gpio_direction_output(pb->lcd_power_hd, 1);
	}
	
	/* enable gpio */
	if (gpio_is_valid(data->enable_gpio)) {
		ret = gpio_request(data->enable_gpio, "bl_enable");
		if (ret != 0) {
			dev_err(&pdev->dev, "failed to request GPIO#%d: %d\n",
				data->enable_gpio, ret);
			goto err_alloc;
		}

		pb->enable_gpio_hd = data->enable_gpio;
		gpio_direction_output(pb->enable_gpio_hd, 0);
	}

	/* pwm */
	pb->legacy = true;
	pb->pwm = pwm_request(data->pwm_id, "pwm-backlight");
	if (IS_ERR(pb->pwm)) {
		ret = PTR_ERR(pb->pwm);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "unable to request PWM\n");
		else
			dev_err(&pdev->dev, "unable to request PWM, probe defer\n");
		
		goto err_alloc;
	}

	dev_info(&pdev->dev, "got pwm for backlight\n");

	/*
	 * The DT case will set the pwm_period_ns field to 0 and store the
	 * period, parsed from the DT, in the PWM device. For the non-DT case,
	 * set the period from platform data if it has not already been set
	 * via the PWM lookup table.
	 */
	pb->period = pwm_get_period(pb->pwm);
	if (!pb->period && (data->pwm_period_ns > 0)) {
		pb->period = data->pwm_period_ns;
		pwm_set_period(pb->pwm, data->pwm_period_ns);
	}

	pb->lth_brightness = data->lth_brightness * (pb->period / pb->scale);

	pwm_set_polarity(pb->pwm, data->polarity);

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = data->max_brightness;
	pdev->dev.init_name = "pwm-backlight";
	bl = backlight_device_register(dev_name(&pdev->dev), &pdev->dev, pb,
				       &pwm_backlight_ops, &props);
	if (IS_ERR(bl)) {
		dev_err(&pdev->dev, "failed to register backlight\n");
		ret = PTR_ERR(bl);
		goto err_alloc;
	}

	if (data->dft_brightness > data->max_brightness) {
		dev_warn(&pdev->dev,
			 "invalid default brightness level: %u, using %u\n",
			 data->dft_brightness, data->max_brightness);
		data->dft_brightness = data->max_brightness;
	}

	bl->props.brightness = data->dft_brightness;
	backlight_update_status(bl);

	platform_set_drvdata(pdev, bl);

	dev_info(&pdev->dev, "pwm-backlight probe end\n");
	
	return 0;

err_alloc:
	if (data->exit)
		data->exit(&pdev->dev);
	gpio_free(data->enable_gpio);
	return ret;
}

static int pwm_backlight_remove(struct platform_device *pdev)
{
	struct backlight_device *bl = platform_get_drvdata(pdev);
	struct pwm_bl_data *pb = bl_get_data(bl);

	backlight_device_unregister(bl);
	pwm_backlight_power_off(pb);

	if (pb->exit)
		pb->exit(&pdev->dev);
	if (pb->legacy)
		pwm_free(pb->pwm);

	return 0;
}

static void pwm_backlight_shutdown(struct platform_device *pdev)
{
	struct backlight_device *bl = platform_get_drvdata(pdev);
	struct pwm_bl_data *pb = bl_get_data(bl);

	pwm_backlight_power_off(pb);
}

#ifdef CONFIG_PM_SLEEP
static int pwm_backlight_suspend(struct device *dev)
{
	struct backlight_device *bl = dev_get_drvdata(dev);
	struct pwm_bl_data *pb = bl_get_data(bl);

	if (pb->notify)
		pb->notify(pb->dev, 0);

	pwm_backlight_power_off(pb);

	if (pb->notify_after)
		pb->notify_after(pb->dev, 0);

	return 0;
}

static int pwm_backlight_resume(struct device *dev)
{
	struct backlight_device *bl = dev_get_drvdata(dev);

	backlight_update_status(bl);

	return 0;
}
#endif

static const struct dev_pm_ops pwm_backlight_pm_ops = {
#ifdef CONFIG_PM_SLEEP
	.suspend = pwm_backlight_suspend,
	.resume = pwm_backlight_resume,
	.poweroff = pwm_backlight_suspend,
	.restore = pwm_backlight_resume,
#endif
};

static struct platform_driver pwm_backlight_driver = {
	.driver		= {
		.name	= "pwm-backlight",
#ifdef CONFIG_PM_SLEEP
		.pm	= &pwm_backlight_pm_ops,
#endif
	},
	.probe		= pwm_backlight_probe,
	.remove		= pwm_backlight_remove,
	.shutdown	= pwm_backlight_shutdown,
};

static int __init pwm_backlight_init(void)
{
	int ret;

	pr_info("%s\n", __func__);

	ret = platform_device_register(&pwm_backlight_device);
	if (ret)
		return ret;

	return platform_driver_register(&pwm_backlight_driver);
}

static void __exit pwm_backlight_exit(void)
{
	platform_driver_unregister(&pwm_backlight_driver);
	platform_device_unregister(&pwm_backlight_device);
}

module_init(pwm_backlight_init);
module_exit(pwm_backlight_exit);

MODULE_DESCRIPTION("PWM based Backlight Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pwm-backlight");

