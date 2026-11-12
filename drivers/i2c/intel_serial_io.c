/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "i2c.h"

#include "acpi.h"
#include "acpi_aml.h"
#include "designware.h"
#include "hid_i2c.h"
#include "intel_sunrisepoint.h"
#include "mmio.h"
#include "pci.h"
#include "serial.h"
#include "spinlock.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define INTEL_VENDOR_ID 0x8086U
#define I2C_PCI_CLASS 0x0CU
#define I2C_PCI_SUBCLASS 0x80U
#define I2C_REGISTER_WINDOW 0x1000U
#define I2C_MAX_CONTROLLERS 8U
#define AML_DEVICE_LIMIT 128U
#define AML_STAGING_LIMIT 128U

typedef struct
{
    i2c_dw_bus_t dw;
    i2c_bus_t bus;
    const struct pci_device *pci;
    volatile uint8_t *registers;
    spinlock_t lock;
    char acpi_path[ACPI_AML_PATH_MAX];
    uint8_t active;
} intel_i2c_controller_t;

static intel_i2c_controller_t controllers[I2C_MAX_CONTROLLERS];
static acpi_aml_device_t aml_devices[AML_DEVICE_LIMIT];
static acpi_aml_device_t aml_staging[AML_STAGING_LIMIT];
static uint32_t aml_device_count;
static uint32_t aml_staging_count;
static uint8_t aml_staging_overflow;
static uint8_t aml_namespace_overflow;
static uint8_t initialized;

static uint32_t dw_mmio_read(void *context,uint32_t offset)
{
    volatile uint8_t *base=(volatile uint8_t*)context;
    return *(volatile uint32_t*)(base+offset);
}

static void dw_mmio_write(void *context,uint32_t offset,uint32_t value)
{
    volatile uint8_t *base=(volatile uint8_t*)context;
    *(volatile uint32_t*)(base+offset)=value;
}

static int path_equal(const char *a,const char *b)
{
    if(!a||!b) return 0;
    if(*a=='\\') ++a;
    if(*b=='\\') ++b;
    for(;;)
    {
        const char *a_end=a,*b_end=b;
        while(*a_end&&*a_end!='.') ++a_end;
        while(*b_end&&*b_end!='.') ++b_end;
        while(a_end>a&&a_end[-1]=='_') --a_end;
        while(b_end>b&&b_end[-1]=='_') --b_end;
        size_t a_length=(size_t)(a_end-a),b_length=(size_t)(b_end-b);
        if(a_length!=b_length) return 0;
        for(size_t i=0U;i<a_length;++i)
        {
            char ca=a[i],cb=b[i];
            if(ca>='a'&&ca<='z') ca=(char)(ca-'a'+'A');
            if(cb>='a'&&cb<='z') cb=(char)(cb-'a'+'A');
            if(ca!=cb) return 0;
        }
        int a_done=*a_end=='\\0',b_done=*b_end=='\\0';
        if(a_done||b_done) return a_done&&b_done;
        a=a_end+1;
        b=b_end+1;
    }
}

static int id_equal(const char *a,const char *b)
{
    if(!a||!b) return 0;
    while(*a&&*b)
    {
        char ca=*a++,cb=*b++;
        if(ca>='a'&&ca<='z') ca=(char)(ca-'a'+'A');
        if(cb>='a'&&cb<='z') cb=(char)(cb-'a'+'A');
        if(ca!=cb) return 0;
    }
    return *a==*b;
}

static int aml_is_pci_root(const acpi_aml_device_t *device)
{
    if(device->invalid) return 0;
    if(id_equal(device->hid,"PNP0A03")||id_equal(device->hid,"PNP0A08"))
        return 1;
    for(uint32_t i=0U;i<device->cid_count;++i)
        if(id_equal(device->cids[i],"PNP0A03")||
           id_equal(device->cids[i],"PNP0A08")) return 1;
    return 0;
}

static int aml_device_usable(const acpi_aml_device_t *device)
{
    if(device->invalid||device->sta_dynamic)
        return 0; /* Do not bind ambiguous or method-dependent ACPI objects. */
    if(device->sta_valid)
        return (device->sta&0x03U)==0x03U&&(device->sta&0x08U)!=0U;
    return 1; /* _STA absent or method-backed: ACPI's default is present/enabled. */
}

static int aml_resource_identity_equal(const acpi_aml_resource_t *a,
                                       const acpi_aml_resource_t *b)
{
    if(a->descriptor_tag!=b->descriptor_tag||
       a->resource_type!=b->resource_type||
       a->serial_bus_type!=b->serial_bus_type||
       a->gpio_connection_type!=b->gpio_connection_type||
       a->gpio_pin_count!=b->gpio_pin_count||
       a->slave_address!=b->slave_address||
       !path_equal(a->resource_source,b->resource_source)) return 0;
    for(uint32_t i=0U;i<a->gpio_pin_count;++i)
        if(a->gpio_pins[i]!=b->gpio_pins[i]) return 0;
    return 1;
}

static int aml_resource_equal(const acpi_aml_resource_t *a,
                              const acpi_aml_resource_t *b)
{
    return aml_resource_identity_equal(a,b)&&
           a->connection_speed_hz==b->connection_speed_hz&&
           a->general_flags==b->general_flags&&
           a->type_flags==b->type_flags&&
           a->gpio_general_flags==b->gpio_general_flags&&
           a->gpio_interrupt_flags==b->gpio_interrupt_flags&&
           a->gpio_pin_configuration==b->gpio_pin_configuration&&
           a->gpio_drive_strength==b->gpio_drive_strength&&
           a->gpio_debounce_timeout==b->gpio_debounce_timeout;
}

static int aml_merge_device(acpi_aml_device_t *destination,
                            const acpi_aml_device_t *source)
{
    if(destination->invalid||source->invalid||
       !path_equal(destination->parent_path,source->parent_path)) return 0;
    if(destination->hid[0]&&source->hid[0]&&
       !id_equal(destination->hid,source->hid)) return 0;
    if(!destination->hid[0]&&source->hid[0])
        memcpy(destination->hid,source->hid,sizeof(destination->hid));
    if(destination->adr_valid&&source->adr_valid&&destination->adr!=source->adr)
        return 0;
    if(!destination->adr_valid&&source->adr_valid)
    {destination->adr=source->adr;destination->adr_valid=1U;}
    if(destination->bbn_valid&&source->bbn_valid&&destination->bbn!=source->bbn)
        return 0;
    if(!destination->bbn_valid&&source->bbn_valid)
    {destination->bbn=source->bbn;destination->bbn_valid=1U;}
    if(destination->segment_valid&&source->segment_valid&&
       destination->segment!=source->segment) return 0;
    if(!destination->segment_valid&&source->segment_valid)
    {destination->segment=source->segment;destination->segment_valid=1U;}
    if(destination->hid_descriptor_address_valid&&
       source->hid_descriptor_address_valid&&
       destination->hid_descriptor_address!=source->hid_descriptor_address)
        return 0;
    if(!destination->hid_descriptor_address_valid&&
       source->hid_descriptor_address_valid)
    {
        destination->hid_descriptor_address=source->hid_descriptor_address;
        destination->hid_descriptor_address_valid=1U;
    }
    if(destination->sta_valid&&source->sta_valid&&destination->sta!=source->sta)
        return 0;
    if(!destination->sta_valid&&source->sta_valid)
    {destination->sta=source->sta;destination->sta_valid=1U;}
    for(uint32_t i=0U;i<source->cid_count;++i)
    {
        uint32_t j=0U;
        for(;j<destination->cid_count;++j)
            if(id_equal(destination->cids[j],source->cids[i])) break;
        if(j==destination->cid_count)
        {
            if(destination->cid_count>=ACPI_AML_MAX_CIDS) return 0;
            memcpy(destination->cids[destination->cid_count++],source->cids[i],
                   ACPI_AML_ID_MAX);
        }
    }
    for(uint32_t i=0U;i<source->resource_count;++i)
    {
        int duplicate=0;
        for(uint32_t j=0U;j<destination->resource_count;++j)
        {
            if(!aml_resource_identity_equal(&destination->resources[j],
                                            &source->resources[i]))
                continue;
            if(!aml_resource_equal(&destination->resources[j],
                                   &source->resources[i]))
                return 0;
            duplicate=1;
            break;
        }
        if(!duplicate)
        {
            if(destination->resource_count>=ACPI_AML_MAX_RESOURCES) return 0;
            destination->resources[destination->resource_count++]=source->resources[i];
        }
    }
    destination->resource_total+=source->resource_total;
    destination->i2c_count+=source->i2c_count;
    destination->sta_dynamic|=source->sta_dynamic;
    return 1;
}

static void aml_collect_staging(const acpi_aml_device_t *device,void *context)
{
    (void)context;
    if(aml_staging_count>=AML_STAGING_LIMIT)
    {aml_staging_overflow=1U;return;}
    aml_staging[aml_staging_count++]=*device;
}

static void aml_commit_staging(void)
{
    for(uint32_t i=0U;i<aml_staging_count;++i)
    {
        acpi_aml_device_t *source=&aml_staging[i];
        uint32_t existing=0U;
        for(;existing<aml_device_count;++existing)
            if(path_equal(aml_devices[existing].path,source->path)) break;
        if(existing<aml_device_count)
        {
            if(!aml_merge_device(&aml_devices[existing],source))
                aml_devices[existing].invalid=1U;
        }
        else if(aml_device_count<AML_DEVICE_LIMIT)
            aml_devices[aml_device_count++]=*source;
        else
            aml_namespace_overflow=1U;
    }
}

static void aml_discover_namespace(void)
{
    acpi_table_inventory_t tables;
    if(!acpi_discover_tables(&tables)) return;
    for(uint32_t table_index=0U;table_index<=tables.ssdt_count;++table_index)
    {
        uint64_t address=table_index==0U?tables.dsdt_address:
                         tables.ssdt_addresses[table_index-1U];
        uint32_t length=table_index==0U?tables.dsdt_length:
                        tables.ssdt_lengths[table_index-1U];
        if(length<=36U) continue;
        aml_staging_count=0U;
        aml_staging_overflow=0U;
        const uint8_t *table=(const uint8_t*)(uintptr_t)address;
        if(acpi_aml_parse_table(table+36U,length-36U,
                                aml_collect_staging,NULL,NULL)&&
           !aml_staging_overflow)
            aml_commit_staging();
        else if(aml_staging_overflow)
            aml_namespace_overflow=1U;
    }
}

static const acpi_aml_device_t *aml_find_device_path(const char *path)
{
    for(uint32_t i=0U;i<aml_device_count;++i)
        if(!aml_devices[i].invalid&&path_equal(aml_devices[i].path,path))
            return &aml_devices[i];
    return NULL;
}

static void *intel_sptlp_map(void *context,uint64_t address,size_t length)
{
    (void)context;
    return mmio_map_region(address,length);
}

static int intel_sptlp_unmap(void *context,void *mapping,size_t length)
{
    (void)context;
    return mmio_unmap_region(mapping,length);
}

static uint32_t intel_sptlp_read32(void *context,
                                   const volatile uint8_t *mapping,
                                   uint32_t offset)
{
    (void)context;
    return *(const volatile uint32_t*)(mapping+offset);
}


static void intel_write_hex8(uint8_t value)
{
    static const char digits[]="0123456789ABCDEF";
    serial_write_bytes(&digits[(value>>4U)&0x0FU],1U);
    serial_write_bytes(&digits[value&0x0FU],1U);
}

static void intel_log_gpio_irq_metadata(
    const acpi_aml_device_t *controller,
    const intel_sptlp_snapshot_t *snapshot)
{
    for(uint32_t i=0U;i<aml_device_count;++i)
    {
        const acpi_aml_device_t *child=&aml_devices[i];
        if(!aml_device_usable(child)) continue;
        for(uint32_t r=0U;r<child->resource_count;++r)
        {
            const acpi_aml_resource_t *resource=&child->resources[r];
            if(resource->resource_type!=5U||
               !path_equal(resource->resource_source,controller->path))
                continue;
            intel_sptlp_gpio_irq_request_t request;
            if(!intel_sptlp_decode_gpio_irq(controller,resource,&request))
                continue;
            uint8_t pending=0U,enabled=0U;
            if(!intel_sptlp_irq_snapshot_state(snapshot,&request,
                                                &pending,&enabled))
                continue;
            serial_write("VANTAOS_GPIO_SPTLP_IRQ_METADATA path=");
            serial_write(child->path);
            serial_write(" trigger=");
            serial_write(request.trigger==INTEL_SPTLP_GPIO_IRQ_EDGE?
                         "edge":"level");
            serial_write(" polarity=");
            serial_write(request.polarity==INTEL_SPTLP_GPIO_IRQ_ACTIVE_HIGH?
                         "high":request.polarity==INTEL_SPTLP_GPIO_IRQ_ACTIVE_LOW?
                         "low":"both");
            serial_write(" shared=");
            serial_write(request.shared?"yes":"no");
            serial_write(" wake=");
            serial_write(request.wake_capable?"yes":"no");
            serial_write(" pins=");
            for(uint32_t pin=0U;pin<request.pin_count;++pin)
            {
                char digits[5];
                uint16_t value=request.pins[pin];
                uint32_t count=0U;
                do {digits[count++]=(char)('0'+(value%10U));value/=10U;}
                while(value!=0U&&count<sizeof(digits));
                while(count!=0U) serial_write_bytes(&digits[--count],1U);
                if(pin+1U<request.pin_count) serial_write(",");
            }
            serial_write(" pending=0x");
            intel_write_hex8(pending);
            serial_write(" enabled=0x");
            intel_write_hex8(enabled);
            serial_write("\n");
        }
    }
}

static void intel_probe_gpio_controllers(void)
{
    const intel_sptlp_mmio_ops_t ops={
        .map=intel_sptlp_map,
        .unmap=intel_sptlp_unmap,
        .read32=intel_sptlp_read32
    };
    for(uint32_t i=0U;i<aml_device_count;++i)
    {
        const acpi_aml_device_t *device=&aml_devices[i];
        if(!aml_device_usable(device)||!id_equal(device->hid,"INT344B"))
            continue;
        intel_sptlp_snapshot_t snapshot;
        if(intel_sptlp_probe_controller(device,&ops,&snapshot))
        {
            serial_write("VANTAOS_GPIO_SPTLP_READ_ONLY_MMIO_READY communities=3 groups=7\n");
            intel_log_gpio_irq_metadata(device,&snapshot);
        }
        else
            serial_write("VANTAOS_GPIO_SPTLP_MMIO_PROBE_SKIPPED\n");
    }
}

static void intel_log_gpio_namespace(void)
{
    for(uint32_t i=0U;i<aml_device_count;++i)
    {
        const acpi_aml_device_t *child=&aml_devices[i];
        if(!aml_device_usable(child)) continue;
        for(uint32_t r=0U;r<child->resource_count;++r)
        {
            const acpi_aml_resource_t *resource=&child->resources[r];
            if(resource->resource_type!=5U) continue;
            const acpi_aml_device_t *gpio=
                aml_find_device_path(resource->resource_source);
            if(!gpio||!aml_device_usable(gpio)||
               !intel_sptlp_validate_gpio_resource(gpio,resource))
            {
                serial_write("VANTAOS_GPIO_ACPI_CONNECTION_UNRESOLVED path=");
                serial_write(resource->resource_source);
                serial_write("\n");
                continue;
            }
            serial_write("VANTAOS_GPIO_SPTLP_CONNECTION_VALID path=");
            serial_write(child->path);
            serial_write(resource->gpio_connection_type==0U?
                         " mode=interrupt pins=":" mode=io pins=");
            for(uint32_t pin=0U;pin<resource->gpio_pin_count;++pin)
            {
                char digits[5];
                uint16_t value=resource->gpio_pins[pin];
                uint32_t count=0U;
                do {digits[count++]=(char)('0'+(value%10U));value/=10U;}
                while(value!=0U&&count<sizeof(digits));
                while(count!=0U) serial_write_bytes(&digits[--count],1U);
                if(pin+1U<resource->gpio_pin_count) serial_write(",");
            }
            serial_write("\n");
        }
    }
}

static int intel_dw_device_id(uint16_t id)
{
    /* Intel Serial IO I2C functions with the DesignWare register interface. */
    return (id>=0x9D60U&&id<=0x9D63U)||
           (id>=0xA160U&&id<=0xA163U)||
           (id>=0x34E8U&&id<=0x34EBU)||
           (id>=0x02E8U&&id<=0x02EBU)||
           (id>=0x06E8U&&id<=0x06EBU)||
           (id>=0x7ACCU&&id<=0x7ACFU)||
           (id>=0x7A4CU&&id<=0x7A4FU);
}

static const acpi_aml_device_t *aml_find_controller(
    const struct pci_device *pci)
{
    for(uint32_t i=0U;i<aml_device_count;++i)
    {
        const acpi_aml_device_t *controller=&aml_devices[i];
        if(controller->invalid||!controller->adr_valid||
           !aml_device_usable(controller)) continue;
        const acpi_aml_device_t *root=NULL;
        for(uint32_t j=0U;j<aml_device_count;++j)
            if(path_equal(aml_devices[j].path,controller->parent_path)&&
               aml_is_pci_root(&aml_devices[j])&&
               aml_device_usable(&aml_devices[j]))
            {root=&aml_devices[j];break;}
        if(!root) continue;
        uint64_t adr=controller->adr;
        if(adr>UINT32_MAX) continue;
        uint32_t slot=(uint32_t)((adr>>16U)&0xFFFFU);
        uint32_t function=(uint32_t)(adr&0xFFFFU);
        uint8_t bus=root->bbn_valid?root->bbn:0U;
        uint16_t segment=root->segment_valid?root->segment:0U;
        if(slot>31U||function>7U) continue;
        if(pci->bus==bus&&pci->slot==slot&&pci->function==function&&
           pci->segment_group==segment) return controller;
    }
    return NULL;
}

static int intel_i2c_transfer(void *context,uint16_t address,uint32_t speed_hz,
                              const uint8_t *write_data,size_t write_length,
                              uint8_t *read_data,size_t read_length)
{
    intel_i2c_controller_t *controller=(intel_i2c_controller_t*)context;
    if(!controller||!controller->active) return 0;
    uint64_t flags=spinlock_lock_irqsave(&controller->lock);
    int result=i2c_dw_transfer(&controller->dw,address,speed_hz,
                               write_data,write_length,read_data,read_length);
    spinlock_unlock_irqrestore(&controller->lock,flags);
    return result;
}

static int intel_controller_start(intel_i2c_controller_t *controller,
                                  const struct pci_device *pci,
                                  const acpi_aml_device_t *acpi)
{
    uint16_t saved_command=pci_read_config16(pci,0x04U);
    uint64_t bar=0U;
    int is_io=0,is_64bit=0;
    int enabled_memory=0;
    if(!pci_get_bar(pci,0U,&bar,&is_io,&is_64bit)||is_io||bar==0U||
       (bar&0xFFFU)!=0U||bar>UINT64_MAX-I2C_REGISTER_WINDOW)
        return 0;
    if((saved_command&0x0002U)==0U)
    {
        if(!pci_enable_memory_space(pci)) return 0;
        enabled_memory=1;
    }
    volatile uint8_t *registers=(volatile uint8_t*)mmio_map_region(
        bar,I2C_REGISTER_WINDOW);
    if(!registers)
    {
        if(enabled_memory) (void)pci_write_config16(pci,0x04U,saved_command);
        return 0;
    }
    memset(controller,0,sizeof(*controller));
    controller->registers=registers;
    controller->pci=pci;
    memcpy(controller->acpi_path,acpi->path,strlen(acpi->path)+1U);
    spinlock_init(&controller->lock);
    controller->dw.context=(void*)registers;
    controller->dw.read32=dw_mmio_read;
    controller->dw.write32=dw_mmio_write;
    controller->dw.timeout_spins=1000000U;
    if(!i2c_dw_initialize(&controller->dw))
    {
        (void)mmio_unmap_region((void*)registers,I2C_REGISTER_WINDOW);
        if(enabled_memory) (void)pci_write_config16(pci,0x04U,saved_command);
        return 0;
    }
    controller->bus.name="Intel Serial IO I2C";
    controller->bus.context=controller;
    controller->bus.transfer=intel_i2c_transfer;
    controller->bus.ready=1U;
    controller->active=1U;
    return 1;
}

static void intel_attach_children(intel_i2c_controller_t *controller)
{
    uint32_t attached=0U;
    for(uint32_t i=0U;i<aml_device_count;++i)
    {
        const acpi_aml_device_t *child=&aml_devices[i];
        if(!aml_device_usable(child)||!child->hid_descriptor_address_valid)
            continue;
        for(uint32_t r=0U;r<child->resource_count;++r)
        {
            const acpi_aml_resource_t *resource=&child->resources[r];
            if(resource->resource_type!=4U||resource->serial_bus_type!=1U||
               !path_equal(resource->resource_source,controller->acpi_path)||
               resource->slave_address==0U||resource->slave_address>0x7FU||
               (resource->type_flags&1U)!=0U||
               resource->connection_speed_hz<10000U||
               resource->connection_speed_hz>400000U)
                continue;
            if(hid_i2c_attach(&controller->bus,resource->slave_address,
                              resource->connection_speed_hz,
                              child->hid_descriptor_address,child->hid))
                ++attached;
        }
    }
    if(attached==0U)
        serial_write("VANTAOS_I2C_CONTROLLER_NO_SUPPORTED_HID_CHILDREN\n");
}

void i2c_initialize(void)
{
    if(initialized) return;
    initialized=1U;
    aml_discover_namespace();
    if(aml_namespace_overflow)
    {
        serial_write("VANTAOS_I2C_ACPI_NAMESPACE_INCOMPLETE\n");
        return;
    }
    intel_log_gpio_namespace();
    intel_probe_gpio_controllers();
    uint32_t controller_count=0U;
    int index=-1;
    while((index=pci_find_class(I2C_PCI_CLASS,I2C_PCI_SUBCLASS,-1,index+1))>=0)
    {
        const struct pci_device *pci=pci_get_device(index);
        if(!pci||pci->vendor_id!=INTEL_VENDOR_ID||
           !intel_dw_device_id(pci->device_id)) continue;
        const acpi_aml_device_t *acpi=aml_find_controller(pci);
        if(!acpi)
        {
            serial_write("VANTAOS_I2C_CONTROLLER_ACPI_UNMATCHED\n");
            continue;
        }
        if(controller_count>=I2C_MAX_CONTROLLERS) break;
        intel_i2c_controller_t *controller=&controllers[controller_count];
        if(!intel_controller_start(controller,pci,acpi))
        {
            serial_write("VANTAOS_I2C_DESIGNWARE_REJECTED\n");
            continue;
        }
        serial_write("VANTAOS_I2C_DESIGNWARE_READY bdf=");
        {
            static const char hex[]="0123456789ABCDEF";
            char bdf[7]={hex[pci->bus>>4U],hex[pci->bus&15U],':',
                hex[pci->slot>>4U],hex[pci->slot&15U],'.',
                hex[pci->function&15U]};
            serial_write_bytes(bdf,sizeof(bdf));
        }
        serial_write(" acpi=");
        serial_write(controller->acpi_path);
        serial_write("\n");
        ++controller_count;
        intel_attach_children(controller);
    }
    if(controller_count==0U)
        serial_write("VANTAOS_I2C_NO_MATCHED_INTEL_DESIGNWARE_CONTROLLER\n");
}
