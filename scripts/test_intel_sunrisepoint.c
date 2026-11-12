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

int main(void)
{
    test_community_boundaries();
    test_gpio_resource_validation();
    puts("Intel Sunrise Point-LP GPIO metadata tests passed");
    return 0;
}
