/*
 * drivers/input/touchscreen/sweep2wake.c
 *
 *
 * Copyright (c) 2012, Dennis Rassmann <showp1984@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * History:
 *	Added sysfs adjustments for different sweep styles
 * 		by paul reioux (aka Faux123) <reioux@gmail.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/input/sweep2wake.h>

/* Tuneables */
#define DEBUG                   1
#define DEFAULT_S2W_Y_LIMIT             900
#define DEFAULT_S2W_X_MAX               800
#define DEFAULT_S2W_X_B1                200
#define DEFAULT_S2W_X_B2                500
#define DEFAULT_S2W_X_FINAL             200
#define DEFAULT_S2W_PWRKEY_DUR          60

/* external function from the ts driver */
extern bool is_single_touch(void);

/* Resources */
int s2w_switch = 0;
bool scr_suspended = false, exec_count = true;
bool scr_on_touch = false, barrier[2] = {false, false};
static struct input_dev * sweep2wake_pwrdev;
static DEFINE_MUTEX(pwrkeyworklock);

static int s2w_start_posn = DEFAULT_S2W_X_B1;
static int s2w_mid_posn = DEFAULT_S2W_X_B2;
static int s2w_end_posn = (DEFAULT_S2W_X_MAX - DEFAULT_S2W_X_FINAL);
static int s2w_threshold = DEFAULT_S2W_X_FINAL;
//static int s2w_max_posn = DEFAULT_S2W_X_MAX;

static int s2w_swap_coord = 0;

int tripon = 0;
int tripoff = 0;
unsigned long triptime = 0;
//unsigned long initial_time = 0;
//unsigned long dt2w_time[2] = {0, 0};
//unsigned int dt2w_x[2] = {0, 0};
//unsigned int dt2w_y[2] = {0, 0};
//int status[2] = {0,0};
#define S2W_TIMEOUT 75

//#ifdef CONFIG_CMDLINE_OPTIONS
/* Read cmdline for s2w */
static int __init read_s2w_cmdline(char *s2w)
{
	if (strcmp(s2w, "1") == 0) {
		printk(KERN_INFO "[cmdline_s2w]: Sweep2Wake enabled. | s2w='%s'", s2w);
		s2w_switch = 1;
	} else if (strcmp(s2w, "0") == 0) {
		printk(KERN_INFO "[cmdline_s2w]: Sweep2Wake disabled. | s2w='%s'", s2w);
		s2w_switch = 0;
	} else {
		printk(KERN_INFO "[cmdline_s2w]: No valid input found. Sweep2Wake disabled. | s2w='%s'", s2w);
		s2w_switch = 0;
	}
	return 1;
}
__setup("s2w=", read_s2w_cmdline);
//#endif

/* PowerKey setter */
void sweep2wake_setdev(struct input_dev * input_device) {
	sweep2wake_pwrdev = input_device;
	return;
}
EXPORT_SYMBOL(sweep2wake_setdev);

static void reset_sweep2wake(void)
{
    //reset sweep2wake
    tripoff = 0;
    tripon = 0;
    triptime = 0;
    return;
}

/* PowerKey work func */
static void sweep2wake_presspwr(struct work_struct * sweep2wake_presspwr_work) {
	if (!mutex_trylock(&pwrkeyworklock))
                return;
    reset_sweep2wake();
	input_event(sweep2wake_pwrdev, EV_KEY, KEY_POWER, 1);
	input_event(sweep2wake_pwrdev, EV_SYN, 0, 0);
	msleep(DEFAULT_S2W_PWRKEY_DUR);
	input_event(sweep2wake_pwrdev, EV_KEY, KEY_POWER, 0);
	input_event(sweep2wake_pwrdev, EV_SYN, 0, 0);
	msleep(DEFAULT_S2W_PWRKEY_DUR);
        mutex_unlock(&pwrkeyworklock);
	return;
}
static DECLARE_WORK(sweep2wake_presspwr_work, sweep2wake_presspwr);

/* PowerKey trigger */
void sweep2wake_pwrtrigger(void) {
	schedule_work(&sweep2wake_presspwr_work);
        return;
}

/* Sweep2wake main function */
void detect_sweep2wake(int sweep_coord, int sweep_height, unsigned long time, int i)
{
	int swap_temp1, swap_temp2;

        //int prev_coord = 0, next_coord = 0;
        bool single_touch = is_single_touch();
#if DEBUG
        pr_info("[sweep2wake]: sweep_coord,sweep_height(%4d,%4d) single:%s\n",
                sweep_coord, sweep_height, (single_touch) ? "true" : "false");
#endif

	if (s2w_swap_coord == 1) {
		//swap the coordinate system
        pr_info("line : %d | func : %s\n", __LINE__, __func__);
		swap_temp1 = sweep_coord;
		swap_temp2 = sweep_height;

		sweep_height = swap_temp1;
		sweep_coord = swap_temp2;
	}

    //left->right
    if ((scr_suspended == true) && (s2w_switch > 0) && sweep_height > 700) {
        printk("[sweep2wake]:left to right x,y(%d,%d) jiffies:%lu\n", sweep_coord, sweep_height, time);
        if (sweep_coord < 100) {
        tripon = 1;
        triptime = time;
        } else if (tripon == 1 && sweep_coord > 300 && time - triptime < 25) {
            tripon = 2;
        } else if (tripon == 2 && sweep_coord > 450 && time - triptime < 50) {
            tripon = 3;
        } else if (tripon == 3 && sweep_coord > 600 && time - triptime < S2W_TIMEOUT) {
            printk(KERN_INFO "[sweep2wake]: ON");
            sweep2wake_pwrtrigger();
        }
    //right->left
    } else if ((scr_suspended == false) && (s2w_switch > 0) && sweep_height > 700) {
        printk("[sweep2wake]:right to left x,y(%d,%d) jiffies:%lu\n", sweep_coord, sweep_height, time);
        if (sweep_coord > 600) {
            tripoff = 1;
            triptime = time;
        } else if (tripoff == 1 && sweep_coord < 450 && time - triptime < 25) {
            tripoff = 2;
        } else if (tripoff == 2 && sweep_coord < 300 && time - triptime < 50) {
            tripoff = 3;
        } else if (tripoff == 3 && sweep_coord < 100 && (time - triptime < S2W_TIMEOUT)) {
            printk(KERN_INFO "[sweep2wake]: OFF");
            sweep2wake_pwrtrigger();
        }
    }

/*    //power on
	if ((single_touch) && (scr_suspended == true) && (s2w_switch > 0)) {
        pr_info("line : %d | func : %s\n", __LINE__, __func__);
		prev_coord = 0;
		next_coord = s2w_start_posn;
		if ((barrier[0] == true) ||
		   ((sweep_coord > prev_coord) &&
		    (sweep_coord < next_coord))) {
            pr_info("line : %d | func : %s\n", __LINE__, __func__);
			prev_coord = next_coord;
			next_coord = s2w_mid_posn;
			barrier[0] = true;
			if ((barrier[1] == true) ||
			   ((sweep_coord > prev_coord) &&
			    (sweep_coord < next_coord))) {
                pr_info("line : %d | func : %s\n", __LINE__, __func__);
				prev_coord = next_coord;
				barrier[1] = true;
				if ((sweep_coord > prev_coord)) {
                    pr_info("line : %d | func : %s\n", __LINE__, __func__);
					if (sweep_coord > s2w_end_posn) {
                        pr_info("line : %d | func : %s\n", __LINE__, __func__);
						if (exec_count) {
							printk(KERN_INFO "[sweep2wake]: ON");
							sweep2wake_pwrtrigger();
							exec_count = false;
						}
					}
				}
			}
		}
	//power off
	} else if ((single_touch) && (scr_suspended == false) && (s2w_switch > 0)) {
		if (s2w_swap_coord == 1) {
			//swap back for off scenario ONLY
			swap_temp1 = sweep_coord;
			swap_temp2 = sweep_height;

			sweep_height = swap_temp1;
			sweep_coord = swap_temp2;
		}

		scr_on_touch=true;
		prev_coord = (DEFAULT_S2W_X_MAX - DEFAULT_S2W_X_FINAL);
		next_coord = DEFAULT_S2W_X_B2;
		if ((barrier[0] == true) ||
		   ((sweep_coord < prev_coord) &&
		    (sweep_coord > next_coord) &&
		    (sweep_height > DEFAULT_S2W_Y_LIMIT))) {
			prev_coord = next_coord;
			next_coord = DEFAULT_S2W_X_B1;
			barrier[0] = true;
			if ((barrier[1] == true) ||
			   ((sweep_coord < prev_coord) &&
			    (sweep_coord > next_coord) &&
			    (sweep_height > DEFAULT_S2W_Y_LIMIT))) {
				prev_coord = next_coord;
				barrier[1] = true;
				if ((sweep_coord < prev_coord) &&
				    (sweep_height > DEFAULT_S2W_Y_LIMIT)) {
					if (sweep_coord < DEFAULT_S2W_X_FINAL) {
						if (exec_count) {
							printk(KERN_INFO "[sweep2wake]: OFF");
							sweep2wake_pwrtrigger();
							exec_count = false;
						}
					}
				}
			}
		}
	}*/
}

/********************* SYSFS INTERFACE ***********************/
static ssize_t s2w_start_posn_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%i\n", s2w_start_posn);
}

static ssize_t s2w_start_posn_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		s2w_start_posn = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static ssize_t s2w_mid_posn_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%i\n", s2w_mid_posn);
}

static ssize_t s2w_mid_posn_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		s2w_mid_posn = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static ssize_t s2w_end_posn_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%i\n", s2w_end_posn);
}

static ssize_t s2w_end_posn_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		s2w_end_posn = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static ssize_t s2w_threshold_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%i\n", s2w_threshold);
}

static ssize_t s2w_threshold_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		s2w_threshold = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static ssize_t s2w_swap_coord_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%i\n", s2w_swap_coord);
}

static ssize_t s2w_swap_coord_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		s2w_swap_coord = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static ssize_t sweep2wake_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%i\n", s2w_switch);
}

static ssize_t sweep2wake_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	unsigned int data;
	if(sscanf(buf, "%i\n", &data) == 1)
		s2w_switch = data;
	else
		pr_info("%s: unknown input!\n", __FUNCTION__);
	return count;
}

static struct kobj_attribute s2w_start_posn_attribute =
	__ATTR(s2w_start_posn,
		0666,
		s2w_start_posn_show,
		s2w_start_posn_store);

static struct kobj_attribute s2w_mid_posn_attribute =
	__ATTR(s2w_mid_posn,
		0666,
		s2w_mid_posn_show,
		s2w_mid_posn_store);

static struct kobj_attribute s2w_end_posn_attribute =
	__ATTR(s2w_end_posn,
		0666,
		s2w_end_posn_show,
		s2w_end_posn_store);

static struct kobj_attribute s2w_threshold_attribute =
	__ATTR(s2w_threshold,
		0666,
		s2w_threshold_show,
		s2w_threshold_store);

static struct kobj_attribute s2w_swap_coord_attribute =
	__ATTR(s2w_swap_coord,
		0666,
		s2w_swap_coord_show,
		s2w_swap_coord_store);

static struct kobj_attribute sweep2wake_attribute =
	__ATTR(s2w_switch,
		0666,
		sweep2wake_show,
		sweep2wake_store);

static struct attribute *s2w_parameters_attrs[] =
	{
		&s2w_start_posn_attribute.attr,
		&s2w_mid_posn_attribute.attr,
		&s2w_end_posn_attribute.attr,
		&s2w_threshold_attribute.attr,
		&s2w_swap_coord_attribute.attr,
		&sweep2wake_attribute.attr,
		NULL,
	};

static struct attribute_group s2w_parameters_attr_group =
	{
		.attrs = s2w_parameters_attrs,
	};

static struct kobject *s2w_parameters_kobj;

/*
 * INIT / EXIT stuff below here
 */

static int __init sweep2wake_init(void)
{
	int sysfs_result;

	s2w_parameters_kobj = kobject_create_and_add("android_touch_kobj", NULL);
	if (!s2w_parameters_kobj) {
		pr_err("%s kobject create failed!\n", __FUNCTION__);
		return -ENOMEM;
        }

	sysfs_result = sysfs_create_group(s2w_parameters_kobj, &s2w_parameters_attr_group);

        if (sysfs_result) {
		pr_info("%s sysfs create failed!\n", __FUNCTION__);
		kobject_put(s2w_parameters_kobj);
	}

	pr_info("[sweep2wake]: %s done\n", __func__);
	return sysfs_result;
}

static void __exit sweep2wake_exit(void)
{
	return;
}

module_init(sweep2wake_init);
module_exit(sweep2wake_exit);

MODULE_DESCRIPTION("Sweep2wake");
MODULE_LICENSE("GPLv2");

