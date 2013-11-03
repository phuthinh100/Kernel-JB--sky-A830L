/* Copyright (c) 2012, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/msm_tsens.h>
#include <linux/workqueue.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/msm_tsens.h>
#include <linux/msm_thermal.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <mach/cpufreq.h>

#define POLLING_DELAY 100

static int temp_threshold;
//module_param(temp_threshold, int, 0755);

static int enabled;
static struct msm_thermal_data msm_thermal_info;
static uint32_t limited_max_freq = MSM_CPUFREQ_NO_LIMIT;
static struct delayed_work check_temp_work;

static unsigned int limit_idx;
static unsigned int limit_idx_low;
static unsigned int limit_idx_high;
static unsigned int max_frequency;
static bool throttling = false;
static struct cpufreq_frequency_table *table;
struct cpufreq_policy *policy = NULL;

static int msm_thermal_get_freq_table(void)
{
	int ret = 0;
	int i = 0;

	table = cpufreq_frequency_get_table(0);
	if (table == NULL) {
		pr_info("%s: error reading cpufreq table\n", __func__);
		ret = -EINVAL;
		goto fail;
	}

	while (table[i].frequency != max_frequency)
		i++;

	limit_idx_low = 6;
	limit_idx_high = limit_idx = i - 1;
	BUG_ON(limit_idx_high <= 0 || limit_idx_high <= limit_idx_low);
fail:
	return ret;
}

static int update_cpu_max_freq(int cpu, uint32_t max_freq)
{
	int ret = 0;

	ret = msm_cpufreq_set_freq_limits(cpu, MSM_CPUFREQ_NO_LIMIT, max_freq);
	if (ret)
		return ret;

	limited_max_freq = max_freq;
	if (max_freq != MSM_CPUFREQ_NO_LIMIT)
		pr_info("msm_thermal: Limiting cpu%d max frequency to %d\n",
				cpu, max_freq);
	else {
		pr_info("msm_thermal: Max frequency reset for cpu%d\n", cpu);
		throttling = false;
	}

	ret = cpufreq_update_policy(cpu);

	return ret;
}

static void check_temp(struct work_struct *work)
{
	static int limit_init;
	struct tsens_device tsens_dev;
	unsigned long temp = 0;
	uint32_t max_freq = limited_max_freq;
	int cpu = 0;
	int ret = 0;
	policy = cpufreq_cpu_get(0);

	tsens_dev.sensor_num = msm_thermal_info.sensor_id;
	ret = tsens_get_temp(&tsens_dev, &temp);
	if (ret) {
		pr_info("msm_thermal: Unable to read TSENS sensor %d\n",
				tsens_dev.sensor_num);
		goto reschedule;
	}

	if (!limit_init) {
        pr_info("line : %d | func : %s\n", __LINE__, __func__);
		ret = msm_thermal_get_freq_table();
		if (ret)
			goto reschedule;
		else
			limit_init = 1;
	}

    pr_info("line : %d | func : %s temp = %lu | temp_threshold = %u\n", __LINE__, __func__, temp, temp_threshold);
	if (temp >= temp_threshold - 30) {
        pr_info("line : %d | func : %s\n", __LINE__, __func__);
		if (!throttling) {
            pr_info("line : %d | func : %s\n", __LINE__, __func__);
			max_frequency = policy->max;
			throttling = true;
		}
		
		if (limit_idx == limit_idx_low)
			goto reschedule;

        pr_info("line : %d | func : %s\n", __LINE__, __func__);
		limit_idx = limit_idx_low;
		if (limit_idx < limit_idx_low)
			limit_idx = limit_idx_low;
		max_freq = table[limit_idx].frequency;
        pr_info("line : %d | func : %s max_freq = %u\n", __LINE__, __func__, max_freq);
	} else if (temp < (temp_threshold - 35)) {
        pr_info("line : %d | func : %s\n", __LINE__, __func__);
		if (limit_idx == limit_idx_high)
			goto reschedule;

		limit_idx = limit_idx_high;
		max_freq = max_frequency;
        pr_info("line : %d | func : %s max_freq = %u\n", __LINE__, __func__, max_freq);
	}
	if (max_freq == limited_max_freq)
		goto reschedule;

	/* Update new limits */
	for_each_possible_cpu(cpu) {
		ret = update_cpu_max_freq(cpu, max_freq);
		if (ret)
			pr_info("Unable to limit cpu%d max freq to %d\n",
					cpu, max_freq);
	}

reschedule:
    pr_info("line : %d | func : %s\n", __LINE__, __func__);
	if (enabled) {
        pr_info("line : %d | func : %s\n", __LINE__, __func__);
		schedule_delayed_work(&check_temp_work, msecs_to_jiffies(POLLING_DELAY));
    }
}

static void disable_msm_thermal(void)
{
	int cpu = 0;

	cancel_delayed_work(&check_temp_work);
	flush_scheduled_work();

    limit_idx = limit_idx_high;
	if (limited_max_freq == MSM_CPUFREQ_NO_LIMIT)
		return;

	for_each_possible_cpu(cpu) {
		update_cpu_max_freq(cpu, MSM_CPUFREQ_NO_LIMIT);
	}
}

static int set_enabled(const char *val, const struct kernel_param *kp)
{
	int ret = 0;

	ret = param_set_bool(val, kp);
	if (!enabled)
		disable_msm_thermal();
	else {
		pr_info("msm_thermal: no action for enabled = %d\n", enabled);
        INIT_DELAYED_WORK(&check_temp_work, check_temp);
        schedule_delayed_work(&check_temp_work, 0);
    }

	pr_info("msm_thermal: enabled = %d\n", enabled);

	return ret;
}

static struct kernel_param_ops module_ops = {
	.set = set_enabled,
	.get = param_get_bool,
};

module_param_cb(enabled, &module_ops, &enabled, 0644);
MODULE_PARM_DESC(enabled, "enforce thermal limit on cpu");

static int set_temp_threshold(const char *val, const struct kernel_param *kp)
{
	return param_set_int(val, kp);
}

static struct kernel_param_ops threshold_ops = {
	.set = set_temp_threshold,
	.get = param_get_int,
};

module_param_cb(temp_threshold, &threshold_ops, &temp_threshold, 0755);

int __devinit msm_thermal_init(struct msm_thermal_data *pdata)
{
	int ret = 0;

	BUG_ON(!pdata);
	BUG_ON(pdata->sensor_id >= TSENS_MAX_SENSORS);
	memcpy(&msm_thermal_info, pdata, sizeof(struct msm_thermal_data));

	enabled = 1;
    temp_threshold = 70;
	INIT_DELAYED_WORK(&check_temp_work, check_temp);
	schedule_delayed_work(&check_temp_work, 0);

	return ret;
}

static int __devinit msm_thermal_dev_probe(struct platform_device *pdev)
{
	int ret = 0;
	char *key = NULL;
	struct device_node *node = pdev->dev.of_node;
	struct msm_thermal_data data;

	memset(&data, 0, sizeof(struct msm_thermal_data));
	key = "qcom,sensor-id";
	ret = of_property_read_u32(node, key, &data.sensor_id);
	if (ret)
		goto fail;
	WARN_ON(data.sensor_id >= TSENS_MAX_SENSORS);

	key = "qcom,temp-hysteresis";
	ret = of_property_read_u32(node, key, &data.temp_hysteresis_degC);
	if (ret)
		goto fail;

	key = "qcom,freq-step";
	ret = of_property_read_u32(node, key, &data.freq_step);

fail:
	if (ret)
		pr_err("%s: Failed reading node=%s, key=%s\n",
		       __func__, node->full_name, key);
	else
		ret = msm_thermal_init(&data);

	return ret;
}

static struct of_device_id msm_thermal_match_table[] = {
	{.compatible = "qcom,msm-thermal"},
	{},
};

static struct platform_driver msm_thermal_device_driver = {
	.probe = msm_thermal_dev_probe,
	.driver = {
		.name = "msm-thermal",
		.owner = THIS_MODULE,
		.of_match_table = msm_thermal_match_table,
	},
};

int __init msm_thermal_device_init(void)
{
	return platform_driver_register(&msm_thermal_device_driver);
}
module_init(msm_thermal_device_init);
