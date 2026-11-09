// SPDX-License-Identifier: GPL-2.0-only
#include "../ec-test-common.h"
static unsigned int saved_boost, saved_gate;
static bool boost_pending, gate_pending;
static int fan_rpm(unsigned int reg, unsigned int *rpm)
{
    unsigned int hi, lo, again;
    int i;
    for (i = 0; i < 3; i++) {
        if (ec_test_read(reg, &hi) || ec_test_read(reg + 1, &lo) || ec_test_read(reg, &again)) return -EIO;
        if (hi == again) { *rpm = (hi << 8) | lo; return 0; }
    }
    return -EAGAIN;
}
static int sample(const char *phase)
{
    unsigned int first, second, temp, mode;
    if (fan_rpm(0x0464, &first) || fan_rpm(0x046c, &second) ||
        ec_test_read(0x043e, &temp) || ec_test_read(0x0751, &mode)) return -EIO;
    pr_info("mechrevo_fan_test: %s main=%u secondary=%u temp=%u mode=0x%02x\n", phase, first, second, temp, mode);
    return temp >= 85 ? -ERANGE : 0;
}
static int restore(void)
{
    int i;
    if (boost_pending) {
        for (i = 0; i < 3; i++) {
            if (!ec_test_mask(0x0751, 0x40, saved_boost)) {
                boost_pending = false;
                pr_info("mechrevo_fan_test: BOOST_RESTORED bit=%u\n", saved_boost);
                break;
            }
            msleep(100);
        }
        if (boost_pending) return -EIO;
    }
    if (gate_pending) {
        for (i = 0; i < 3; i++) {
            if (!ec_test_mask(0x0741, 1, saved_gate)) {
                gate_pending = false;
                pr_info("mechrevo_fan_test: GATE_RESTORED bit=%u\n", saved_gate);
                break;
            }
            msleep(100);
        }
    }
    return gate_pending ? -EIO : 0;
}
static int __init test_init(void)
{
    unsigned int value;
    int i, ret = ec_test_open();
    if (ret) return ret;
    if (ec_test_read(0x043e, &value) || value > 75) return -EBUSY;
    /* Only test from ordinary firmware control, never overwrite a custom curve. */
    if (ec_test_read(0x07c6, &value) || (value & 4)) return -EBUSY;
    if (ec_test_read(0x0751, &value) || (value & 0x40)) return -EBUSY;
    saved_boost = value & 0x40;
    if (ec_test_read(0x0741, &value)) return -EIO;
    saved_gate = value & 1;
    ret = sample("BEFORE");
    if (ret) return ret;
    gate_pending = true;
    ret = ec_test_mask(0x0741, 1, 1);
    if (!ret) {
        boost_pending = true;
        ret = ec_test_mask(0x0751, 0x40, 0x40);
        if (!ret) {
            for (i = 0; i < 5; i++) {
                msleep(2000);
                ret = sample("BOOST");
                if (ret) break;
            }
        }
    }
    pr_info("mechrevo_fan_test: test_result=%d\n", ret);
    if (restore()) { pr_err("mechrevo_fan_test: RESTORE_FAILED\n"); return 0; }
    for (i = 0; i < 5; i++) { msleep(2000); sample("AFTER"); }
    return ret;
}
static void __exit test_exit(void)
{
    int ret = restore();
    pr_info("mechrevo_fan_test: unloaded restore=%d\n", ret);
}
module_init(test_init);
module_exit(test_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Brief full-fan boost test with automatic restoration, no curve edits");
