// SPDX-License-Identifier: GPL-2.0-only
/* Save inactive table and restore it after the controlled experiment. */
#include "../ec-test-common.h"
#include "../fan-manual/wmi-ec.h"
static unsigned int table[96], flags[6];
static const unsigned int regs[] = {0x0726,0x0727,0x07c5,0x07c6,0x0751,0x0741};
static const unsigned int masks[] = {0x80,0x40,0x80,4,0xf0,1};
static int __init start(void)
{
    unsigned int i;
    int ret=ec_test_open();
    if(ret) return ret;
    for(i=0;i<6;i++) if(fan_read(regs[i],&flags[i])) return -EIO;
    if(flags[3]&4) return -EBUSY;
    for(i=0;i<96;i++) if(fan_read(0x0f00+i,&table[i])) return -EIO;
    pr_info("mechrevo_table_backup: saved inactive table and flags\n");
    return 0;
}
static void __exit stop(void)
{
    unsigned int i,v;
    if(fan_read(0x07c6,&v) || (v&4)) {
        pr_err("mechrevo_table_backup: RESTORE_SKIPPED curve still enabled\n"); return;
    }
    for(i=0;i<96;i++) {
        if(fan_read(0x0f00+i,&v) || (v!=table[i] && fan_write(0x0f00+i,table[i])) ||
           fan_read(0x0f00+i,&v) || v!=table[i]) {
            pr_err("mechrevo_table_backup: RESTORE_FAILED table index=%u\n",i); return;
        }
    }
    for(i=0;i<6;i++) if(fan_mask(regs[i],masks[i],flags[i])) {
        pr_err("mechrevo_table_backup: RESTORE_FAILED flags index=%u\n",i); return;
    }
    pr_info("mechrevo_table_backup: restored all 96 table bytes and control flags\n");
}
module_init(start); module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Inactive EC fan table backup for bounded experiment");
