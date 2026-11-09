// SPDX-License-Identifier: GPL-2.0-only
#include "../ec-test-common.h"
#include "../fan-manual/wmi-ec.h"
static unsigned int table[96], split, enable, mode, custom, custom_flag, pwm[2];
static bool pending;
static unsigned int threshold;
static int restore(void)
{
    unsigned int v;
    int i, ret = 0;
    if (!pending) return 0;
    ret |= ec_test_mask(0x0751, 0x40, 0x40);
    ret |= ec_test_mask(0x07c6, 4, 0);
    for (i=0; i<96; i++) {
        ret |= ec_test_write(0x0f00+i, table[i]);
        if (ec_test_read(0x0f00+i, &v) || v != table[i]) ret = -EIO;
    }
    ret |= ec_test_mask(0x07c5, 0x80, split);
    ret |= ec_test_mask(0x07c6, 4, enable);
    ret |= ec_test_write(0x1804, pwm[0]);
    ret |= ec_test_write(0x1809, pwm[1]);
    ret |= ec_test_mask(0x0751, 0xf0, mode);
    ret |= ec_test_mask(0x0727, 0x40, custom);
    ret |= ec_test_mask(0x0726, 0x80, custom_flag);
    ret |= ec_test_mask(0x0741, 1, 0);
    if (!ret) pending=false;
    pr_info("mechrevo_curve_test: RESTORE result=%d\n", ret);
    return ret;
}
static int sample(unsigned int first, unsigned int second, bool settled)
{
    unsigned int h,l,h2,l2,t,a,b;
    if (ec_test_read(0x0464,&h) || ec_test_read(0x0465,&l) || ec_test_read(0x046c,&h2) || ec_test_read(0x046d,&l2) || ec_test_read(0x043e,&t) || ec_test_read(0x1804,&a) || ec_test_read(0x1809,&b)) return -EIO;
    pr_info("mechrevo_curve_test: target=%u/%u rpm=%u/%u raw=%u/%u temp=%u\n",first,second,(h<<8)|l,(h2<<8)|l2,a,b,t);
    if (t>=75) return -ERANGE;
    if (settled && ((first==100 && ((h<<8)|l)<3000) || (second==100 && ((h2<<8)|l2)<3000))) return -EOPNOTSUPP;
    return 0;
}
static int __init start(void)
{

    unsigned int v;
    int ret=ec_test_open(), i,j,k;
    if (ret) return ret;
    if (ec_test_read(0x043e,&v) || v>65) return -EBUSY;
    if (ec_test_read(0x0741,&v) || (v&1)) return -EBUSY;
    if (ec_test_read(0x0751,&mode) || (mode&0x40)) return -EBUSY;
    if (ec_test_read(0x07c5,&split) || ec_test_read(0x07c6,&enable) || (enable&4)) return -EBUSY;
    if (ec_test_read(0x0727,&custom) || (custom&0x40)) return -EBUSY;
    if (ec_test_read(0x1804,&pwm[0]) || ec_test_read(0x1809,&pwm[1])) return -EIO;
    for(i=0;i<96;i++) if(ec_test_read(0x0f00+i,&table[i])) return -EIO;
    if(ec_test_read(0x0726,&custom_flag)) return -EIO;
    if(ec_test_read(0x043e,&v)) return -EIO;
    threshold=v+3;
    pending=true;
    ret=ec_test_mask(0x0741,1,1);
    if (!ret) ret=ec_test_mask(0x0726,0x80,0x80);
    if (!ret) ret=ec_test_mask(0x0727,0x40,0x40);
    if (!ret) ret=ec_test_mask(0x0751,0xb0,0x00);
    if (!ret) ret=ec_test_mask(0x0751,0x40,0x40);
    for(i=0; !ret && i<2; i++) for(j=0; !ret && j<16; j++) {
        unsigned int up = j==0?20:j==15?255:i?40+j*4:j==1?threshold:75+(j-2)*3;
        unsigned int duty = j==0?0:i?120:j==1?80:j==2?160:200;
        ret=ec_test_mask(0x0f00+i*48+j,255,up);
        if(!ret && j<15) ret=ec_test_mask(0x0f10+i*48+j+1,255,up-3);
        if(!ret) ret=ec_test_mask(0x0f20+i*48+j,255,duty);
    }
    if(!ret) ret=ec_test_mask(0x07c5,0x80,0x80);
    if(!ret) ret=ec_test_mask(0x07c6,4,4);
    if(!ret) ret=ec_test_mask(0x0751,0x40,0);
    pr_info("mechrevo_curve_test: CURVE_READY cpu_threshold=%u gpu_fixed=60; no PWM writes\n",threshold);
    /* No immediate PWM command: the EC must use its table by itself. */
    for(k=0; !ret && k<36; k++) {
        msleep(2000);
        ret=sample(40,60,false);
    }
    pr_info("mechrevo_curve_test: EXECUTION result=%d; capability requires sustained RPM review\n",ret);
    if(restore()) return 0;
    return ret;
}
static void __exit stop(void) {restore();}
module_init(start); module_exit(stop);
MODULE_LICENSE("GPL");

MODULE_DESCRIPTION("Bounded firmware fan table validation with complete restoration");
