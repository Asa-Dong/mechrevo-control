// SPDX-License-Identifier: GPL-2.0-only
/* MECHREVO X6xR55xK: fixed-purpose EC interfaces, no arbitrary register API.
 * Protocol implementation reference copyright (c) 2020-2024 TUXEDO Computers GmbH.
 * Protocol references: TUXEDO drivers v4.25.0_rc, sklynic MECHREVO patch.
 * Register/state mappings additionally verified on this machine; see docs/validation.
 */
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/hwmon.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/wmi.h>
#include <linux/workqueue.h>

#define DIRTY_MODE BIT(0)
#define DIRTY_LIGHT BIT(1)
#define DIRTY_RGB BIT(2)
#define DIRTY_CHARGE BIT(3)
#define DIRTY_FAN BIT(4)
#define DIRTY_CURVE BIT(5)
#define DIRTY_GATE BIT(6)
struct mr {
	struct mutex lock;
	acpi_handle ec;
	struct device *dev, *hwmon;
	struct delayed_work fan_work;
	struct notifier_block reboot;
	bool active, suspended, removing, manual_fans, curve_fans, curve_valid;
	unsigned int fan_target[2];
	unsigned int curve_points[10];
	unsigned int dirty, mode, light, rgb[3], charge;
	unsigned long last_key;
	int last_error;
};
static int rd(struct mr *d, u16 reg, unsigned int *v)
{
	union acpi_object arg = {.integer = {.type = ACPI_TYPE_INTEGER, .value = reg}};
	struct acpi_object_list args = {.count = 1, .pointer = &arg};
	unsigned long long out;
	if (ACPI_FAILURE(acpi_evaluate_integer(d->ec, "ECRR", &args, &out)) || out > 255)
		return -EIO;
	*v = out;
	usleep_range(6000, 12000);
	return 0;
}
static int wr(struct mr *d, u16 reg, unsigned int value)
{
	union acpi_object arg[] = {
	    {.integer = {.type = ACPI_TYPE_INTEGER, .value = reg}},
	    {.integer = {.type = ACPI_TYPE_INTEGER, .value = value}},
	};
	struct acpi_object_list args = {.count = 2, .pointer = arg};
	acpi_status ret = acpi_evaluate_object(d->ec, "ECRW", &args, NULL);
	usleep_range(6000, 12000);
	return ACPI_FAILURE(ret) ? -EIO : 0;
}
/* Immediate PWM commands are firmware transactions, not MMIO stores.
 * Protocol from TUXEDO uniwill_wmi.c. Only the two validated fan addresses and their split-control
 * flag.
 */
static int fan_wmi(u16 reg, unsigned int *value, bool write)
{
	u8 bytes[40] = {0};
	struct acpi_buffer in = {sizeof(bytes), bytes}, out = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *obj;
	acpi_status status;
	int ret = -EIO;
	bytes[0] = reg & 255;
	bytes[1] = reg >> 8;
	bytes[2] = write ? *value : 0;
	bytes[5] = write ? 0 : 1;
	status = wmi_evaluate_method("ABBC0F6F-8EA1-11D1-00A0-C90629100000", 0, 4, &in, &out);
	obj = out.pointer;
	if (ACPI_SUCCESS(status) && obj && obj->type == ACPI_TYPE_BUFFER &&
	    obj->buffer.length >= 4 && memcmp(obj->buffer.pointer, "\xfe\xfe\xfe\xfe", 4)) {
		if (!write)
			*value = obj->buffer.pointer[0];
		ret = 0;
	}
	kfree(out.pointer);
	return ret;
}
static int fan_io(unsigned int channel, unsigned int *value, bool write)
{
	return fan_wmi(channel == 2 ? 0x07c5 : channel ? 0x1809 : 0x1804, value, write);
}
static int curve_mask(unsigned int reg, unsigned int bits, unsigned int value)
{
	unsigned int v, check;
	if (fan_wmi(reg, &v, false))
		return -EIO;
	v = (v & ~bits) | (value & bits);
	if (fan_wmi(reg, &v, true) || fan_wmi(reg, &check, false))
		return -EIO;
	return (check & bits) == (value & bits) ? 0 : -EIO;
}
/* Curve table bytes have a verified MMIO read path. Avoid two slow WMI
 * reads and skip unchanged bytes, retaining a readback after each write. */
static int curve_byte(struct mr *d, unsigned int reg, unsigned int value)
{
	unsigned int observed;
	if (rd(d, reg, &observed))
		return -EIO;
	if (observed == value)
		return 0;
	if (fan_wmi(reg, &value, true) || rd(d, reg, &observed))
		return -EIO;
	return observed == value ? 0 : -EIO;
}
static int fan_split_set(unsigned int bits)
{
	unsigned int value, check;
	if (fan_io(2, &value, false))
		return -EIO;
	value = (value & ~0x80) | (bits & 0x80);
	if (fan_io(2, &value, true) || fan_io(2, &check, false))
		return -EIO;
	return (check & 0x80) == (value & 0x80) ? 0 : -EIO;
}
static int fan_restore(struct mr *d)
{
	int ret;
	d->manual_fans = false;
	ret = fan_split_set(0);
	if (!ret)
		d->dirty &= ~DIRTY_FAN;
	return ret;
}
static int mask(struct mr *d, u16 reg, unsigned int bits, unsigned int value)
{
	unsigned int v;
	if (rd(d, reg, &v) || wr(d, reg, (v & ~bits) | (value & bits)) || rd(d, reg, &v))
		return -EIO;
	return (v & bits) == (value & bits) ? 0 : -EIO;
}
static int brightness(struct mr *d, unsigned int level)
{
	unsigned int v;
	if (rd(d, 0x078c, &v) || wr(d, 0x078c, (v & 0xf) | (level << 5) | 0x10) ||
	    rd(d, 0x078c, &v))
		return -EIO;
	return (v >> 5) == level ? 0 : -EIO;
}
static int color(struct mr *d, const unsigned int rgb[3])
{
	unsigned int v;
	int i;
	for (i = 0; i < 3; i++)
		if (wr(d, 0x0769 + i, rgb[i]))
			return -EIO;
	if (rd(d, 0x0767, &v) || wr(d, 0x0767, v | 0x20))
		return -EIO;
	for (i = 0; i < 3; i++)
		if (rd(d, 0x0769 + i, &v) || v != rgb[i])
			return -EIO;
	return 0;
}
/* EC RAM curve protocol independently tested on this machine.
 * Additional protocol reference: Open-Mechrevo-FanSlider c6665cfc (MIT).
 * First duty MUST be zero; custom flag 0726.7 is required in addition to 0727.6.
 */
/* Only explicit control changes call this, never lifecycle callbacks. */
static int curve_restore(struct mr *d)
{
	if (!d->curve_fans && !(d->dirty & DIRTY_CURVE))
		return 0;
	if (curve_mask(0x07c6, 4, 0) || curve_mask(0x0727, 0x40, 0) ||
	    curve_mask(0x0726, 0x80, 0) || curve_mask(0x07c5, 0x80, 0) ||
	    curve_mask(0x0751, 0xf0, d->mode))
		return -EIO;
	d->curve_fans = false;
	d->dirty &= ~DIRTY_CURVE;
	return 0;
}
/* Read existing EC state; no register writes and no automatic PWM replay. */
static int read_fans(struct mr *d)
{
	unsigned int gate, flags, enabled, value, points[10];
	int i;
	if (rd(d, 0x0726, &flags) || rd(d, 0x0727, &enabled) || rd(d, 0x07c6, &gate))
		return -EIO;
	d->curve_fans = (flags & 0x80) && (enabled & 0x40) && (gate & 4);
	for (i = 0; i < 2; i++) {
		if (fan_io(i, &value, false) || value > 200)
			return -EIO;
		d->fan_target[i] = value / 2;
	}
	d->curve_valid = false;
	for (i = 0; i < 10; i++) {
		if (rd(d, 0x0f01 + (i / 5) * 48 + i % 5, &value))
			return -EIO;
		if (value != 40 + (i % 5) * 10)
			return 0;
		if (rd(d, 0x0f21 + (i / 5) * 48 + i % 5, &value))
			return -EIO;
		if (value > 200 || (value & 1))
			return 0;
		points[i] = value / 2;
		if (i % 5 && points[i] < points[i - 1])
			return 0;
	}
	memcpy(d->curve_points, points, sizeof(points));
	d->curve_valid = true;
	return 0;
}
static int curve_apply(struct mr *d)
{
	unsigned int up, duty, v;
	int i, j, ret;
	/* A user explicitly choosing curve mode may initialize an absent table. */
	if (!d->curve_valid) {
		const unsigned int defaults[] = {30, 40, 60, 80, 100, 30, 40, 60, 80, 100};
		memcpy(d->curve_points, defaults, sizeof(defaults));
		d->curve_valid = true;
	}
	if (!d->curve_fans) {
		ret = fan_restore(d);
		if (ret)
			return ret;
	}
	d->dirty |= DIRTY_CURVE;
	if (curve_mask(0x0751, 0xf0, 0) || curve_mask(0x0726, 0x80, 0x80) ||
	    curve_mask(0x0727, 0x40, 0x40) || curve_mask(0x07c5, 0x80, 0x80) ||
	    curve_mask(0x07c6, 4, 0))
		return -EIO;
	for (i = 0; i < 2; i++)
		for (j = 0; j < 16; j++) {
			up = j == 0 ? 20 : j < 6 ? 30 + j * 10 : j == 15 ? 255 : 80 + (j - 5) * 5;
			duty = j == 0 ? 0 : d->curve_points[i * 5 + (j < 6 ? j - 1 : 4)] * 2;
			if (curve_byte(d, 0x0f00 + i * 48 + j, up))
				return -EIO;
			if (j < 15 && curve_byte(d, 0x0f10 + i * 48 + j + 1, up - 3))
				return -EIO;
			if (curve_byte(d, 0x0f20 + i * 48 + j, duty))
				return -EIO;
		}
	if (curve_mask(0x07c6, 4, 4) || curve_mask(0x0751, 0x40, 0))
		return -EIO;
	/* No PWM writes or host temperature loop while curve_fans is true. */
	d->curve_fans = true;
	d->manual_fans = false;
	/* Reject a firmware gate loss rather than silently claiming curve control. */
	if (rd(d, 0x07c6, &v) || !(v & 4))
		return -EIO;
	return 0;
}
/* lock held. Each dirty flag is set BEFORE the corresponding write. */
static int restore(struct mr *d)
{
	unsigned int flag;
	int i, ret = 0;
	d->active = false;
	d->manual_fans = false;
	if (curve_restore(d)) {
		d->last_error = -EIO;
		return -EIO;
	}
	if ((d->dirty & ~DIRTY_GATE) && !(d->dirty & DIRTY_GATE)) {
		d->dirty |= DIRTY_GATE;
		if (mask(d, 0x0741, 1, 1))
			return -EIO;
	}
	for (flag = DIRTY_MODE; flag <= DIRTY_GATE; flag <<= 1) {
		if (!(d->dirty & flag))
			continue;
		for (i = 0; i < 3; i++) {
			switch (flag) {
			case DIRTY_MODE:
				ret = mask(d, 0x0751, 0xf0, d->mode);
				break;
			case DIRTY_LIGHT:
				ret = brightness(d, d->light);
				break;
			case DIRTY_RGB:
				ret = color(d, d->rgb);
				break;
			case DIRTY_FAN:
				ret = fan_restore(d);
				break;
			case DIRTY_CHARGE:
				ret = mask(d, 0x07a6, 0x30, d->charge);
				break;
			default:
				ret = mask(d, 0x0741, 1, 0);
				break;
			}
			if (!ret) {
				d->dirty &= ~flag;
				break;
			}
			msleep(50);
		}
	}
	if (d->dirty) {
		d->last_error = -EIO;
		dev_err(d->dev, "restore incomplete: dirty=%x\n", d->dirty);
		return -EIO;
	}
	d->last_error = 0;
	return 0;
}
static int fan_apply(struct mr *d)
{
	unsigned int value;
	int i;
	for (i = 0; i < 2; i++) {
		value = d->fan_target[i] * 2;
		if (fan_io(i, &value, true))
			return -EIO;
	}
	return 0;
}
static void fan_tick(struct work_struct *work)
{
	struct mr *d = container_of(to_delayed_work(work), struct mr, fan_work);
	mutex_lock(&d->lock);
	if (d->manual_fans && d->active && !d->suspended && !d->removing) {
		if (fan_apply(d)) {
			d->last_error = -EIO;
			d->manual_fans = false;
			d->active = false;
		} else
			mod_delayed_work(system_wq, &d->fan_work, msecs_to_jiffies(800));
	}
	mutex_unlock(&d->lock);
}
static int fan_manual(struct mr *d)
{
	if (curve_restore(d))
		return -EIO;
	if (!d->manual_fans) {
		d->dirty |= DIRTY_FAN | DIRTY_MODE;
		if (fan_split_set(0x80))
			return -EIO;
		if (mask(d, 0x0751, 0x40, 0))
			return -EIO;
		d->manual_fans = true;
	}
	if (fan_apply(d))
		return -EIO;
	mod_delayed_work(system_wq, &d->fan_work, msecs_to_jiffies(800));
	return 0;
}
static int acquire(struct mr *d)
{
	unsigned int v;
	int i, ret;
	if (d->suspended || d->removing)
		return -EBUSY;
	if (d->active)
		return 0;
	if (d->dirty)
		return -EIO;
	if (read_fans(d))
		return -EIO;
	if (rd(d, 0x0751, &d->mode))
		return -EIO;
	/* Custom curves use mode bits 0; office is the explicit reset fallback. */
	if (d->curve_fans && !(d->mode & 0xb0))
		d->mode = 0xa0;
	else if ((d->mode & 0xb0) != 0xa0 && (d->mode & 0xb0) != 0x20 && (d->mode & 0xb0) != 0x30)
		return -EBUSY;
	d->mode &= 0xf0;
	if (rd(d, 0x078c, &v) || (v >> 5) > 4)
		return -EIO;
	d->light = v >> 5;
	if (rd(d, 0x0767, &v) || (v & 0x80))
		return -EOPNOTSUPP;
	for (i = 0; i < 3; i++)
		if (rd(d, 0x0769 + i, &d->rgb[i]) || d->rgb[i] > 50)
			return -EIO;
	if (rd(d, 0x07a6, &v) || ((v >> 4) & 3) > 2)
		return -EIO;
	d->charge = v & 0x30;
	d->dirty = DIRTY_GATE;
	ret = mask(d, 0x0741, 1, 1);
	if (ret) {
		d->last_error = ret;
		return ret;
	}
	d->active = true;
	d->last_error = 0;
	return 0;
}
static int available(struct mr *d)
{
	unsigned int v;
	if (d->suspended || d->removing)
		return -EBUSY;
	if (!d->active)
		return -EACCES;
	if (rd(d, 0x0741, &v) || !(v & 1))
		return -EIO;
	return 0;
}
static int mode_value(const char *buf)
{
	if (sysfs_streq(buf, "office"))
		return 0xa0;
	if (sysfs_streq(buf, "balanced"))
		return 0x20;
	if (sysfs_streq(buf, "turbo"))
		return 0x30;
	return -EINVAL;
}
static const char *mode_name(unsigned int v)
{
	switch (v & 0xb0) {
	case 0xa0:
		return "office";
	case 0x20:
		return "balanced";
	case 0x30:
		return "turbo";
	default:
		return "unknown";
	}
}
static ssize_t control_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mr *d = dev_get_drvdata(dev);
	ssize_t ret;
	unsigned int gate;
	mutex_lock(&d->lock);
	if (d->suspended || d->removing)
		ret = -EBUSY;
	else if (rd(d, 0x0741, &gate))
		ret = -EIO;
	else
		ret = sysfs_emit(buf, "%s\n",
				 d->dirty && !d->active ? "restore_failed"
				 : d->active		? "active"
				 : (gate & 1)		? "retained"
							: "firmware");
	mutex_unlock(&d->lock);
	return ret;
}
static ssize_t control_store(struct device *dev, struct device_attribute *attr, const char *buf,
			     size_t count)
{
	struct mr *d = dev_get_drvdata(dev);
	int ret;
	mutex_lock(&d->lock);
	if (d->suspended || d->removing) {
		mutex_unlock(&d->lock);
		return -EBUSY;
	}
	if (sysfs_streq(buf, "release")) {
		ret = 0;
		if (!d->active && !d->dirty)
			ret = acquire(d);
		if (!ret) {
			d->dirty |= DIRTY_FAN | DIRTY_GATE;
			ret = restore(d);
		}
	} else if (sysfs_streq(buf, "acquire"))
		ret = acquire(d);
	else
		ret = -EINVAL;
	mutex_unlock(&d->lock);
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(control);

static ssize_t setting_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mr *d = dev_get_drvdata(dev);
	unsigned int v, rgb[3];
	int ret = 0, i;
	mutex_lock(&d->lock);
	if (d->suspended || d->removing) {
		ret = -EBUSY;
		goto out;
	}
	if (!strcmp(attr->attr.name, "fan_control")) {
		unsigned int split, gate;
		ret = rd(d, 0x0751, &v);
		if (!ret)
			ret = rd(d, 0x07c6, &gate);
		if (!ret)
			ret = fan_io(2, &split, false);
		if (!ret)
			ret = sysfs_emit(buf, "%s\n",
					 (gate & 4)	  ? "curve"
					 : (v & 0x40)	  ? "boost"
					 : (split & 0x80) ? "manual"
							  : "auto");
	} else if (!strcmp(attr->attr.name, "fan_curve")) {
		if (!d->curve_valid) {
			ret = -ENODATA;
			goto out;
		}
		ret = sysfs_emit(buf, "%u %u %u %u %u %u %u %u %u %u\n", d->curve_points[0],
				 d->curve_points[1], d->curve_points[2], d->curve_points[3],
				 d->curve_points[4], d->curve_points[5], d->curve_points[6],
				 d->curve_points[7], d->curve_points[8], d->curve_points[9]);
	} else if (!strcmp(attr->attr.name, "fan1_percent") ||
		   !strcmp(attr->attr.name, "fan2_percent")) {
		i = attr->attr.name[3] == '2';
		if (!d->manual_fans) {
			ret = fan_io(i, &v, false);
			if (ret)
				goto out;
			if (v > 200) {
				ret = -ERANGE;
				goto out;
			}
			d->fan_target[i] = v / 2;
		}
		ret = sysfs_emit(buf, "%u\n", d->fan_target[i]);
	} else if (!strcmp(attr->attr.name, "mode")) {
		ret = rd(d, 0x0751, &v);
		if (!ret)
			ret = sysfs_emit(buf, "%s\n", mode_name(v));
	} else if (!strcmp(attr->attr.name, "keyboard_brightness")) {
		ret = rd(d, 0x078c, &v);
		if (!ret)
			ret = sysfs_emit(buf, "%u\n", v >> 5);
	} else if (!strcmp(attr->attr.name, "charging_profile")) {
		ret = rd(d, 0x07a6, &v);
		if (!ret)
			ret = sysfs_emit(buf, "%s\n",
					 (v & 0x30) == 0      ? "high_capacity"
					 : (v & 0x30) == 0x10 ? "balanced"
					 : (v & 0x30) == 0x20 ? "stationary"
							      : "unknown");
	} else {
		for (i = 0; i < 3; i++) {
			ret = rd(d, 0x0769 + i, &rgb[i]);
			if (ret)
				goto out;
		}
		ret = sysfs_emit(buf, "%u %u %u\n", rgb[0], rgb[1], rgb[2]);
	}
out:
	mutex_unlock(&d->lock);
	return ret;
}
static ssize_t setting_store(struct device *dev, struct device_attribute *attr, const char *buf,
			     size_t count)
{
	struct mr *d = dev_get_drvdata(dev);
	unsigned int v = 0, rgb[3], points[10];
	char extra;
	int ret, kind, i;
	/* Validate before acquiring the lock or touching hardware. */
	if (!strcmp(attr->attr.name, "fan_control")) {
		if (sysfs_streq(buf, "auto"))
			v = 0;
		else if (sysfs_streq(buf, "manual"))
			v = 1;
		else if (sysfs_streq(buf, "boost"))
			v = 2;
		else if (sysfs_streq(buf, "curve"))
			v = 3;
		else
			return -EINVAL;
		kind = DIRTY_FAN;
	} else if (!strcmp(attr->attr.name, "fan_curve")) {
		if (sscanf(buf, "%u %u %u %u %u %u %u %u %u %u %c", &points[0], &points[1],
			   &points[2], &points[3], &points[4], &points[5], &points[6], &points[7],
			   &points[8], &points[9], &extra) != 10)
			return -EINVAL;
		for (i = 0; i < 10; i++) {
			if (points[i] > 100 || (i % 5 && points[i] < points[i - 1]))
				return -EINVAL;
		}
		kind = DIRTY_CURVE;
	} else if (!strcmp(attr->attr.name, "fan1_percent") ||
		   !strcmp(attr->attr.name, "fan2_percent")) {
		if (kstrtouint(buf, 10, &v) || v > 100)
			return -EINVAL;
		kind = DIRTY_FAN;
	} else if (!strcmp(attr->attr.name, "mode")) {
		ret = mode_value(buf);
		if (ret < 0)
			return ret;
		v = ret;
		kind = DIRTY_MODE;
	} else if (!strcmp(attr->attr.name, "keyboard_brightness")) {
		if (kstrtouint(buf, 10, &v) || v > 4)
			return -EINVAL;
		kind = DIRTY_LIGHT;
	} else if (!strcmp(attr->attr.name, "charging_profile")) {
		if (sysfs_streq(buf, "high_capacity"))
			v = 0;
		else if (sysfs_streq(buf, "balanced"))
			v = 0x10;
		else if (sysfs_streq(buf, "stationary"))
			v = 0x20;
		else
			return -EINVAL;
		kind = DIRTY_CHARGE;
	} else {
		if (sscanf(buf, "%u %u %u %c", &rgb[0], &rgb[1], &rgb[2], &extra) != 3 ||
		    rgb[0] > 50 || rgb[1] > 50 || rgb[2] > 50)
			return -EINVAL;
		kind = DIRTY_RGB;
	}
	mutex_lock(&d->lock);
	ret = available(d);
	if (ret)
		goto out;
	if (kind == DIRTY_CURVE) {
		if (d->curve_fans && !memcmp(d->curve_points, points, sizeof(points))) {
			ret = 0;
		} else {
			memcpy(d->curve_points, points, sizeof(points));
			d->curve_valid = true;
			ret = curve_apply(d);
		}
	} else if (kind == DIRTY_FAN) {
		if (strcmp(attr->attr.name, "fan_control")) {
			d->fan_target[attr->attr.name[3] == '2'] = v;
			ret = fan_manual(d);
		} else if (v == 1)
			ret = fan_manual(d);
		else if (v == 3)
			ret = d->curve_fans ? 0 : curve_apply(d);
		else {
			ret = curve_restore(d);
			if (!ret)
				ret = fan_restore(d);
			d->dirty |= DIRTY_MODE;
			if (!ret)
				ret = mask(d, 0x0751, 0x40, v == 2 ? 0x40 : 0);
		}
	} else {
		if (kind == DIRTY_MODE && curve_restore(d)) {
			ret = -EIO;
			goto failed;
		}
		d->dirty |= kind;
		if (kind == DIRTY_MODE)
			ret = mask(d, 0x0751, 0xb0, v);
		else if (kind == DIRTY_LIGHT)
			ret = brightness(d, v);
		else if (kind == DIRTY_CHARGE)
			ret = mask(d, 0x07a6, 0x30, v);
		else
			ret = color(d, rgb);
	}
failed:
	if (ret) {
		d->last_error = ret;
		d->active = false;
		d->manual_fans = false;
	}
out:
	mutex_unlock(&d->lock);
	return ret ? ret : count;
}
#define SETTING(n)                                                                                 \
	static struct device_attribute dev_attr_##n = __ATTR(n, 0644, setting_show, setting_store)
SETTING(mode);
SETTING(fan_control);
SETTING(fan_curve);
SETTING(fan1_percent);
SETTING(fan2_percent);
SETTING(keyboard_brightness);
SETTING(keyboard_rgb);
SETTING(charging_profile);
static ssize_t last_error_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mr *d = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%d\n", READ_ONCE(d->last_error));
}
static DEVICE_ATTR_RO(last_error);
static struct attribute *attrs[] = {&dev_attr_fan_curve.attr,
				    &dev_attr_fan_control.attr,
				    &dev_attr_fan1_percent.attr,
				    &dev_attr_fan2_percent.attr,
				    &dev_attr_control.attr,
				    &dev_attr_mode.attr,
				    &dev_attr_keyboard_brightness.attr,
				    &dev_attr_keyboard_rgb.attr,
				    &dev_attr_charging_profile.attr,
				    &dev_attr_last_error.attr,
				    NULL};
static const struct attribute_group group = {.attrs = attrs};
static const struct attribute_group *groups[] = {&group, NULL};

static umode_t visible(const void *data, enum hwmon_sensor_types type, u32 attr, int channel)
{
	return 0444;
}
static int sensor_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel,
		       long *value)
{
	struct mr *d = dev_get_drvdata(dev);
	unsigned int hi, lo, again;
	int ret = -EOPNOTSUPP, i;
	mutex_lock(&d->lock);
	if (d->suspended || d->removing) {
		ret = -EBUSY;
		goto out;
	}
	if (type == hwmon_temp && attr == hwmon_temp_input) {
		ret = rd(d, 0x043e, &hi);
		if (!ret)
			*value = hi * 1000;
	} else if (type == hwmon_fan && attr == hwmon_fan_input) {
		unsigned int reg = channel ? 0x046c : 0x0464;
		ret = -EAGAIN;
		for (i = 0; i < 3; i++) {
			if (rd(d, reg, &hi) || rd(d, reg + 1, &lo) || rd(d, reg, &again)) {
				ret = -EIO;
				break;
			}
			if (hi == again) {
				*value = (hi << 8) | lo;
				ret = 0;
				break;
			}
		}
	}
out:
	mutex_unlock(&d->lock);
	return ret;
}
static int sensor_label(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel,
			const char **str)
{
	*str = type == hwmon_temp ? "CPU EC" : channel ? "Secondary" : "Main";
	return 0;
}
static const struct hwmon_ops ops = {
    .is_visible = visible, .read = sensor_read, .read_string = sensor_label};
static const struct hwmon_channel_info *const info[] = {
    HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_LABEL),
    HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT | HWMON_F_LABEL, HWMON_F_INPUT | HWMON_F_LABEL), NULL};
static const struct hwmon_chip_info chip = {.ops = &ops, .info = info};
static void notify(struct wmi_device *wdev, union acpi_object *obj)
{
	struct mr *d = dev_get_drvdata(&wdev->dev);
	unsigned int v, next;
	int ret;
	if (!d || !obj || obj->type != ACPI_TYPE_INTEGER || obj->integer.value != 0xb0)
		return;
	mutex_lock(&d->lock);
	if (!d->active || d->suspended || d->removing)
		goto out;
	if (d->last_key && time_before(jiffies, d->last_key + msecs_to_jiffies(350)))
		goto out;
	d->last_key = jiffies;
	ret = available(d);
	if (ret)
		goto fail;
	ret = curve_restore(d);
	if (ret)
		goto fail;
	ret = rd(d, 0x0751, &v);
	if (ret)
		goto fail;
	switch (v & 0xb0) {
	case 0xa0:
		next = 0x30;
		break;
	case 0x30:
		next = 0x20;
		break;
	case 0x20:
		next = 0xa0;
		break;
	default:
		ret = -EINVAL;
		goto fail;
	}
	d->dirty |= DIRTY_MODE;
	ret = mask(d, 0x0751, 0xb0, next);
	if (!ret) {
		sysfs_notify(&d->hwmon->kobj, NULL, "mode");
		goto out;
	}
fail:
	d->last_error = ret;
	d->active = false;
	d->manual_fans = false;
out:
	mutex_unlock(&d->lock);
}
static int reboot_device(struct notifier_block *nb, unsigned long action, void *unused)
{
	struct mr *d = container_of(nb, struct mr, reboot);
	mutex_lock(&d->lock);
	d->suspended = true;
	mutex_unlock(&d->lock);
	cancel_delayed_work_sync(&d->fan_work);
	return NOTIFY_DONE;
}
static int probe(struct wmi_device *wdev, const void *context)
{
	struct mr *d;
	unsigned int v;
	acpi_handle method;
	int ret;
	if (!dmi_match(DMI_SYS_VENDOR, "MECHREVO") ||
	    !dmi_match(DMI_BOARD_NAME, "JIAOLONG Series-X6xR55xK"))
		return -ENODEV;
	d = devm_kzalloc(&wdev->dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;
	d->dev = &wdev->dev;
	mutex_init(&d->lock);
	INIT_DELAYED_WORK(&d->fan_work, fan_tick);
	if (ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB_.INOU", &d->ec)) ||
	    ACPI_FAILURE(acpi_get_handle(d->ec, "ECRW", &method)))
		return -ENODEV;
	if (rd(d, 0x0740, &v) || v != 0x1a || rd(d, 0x0766, &v) || !(v & 4) || rd(d, 0x078e, &v) ||
	    (v & 0x48) != 0x48)
		return -ENODEV;
	ret = read_fans(d);
	if (ret)
		return ret;
	dev_set_drvdata(&wdev->dev, d);
	d->hwmon = hwmon_device_register_with_info(&wdev->dev, "mechrevo", d, &chip, groups);
	if (IS_ERR(d->hwmon))
		return PTR_ERR(d->hwmon);
	d->reboot.notifier_call = reboot_device;
	ret = register_reboot_notifier(&d->reboot);
	if (ret) {
		hwmon_device_unregister(d->hwmon);
		return ret;
	}
	dev_info(d->dev, "read-only monitoring ready; controls require explicit acquire\n");
	return 0;
}
static void remove_device(struct wmi_device *wdev)
{
	struct mr *d = dev_get_drvdata(&wdev->dev);
	mutex_lock(&d->lock);
	d->removing = true;
	mutex_unlock(&d->lock);
	unregister_reboot_notifier(&d->reboot);
	hwmon_device_unregister(d->hwmon);
	cancel_delayed_work_sync(&d->fan_work);
}
static int suspend_device(struct device *dev)
{
	struct mr *d = dev_get_drvdata(dev);
	mutex_lock(&d->lock);
	d->suspended = true;
	d->manual_fans = false;
	mutex_unlock(&d->lock);
	cancel_delayed_work_sync(&d->fan_work);
	return 0;
}
static int resume_device(struct device *dev)
{
	struct mr *d = dev_get_drvdata(dev);
	int ret;
	mutex_lock(&d->lock);
	d->active = false;
	d->dirty = 0;
	d->suspended = false;
	ret = read_fans(d);
	if (ret)
		d->last_error = ret;
	mutex_unlock(&d->lock);
	return ret;
}
static DEFINE_SIMPLE_DEV_PM_OPS(pm, suspend_device, resume_device);
static const struct wmi_device_id ids[] = {{"ABBC0F72-8EA1-11D1-00A0-C90629100000", NULL}, {}};
static struct wmi_driver mr_driver = {
    .driver = {.name = "mechrevo-control", .pm = pm_sleep_ptr(&pm)},
    .id_table = ids,
    .probe = probe,
    .remove = remove_device,
    .notify = notify,
};
module_wmi_driver(mr_driver);
/* No modalias: loading remains explicit while this hardware-specific driver matures. */
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MECHREVO X6xR55xK hardware monitor and explicit controls");
MODULE_VERSION("1.1.0");
