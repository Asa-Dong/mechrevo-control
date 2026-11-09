// SPDX-License-Identifier: GPL-2.0-only
/* Fixed-register read-only sampling, stops automatically after five minutes. */
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>

static acpi_handle handle;
static unsigned long deadline;
static const unsigned int registers[] = { 0x0751, 0x0741, 0x0727, 0x07c6 };
static unsigned int previous[ARRAY_SIZE(registers)];
static bool first = true;
static void sample(struct work_struct *work);
static DECLARE_DELAYED_WORK(poll_work, sample);

static void sample(struct work_struct *work)
{
    unsigned int values[ARRAY_SIZE(registers)], i;
    bool changed = first;
    for (i = 0; i < ARRAY_SIZE(registers); i++) {
        union acpi_object arg = { .integer = {
            .type = ACPI_TYPE_INTEGER, .value = registers[i] } };
        struct acpi_object_list args = { .count = 1, .pointer = &arg };
        unsigned long long result;
        if (ACPI_FAILURE(acpi_evaluate_integer(handle, "ECRR", &args, &result)) || result > 255) {
            pr_err("mechrevo_native_watch: read failed; sampling stopped\n");
            return;
        }
        values[i] = result;
        changed |= values[i] != previous[i];
    }
    if (changed)
        pr_info("mechrevo_native_watch: mode=%02x gate=%02x custom=%02x curve=%02x\n",
                values[0], values[1], values[2], values[3]);
    memcpy(previous, values, sizeof(values));
    first = false;
    if (time_before(jiffies, deadline))
        schedule_delayed_work(&poll_work, msecs_to_jiffies(500));
    else
        pr_info("mechrevo_native_watch: sampling complete\n");
}

static int __init start(void)
{
    if (!dmi_match(DMI_SYS_VENDOR, "MECHREVO") ||
        !dmi_match(DMI_BOARD_NAME, "JIAOLONG Series-X6xR55xK")) return -ENODEV;
    if (ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB_.INOU", &handle))) return -ENODEV;
    deadline = jiffies + msecs_to_jiffies(300000);
    schedule_delayed_work(&poll_work, 0);
    return 0;
}
static void __exit stop(void)
{
    cancel_delayed_work_sync(&poll_work);
    pr_info("mechrevo_native_watch: removed (no EC writes)\n");
}
module_init(start);
module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only native mode key state sampler");
