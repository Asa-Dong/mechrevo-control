// SPDX-License-Identifier: GPL-2.0-only
/* WMI event observer with temporary manual gate; no input injection. */
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/wmi.h>
#include "../ec-test-common.h"
static unsigned int saved_gate;
static bool gate_pending;
static int restore_gate(void)
{
    int i;
    if (!gate_pending) return 0;
    for (i=0; i<3; i++) {
        if (!ec_test_mask(0x0741, 1, saved_gate)) { gate_pending=false; return 0; }
        msleep(100);
    }
    return -EIO;
}
static int observed_probe(struct wmi_device *dev, const void *context)
{
    pr_info("mechrevo_key_probe: observer attached\n");
    return 0;
}
static void observed_notify(struct wmi_device *dev, union acpi_object *obj)
{
    if (obj && obj->type == ACPI_TYPE_INTEGER)
        pr_info("mechrevo_key_probe: event=0x%llx\n", obj->integer.value);
    else
        pr_info("mechrevo_key_probe: non-integer event\n");
}
static const struct wmi_device_id ids[] = {
    { "ABBC0F72-8EA1-11D1-00A0-C90629100000", NULL }, { }
};
static struct wmi_driver observer = {
    .driver = { .name = "mechrevo-key-observer" },
    .id_table = ids,
    .probe = observed_probe,
    .notify = observed_notify,
};
static int __init start(void)
{
    if (!dmi_match(DMI_SYS_VENDOR, "MECHREVO") ||
        !dmi_match(DMI_BOARD_NAME, "JIAOLONG Series-X6xR55xK")) return -ENODEV;
    {
        unsigned int value;
        int ret = ec_test_open();
        if (ret) return ret;
        if (ec_test_read(0x0741, &value)) return -EIO;
        saved_gate = value & 1;
        ret = wmi_driver_register(&observer);
        if (ret) return ret;
        gate_pending = true;
        ret = ec_test_mask(0x0741, 1, 1);
        if (ret) {
            if (restore_gate()) {
                pr_err("mechrevo_key_probe: RESTORE_FAILED, retained for cleanup\n");
                return 0;
            }
            wmi_driver_unregister(&observer);
            return ret;
        }
        pr_info("mechrevo_key_probe: manual gate enabled, original=%u\n", saved_gate);
        return 0;
    }
}
static void __exit stop(void)
{
    int ret = restore_gate();
    pr_info("mechrevo_key_probe: gate_restore=%d\n", ret);
    wmi_driver_unregister(&observer);
    pr_info("mechrevo_key_probe: observer removed\n");
}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Temporary read-only WMI mode key observer");
