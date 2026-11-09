// SPDX-License-Identifier: GPL-2.0-only
/* Only replay three states observed on this exact machine. */
#include "../ec-test-common.h"
#define MODE_MASK 0xb0
static unsigned int saved_mode, saved_gate;
static bool mode_pending, gate_pending;

static int restore(void)
{
    int i;
    if (mode_pending) {
        for (i = 0; i < 3; i++) {
            if (!ec_test_mask(0x0751, MODE_MASK, saved_mode)) {
                mode_pending = false;
                pr_info("mechrevo_mode_test: MODE_RESTORED=%02x\n", saved_mode);
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
                pr_info("mechrevo_mode_test: GATE_RESTORED=%u\n", saved_gate);
                break;
            }
            msleep(100);
        }
    }
    return gate_pending ? -EIO : 0;
}

static int __init start(void)
{
    static const unsigned int targets[] = { 0x20, 0x30, 0xa0 };
    unsigned int value, temp, i, tick;
    int ret = ec_test_open();
    if (ret) return ret;
    if (ec_test_read(0x043e, &temp) || temp > 75) return -EBUSY;
    if (ec_test_read(0x0727, &value) || value != 0) return -EBUSY;
    if (ec_test_read(0x07c6, &value) || value != 0) return -EBUSY;
    if (ec_test_read(0x0751, &value)) return -EIO;
    if (value != 0xa0 && value != 0x20 && value != 0x30) return -EBUSY;
    saved_mode = value & MODE_MASK;
    if (ec_test_read(0x0741, &value) || (value & 1)) return -EBUSY;
    saved_gate = value & 1;
    pr_info("mechrevo_mode_test: BEFORE mode=%02x gate=%02x temp=%u\n", saved_mode, value, temp);
    gate_pending = true;
    ret = ec_test_mask(0x0741, 1, 1);
    for (i = 0; !ret && i < ARRAY_SIZE(targets); i++) {
        mode_pending = true;
        ret = ec_test_mask(0x0751, MODE_MASK, targets[i]);
        pr_info("mechrevo_mode_test: TARGET=%02x result=%d\n", targets[i], ret);
        for (tick = 0; !ret && tick < 8; tick++) {
            msleep(1000);
            if (ec_test_read(0x043e, &temp) || ec_test_read(0x0751, &value)) ret = -EIO;
            else if (temp >= 85) ret = -ERANGE;
            else if ((value & MODE_MASK) != targets[i]) ret = -EIO;
        }
        pr_info("mechrevo_mode_test: HOLD target=%02x result=%d temp=%u\n", targets[i], ret, temp);
    }
    pr_info("mechrevo_mode_test: test_result=%d\n", ret);
    if (restore()) {
        pr_err("mechrevo_mode_test: RESTORE_FAILED retained for cleanup\n");
        return 0;
    }
    return ret;
}
static void __exit stop(void)
{
    pr_info("mechrevo_mode_test: unloaded restore=%d\n", restore());
}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Bounded observed-mode write test with restoration");
