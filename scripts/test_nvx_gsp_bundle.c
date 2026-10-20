/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_bundle.h"
#include <assert.h>
#include <string.h>
static const uint8_t bytes[16]={0x47,0x53,0x50};
static const char *const names[NVX_GSP_COMPONENT_COUNT]={"gsp","ucodes","booter_load","booter_unload","bootloader"};
static unsigned requests,verifies,releases;static char paths[5][128];
static int fail_request=-1,fail_verify=-1,invalid_blob=-1,oversized=-1;
static int component(const char *p){for(int i=0;i<5;i++)if(strstr(p,names[i]))return i;return -1;}
static int request(const char *p,nvx_firmware_blob_t *b,void *ctx){(void)ctx;int i=component(p);assert(i>=0);if(requests<5U){strncpy(paths[requests],p,127U);paths[requests][127]='\0';}requests++;b->data=i==invalid_blob?0:bytes;b->size=i==oversized?(size_t)NVX_FIRMWARE_MAX_BYTES+1U:sizeof(bytes);b->release_context=(void *)bytes;return i!=fail_request;}
static int verify(const char *p,const nvx_firmware_blob_t *b,void *ctx){(void)ctx;int i=component(p);verifies++;return i>=0&&b->data==bytes&&i!=fail_verify;}
static void release_blob(nvx_firmware_blob_t *b,void *ctx){(void)ctx;releases++;*b=(nvx_firmware_blob_t){0};}
static void reset(void){requests=verifies=releases=0;fail_request=fail_verify=invalid_blob=oversized=-1;memset(paths,0,sizeof(paths));}
int main(void){
 nvx_firmware_provider_t p={request,verify,release_blob,0};nvx_gsp_bundle_t b={0};
 assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_AMPERE,"ga106","535.113.01",&b)==NVX_GSP_BUNDLE_OK);
 assert(b.acquired&&b.total_bytes==sizeof(bytes)*5U&&requests==5&&verifies==5&&releases==0);
 assert(strcmp(paths[0],"nvidia/ga106/gsp/gsp-535.113.01.bin")==0);
 assert(strcmp(paths[1],"nvidia/ga106/gsp/ucodes-535.113.01.bin")==0);
 assert(strcmp(paths[2],"nvidia/ga106/gsp/booter_load-535.113.01.bin")==0);
 assert(strcmp(paths[3],"nvidia/ga106/gsp/booter_unload-535.113.01.bin")==0);
 assert(strcmp(paths[4],"nvidia/ga106/gsp/bootloader-535.113.01.bin")==0);
 for(unsigned i=0;i<5U;i++)assert(b.components[i].acquired&&b.components[i].kind==(nvx_gsp_component_kind_t)i);
 assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_AMPERE,"ga106","535.113.01",&b)==NVX_GSP_BUNDLE_ALREADY_ACQUIRED);
 nvx_gsp_bundle_release(&b);assert(!b.acquired&&releases==5);
 reset();fail_request=3;assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_AMPERE,"ga106","535.113.01",&b)==NVX_GSP_BUNDLE_COMPONENT_NOT_FOUND);assert(requests==4&&verifies==3&&releases==4&&!b.acquired);
 reset();fail_verify=2;assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_AMPERE,"ga106","535.113.01",&b)==NVX_GSP_BUNDLE_VERIFICATION_FAILED);assert(requests==3&&verifies==3&&releases==3&&!b.acquired);
 reset();invalid_blob=4;assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_AMPERE,"ga106","535.113.01",&b)==NVX_GSP_BUNDLE_INVALID_COMPONENT);assert(releases==5&&!b.acquired);
 reset();oversized=1;assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_AMPERE,"ga106","535.113.01",&b)==NVX_GSP_BUNDLE_COMPONENT_TOO_LARGE);assert(releases==2&&!b.acquired);
 reset();assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_AMPERE,"../ga106","535.113.01",&b)==NVX_GSP_BUNDLE_INVALID_ARGUMENT);
 assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_AMPERE,"ga106","../../bad",&b)==NVX_GSP_BUNDLE_INVALID_ARGUMENT);
 assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_AMPERE,"ga106","535..113",&b)==NVX_GSP_BUNDLE_INVALID_ARGUMENT);
 assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_AMPERE,"ga106","535.113.01",0)==NVX_GSP_BUNDLE_INVALID_ARGUMENT);
 assert(nvx_gsp_bundle_acquire(0,NVX_FAMILY_AMPERE,"ga106","535.113.01",&b)==NVX_GSP_BUNDLE_PROVIDER_INCOMPLETE);
 assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_ADA,"ad106","535.113.01",&b)==NVX_GSP_BUNDLE_UNSUPPORTED_FAMILY);
 return 0;
}
