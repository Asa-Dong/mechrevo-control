// SPDX-License-Identifier: GPL-2.0-only
/* Fixed, reversible charging-profile diagnostic. Not a general EC write API. */
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/delay.h>

#define PROFILE_REG 0x07a6
#define PROFILE_MASK 0x30
static acpi_handle handle;
static unsigned int original_bits;
static bool restore_needed;
static bool gate_restore_needed;
static unsigned int original_gate_bit;

static int read_byte(unsigned int reg, unsigned int *value)
{
    union acpi_object arg = {.integer = {.type = ACPI_TYPE_INTEGER, .value = reg}};
    struct acpi_object_list args = {.count = 1, .pointer = &arg};
    unsigned long long out;
    acpi_status st = acpi_evaluate_integer(handle, "ECRR", &args, &out);
    if (ACPI_FAILURE(st) || out > 255)
        return -EIO;
    *value = out;
    usleep_range(6000, 12000);
    return 0;
}
static int write_byte(unsigned int reg, unsigned int value)
{
    union acpi_object arg[2] = {
        {.integer = {.type = ACPI_TYPE_INTEGER, .value = reg}},
        {.integer = {.type = ACPI_TYPE_INTEGER, .value = value}},
    };
    struct acpi_object_list args = {.count = 2, .pointer = arg};
    acpi_status st = acpi_evaluate_object(handle, "ECRW", &args, NULL);
    usleep_range(6000, 12000);
    return ACPI_FAILURE(st) ? -EIO : 0;
}
static int restore_profile(void)
{
    unsigned int value;
    int i;
    if (!restore_needed)
        return 0;
    for (i = 0; i < 3; i++) {
        /* Preserve unrelated bits, including changes made by firmware. */
        if (!read_byte(PROFILE_REG, &value) &&
            !write_byte(PROFILE_REG, (value & ~PROFILE_MASK) | original_bits) &&
            !read_byte(PROFILE_REG, &value) &&
            (value & PROFILE_MASK) == original_bits) {
            restore_needed = false;
            pr_info("mechrevo_charge_test: RESTORED raw=0x%02x profile=%u\n",
                    value, original_bits >> 4);
            return 0;
        }
        msleep(100);
    }
    pr_err("mechrevo_charge_test: RESTORE FAILED original_profile=%u\n", original_bits >> 4);
    return -EIO;
}
static int restore_gate(void)
{
    unsigned int value;
    int i;
    if (!gate_restore_needed) return 0;
    for (i = 0; i < 3; i++) {
        if (!read_byte(0x0741, &value) &&
            !write_byte(0x0741, (value & ~1U) | original_gate_bit) &&
            !read_byte(0x0741, &value) && (value & 1) == original_gate_bit) {
            gate_restore_needed = false;
            pr_info("mechrevo_charge_test: GATE_RESTORED raw=0x%02x\n", value);
            return 0;
        }
        msleep(100);
    }
    pr_err("mechrevo_charge_test: GATE_RESTORE_FAILED\n");
    return -EIO;
}
static int __init charge_init(void)
{
    acpi_handle method;
    unsigned int value, target;
    int ret;
    if (!dmi_match(DMI_SYS_VENDOR, "MECHREVO") ||
        !dmi_match(DMI_BOARD_NAME, "JIAOLONG Series-X6xR55xK"))
        return -ENODEV;
    if (ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB_.INOU", &handle)) ||
        ACPI_FAILURE(acpi_get_handle(handle, "ECRW", &method)))
        return -ENODEV;
    ret = read_byte(0x078e, &value);
    if (ret || !(value & BIT(3)))
        return -ENODEV;
    ret = read_byte(PROFILE_REG, &value);
    if (ret)
        return ret;
    original_bits = value & PROFILE_MASK;
    if ((original_bits >> 4) > 2)
        return -EINVAL;
    pr_info("mechrevo_charge_test: BEFORE raw=0x%02x profile=%u\n", value, original_bits >> 4);
    /* Do not switch away from stationary if it was already selected. */
    if (original_bits == 0x20) {
        pr_info("mechrevo_charge_test: SKIPPED already stationary\n");
        return 0;
    }
    target = (value & ~PROFILE_MASK) | 0x20;
    restore_needed = true; /* Set before attempting any write. */
    ret = read_byte(0x0741, &value);
    if (ret) { restore_needed = false; return ret; }
    original_gate_bit = value & 1;
    pr_info("mechrevo_charge_test: GATE_BEFORE raw=0x%02x\n", value);
    gate_restore_needed = true;
    ret = write_byte(0x0741, value | 1);
    if (!ret) ret = read_byte(0x0741, &value);
    if (!ret && !(value & 1)) ret = -EIO;
    if (!ret) {
        pr_info("mechrevo_charge_test: GATE_ENABLED raw=0x%02x\n", value);
        ret = write_byte(PROFILE_REG, target);
    }
    if (!ret) {
        msleep(1000);
        ret = read_byte(PROFILE_REG, &value);
        if (!ret) {
            pr_info("mechrevo_charge_test: READBACK raw=0x%02x profile=%u\n", value, (value & PROFILE_MASK) >> 4);
            if ((value & PROFILE_MASK) != 0x20)
                ret = -EIO;
        }
    }
    pr_info("mechrevo_charge_test: switch_result=%d\n", ret);
    /* If restoration fails, remain loaded so unload can retry. */
    if (restore_profile())
        return 0;
    if (restore_gate())
        return 0;
    return ret;
}
static void __exit charge_exit(void)
{
    if (!restore_profile()) restore_gate();
    pr_info("mechrevo_charge_test: unloaded restore_pending=%d gate_pending=%d\n", restore_needed, gate_restore_needed);
}
module_init(charge_init);
module_exit(charge_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Fixed stationary charging-profile readback and restoration test");
