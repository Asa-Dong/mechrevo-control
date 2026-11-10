#!/usr/bin/env python3
"""Exercise the driver's actual charge transactions with EC faults and firmware ticks."""
from pathlib import Path
import subprocess
import tempfile

source = Path('driver/mechrevo_control.c').read_text()
state = source[source.index('struct mr_charge {'):source.index('struct mr {')]
functions = source[source.index('static int charge_read('):source.index('static int brightness(')]
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
typedef unsigned short u16;
#define DIRTY_CHARGE 8
#define DMI_BIOS_VERSION 0
#define dev_err(...) ((void)0)
static unsigned int ec[65536], writes, fail_reg, fail_left;
static bool io_failure, drop_gate, bios_ok = true;
static unsigned long jiffies, last_tick;
#define msecs_to_jiffies(ms) (ms)
#define time_before(a, b) ((a) < (b))
static bool dmi_match(int key, const char *name) { return bios_ok; }
static void tick(void) {
    if (ec[0x07c3] == 4) {
        ec[0x087f] = ec[0x07b9] & 127;
        if (!drop_gate) ec[0x0742] |= 4;
        else ec[0x0742] &= ~4U;
        if ((ec[0x07b9] & 127) && (ec[0x07b9] & 127) <= 94) ec[0x07b9] |= 128;
    } else {
        ec[0x0742] &= ~4U;
        ec[0x07b9] &= 127;
    }
}
static void msleep(unsigned int ms) {
    jiffies += ms;
    while (jiffies - last_tick >= 1000) { last_tick += 1000; tick(); }
}
'''
stubs = r'''
struct mr { unsigned int dirty; bool charge_saved; struct mr_charge saved_charge; };
static int rd(struct mr *d, u16 reg, unsigned int *v) {
    if (io_failure) return -EIO;
    *v = reg == 0x087f ? 255 : ec[reg]; return 0;
}
static int wr(struct mr *d, u16 reg, unsigned int v) {
    assert(reg != 0x0770 && reg != 0x087f); /* Never write ROM ID or storage. */
    if (reg == 0x07c3 && v == 7) assert(ec[0x087f] == (ec[0x07b9] & 127));
    writes++;
    if (reg == fail_reg && fail_left) { fail_left--; return -EIO; }
    ec[reg] = v; return 0;
}
static int fan_wmi(u16 reg, unsigned int *v, bool write) {
    assert(reg == 0x087f && !write);
    if (io_failure) return -EIO;
    *v = ec[reg]; return 0;
}
static int mask(struct mr *d, u16 reg, unsigned int bits, unsigned int v) {
    unsigned int old;
    if (rd(d, reg, &old)) return -EIO;
    return wr(d, reg, (old & ~bits) | (v & bits));
}
static void reset(struct mr *d) {
    memset(d, 0, sizeof(*d)); memset(ec, 0, sizeof(ec));
    ec[0x0770] = 255; ec[0x07c3] = 7; ec[0x07a6] = 0xe1;
    fail_left = fail_reg = writes = 0; io_failure = drop_gate = false; bios_ok = true;
    jiffies = last_tick = 0;
}
'''
main = r'''
int main(void) {
    struct mr d; struct mr_charge s;
    reset(&d);
    assert(charge_set(&d, 80) == 0);
    assert(jiffies == 1000); /* Return on the first firmware update, no fixed 2.5s sleep. */
    assert(charge_read(&d, &s) == 0 && charge_valid(&s));
    assert(s.limit == 80 && s.resume == 75 && s.gate && s.reached);
    assert(ec[0x07a6] == 0xc1 && ec[0x0770] == 255);
    unsigned int previous_writes = writes;
    assert(charge_set(&d, 80) == 0 && writes == previous_writes);
    assert(charge_set(&d, 60) == 0 && ec[0x07d0] == 55);
    assert(charge_restore(&d) == 0 && !d.charge_saved);
    assert(ec[0x07a6] == 0xe1 && ec[0x07b9] == 0 && ec[0x087f] == 0 && ec[0x07c3] == 7);
    reset(&d);
    assert(charge_set(&d, 60) == 0 && charge_set(&d, 0) == 0);
    assert(ec[0x07d0] == 0 && ec[0x087f] == 0 && ec[0x07c3] == 7 && !(ec[0x0742] & 4));
    /* Actual reboot keeps thresholds but disables platform gating. */
    for (unsigned int limit = 60; limit <= 80; limit += 20) {
        reset(&d); ec[0x07a6] = 1;
        ec[0x07b9] = ec[0x087f] = limit; ec[0x07d0] = limit - 5;
        assert(charge_read(&d, &s) == 0 && charge_dormant(&s) && !charge_valid(&s));
        assert(charge_set(&d, limit) == 0 && writes > 0);
        assert(charge_read(&d, &s) == 0 && charge_valid(&s));
        assert(charge_restore(&d) == 0);
        assert(charge_read(&d, &s) == 0 && charge_dormant(&s));
        assert(charge_set(&d, 0) == 0 && !ec[0x087f] && ec[0x07c3] == 7);
        reset(&d); ec[0x07a6] = 1;
        ec[0x07b9] = ec[0x087f] = limit; ec[0x07d0] = limit - 5;
        fail_reg = 0x07b9; fail_left = 1;
        assert(charge_set(&d, limit) == -EIO && !d.charge_saved);
        assert(charge_read(&d, &s) == 0 && charge_dormant(&s));
        ec[0x07d0]++;
        unsigned int previous = writes;
        assert(charge_set(&d, limit) == -EOPNOTSUPP && writes == previous);
    }
    const unsigned int addresses[] = {0x07a6, 0x07d0, 0x07b9, 0x07c3};
    for (unsigned int i = 0; i < 4; i++) {
        reset(&d); fail_reg = addresses[i]; fail_left = 1;
        assert(charge_set(&d, 60) == -EIO);
        assert(!d.charge_saved && !(d.dirty & DIRTY_CHARGE));
        assert(ec[0x07a6] == 0xe1 && ec[0x07c3] == 7 && ec[0x07b9] == 0 && ec[0x087f] == 0);
    }
    reset(&d); drop_gate = true;
    assert(charge_set(&d, 80) == -EIO && !d.charge_saved && ec[0x07c3] == 7);
    reset(&d); fail_reg = 0x07c3; fail_left = 2;
    assert(charge_set(&d, 80) == -EIO && d.charge_saved && (d.dirty & DIRTY_CHARGE));
    assert(charge_restore(&d) == 0 && !d.charge_saved);
    reset(&d); io_failure = true;
    assert(charge_set(&d, 60) == -EIO && !writes);
    reset(&d); ec[0x0770] = 4;
    assert(charge_set(&d, 60) == -EOPNOTSUPP && !writes);
    reset(&d); bios_ok = false;
    assert(charge_set(&d, 60) == -EOPNOTSUPP && !writes);
    reset(&d); ec[0x087f] = 80;
    assert(charge_set(&d, 60) == -EOPNOTSUPP && !writes);
    reset(&d);
    assert(charge_set(&d, 50) == -EINVAL && !writes);
    puts("PASS: actual charge transactions, 80/75 and 60/55, cancellation, readback, rollback and fault boundaries");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / 'charge.c'
    c.write_text(prefix + state + stubs + functions + main)
    binary = Path(tmp) / 'charge'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Wno-unused-parameter',
                    '-Werror', str(c), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
