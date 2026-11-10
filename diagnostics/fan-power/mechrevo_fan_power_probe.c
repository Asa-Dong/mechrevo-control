// SPDX-License-Identifier: GPL-2.0-only
/* Fixed-address read-only fan control/table snapshot. No write commands. */
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/module.h>

static int __init fan_power_init(void)
{
    static const unsigned int regs[] = {0x0741, 0x0726, 0x0727, 0x07c5, 0x07c6,
        0x0751, 0x07c3, 0x0787, 0x043e, 0x044f, 0x075b, 0x075c};
    acpi_handle handle;
    unsigned int i, reg;
    if (!dmi_match(DMI_SYS_VENDOR, "MECHREVO") ||
        !dmi_match(DMI_BOARD_NAME, "JIAOLONG Series-X6xR55xK"))
        return -ENODEV;
    if (ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB_.INOU", &handle)))
        return -ENODEV;
    for (i = 0; i < ARRAY_SIZE(regs) + 96; i++) {
        union acpi_object arg = {.integer = {.type = ACPI_TYPE_INTEGER}};
        struct acpi_object_list args = {.count = 1, .pointer = &arg};
        unsigned long long value;
        reg = i < ARRAY_SIZE(regs) ? regs[i] : 0x0f00 + i - ARRAY_SIZE(regs);
        arg.integer.value = reg;
        if (ACPI_FAILURE(acpi_evaluate_integer(handle, "ECRR", &args, &value)) || value > 255)
            return -EIO;
        pr_info("mechrevo_fan_power_probe: reg=0x%04x value=0x%02llx\n", reg, value);
        usleep_range(6000, 12000);
    }
    return 0;
}
static void __exit fan_power_exit(void) {}
module_init(fan_power_init);
module_exit(fan_power_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only fan flags and table snapshot for MECHREVO X6xR55xK");
