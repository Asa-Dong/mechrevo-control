/* SPDX-License-Identifier: GPL-2.0-only */
/* Fixed-purpose diagnostics only; no userspace write API. */
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/delay.h>
static acpi_handle ec_handle;
static int ec_test_open(void)
{
    acpi_handle method;
    if (!dmi_match(DMI_SYS_VENDOR, "MECHREVO") ||
        !dmi_match(DMI_BOARD_NAME, "JIAOLONG Series-X6xR55xK")) return -ENODEV;
    if (ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB_.INOU", &ec_handle)) ||
        ACPI_FAILURE(acpi_get_handle(ec_handle, "ECRW", &method))) return -ENODEV;
    return 0;
}
static int ec_test_read(unsigned int reg, unsigned int *value)
{
    union acpi_object arg = {.integer = {.type = ACPI_TYPE_INTEGER, .value = reg}};
    struct acpi_object_list args = {.count = 1, .pointer = &arg};
    unsigned long long out;
    acpi_status st = acpi_evaluate_integer(ec_handle, "ECRR", &args, &out);
    if (ACPI_FAILURE(st) || out > 255) return -EIO;
    *value = out;
    usleep_range(6000, 12000);
    return 0;
}
static int ec_test_write(unsigned int reg, unsigned int value)
{
    union acpi_object arg[2] = {
        {.integer = {.type = ACPI_TYPE_INTEGER, .value = reg}},
        {.integer = {.type = ACPI_TYPE_INTEGER, .value = value}},
    };
    struct acpi_object_list args = {.count = 2, .pointer = arg};
    acpi_status st = acpi_evaluate_object(ec_handle, "ECRW", &args, NULL);
    usleep_range(6000, 12000);
    return ACPI_FAILURE(st) ? -EIO : 0;
}
static int ec_test_mask(unsigned int reg, unsigned int mask, unsigned int bits)
{
    unsigned int value;
    int ret = ec_test_read(reg, &value);
    if (ret) return ret;
    ret = ec_test_write(reg, (value & ~mask) | (bits & mask));
    if (ret) return ret;
    ret = ec_test_read(reg, &value);
    if (ret) return ret;
    return (value & mask) == (bits & mask) ? 0 : -EIO;
}
