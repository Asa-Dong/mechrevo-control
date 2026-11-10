// SPDX-License-Identifier: GPL-2.0-only
/* Fixed 80 -> 60 -> cancel transaction test. No arbitrary register API.
 * Protocol: Leo-stro/mechrevo-charge-limit (MIT), FINDINGS.md, 2026-11-10.
 * Requires an initially unrestricted EC and restores that state before returning.
 */
#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/wmi.h>

static bool pending;
static bool platform_pending;
static acpi_handle handle;

static int io(u16 reg, unsigned int *value, bool write)
{
    u8 bytes[40] = {0};
    struct acpi_buffer in = {sizeof(bytes), bytes}, out = {ACPI_ALLOCATE_BUFFER, NULL};
    union acpi_object *obj;
    acpi_status status;
    int ret = -EIO;
    bytes[0] = reg & 255;
    bytes[1] = reg >> 8;
    bytes[2] = write ? *value : 0;
    bytes[5] = write ? 0 : 1;
    status = wmi_evaluate_method("ABBC0F6F-8EA1-11D1-00A0-C90629100000", 0, 4, &in, &out);
    obj = out.pointer;
    if (ACPI_SUCCESS(status) && obj && obj->type == ACPI_TYPE_BUFFER &&
        obj->buffer.length >= 4 && memcmp(obj->buffer.pointer, "\xfe\xfe\xfe\xfe", 4)) {
        if (!write)
            *value = obj->buffer.pointer[0];
        ret = 0;
    }
    kfree(out.pointer);
    usleep_range(6000, 12000);
    return ret;
}
static int read_byte(u16 reg, unsigned int *v) { return io(reg, v, false); }
static int write_byte(u16 reg, unsigned int v)
{
    union acpi_object arg[] = {
        {.integer = {.type = ACPI_TYPE_INTEGER, .value = reg}},
        {.integer = {.type = ACPI_TYPE_INTEGER, .value = v}},
    };
    struct acpi_object_list args = {.count = 2, .pointer = arg};
    acpi_status status = acpi_evaluate_object(handle, "ECRW", &args, NULL);
    usleep_range(6000, 12000);
    return ACPI_FAILURE(status) ? -EIO : 0;
}

static int platform_restore(void)
{
    unsigned int v;
    int i;
    if (!platform_pending)
        return 0;
    for (i = 0; i < 3; i++) {
        if (!write_byte(0x07c3, 7) && !read_byte(0x07c3, &v) && v == 7) {
            platform_pending = false;
            return 0;
        }
        msleep(100);
    }
    pr_err("mechrevo_charge_limit_test: PLATFORM RESTORE FAILED\n");
    return -EIO;
}
static int set_limit(unsigned int target)
{
    unsigned int gate, live, stored, platform, rom;
    int ret;
    if (write_byte(0x07b9, target))
        return -EIO;
    platform_pending = true;
    ret = write_byte(0x07c3, 4);
    if (!ret) {
        msleep(2500);
        ret = read_byte(0x07c3, &platform);
        if (!ret && platform != 4)
            ret = -EIO;
        if (!ret && !read_byte(0x0742, &gate) && !read_byte(0x07b9, &live) &&
            !read_byte(0x087f, &stored))
            pr_info("mechrevo_charge_limit_test: HOLD target=%u gate=0x%02x live=0x%02x stored=0x%02x platform=0x%02x\n",
                    target, gate, live, stored, platform);
    }
    if (platform_restore())
        return -EIO;
    if (ret)
        return ret;
    msleep(7000);
    if (read_byte(0x0742, &gate) || read_byte(0x07b9, &live) ||
        read_byte(0x087f, &stored) || read_byte(0x07c3, &platform) || read_byte(0x0770, &rom))
        return -EIO;
    pr_info("mechrevo_charge_limit_test: target=%u gate=0x%02x live=0x%02x stored=0x%02x platform=0x%02x rom=0x%02x\n",
            target, gate, live, stored, platform, rom);
    return ((live & 0x7f) == target && (stored & 0x7f) == target &&
            !!(gate & BIT(2)) == !!target && platform == 7 && rom == 255) ? 0 : -ENODATA;
}
static int restore_limit(void)
{
    int ret = platform_restore();
    if (ret || !pending)
        return ret;
    ret = set_limit(0);
    if (!ret)
        pending = false;
    pr_info("mechrevo_charge_limit_test: RESTORE result=%d pending=%d\n", ret, pending);
    return ret;
}
static int __init limit_init(void)
{
    static const u16 regs[] = {0x0740, 0x0742, 0x0770, 0x07c3, 0x07b9, 0x087f};
    unsigned int v[ARRAY_SIZE(regs)], i;
    int ret;
    if (!dmi_match(DMI_SYS_VENDOR, "MECHREVO") ||
        !dmi_match(DMI_BOARD_NAME, "JIAOLONG Series-X6xR55xK"))
        return -ENODEV;
    if (ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB_.INOU", &handle)))
        return -ENODEV;
    for (i = 0; i < ARRAY_SIZE(regs); i++) {
        if (read_byte(regs[i], &v[i]))
            return -EIO;
    }
    if (v[0] != 0x1a || (v[1] & BIT(2)) || v[2] != 255 || v[3] != 7 ||
        v[4] != 0 || v[5] != 0)
        return -ENODEV;
    pending = true;
    ret = set_limit(80);
    pr_info("mechrevo_charge_limit_test: ACPI 80 result=%d\n", ret);
    if (!ret || ret == -ENODATA)
        ret = set_limit(60);
    pr_info("mechrevo_charge_limit_test: TEST result=%d\n", ret);
    /* Remain loaded if cleanup fails, so module exit can retry. */
    restore_limit();
    return 0;
}
static void __exit limit_exit(void)
{
    restore_limit();
    pr_info("mechrevo_charge_limit_test: unloaded pending=%d platform_pending=%d\n",
            pending, platform_pending);
}
module_init(limit_init);
module_exit(limit_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Reversible fixed 80/60/cancel charging limit transaction test");
