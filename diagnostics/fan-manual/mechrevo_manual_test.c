// SPDX-License-Identifier: GPL-2.0-only
#include "../ec-test-common.h"
#include "wmi-ec.h"
static unsigned int mode, split, pwm[2];
static bool pending;
static int restore(void)
{
    int ret = 0;
    if (!pending) return 0;
    ret |= ec_test_mask(0x0751, 0x40, 0x40);
    ret |= ec_test_write(0x1804, pwm[0]);
    ret |= ec_test_write(0x1809, pwm[1]);
    ret |= ec_test_mask(0x07c5, 0x80, split);
    ret |= ec_test_mask(0x0751, 0xf0, mode);
    ret |= ec_test_mask(0x0741, 1, 0);
    if (!ret) pending=false;
    pr_info("mechrevo_manual_test: RESTORE result=%d\n", ret);
    return ret;
}
static int sample(unsigned int first, unsigned int second, bool settled)
{
    unsigned int h,l,h2,l2,t,a,b;
    if (ec_test_read(0x0464,&h) || ec_test_read(0x0465,&l) || ec_test_read(0x046c,&h2) || ec_test_read(0x046d,&l2) || ec_test_read(0x043e,&t) || ec_test_read(0x1804,&a) || ec_test_read(0x1809,&b)) return -EIO;
    pr_info("mechrevo_manual_test: target=%u/%u rpm=%u/%u raw=%u/%u temp=%u\n",first,second,(h<<8)|l,(h2<<8)|l2,a,b,t);
    if (t>=75) return -ERANGE;
    if (settled && ((first==100 && ((h<<8)|l)<3000) || (second==100 && ((h2<<8)|l2)<3000))) return -EOPNOTSUPP;
    return 0;
}
static int __init start(void)
{
    static const unsigned int targets[][2]={{100,100},{50,100},{100,50},{30,30}};
    unsigned int v;
    int ret=ec_test_open(), i,k;
    if (ret) return ret;
    if (ec_test_read(0x043e,&v) || v>65) return -EBUSY;
    if (ec_test_read(0x0741,&v) || (v&1)) return -EBUSY;
    if (ec_test_read(0x0751,&mode) || (mode&0x40)) return -EBUSY;
    if (ec_test_read(0x1804,&pwm[0]) || ec_test_read(0x1809,&pwm[1])) return -EIO;
    if(ec_test_read(0x07c5,&split)) return -EIO;
    pending=true;
    ret=ec_test_mask(0x0741,1,1);
    if(!ret) ret=ec_test_mask(0x07c5,0x80,0x80);
    for(i=0; !ret && i<ARRAY_SIZE(targets); i++) {
        for(k=0; !ret && k<12; k++) {
            ret=ec_test_write(0x1804,targets[i][0]*2);
            if(!ret) ret=ec_test_write(0x1809,targets[i][1]*2);
            msleep(800);
            if(!ret && k%3==2) ret=sample(targets[i][0],targets[i][1],k==11);
        }
    }
    pr_info("mechrevo_manual_test: TEST result=%d\n",ret);
    if(restore()) return 0;
    return ret;
}
static void __exit stop(void) {restore();}
module_init(start); module_exit(stop);
MODULE_LICENSE("GPL");

MODULE_DESCRIPTION("Bounded manual fan validation with custom profile and full table restoration");
