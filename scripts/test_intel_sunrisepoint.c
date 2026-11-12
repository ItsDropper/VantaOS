/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "intel_sunrisepoint.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_community_boundaries(void)
{
    static const struct {uint16_t pin;uint8_t community;} cases[]={
        {0U,0U},{47U,0U},{48U,1U},{119U,1U},{120U,2U},{151U,2U}
    };
    for(uint32_t pin=0U;pin<INTEL_SPTLP_PIN_COUNT;++pin)
    {
        intel_sptlp_irq_location_t location={0};
        uint8_t community=0xFFU;
        assert(intel_sptlp_pin_irq_location((uint16_t)pin,&location));
        assert(intel_sptlp_pin_community((uint16_t)pin,&community));
        assert(location.community==community);
        assert(location.group<3U&&location.bit<INTEL_SPTLP_GPP_SIZE);
        assert(location.bit_mask==(1U<<location.bit));
        assert(location.interrupt_status_offset==
               INTEL_SPTLP_GPI_IS_OFFSET+(uint32_t)location.group*4U);
        assert(location.interrupt_enable_offset==
               INTEL_SPTLP_GPI_IE_OFFSET+(uint32_t)location.group*4U);
    }
    for(uint32_t i=0U;i<sizeof(cases)/sizeof(cases[0]);++i)
    {
        uint8_t community=0xFFU;
        assert(intel_sptlp_pin_community(cases[i].pin,&community));
        assert(community==cases[i].community);
    }
    uint8_t community=0xA5U;
    assert(!intel_sptlp_pin_community(152U,&community));
    assert(community==0xA5U);
    assert(!intel_sptlp_pin_community(0U,NULL));
}


static void test_pin_irq_locations(void)
{
    static const struct
    {
        uint16_t pin;
        uint8_t community;
        uint8_t group;
        uint8_t bit;
        uint32_t status_offset;
        uint32_t enable_offset;
        uint32_t bit_mask;
    } cases[]={
        {0U,0U,0U,0U,0x100U,0x120U,0x000001U},
        {23U,0U,0U,23U,0x100U,0x120U,0x800000U},
        {24U,0U,1U,0U,0x104U,0x124U,0x000001U},
        {47U,0U,1U,23U,0x104U,0x124U,0x800000U},
        {48U,1U,0U,0U,0x100U,0x120U,0x000001U},
        {72U,1U,1U,0U,0x104U,0x124U,0x000001U},
        {96U,1U,2U,0U,0x108U,0x128U,0x000001U},
        {119U,1U,2U,23U,0x108U,0x128U,0x800000U},
        {120U,2U,0U,0U,0x100U,0x120U,0x000001U},
        {144U,2U,1U,0U,0x104U,0x124U,0x000001U},
        {151U,2U,1U,7U,0x104U,0x124U,0x000080U}
    };
    for(uint32_t i=0U;i<sizeof(cases)/sizeof(cases[0]);++i)
    {
        intel_sptlp_irq_location_t location={0};
        assert(intel_sptlp_pin_irq_location(cases[i].pin,&location));
        assert(location.community==cases[i].community);
        assert(location.group==cases[i].group);
        assert(location.bit==cases[i].bit);
        assert(location.interrupt_status_offset==cases[i].status_offset);
        assert(location.interrupt_enable_offset==cases[i].enable_offset);
        assert(location.bit_mask==cases[i].bit_mask);
    }
    intel_sptlp_irq_location_t unchanged={9U,8U,7U,6U,5U,4U};
    assert(!intel_sptlp_pin_irq_location(152U,&unchanged));
    assert(unchanged.community==9U&&unchanged.group==8U&&
           unchanged.bit==7U&&unchanged.interrupt_status_offset==6U&&
           unchanged.interrupt_enable_offset==5U&&unchanged.bit_mask==4U);
    assert(!intel_sptlp_pin_irq_location(0U,NULL));
}

static void test_gpio_resource_validation(void)
{
    acpi_aml_device_t controller={0};
    acpi_aml_resource_t resource={0};
    strcpy(controller.path,"\\_SB_.GPIO");
    strcpy(controller.hid,"INT344B");
    resource.resource_type=5U;
    resource.gpio_connection_type=0U;
    resource.gpio_pin_count=2U;
    resource.gpio_pins[0]=7U;
    resource.gpio_pins[1]=151U;
    strcpy(resource.resource_source,"\\_SB_.GPIO");
    assert(intel_sptlp_validate_gpio_resource(&controller,&resource));

    resource.gpio_connection_type=1U;
    assert(intel_sptlp_validate_gpio_resource(&controller,&resource));
    resource.gpio_connection_type=2U;
    assert(!intel_sptlp_validate_gpio_resource(&controller,&resource));
    resource.gpio_connection_type=0U;

    resource.gpio_pins[1]=152U;
    assert(!intel_sptlp_validate_gpio_resource(&controller,&resource));
    resource.gpio_pins[1]=8U;
    resource.gpio_pin_count=0U;
    assert(!intel_sptlp_validate_gpio_resource(&controller,&resource));
    resource.gpio_pin_count=ACPI_AML_MAX_GPIO_PINS+1U;
    assert(!intel_sptlp_validate_gpio_resource(&controller,&resource));
    resource.gpio_pin_count=1U;

    strcpy(resource.resource_source,"\\_SB_.OTHER");
    assert(!intel_sptlp_validate_gpio_resource(&controller,&resource));
    strcpy(resource.resource_source,"\\_SB_.GPIO");
    strcpy(controller.hid,"INT3451");
    assert(!intel_sptlp_validate_gpio_resource(&controller,&resource));
    strcpy(controller.hid,"INT344B");
    controller.invalid=1U;
    assert(!intel_sptlp_validate_gpio_resource(&controller,&resource));
    controller.invalid=0U;
    controller.sta_dynamic=1U;
    assert(!intel_sptlp_validate_gpio_resource(&controller,&resource));
    controller.sta_dynamic=0U;
    controller.sta_valid=1U;
    controller.sta=0x0DU;
    assert(!intel_sptlp_validate_gpio_resource(&controller,&resource));
    controller.sta=0x0FU;
    assert(intel_sptlp_validate_gpio_resource(&controller,&resource));
}


typedef struct
{
    uint8_t windows[INTEL_SPTLP_COMMUNITY_COUNT][0x1000U];
    uint64_t bases[INTEL_SPTLP_COMMUNITY_COUNT];
    uint32_t map_calls;
    uint32_t unmap_calls;
    uint32_t read_calls;
    int fail_map_at;
    int fail_unmap;
} fake_mmio_t;

static void *fake_map(void *context,uint64_t address,size_t length)
{
    fake_mmio_t *fake=(fake_mmio_t*)context;
    uint32_t index=fake->map_calls++;
    if(index>=INTEL_SPTLP_COMMUNITY_COUNT||index==(uint32_t)fake->fail_map_at||
       length>sizeof(fake->windows[index])||address!=fake->bases[index])
        return NULL;
    return fake->windows[index];
}

static int fake_unmap(void *context,void *mapping,size_t length)
{
    fake_mmio_t *fake=(fake_mmio_t*)context;
    (void)mapping;
    (void)length;
    ++fake->unmap_calls;
    return !fake->fail_unmap;
}

static uint32_t fake_read32(void *context,const volatile uint8_t *mapping,
                            uint32_t offset)
{
    fake_mmio_t *fake=(fake_mmio_t*)context;
    ++fake->read_calls;
    return *(const volatile uint32_t*)(mapping+offset);
}

static void test_read_only_mmio_probe(void)
{
    acpi_aml_device_t controller={0};
    fake_mmio_t fake={.fail_map_at=-1};
    intel_sptlp_snapshot_t snapshot={0};
    const intel_sptlp_mmio_ops_t ops={
        .context=&fake,.map=fake_map,.unmap=fake_unmap,.read32=fake_read32
    };
    strcpy(controller.hid,"INT344B");
    strcpy(controller.path,"\\_SB_.GPIO");
    controller.sta_valid=1U;
    controller.sta=0x0FU;
    controller.resource_count=4U;
    for(uint32_t i=0U;i<INTEL_SPTLP_COMMUNITY_COUNT;++i)
    {
        acpi_aml_resource_t *resource=&controller.resources[i];
        fake.bases[i]=0x100000U+(uint64_t)i*0x2000U;
        resource->resource_type=1U;
        resource->descriptor_tag=0x86U;
        resource->address=fake.bases[i];
        resource->minimum=fake.bases[i];
        resource->maximum=fake.bases[i];
        resource->length=0x1000U;
        *(uint32_t*)(fake.windows[i]+INTEL_SPTLP_PAD_OWN_OFFSET)=0x10U+i;
        *(uint32_t*)(fake.windows[i]+INTEL_SPTLP_PADCFGLOCK_OFFSET)=0x20U+i;
        *(uint32_t*)(fake.windows[i]+INTEL_SPTLP_HOSTSW_OWN_OFFSET)=0x30U+i;
        for(uint32_t group=0U;group<INTEL_SPTLP_MAX_GPP_GROUPS;++group)
        {
            *(uint32_t*)(fake.windows[i]+INTEL_SPTLP_GPI_IS_OFFSET+group*4U)=
                0x50U+i*4U+group;
            *(uint32_t*)(fake.windows[i]+INTEL_SPTLP_GPI_IE_OFFSET+group*4U)=
                0x60U+i*4U+group;
        }
    }
    controller.resources[3].resource_type=3U; /* Ignore the ACPI IRQ resource. */
    assert(intel_sptlp_probe_controller(&controller,&ops,&snapshot));
    assert(fake.map_calls==3U&&fake.unmap_calls==3U&&fake.read_calls==9U+2U*(2U+3U+2U));
    for(uint32_t i=0U;i<INTEL_SPTLP_COMMUNITY_COUNT;++i)
    {
        assert(snapshot.communities[i].base==fake.bases[i]);
        assert(snapshot.communities[i].pad_ownership==0x10U+i);
        assert(snapshot.communities[i].pad_config_lock==0x20U+i);
        assert(snapshot.communities[i].host_software_ownership==0x30U+i);
        static const uint8_t expected_groups[]={2U,3U,2U};
        assert(snapshot.communities[i].group_count==expected_groups[i]);
        for(uint32_t group=0U;group<expected_groups[i];++group)
        {
            assert(snapshot.communities[i].interrupt_status[group]==
                   0x50U+i*4U+group);
            assert(snapshot.communities[i].interrupt_enable[group]==
                   0x60U+i*4U+group);
        }
    }

    memset(&fake,0,sizeof(fake));
    fake.fail_map_at=-1;
    for(uint32_t i=0U;i<INTEL_SPTLP_COMMUNITY_COUNT;++i)
        fake.bases[i]=0x100000U+(uint64_t)i*0x2000U;
    fake.fail_unmap=1;
    memset(&snapshot,0x5A,sizeof(snapshot));
    assert(!intel_sptlp_probe_controller(&controller,&ops,&snapshot));
    assert(fake.map_calls==3U&&fake.unmap_calls==3U);
    assert(snapshot.communities[0].base==UINT64_C(0x5A5A5A5A5A5A5A5A));

    memset(&fake,0,sizeof(fake));
    fake.fail_map_at=2;
    for(uint32_t i=0U;i<INTEL_SPTLP_COMMUNITY_COUNT;++i)
        fake.bases[i]=0x100000U+(uint64_t)i*0x2000U;
    memset(&snapshot,0xA5,sizeof(snapshot));
    assert(!intel_sptlp_probe_controller(&controller,&ops,&snapshot));
    assert(fake.map_calls==3U&&fake.unmap_calls==2U);
    assert(snapshot.communities[0].base==UINT64_C(0xA5A5A5A5A5A5A5A5));

    fake.map_calls=0U;
    fake.unmap_calls=0U;
    fake.fail_map_at=-1;
    controller.resources[1].address=controller.resources[0].address;
    controller.resources[1].minimum=controller.resources[0].minimum;
    controller.resources[1].maximum=controller.resources[0].maximum;
    assert(!intel_sptlp_probe_controller(&controller,&ops,&snapshot));
    assert(fake.map_calls==0U&&fake.unmap_calls==0U);
    controller.resources[1].address=fake.bases[1];
    controller.resources[1].minimum=fake.bases[1];
    controller.resources[1].maximum=fake.bases[1];
    controller.resources[1].length=0x100U;
    assert(!intel_sptlp_probe_controller(&controller,&ops,&snapshot));
    controller.resources[1].length=0x128U;
    assert(!intel_sptlp_probe_controller(&controller,&ops,&snapshot));
    controller.resources[1].length=INTEL_SPTLP_MMIO_MIN_LENGTH;
    assert(intel_sptlp_probe_controller(&controller,&ops,&snapshot));
    controller.resources[1].length=0x1000U;
    controller.resources[2].address=UINT64_MAX-0x100U;
    controller.resources[2].minimum=controller.resources[2].address;
    controller.resources[2].maximum=controller.resources[2].address;
    controller.resources[2].length=0x1000U;
    assert(!intel_sptlp_probe_controller(&controller,&ops,&snapshot));
}

int main(void)
{
    test_community_boundaries();
    test_pin_irq_locations();
    test_gpio_resource_validation();
    test_read_only_mmio_probe();
    puts("Intel Sunrise Point-LP GPIO metadata tests passed");
    return 0;
}
