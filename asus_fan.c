/*
 *  asus_fan.c - Asus a8j fan control
 *
 *
 *  Copyright (C) 2007-2010 Dmitry Ursegov
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 *  The development page for this driver is located at
 *  http://code.google.com/p/asusfan/
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#include <linux/workqueue.h>

MODULE_AUTHOR("Dmitry Ursegov");
MODULE_LICENSE("GPL");

#define NUM_ZONES 4
/*
When the temperature goes down to a low zone it is better to stay at
a high speed some degrees more to reduce fan speed switching
*/
#define TMP_DIFF 3
#define TIMER_FREQ 10	//seconds

struct tmp_zone {
	int tmp;	//celsius
	int speed;	//0-255 (~85 - fan is disabled)
};

static struct tmp_zone zone[NUM_ZONES] = {{ 60, 100 },
					  { 65, 120 },
					  { 70, 140 },
					  { 75, 160 }};

static void timer_handler(struct work_struct *work);

static DECLARE_DELAYED_WORK(ws, timer_handler);

static struct workqueue_struct *wqs;

static void set_fan_speed(int speed)
{
	struct acpi_object_list params;
	union acpi_object in_objs[2], in_obj;
	acpi_status status;

	/*
	It is required to evaluate this object with a maximum
	temperature supported by the manual control. If temperature is
	higher the manual control will be disabled
	*/
	params.count = 2;
	params.pointer = in_objs;
	in_objs[0].type = in_objs[1].type = ACPI_TYPE_INTEGER;
	in_objs[0].integer.value = zone[NUM_ZONES-1].tmp; //temp 
	in_objs[1].integer.value = 0;
	status = acpi_evaluate_object(NULL, "\\_TZ.WTML",
					&params, NULL);
	if (status != AE_OK)
		printk("_TZ.WTML error\n");

	//Set fan speed	
	params.count = 1;
	params.pointer = &in_obj;
	in_obj.type = ACPI_TYPE_INTEGER;
	in_obj.integer.value = ((0x84 << 16)
				+ (speed << 8) + (0xc4));
	status = acpi_evaluate_object(NULL, "\\_SB.ATKD.ECRW",
					&params, NULL);
	if (status != AE_OK) printk("_SB.ATKD.ECRW error\n");
}

static void timer_handler(struct work_struct *work)
{
	struct acpi_buffer output;
	union acpi_object out_obj;
	acpi_status status;

	static int prev_zone;
	int curr_zone, tmp;

	output.length = sizeof(out_obj);
	output.pointer = &out_obj;

	//Get current temperature	
	status = acpi_evaluate_object(NULL, "\\_TZ.THRM._TMP", 
					NULL, &output);
	if (status != AE_OK) printk("_TZ.THRM._TMP error\n");
	tmp = (int)((out_obj.integer.value-2732))/10;

	if (tmp >= zone[NUM_ZONES-1].tmp)
		goto out;

	//Set fan speed and save previous zone
	for(curr_zone=0; curr_zone<(NUM_ZONES-1); curr_zone++)
		if(tmp < zone[curr_zone].tmp)
			break;

	if(unlikely(curr_zone < prev_zone &&
			tmp > zone[curr_zone].tmp-TMP_DIFF)) {
		set_fan_speed(zone[prev_zone].speed);
		}
	else {
		set_fan_speed(zone[curr_zone].speed);
		prev_zone = curr_zone;
	}
out:		
	queue_delayed_work(wqs, &ws, TIMER_FREQ*HZ);
}

static int asus_fan_init(void)
{

	//Workqueue settings
	wqs = create_singlethread_workqueue("tmp");
	queue_delayed_work(wqs, &ws, HZ);

	printk("Asus Fan Control for A8J version 0.6\n");

	return 0;
}

static void asus_fan_exit(void)
{
	cancel_delayed_work(&ws);
	flush_workqueue(wqs);
	destroy_workqueue(wqs);
	printk("Asus Fan Control driver unloaded\n");
}


module_init(asus_fan_init);
module_exit(asus_fan_exit);

