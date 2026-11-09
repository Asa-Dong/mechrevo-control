// SPDX-License-Identifier: GPL-2.0-only
#include "../ec-test-common.h"
static unsigned int original_rgb[3], gate_bit, mode;
static bool color_pending, gate_pending;
static int apply_color(const unsigned int rgb[3])
{
    int i, ret;
    unsigned int mode_value;
    for (i = 0; i < 3; i++) {
        ret = ec_test_write(0x0769 + i, rgb[i]);
        if (ret) return ret;
    }
    ret = ec_test_read(0x0767, &mode_value);
    if (ret) return ret;
    return ec_test_write(0x0767, mode_value | 0x20);
}
static int verify_color(const unsigned int rgb[3])
{
    unsigned int value;
    int i;
    for (i = 0; i < 3; i++)
        if (ec_test_read(0x0769 + i, &value) || value != rgb[i]) return -EIO;
    return 0;
}
static int restore(void)
{
    int i;
    if (color_pending) {
        for (i = 0; i < 3; i++) {
            if (!apply_color(original_rgb) && !verify_color(original_rgb) &&
                !ec_test_mask(0x0767, 0x80, mode)) {
                color_pending = false;
                pr_info("mechrevo_rgb_test: COLOR_RESTORED %u,%u,%u\n", original_rgb[0], original_rgb[1], original_rgb[2]);
                break;
            }
            msleep(100);
        }
        if (color_pending) return -EIO;
    }
    if (gate_pending) {
        for (i = 0; i < 3; i++) {
            if (!ec_test_mask(0x0741, 1, gate_bit)) {
                gate_pending = false;
                pr_info("mechrevo_rgb_test: GATE_RESTORED bit=%u\n", gate_bit);
                break;
            }
            msleep(100);
        }
    }
    return gate_pending ? -EIO : 0;
}
static int __init test_init(void)
{
    unsigned int value, target[3] = {50, 0, 0};
    int i, ret = ec_test_open();
    if (ret) return ret;
    if (ec_test_read(0x0766, &value) || !(value & BIT(2))) return -ENODEV;
    if (ec_test_read(0x0767, &mode) || (mode & 0x80)) return -EINVAL;
    for (i = 0; i < 3; i++) {
        if (ec_test_read(0x0769 + i, &original_rgb[i]) || original_rgb[i] > 50) return -EIO;
    }
    if (!(original_rgb[0] | original_rgb[1] | original_rgb[2])) return -EINVAL;
    if (original_rgb[0] == 50 && original_rgb[1] == 0 && original_rgb[2] == 0) {
        target[0] = 0; target[2] = 50;
    }
    if (ec_test_read(0x0741, &value)) return -EIO;
    gate_bit = value & 1;
    pr_info("mechrevo_rgb_test: BEFORE %u,%u,%u mode=0x%02x gate=%u\n", original_rgb[0], original_rgb[1], original_rgb[2], mode, gate_bit);
    gate_pending = true;
    ret = ec_test_mask(0x0741, 1, 1);
    if (!ret) {
        color_pending = true;
        ret = apply_color(target);
        if (!ret) {
            msleep(8000);
            ret = verify_color(target);
            pr_info("mechrevo_rgb_test: TARGET %u,%u,%u result=%d\n", target[0], target[1], target[2], ret);
        }
    }
    if (restore()) { pr_err("mechrevo_rgb_test: RESTORE_FAILED\n"); return 0; }
    return ret;
}
static void __exit test_exit(void)
{
    int ret = restore();
    pr_info("mechrevo_rgb_test: unloaded restore=%d\n", ret);
}
module_init(test_init);
module_exit(test_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Fixed RGB test with original color restoration");
