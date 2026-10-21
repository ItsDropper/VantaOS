/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "partition_scan.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#define SECTORS 2048U
#define SS 512U
#define ENTRY_SIZE 128U
#define ENTRY_COUNT 128U
#define ARRAY_LBA 2U
#define BACKUP_ARRAY (SECTORS - 33U)
#define BACKUP_HEADER (SECTORS - 1U)
typedef struct { uint8_t *data; uint64_t sectors; } memdisk_t;
static void wr32(uint8_t *p, uint32_t v)
{ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static void wr64(uint8_t *p, uint64_t v)
{ wr32(p,(uint32_t)v); wr32(p+4U,(uint32_t)(v>>32)); }
static uint32_t crc_step(uint32_t crc, const uint8_t *p, size_t n)
{
    for(size_t i=0;i<n;++i){crc^=p[i];for(unsigned b=0;b<8U;++b)crc=(crc>>1U)^(0xEDB88320U&(0U-(crc&1U)));}
    return crc;
}
static uint32_t crc(const uint8_t *p,size_t n){return ~crc_step(0xFFFFFFFFU,p,n);}
static int memread(void *ctx,uint64_t lba,uint32_t count,void *out)
{
    memdisk_t *d=(memdisk_t*)ctx;
    if(!d||!out||!count||lba>=d->sectors||(uint64_t)count>d->sectors-lba)return 0;
    for(uint64_t i=0;i<(uint64_t)count*SS;++i)((uint8_t*)out)[i]=d->data[lba*SS+i];
    return 1;
}
static void mbr_entry(uint8_t *m,unsigned i,uint8_t type,uint32_t start,uint32_t n)
{uint8_t *p=m+446U+i*16U;p[4]=type;wr32(p+8U,start);wr32(p+12U,n);}
static void gpt_header(uint8_t *h,uint64_t current,uint64_t backup,uint64_t array,uint32_t array_crc)
{
    for(unsigned i=0;i<SS;++i)h[i]=0;
    h[0]='E';h[1]='F';h[2]='I';h[3]=' ';h[4]='P';h[5]='A';h[6]='R';h[7]='T';
    wr32(h+8U,0x10000U);wr32(h+12U,92U);wr64(h+24U,current);wr64(h+32U,backup);
    wr64(h+40U,34U);wr64(h+48U,SECTORS-34U);
    for(unsigned i=0;i<16U;++i)h[56U+i]=(uint8_t)(i+1U);
    wr64(h+72U,array);wr32(h+80U,ENTRY_COUNT);wr32(h+84U,ENTRY_SIZE);wr32(h+88U,array_crc);
    wr32(h+16U,crc(h,92U));
}
static void refresh_crc(uint8_t *h){wr32(h+16U,0U);wr32(h+16U,crc(h,92U));}
static void gpt_image(memdisk_t *d)
{
    uint8_t *m=d->data;m[510]=0x55;m[511]=0xAA;mbr_entry(m,0U,0xEEU,1U,SECTORS-1U);
    uint8_t *a=d->data+ARRAY_LBA*SS;
    a[0]=0x28;a[1]=0x73;wr64(a+32U,40U);wr64(a+40U,99U);a[56]='F';a[58]='A';a[60]='T';
    uint8_t *b=a+ENTRY_SIZE;b[0]=0x28;b[1]=0x73;wr64(b+32U,100U);wr64(b+40U,199U);b[56]='D';b[58]='A';b[60]='T';
    uint32_t sum=crc(a,ENTRY_COUNT*ENTRY_SIZE);
    for(unsigned i=0;i<ENTRY_COUNT*ENTRY_SIZE;++i)d->data[BACKUP_ARRAY*SS+i]=a[i];
    gpt_header(d->data+SS,1U,BACKUP_HEADER,ARRAY_LBA,sum);
    gpt_header(d->data+BACKUP_HEADER*SS,BACKUP_HEADER,1U,BACKUP_ARRAY,sum);
}
static void wipe(memdisk_t *d){for(uint64_t i=0;i<d->sectors*SS;++i)d->data[i]=0U;}
int main(void)
{
    memdisk_t d={0};d.sectors=SECTORS;d.data=(uint8_t*)calloc((size_t)d.sectors,SS);assert(d.data);
    partition_scan_entry_t out[PARTITION_SCAN_MAX];size_t n=0U;
    gpt_image(&d);
    assert(partition_scan(memread,&d,d.sectors,out,PARTITION_SCAN_MAX,&n)==PARTITION_SCAN_OK);
    assert(n==2U&&out[0].start_lba==40U&&out[1].start_lba==100U&&out[0].scheme==PARTITION_SCHEME_GPT);
    out[0].start_lba = 0x12345678U;
    d.data[SS+16U]^=1U;
    assert(partition_scan(memread,&d,d.sectors,out,PARTITION_SCAN_MAX,&n)==PARTITION_SCAN_BAD_TABLE);
    assert(out[0].start_lba == 0x12345678U && n == 0U);
    wipe(&d);gpt_image(&d);d.data[ARRAY_LBA*SS+5U]^=1U;
    assert(partition_scan(memread,&d,d.sectors,out,PARTITION_SCAN_MAX,&n)==PARTITION_SCAN_BAD_TABLE);
    wipe(&d);gpt_image(&d);d.data[BACKUP_HEADER*SS+16U]^=1U;
    assert(partition_scan(memread,&d,d.sectors,out,PARTITION_SCAN_MAX,&n)==PARTITION_SCAN_BAD_TABLE);
    wipe(&d);gpt_image(&d);wr32(d.data+SS+80U,0xFFFFFFFFU);refresh_crc(d.data+SS);
    assert(partition_scan(memread,&d,d.sectors,out,PARTITION_SCAN_MAX,&n)==PARTITION_SCAN_BAD_TABLE);
    wipe(&d);gpt_image(&d);wr64(d.data+SS+32U,d.sectors+5U);refresh_crc(d.data+SS);
    assert(partition_scan(memread,&d,d.sectors,out,PARTITION_SCAN_MAX,&n)==PARTITION_SCAN_BAD_TABLE);
    wipe(&d);gpt_image(&d);uint8_t *a=d.data+ARRAY_LBA*SS;wr64(a+ENTRY_SIZE+32U,90U);
    uint32_t sum=crc(a,ENTRY_COUNT*ENTRY_SIZE);uint8_t *ba=d.data+BACKUP_ARRAY*SS;
    for(unsigned i=0;i<ENTRY_COUNT*ENTRY_SIZE;++i)ba[i]=a[i];
    wr32(d.data+SS+88U,sum);refresh_crc(d.data+SS);
    wr32(d.data+BACKUP_HEADER*SS+88U,sum);refresh_crc(d.data+BACKUP_HEADER*SS);
    assert(partition_scan(memread,&d,d.sectors,out,PARTITION_SCAN_MAX,&n)==PARTITION_SCAN_BAD_TABLE);
    wipe(&d);d.data[510]=0x55;d.data[511]=0xAA;mbr_entry(d.data,0U,0x0CU,10U,100U);
    assert(partition_scan(memread,&d,d.sectors,out,PARTITION_SCAN_MAX,&n)==PARTITION_SCAN_OK&&n==1U);
    d.sectors=50U;
    assert(partition_scan(memread,&d,d.sectors,out,PARTITION_SCAN_MAX,&n)==PARTITION_SCAN_BAD_TABLE);
    d.sectors=SECTORS;wipe(&d);d.data[510]=0x55;d.data[511]=0xAA;
    mbr_entry(d.data,0U,0x0FU,100U,500U);
    uint8_t *ebr=d.data+100U*SS;ebr[510]=0x55;ebr[511]=0xAA;
    mbr_entry(ebr,0U,0x0CU,1U,20U);mbr_entry(ebr,1U,0x0FU,0U,500U);
    assert(partition_scan(memread,&d,d.sectors,out,PARTITION_SCAN_MAX,&n)==PARTITION_SCAN_BAD_TABLE);
    free(d.data);return 0;
}
