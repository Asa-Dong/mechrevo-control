// SPDX-License-Identifier: GPL-2.0-only
/* One-shot, read-only diagnostic for the exact tested board. No write API. */
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/delay.h>

struct probe_register { unsigned int offset; const char *name; };
static const struct probe_register registers[] = {
    {0x0740, "barebone_id"}, {0x0767, "rgb_mode"},
    {0x0769, "rgb_red"}, {0x076a, "rgb_green"}, {0x076b, "rgb_blue"},
    {0x07c5, "fan_table_split"}, {0x07c6, "fan_table_enable"}, {0x0766, "keyboard_features"},
    {0x078c, "keyboard_status"}, {0x078e, "capabilities"},
    {0x07a6, "charging_profile"}, {0x0751, "mode"},
    {0x0741, "manual_mode"}, {0x0727, "custom_profile"},
    {0x0464, "main_fan_high"}, {0x0465, "main_fan_low"},
    {0x046c, "secondary_fan_high"}, {0x046d, "secondary_fan_low"},
    {0x043e, "cpu_temperature"},
};
static int __init probe_init(void)
{
    acpi_handle handle, method;
    unsigned int i;
    if (!dmi_match(DMI_SYS_VENDOR, "MECHREVO") ||
        !dmi_match(DMI_BOARD_NAME, "JIAOLONG Series-X6xR55xK"))
        return -ENODEV;
    if (ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB_.INOU", &handle)) ||
        ACPI_FAILURE(acpi_get_handle(handle, "ECRR", &method)))
        return -ENODEV;
    for (i = 0; i < ARRAY_SIZE(registers); ++i) {
        union acpi_object arg = {.integer = {.type = ACPI_TYPE_INTEGER,
            .value = registers[i].offset}};
        struct acpi_object_list args = {.count = 1, .pointer = &arg};
        unsigned long long value;
        acpi_status result = acpi_evaluate_integer(handle, "ECRR", &args, &value);
        if (ACPI_FAILURE(result) || value > 255) {
            pr_err("mechrevo_probe: failed at 0x%04x\n", registers[i].offset);
            return -EIO;
        }
        pr_info("mechrevo_probe: %s offset=0x%04x value=0x%02llx\n",
                registers[i].name, registers[i].offset, value);
        usleep_range(6000, 12000);
    }
    return 0;
}
static void __exit probe_exit(void)
{
    pr_info("mechrevo_probe: unloaded\n");
}
module_init(probe_init);
module_exit(probe_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only one-shot capability probe for MECHREVO X6xR55xK");
