#!/usr/bin/env python3
"""Compile the actual lifecycle callbacks against an in-memory EC and kernel stubs."""
from pathlib import Path
import subprocess
import tempfile

source = Path('driver/mechrevo_control.c').read_text()
def function(name):
    import re
    match = re.search(r'^static (?:int|void) ' + name + r'\(', source, re.M)
    start = match.start()
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define BIT(n) (1U << (n))
#define DIRTY_GATE BIT(6)
#define DIRTY_CURVE BIT(5)
#define NOTIFY_DONE 0
#define container_of(ptr, type, member) ((type *)((char *)(ptr) - offsetof(type, member)))
struct device { void *data; };
struct wmi_device { struct device dev; };
struct notifier_block { int dummy; };
struct mr {
    int lock, fan_work;
    struct notifier_block reboot;
    struct device *dev, *hwmon;
    bool active, suspended, removing, manual_fans, curve_fans, curve_valid, curve_legacy;
    unsigned int fan_target[2], curve_points[10], dirty, mode, light, rgb[3], charge;
    int last_error;
};
static unsigned int ec[65536], pwm[2];
static int reads, writes, cancelled, pwm_writes[2];
static bool failed_read;
static void *dev_get_drvdata(struct device *dev) { return dev->data; }
static void mutex_lock(int *lock) { (void)lock; }
static void mutex_unlock(int *lock) { (void)lock; }
static void cancel_delayed_work_sync(int *work) { (void)work; cancelled++; }
static void unregister_reboot_notifier(struct notifier_block *nb) { (void)nb; }
static void hwmon_device_unregister(struct device *dev) { (void)dev; }
static int rd(struct mr *d, unsigned short reg, unsigned int *v) {
    (void)d; reads++; if (failed_read) return -EIO; *v = ec[reg]; return 0;
}
static int fan_io(unsigned int ch, unsigned int *v, bool write) {
    assert(ch < 2); if (write) { writes++; pwm_writes[ch]++; pwm[ch] = *v; } else { reads++; *v = pwm[ch]; } return 0;
}
static int mask(struct mr *d, unsigned short reg, unsigned int bits, unsigned int value) {
    (void)d; writes++; ec[reg] = (ec[reg] & ~bits) | value; return 0;
}
static int fan_restore(struct mr *d) { d->manual_fans = false; return 0; }
static int curve_mask(unsigned int reg, unsigned int bits, unsigned int value) {
    writes++; ec[reg] = (ec[reg] & ~bits) | (value & bits); return 0;
}
static int curve_byte(struct mr *d, unsigned int reg, unsigned int value) {
    (void)d; writes++; ec[reg] = value; return 0;
}
'''
main = r'''
int main(void) {
    struct mr d = { .active = true, .manual_fans = true, .dirty = 127 };
    struct wmi_device dev = { .dev = { .data = &d } };
    unsigned int before[65536];
    ec[0x0741] = 0x81; ec[0x0726] = 0x80; ec[0x0727] = 0x40; ec[0x07c6] = 4;
    ec[0x078c] = 4 << 5; ec[0x0769] = 20; ec[0x076a] = 30; ec[0x076b] = 40; ec[0x07a6] = 0x20;
    pwm[0] = 70; pwm[1] = 110;
    ec[0x0f06] = ec[0x0f36] = 85;
    for (int i = 0; i < 10; i++) {
        ec[0x0f21 + (i / 5) * 48 + i % 5] = 40 + (i % 5) * 20;
        ec[0x0f01 + (i / 5) * 48 + i % 5] = 40 + (i % 5) * 10;
    }
    memcpy(before, ec, sizeof(ec));
    assert(suspend_device(&dev.dev) == 0);
    assert(d.suspended && !d.manual_fans && cancelled == 1);
    assert(resume_device(&dev.dev) == 0);
    assert(!d.active && !d.dirty && !d.manual_fans && !d.suspended);
    assert(d.curve_fans && d.curve_valid && d.curve_points[4] == 60);
    assert(d.fan_target[0] == 35 && d.fan_target[1] == 55);
    assert(reads && !writes && !memcmp(before, ec, sizeof(ec)));
    /* Existing curve and gate must remain usable after reloading the driver. */
    assert(acquire(&d) == 0 && d.active && d.mode == 0xa0);
    assert(d.charge == 0x20 && d.light == 4 && d.rgb[2] == 40);
    assert(writes == 1 && !memcmp(before, ec, sizeof(ec)));
    writes = 0;
    assert(reboot_device(&d.reboot, 0, NULL) == NOTIFY_DONE);
    remove_device(&dev);
    assert(!writes && !memcmp(before, ec, sizeof(ec)));
    /* Invalid tables are not replaced with invented defaults. */
    ec[0x0f21] = 255;
    assert(read_fans(&d) == 0 && !d.curve_valid);
    failed_read = true;
    assert(resume_device(&dev.dev) == -EIO && d.last_error == -EIO);
    assert(!writes);
    failed_read = false;
    unsigned int nodes[] = {0, 0, 45, 72, 100, 0, 0, 41, 72, 100};
    memcpy(d.curve_points, nodes, sizeof(nodes));
    d.curve_valid = true; d.curve_fans = true;
    ec[0x0751] = 0xa0;
    ec[0x043e] = 65;
    pwm[0] = 77; pwm[1] = 88;
    int cpu_writes = pwm_writes[0], gpu_writes = pwm_writes[1];
    assert(curve_apply(&d) == 0 && !d.curve_legacy);
    assert(pwm[0] == 77 && pwm[1] == 88);
    assert(pwm_writes[0] == cpu_writes && pwm_writes[1] == gpu_writes);
    assert(ec[0x0751] == 0xa0);
    for (int fan = 0; fan < 2; fan++) {
        unsigned int offset = fan * 48;
        assert(ec[0x0f21 + offset] == 0 && ec[0x0f01 + offset] == 40);
        /* At 55C the outgoing 50C node still requests zero; at 65C
         * the outgoing 60C node requests its own nonzero percentage. */
        for (int temp = 21; temp < 100; temp++) {
            int slot = 0;
            while ((unsigned int)temp >= ec[0x0f00 + offset + slot]) slot++;
            unsigned int wanted = temp < 40 ? 0 : nodes[fan * 5 + (temp >= 80 ? 4 : (temp - 40) / 10)];
            assert(ec[0x0f20 + offset + slot] == wanted * 2);
        }
    }
    assert(read_fans(&d) == 0 && d.curve_valid && !d.curve_legacy);
    assert(!memcmp(d.curve_points, nodes, sizeof(nodes)));
    puts("PASS: suspend/resume/reboot/unload preserve EC; retained state can be acquired; invalid reads reported");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / 'lifecycle.c'
    c.write_text(prefix + '\n'.join(function(n) for n in ['read_fans', 'curve_apply', 'acquire', 'reboot_device', 'remove_device', 'suspend_device', 'resume_device']) + main)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-sign-compare', '-Werror', str(c), '-o', str(Path(tmp) / 'test')], check=True)
    subprocess.run([str(Path(tmp) / 'test')], check=True)
