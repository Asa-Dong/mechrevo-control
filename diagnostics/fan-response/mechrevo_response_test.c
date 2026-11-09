// SPDX-License-Identifier: GPL-2.0-only
/* Explicit opt-in experiment: only 0x0787, raw 3/6 or 0x83/0x86, automatic rollback. */
#include "../ec-test-common.h"
#include "../fan-manual/wmi-ec.h"
#include <linux/workqueue.h>
static unsigned int candidate;
module_param(candidate, uint, 0400);
static unsigned int saved;
static bool changed;
static DEFINE_MUTEX(response_lock);
static int rollback(void)
{
    unsigned int check;
    if (!changed) return 0;
    if (fan_write(0x0787, saved) || fan_read(0x0787, &check) || check != saved) {
        pr_err("mechrevo_response_test: RESTORE_FAILED expected=%u\n", saved);
        return -EIO;
    }
    changed = false;
    pr_info("mechrevo_response_test: restored raw=%u\n", saved);
    return 0;
}
static void expire(struct work_struct *work)
{
    mutex_lock(&response_lock);
    rollback();
    mutex_unlock(&response_lock);
}
static DECLARE_DELAYED_WORK(expiry, expire);
static int __init start(void)
{
    unsigned int check, wmi;
    int ret;
    if (candidate != 3 && candidate != 6 && candidate != 0x83 && candidate != 0x86) return -EINVAL;
    ret = ec_test_open();
    if (ret) return ret;
    if (fan_read(0x0787, &saved) || fan_wmi(0x0787, &wmi, false) || wmi != saved)
        return -EIO;
    /* This experiment was reviewed only for the observed zero baseline. */
    if (saved != 0) return -EBUSY;
    changed = true;
    ret = fan_write(0x0787, candidate);
    if (ret || fan_read(0x0787, &check) || check != candidate) {
        if (rollback()) {
            schedule_delayed_work(&expiry, msecs_to_jiffies(1000));
            return 0; /* Keep module available to retry restoration on unload. */
        }
        return -EIO;
    }
    pr_info("mechrevo_response_test: original=%u candidate=%u readback=%u\n", saved, candidate, check);
    schedule_delayed_work(&expiry, msecs_to_jiffies(180000));
    return 0;
}
static void __exit stop(void)
{
    cancel_delayed_work_sync(&expiry);
    mutex_lock(&response_lock);
    rollback();
    mutex_unlock(&response_lock);
}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Bounded fan response experiment with 180s rollback");
