/* SPDX-License-Identifier: GPL-2.0-only */
/* Protocol: TUXEDO uniwill_wmi.c; bounded buffer validation added. */
#include <linux/wmi.h>
static int fan_wmi(unsigned int reg, unsigned int *value, bool write)
{
    u8 bytes[40]={0};
    struct acpi_buffer in={sizeof(bytes),bytes},out={ACPI_ALLOCATE_BUFFER,NULL};
    union acpi_object *obj;
    acpi_status status;
    int ret=-EIO;
    bytes[0]=reg&255; bytes[1]=reg>>8;
    bytes[2]=write?*value:0; bytes[5]=write?0:1;
    status=wmi_evaluate_method("ABBC0F6F-8EA1-11D1-00A0-C90629100000",0,4,&in,&out);
    obj=out.pointer;
    if(ACPI_SUCCESS(status) && obj && obj->type==ACPI_TYPE_BUFFER && obj->buffer.length>=4 &&
       memcmp(obj->buffer.pointer,"\xfe\xfe\xfe\xfe",4)) {
        if (!write) *value=obj->buffer.pointer[0];
        ret=0;
    }
    kfree(out.pointer);
    return ret;
}
static int fan_read(unsigned int reg,unsigned int *v) {return reg>=0x1800?fan_wmi(reg,v,false):ec_test_read(reg,v);}
static int fan_write(unsigned int reg,unsigned int v) {return fan_wmi(reg,&v,true);}
static int fan_mask(unsigned int reg,unsigned int bits,unsigned int value)
{
    unsigned int v;
    if(fan_read(reg,&v) || fan_write(reg,(v&~bits)|(value&bits)) || fan_read(reg,&v)) return -EIO;
    return (v&bits)==(value&bits)?0:-EIO;
}
#define ec_test_read fan_read
#define ec_test_write fan_write
#define ec_test_mask fan_mask
