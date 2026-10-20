/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_bundle.h"
#include <string.h>
static const char *const names[NVX_GSP_COMPONENT_COUNT] = {
    "booter_load", "booter_unload", "bootloader", "gsp"
};
static int valid_token(const char *s, size_t max, int dots)
{
    size_t n=0;
    if (!s || !s[0]) return 0;
    while (s[n]) {
        char c=s[n];
        if (n>=max || !((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
            (c>='0'&&c<='9')||c=='_'||c=='-'||(dots&&c=='.'))) return 0;
        ++n;
    }
    return !dots || (s[0]!='.' && s[n-1]!='.');
}
static int append_text(char *dst, size_t capacity, size_t *length,
                       const char *text)
{
    size_t n = strlen(text);
    if (*length >= capacity || n >= capacity - *length)
        return 0;
    memcpy(dst + *length, text, n);
    *length += n;
    dst[*length] = '\\0';
    return 1;
}
static void clear_component(nvx_gsp_component_t *c)
{
    c->blob=(nvx_firmware_blob_t){0};
    c->kind=NVX_GSP_RUNTIME;
    c->acquired=0;
}
static void release_parts(nvx_gsp_bundle_t *b)
{
    for(size_t i=0;i<NVX_GSP_COMPONENT_COUNT;i++) {
        nvx_gsp_component_t *c=&b->components[i];
        if(c->acquired) {
            b->provider.release(&c->blob,b->provider.context);
            clear_component(c);
        }
    }
    b->total_bytes=0;
    b->acquired=0;
    b->family=NVX_FAMILY_UNKNOWN;
}
static nvx_gsp_bundle_status_t acquire_one(nvx_gsp_bundle_t *b,size_t i,
                                           const char *path)
{
    nvx_firmware_blob_t blob={0};
    if(!b->provider.request(path,&blob,b->provider.context)) {
        if(blob.data || blob.release_context)
            b->provider.release(&blob,b->provider.context);
        return NVX_GSP_BUNDLE_COMPONENT_NOT_FOUND;
    }
    if(!blob.data || !blob.size) {
        b->provider.release(&blob,b->provider.context);
        return NVX_GSP_BUNDLE_INVALID_COMPONENT;
    }
    if(blob.size>(size_t)NVX_FIRMWARE_MAX_BYTES) {
        b->provider.release(&blob,b->provider.context);
        return NVX_GSP_BUNDLE_COMPONENT_TOO_LARGE;
    }
    if(b->total_bytes>(uint64_t)NVX_GSP_BUNDLE_MAX_BYTES ||
       (uint64_t)blob.size>(uint64_t)NVX_GSP_BUNDLE_MAX_BYTES-b->total_bytes) {
        b->provider.release(&blob,b->provider.context);
        return NVX_GSP_BUNDLE_TOTAL_TOO_LARGE;
    }
    if(!b->provider.verify(path,&blob,b->provider.context)) {
        b->provider.release(&blob,b->provider.context);
        return NVX_GSP_BUNDLE_VERIFICATION_FAILED;
    }
    b->components[i].blob=blob;
    b->components[i].kind=(nvx_gsp_component_kind_t)i;
    b->components[i].acquired=1;
    b->total_bytes+=(uint64_t)blob.size;
    return NVX_GSP_BUNDLE_OK;
}
nvx_gsp_bundle_status_t nvx_gsp_bundle_acquire(
    const nvx_firmware_provider_t *p,nvx_generation_family_t family,
    const char *chip,const char *version,nvx_gsp_bundle_t *out)
{
    char path[96];
    nvx_gsp_bundle_status_t status=NVX_GSP_BUNDLE_OK;
    if(!out) return NVX_GSP_BUNDLE_INVALID_ARGUMENT;
    if(out->acquired) return NVX_GSP_BUNDLE_ALREADY_ACQUIRED;
    if(!p||!p->request||!p->verify||!p->release)
        return NVX_GSP_BUNDLE_PROVIDER_INCOMPLETE;
    if(family!=NVX_FAMILY_AMPERE) return NVX_GSP_BUNDLE_UNSUPPORTED_FAMILY;
    if(!valid_token(chip,NVX_GSP_CHIP_MAX,0)||
       !valid_token(version,NVX_GSP_VERSION_MAX,1))
        return NVX_GSP_BUNDLE_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));
    out->provider=*p;
    out->family=family;
    memcpy(out->chip,chip,strlen(chip)+1U);
    memcpy(out->version,version,strlen(version)+1U);
    for(size_t i=0;i<NVX_GSP_COMPONENT_COUNT;i++) {
        size_t length=0U;
        path[0]='\\0';
        if(!append_text(path,sizeof(path),&length,"nvidia/") ||
           !append_text(path,sizeof(path),&length,out->chip) ||
           !append_text(path,sizeof(path),&length,"/gsp/") ||
           !append_text(path,sizeof(path),&length,names[i]) ||
           !append_text(path,sizeof(path),&length,"-") ||
           !append_text(path,sizeof(path),&length,out->version) ||
           !append_text(path,sizeof(path),&length,".bin")) {
            status=NVX_GSP_BUNDLE_INVALID_ARGUMENT;
            break;
        }
        status=acquire_one(out,i,path);
        if(status!=NVX_GSP_BUNDLE_OK) break;
    }
    if(status!=NVX_GSP_BUNDLE_OK) {
        release_parts(out);
        memset(&out->provider,0,sizeof(out->provider));
        out->chip[0]=out->version[0]='\0';
        return status;
    }
    out->acquired=1;
    return NVX_GSP_BUNDLE_OK;
}
void nvx_gsp_bundle_release(nvx_gsp_bundle_t *b)
{
    if(!b||!b->acquired) return;
    release_parts(b);
    memset(&b->provider,0,sizeof(b->provider));
    b->chip[0]=b->version[0]='\0';
}
