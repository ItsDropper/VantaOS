/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_bundle.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const uint8_t bytes[16]={0x47,0x53,0x50};
static unsigned requests,verifies,releases;
static char requested_paths[3][96];
static int fail_request=-1,fail_verify=-1,invalid_blob=-1,oversized=-1;
static size_t reported_size=sizeof(bytes);
static int component(const char *p) {
    const char *n[]={"gsp_ga10x","ucodes_ga10x","gsp_log_ga10x"};
    for(int i=0;i<3;i++) if(strstr(p,n[i])) return i;
    return -1;
}
static int request(const char *n,nvx_firmware_blob_t *b,void *ctx) {
    (void)ctx; int i=component(n); assert(i>=0); requests++;
    if(requests<=3U) { strncpy(requested_paths[requests-1U],n,95U);
        requested_paths[requests-1U][95]='\0'; }
    b->data=i==invalid_blob?0:bytes;
    b->size=i==oversized?(size_t)NVX_FIRMWARE_MAX_BYTES+1U:reported_size;
    b->release_context=(void *)bytes; return i!=fail_request;
}
static int verify(const char *n,const nvx_firmware_blob_t *b,void *ctx) {
    (void)ctx; int i=component(n); verifies++; return i>=0&&b->data==bytes&&i!=fail_verify;
}
static void release_blob(nvx_firmware_blob_t *b,void *ctx) {
    (void)ctx; releases++; *b=(nvx_firmware_blob_t){0};
}
static void reset(void) {
    requests=verifies=releases=0; fail_request=fail_verify=invalid_blob=oversized=-1;
    reported_size=sizeof(bytes);
}
int main(void) {
    nvx_firmware_provider_t p={request,verify,release_blob,0};
    nvx_gsp_bundle_t b={0};
    assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_AMPERE,"ga102","535.113.01",&b)==NVX_GSP_BUNDLE_OK);
    assert(b.acquired&&b.total_bytes==sizeof(bytes)*3U);
    assert(strcmp(requested_paths[0],"nvidia/535.113.01/gsp_ga10x.bin")==0);
    assert(strcmp(requested_paths[1],"nvidia/535.113.01/ucodes_ga10x.bin")==0);
    assert(strcmp(requested_paths[2],"nvidia/535.113.01/gsp_log_ga10x.bin")==0);
    assert(b.components[0].kind==NVX_GSP_RUNTIME&&b.components[1].kind==NVX_GSP_UCODE_ARCHIVE);
    assert(requests==3&&verifies==3&&releases==0);
    assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_AMPERE,"ga102","535.113.01",&b)==NVX_GSP_BUNDLE_ALREADY_ACQUIRED);
    nvx_gsp_bundle_release(&b); assert(!b.acquired&&releases==3);
    reset(); fail_request=1;
    assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_AMPERE,"ga102","535.113.01",&b)==NVX_GSP_BUNDLE_COMPONENT_NOT_FOUND);
    assert(requests==2&&verifies==1&&releases==2);
    reset(); fail_request=2;
    assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_AMPERE,"ga102","535.113.01",&b)==NVX_GSP_BUNDLE_OK);
    assert(b.acquired&&!b.components[2].acquired&&requests==3&&verifies==2);
    nvx_gsp_bundle_release(&b);assert(releases==3);
    reset(); fail_verify=1;
    assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_AMPERE,"ga102","535.113.01",&b)==NVX_GSP_BUNDLE_VERIFICATION_FAILED);
    assert(verifies==2&&releases==2);
    reset(); invalid_blob=0;
    assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_AMPERE,"ga102","535.113.01",&b)==NVX_GSP_BUNDLE_INVALID_COMPONENT);
    assert(releases==1);
    reset(); oversized=1;
    assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_AMPERE,"ga102","535.113.01",&b)==NVX_GSP_BUNDLE_COMPONENT_TOO_LARGE);
    assert(releases==2);
    reset(); reported_size=50U*1024U*1024U;
    assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_AMPERE,"ga102","535.113.01",&b)==NVX_GSP_BUNDLE_OK);
    assert(b.acquired&&!b.components[2].acquired&&b.total_bytes==100U*1024U*1024U);
    assert(requests==3&&verifies==2&&releases==1);
    nvx_gsp_bundle_release(&b);assert(releases==3);
    reset();
    assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_ADA,"ad102","535.113.01",&b)==NVX_GSP_BUNDLE_UNSUPPORTED_FAMILY);
    assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_AMPERE,"../ga102","535.113.01",&b)==NVX_GSP_BUNDLE_INVALID_ARGUMENT);
    assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_AMPERE,"ga102","../../bad",&b)==NVX_GSP_BUNDLE_INVALID_ARGUMENT);
    assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_AMPERE,"ga102","535..113",&b)==NVX_GSP_BUNDLE_INVALID_ARGUMENT);
    assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_AMPERE,"ga102",".535.113",&b)==NVX_GSP_BUNDLE_INVALID_ARGUMENT);
    assert(nvx_gsp_bundle_acquire(&p,NVX_FAMILY_AMPERE,"ga102","535.113.01",0)==NVX_GSP_BUNDLE_INVALID_ARGUMENT);
    assert(nvx_gsp_bundle_acquire(0,NVX_FAMILY_AMPERE,"ga102","535.113.01",&b)==NVX_GSP_BUNDLE_PROVIDER_INCOMPLETE);
    return 0;
}
