// SPDX-License-Identifier: GPL-2.0-only
/* Fixed, read-only charge-limit survey for this board. No write commands.
 * WMI mailbox format: TUXEDO uniwill_wmi.c and our fan_wmi implementation.
 */
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/wmi.h>

static const u16 registers[] = {0x0740, 0x0742, 0x0770, 0x07c3,
                               0x07a6, 0x07b9, 0x07d0, 0x087f};

static int mailbox_read(u16 reg, unsigned int *value)
{
    u8 bytes[40] = {0};
    struct acpi_buffer in = {sizeof(bytes), bytes}, out = {ACPI_ALLOCATE_BUFFER, NULL};
    union acpi_object *obj;
    acpi_status status;
    int ret = -EIO;
    bytes[0] = reg & 255;
    bytes[1] = reg >> 8;
    bytes[5] = 1; /* Read only. */
    status = wmi_evaluate_method("ABBC0F6F-8EA1-11D1-00A0-C90629100000", 0, 4, &in, &out);
    obj = out.pointer;
    if (ACPI_SUCCESS(status) && obj && obj->type == ACPI_TYPE_BUFFER &&
        obj->buffer.length >= 4 && memcmp(obj->buffer.pointer, "\xfe\xfe\xfe\xfe", 4)) {
        *value = obj->buffer.pointer[0];
        ret = 0;
    }
    kfree(out.pointer);
    usleep_range(6000, 12000);
    return ret;
}

static int __init charge_probe_init(void)
{
    acpi_handle handle;
    unsigned int i, sample;
    if (!dmi_match(DMI_SYS_VENDOR, "MECHREVO") ||
        !dmi_match(DMI_BOARD_NAME, "JIAOLONG Series-X6xR55xK"))
        return -ENODEV;
    if (ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB_.INOU", &handle)))
        return -ENODEV;
    for (sample = 0; sample < 3; sample++) {
        for (i = 0; i < ARRAY_SIZE(registers); i++) {
            union acpi_object arg = {.integer = {.type = ACPI_TYPE_INTEGER,
                                                .value = registers[i]}};
            struct acpi_object_list args = {.count = 1, .pointer = &arg};
            unsigned long long direct;
            unsigned int mailbox;
            acpi_status status = acpi_evaluate_integer(handle, "ECRR", &args, &direct);
            usleep_range(6000, 12000);
            if (ACPI_FAILURE(status) || direct > 255 || mailbox_read(registers[i], &mailbox))
                return -EIO;
            pr_info("mechrevo_charge_probe: sample=%u reg=0x%04x ECRR=0x%02llx WMI=0x%02x\n",
                    sample, registers[i], direct, mailbox);
        }
        if (sample < 2)
            msleep(1100);
    }
    return 0;
}

static void __exit charge_probe_exit(void)
{
    pr_info("mechrevo_charge_probe: unloaded (read-only)\n");
}
module_init(charge_probe_init);
module_exit(charge_probe_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only charge limit survey for MECHREVO X6xR55xK");
