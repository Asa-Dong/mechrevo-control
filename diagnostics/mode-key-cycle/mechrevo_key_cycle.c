// SPDX-License-Identifier: GPL-2.0-only
#include <linux/wmi.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include "../ec-test-common.h"

static DEFINE_MUTEX(lock);
static bool bound, active, mode_pending, gate_pending;
static unsigned int saved_mode, saved_gate, count;
static unsigned long last_event;
static unsigned int timeout_seconds = 180;
module_param(timeout_seconds, uint, 0444);
MODULE_PARM_DESC(timeout_seconds, "Restoration timeout in seconds (30..180)");
static bool timeout_only;
module_param(timeout_only, bool, 0444);
MODULE_PARM_DESC(timeout_only, "Accept only one event, then wait for timeout restoration");

/* Caller holds lock; retain pending flags for a later cleanup retry. */
static int restore(void)
{
    int i;
    active = false;
    if (mode_pending) {
        for (i = 0; i < 3; i++) {
            if (!ec_test_mask(0x0751, 0xb0, saved_mode)) {
                mode_pending = false;
                break;
            }
            msleep(100);
        }
        if (mode_pending) return -EIO;
    }
    if (gate_pending) {
        for (i = 0; i < 3; i++) {
            if (!ec_test_mask(0x0741, 1, saved_gate)) {
                gate_pending = false;
                break;
            }
            msleep(100);
        }
    }
    pr_info("mechrevo_key_cycle: restore mode=%02x gate=%u pending=%u\n",
            saved_mode, saved_gate, gate_pending);
    return gate_pending ? -EIO : 0;
}

static void expire(struct work_struct *work)
{
    mutex_lock(&lock);
    pr_info("mechrevo_key_cycle: timeout restore=%d\n", restore());
    mutex_unlock(&lock);
}
static DECLARE_DELAYED_WORK(timeout_work, expire);

static int probe(struct wmi_device *dev, const void *context)
{
    bound = true;
    return 0;
}
static void remove_device(struct wmi_device *dev)
{
    mutex_lock(&lock);
    bound = false;
    pr_info("mechrevo_key_cycle: detach restore=%d\n", restore());
    mutex_unlock(&lock);
}
static void notify_key(struct wmi_device *dev, union acpi_object *obj)
{
    unsigned int mode, next, temp, gate;
    int ret;
    if (!obj || obj->type != ACPI_TYPE_INTEGER || obj->integer.value != 0xb0) return;
    mutex_lock(&lock);
    if (!active) goto out;
    if (timeout_only && count) goto out;
    if (count && time_before(jiffies, last_event + msecs_to_jiffies(350))) goto out;
    last_event = jiffies;
    if (ec_test_read(0x0751, &mode) || ec_test_read(0x043e, &temp) ||
        ec_test_read(0x0741, &gate) || !(gate & 1) || temp >= 85) goto fail;
    switch (mode) {
    case 0xa0: next = 0x30; break;
    case 0x30: next = 0x20; break;
    case 0x20: next = 0xa0; break;
    default: goto fail;
    }
    mode_pending = true;
    ret = ec_test_mask(0x0751, 0xb0, next);
    pr_info("mechrevo_key_cycle: event=0xb0 step=%u mode=%02x->%02x result=%d temp=%u\n",
            count + 1, mode, next, ret, temp);
    if (ret) goto fail;
    count++;
    if (!timeout_only && count == 3)
        pr_info("mechrevo_key_cycle: complete restore=%d\n", restore());
    goto out;
fail:
    pr_err("mechrevo_key_cycle: aborted restore=%d\n", restore());
out:
    mutex_unlock(&lock);
}
static const struct wmi_device_id ids[] = {
    { "ABBC0F72-8EA1-11D1-00A0-C90629100000", NULL }, { }
};
static struct wmi_driver driver = {
    .driver = { .name = "mechrevo-key-cycle-test" },
    .id_table = ids, .probe = probe, .remove = remove_device, .notify = notify_key,
};
static int __init start(void)
{
    unsigned int value;
    int ret;
    if (timeout_seconds < 30 || timeout_seconds > 180) return -EINVAL;
    ret = ec_test_open();
    if (ret) return ret;
    if (ec_test_read(0x043e, &value) || value > 75) return -EBUSY;
    if (ec_test_read(0x0727, &value) || value) return -EBUSY;
    if (ec_test_read(0x07c6, &value) || value) return -EBUSY;
    if (ec_test_read(0x0751, &saved_mode)) return -EIO;
    if (saved_mode != 0xa0 && saved_mode != 0x30 && saved_mode != 0x20) return -EBUSY;
    if (ec_test_read(0x0741, &value) || (value & 1)) return -EBUSY;
    saved_gate = value & 1;
    ret = wmi_driver_register(&driver);
    if (ret) return ret;
    if (!bound) { wmi_driver_unregister(&driver); return -ENODEV; }
    mutex_lock(&lock);
    gate_pending = true;
    ret = ec_test_mask(0x0741, 1, 1);
    if (!ret) active = true;
    else if (restore()) {
        pr_err("mechrevo_key_cycle: init restoration failed, retained for cleanup\n");
        ret = 0;
    }
    mutex_unlock(&lock);
    if (ret) { wmi_driver_unregister(&driver); return ret; }
    schedule_delayed_work(&timeout_work, msecs_to_jiffies(timeout_seconds * 1000));
    pr_info("mechrevo_key_cycle: ready active=%u initial=%02x limit=3 timeout=%us\n", active, saved_mode, timeout_seconds);
    return 0;
}
static void __exit stop(void)
{
    cancel_delayed_work_sync(&timeout_work);
    mutex_lock(&lock);
    pr_info("mechrevo_key_cycle: unload restore=%d\n", restore());
    mutex_unlock(&lock);
    wmi_driver_unregister(&driver);
}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Bounded three-event mode key cycle validation");
