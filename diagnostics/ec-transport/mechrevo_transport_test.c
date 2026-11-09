// SPDX-License-Identifier: GPL-2.0-only
#include "../ec-test-common.h"
#include <linux/wmi.h>
static int __init start(void)
{
    const unsigned int regs[]={0x726,0x727,0x740,0x741,0x751,0x78e,0x7c5,0x7c6,0xf20,0xf50,0x1804,0x1809};
    int i,ret=ec_test_open();
    if(ret) return ret;
    for(i=0;i<ARRAY_SIZE(regs);i++) {
        u8 bytes[40]={0};
        struct acpi_buffer in={sizeof(bytes),bytes},out={ACPI_ALLOCATE_BUFFER,NULL};
        union acpi_object *obj;
        unsigned int mmio=0;
        acpi_status status;
        bytes[0]=regs[i]&255; bytes[1]=regs[i]>>8; bytes[5]=1;
        ec_test_read(regs[i],&mmio);
        status=wmi_evaluate_method("ABBC0F6F-8EA1-11D1-00A0-C90629100000",0,4,&in,&out);
        obj=out.pointer;
        if(ACPI_SUCCESS(status) && obj && obj->type==ACPI_TYPE_BUFFER && obj->buffer.length>=4)
            pr_info("mechrevo_transport_test: %04x MMIO=%02x WMI=%*ph\n",regs[i],mmio,4,obj->buffer.pointer);
        else pr_info("mechrevo_transport_test: %04x MMIO=%02x WMI error status=%u type=%u\n",regs[i],mmio,status,obj?obj->type:0);
        kfree(out.pointer);
    }
    for (i=0; i<6; i++) {
        u8 block[16]; unsigned int j,v;
        for(j=0;j<16;j++) {
            if(ec_test_read(0xf00+i*16+j,&v)) return -EIO;
            block[j]=v;
        }
        pr_info("mechrevo_transport_test: TABLE %04x=%*ph\n",0xf00+i*16,16,block);
    }
    return 0;
}
static void __exit stop(void) {}
module_init(start); module_exit(stop);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only comparison of fixed vendor EC registers via ACPI and WMI");
