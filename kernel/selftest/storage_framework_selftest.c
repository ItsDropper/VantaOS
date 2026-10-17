#include "block_device.h"
#include "dma.h"
#include "serial.h"
#include <stdint.h>
#include <stddef.h>

#ifdef VANTAOS_SELF_TEST
static uint8_t mock_storage[8U * 512U];
static block_device_t mock_device;
static block_device_t duplicate_device;
static int unregister_was_busy;

static block_status_t mock_read(block_device_t* device,uint64_t lba,
                                uint32_t count,void* output)
{
    uint8_t* dst=(uint8_t*)output;
    const uint8_t* src=mock_storage+(size_t)lba*512U;
    size_t bytes=(size_t)count*512U;
    for(size_t i=0;i<bytes;i++)dst[i]=src[i];
    unregister_was_busy=block_device_unregister(device)==BLOCK_STATUS_BUSY;
    return BLOCK_STATUS_OK;
}
static block_status_t mock_write(block_device_t* device,uint64_t lba,
                                 uint32_t count,const void* input)
{
    (void)device;(void)lba;(void)count;(void)input;
    return BLOCK_STATUS_OK;
}
static const block_device_ops_t mock_ops={mock_read,mock_write,0};

void storage_framework_selftest(void)
{
    for(size_t i=0;i<sizeof(mock_storage);i++)mock_storage[i]=(uint8_t)(i^0x5AU);
    mock_device=(block_device_t){.name="selftest0",.sector_count=8,
        .sector_size=512,.flags=BLOCK_DEVICE_READ_ONLY,.ops=&mock_ops};
    duplicate_device=(block_device_t){.name="selftest0",.sector_count=8,
        .sector_size=512,.ops=&mock_ops};
    uint8_t output[1024];
    int ok=block_device_register(&mock_device)==BLOCK_STATUS_OK;
    ok=ok&&block_device_count()==1U;
    ok=ok&&block_device_find("selftest0")==&mock_device;
    ok=ok&&block_device_get(0)==&mock_device;
    ok=ok&&block_device_get(1)==0;
    ok=ok&&block_device_register(&duplicate_device)==BLOCK_STATUS_BUSY;
    ok=ok&&block_device_read(&mock_device,1,2,output)==BLOCK_STATUS_OK;
    ok=ok&&unregister_was_busy;
    for(size_t i=0;i<sizeof(output);i++)
        ok=ok&&output[i]==mock_storage[512U+i];
    ok=ok&&block_device_read(&mock_device,7,2,output)==BLOCK_STATUS_OUT_OF_RANGE;
    ok=ok&&block_device_read(&mock_device,0,0,output)==BLOCK_STATUS_INVALID;
    ok=ok&&block_device_write(&mock_device,0,1,output)==BLOCK_STATUS_READ_ONLY;
    ok=ok&&block_device_flush(&mock_device)==BLOCK_STATUS_UNSUPPORTED;
    ok=ok&&block_device_unregister(&mock_device)==BLOCK_STATUS_OK;
    ok=ok&&block_device_count()==0U&&!mock_device.online;
    dma_page_t page={0};
    int dma_ok=dma_alloc_page(&page)&&dma_page_is_valid(&page);
    if(dma_ok) {
        const uint8_t* bytes=(const uint8_t*)page.virtual_address;
        for(size_t i=0;i<page.size;i++)dma_ok=dma_ok&&bytes[i]==0;
        dma_free_page(&page);
        dma_ok=dma_ok&&!page.virtual_address&&!page.physical_address&&!page.size;
    }
    ok=ok&&dma_ok;
    serial_write(ok?"VANTAOS_STORAGE_FRAMEWORK_OK\n":
                    "VANTAOS_STORAGE_FRAMEWORK_FAIL\n");
}
#endif
