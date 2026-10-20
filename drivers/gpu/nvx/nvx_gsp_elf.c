/*
 * Copyright (c) 2026 ItsDropper
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include "nvx_gsp_elf.h"
#include <string.h>
#define ELF_HEADER_SIZE 64U
#define ELF_PROGRAM_SIZE 56U
#define ELF_SECTION_SIZE 64U
#define ELF_ET_EXEC 2U
#define ELF_ET_DYN 3U
#define ELF_EM_RISCV 243U
#define ELF_PT_LOAD 1U
#define ELF_PF_X 1U
#define ELF_SHT_STRTAB 3U
#define ELF_SHT_NOBITS 8U
static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0]|((uint16_t)p[1]<<8U); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8U)|((uint32_t)p[2]<<16U)|((uint32_t)p[3]<<24U); }
static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p)|((uint64_t)le32(p+4U)<<32U); }
static int range_ok(size_t total,uint64_t off,uint64_t len) { return off<=total&&len<=(uint64_t)total-off; }
static int power2(uint64_t v) { return v&&!(v&(v-1U)); }
static int string_matches(const uint8_t *tab,size_t size,uint32_t off,const char *expected) {
    size_t n=0U;
    if(off>=size) return 0;
    while(expected[n]) { if(n>=128U||n>=size-off||tab[off+n]!=(uint8_t)expected[n]) return 0; ++n; }
    return n<size-off&&tab[off+n]==0U;
}
nvx_gsp_elf_status_t nvx_gsp_elf_parse(const nvx_gsp_component_t *component,
    const char *signature_name,nvx_gsp_elf_info_t *out)
{
    const uint8_t *data; size_t size; uint64_t phoff,shoff,entry;
    uint16_t type,machine,ehsize,phentsize,phnum,shentsize,shnum,shstrndx;
    uint32_t signature_index=0U,signature_matches=0U; uint64_t str_off,str_size;
    nvx_gsp_elf_info_t info={0};
    if(out) memset(out,0,sizeof(*out));
    if(!component||!out||!signature_name||!component->acquired||!component->blob.data)
        return NVX_GSP_ELF_INVALID_ARGUMENT;
    if(component->kind!=NVX_GSP_RUNTIME) return NVX_GSP_ELF_UNSUPPORTED_FORMAT;
    size=component->blob.size; data=component->blob.data;
    if(size<ELF_HEADER_SIZE) return NVX_GSP_ELF_TRUNCATED;
    if(data[0]!=0x7FU||data[1]!='E'||data[2]!='L'||data[3]!='F')
        return NVX_GSP_ELF_BAD_MAGIC;
    if(data[4]!=2U||data[5]!=1U||data[6]!=1U)
        return NVX_GSP_ELF_UNSUPPORTED_FORMAT;
    type=le16(data+16U); machine=le16(data+18U);
    if((type!=ELF_ET_EXEC&&type!=ELF_ET_DYN)||machine!=ELF_EM_RISCV||le32(data+20U)!=1U)
        return NVX_GSP_ELF_UNSUPPORTED_FORMAT;
    entry=le64(data+24U); phoff=le64(data+32U); shoff=le64(data+40U);
    info.flags=le32(data+48U); ehsize=le16(data+52U);
    phentsize=le16(data+54U); phnum=le16(data+56U);
    shentsize=le16(data+58U); shnum=le16(data+60U); shstrndx=le16(data+62U);
    if(ehsize!=ELF_HEADER_SIZE||!phnum||phnum>128U||phentsize!=ELF_PROGRAM_SIZE)
        return NVX_GSP_ELF_HEADER_INVALID;
    if(!range_ok(size,phoff,(uint64_t)phnum*phentsize))
        return NVX_GSP_ELF_PROGRAM_TABLE_INVALID;
    if(!shnum||shnum>256U||shentsize!=ELF_SECTION_SIZE||shstrndx>=shnum||
       !range_ok(size,shoff,(uint64_t)shnum*shentsize))
        return NVX_GSP_ELF_SECTION_TABLE_INVALID;
    const uint8_t *strhdr=data+shoff+(uint64_t)shstrndx*shentsize;
    if(le32(strhdr+4U)!=ELF_SHT_STRTAB) return NVX_GSP_ELF_STRING_TABLE_INVALID;
    str_off=le64(strhdr+24U); str_size=le64(strhdr+32U);
    if(!str_size||!range_ok(size,str_off,str_size)) return NVX_GSP_ELF_STRING_TABLE_INVALID;
    for(uint32_t i=0;i<shnum;i++) {
        const uint8_t *sh=data+shoff+(uint64_t)i*shentsize;
        uint32_t name=le32(sh),stype=le32(sh+4U);
        uint64_t off=le64(sh+24U),bytes=le64(sh+32U),align=le64(sh+48U);
        if(!string_matches(data+str_off,(size_t)str_size,name,"")) {
            /* Validate termination separately; names may not match the target. */
            if(name>=str_size||!memchr(data+str_off+name,0,(size_t)(str_size-name)))
                return NVX_GSP_ELF_STRING_TABLE_INVALID;
        }
        if(align>1U&&!power2(align)) return NVX_GSP_ELF_SECTION_TABLE_INVALID;
        if(stype!=ELF_SHT_NOBITS&&!range_ok(size,off,bytes))
            return NVX_GSP_ELF_SECTION_TABLE_INVALID;
        if(string_matches(data+str_off,(size_t)str_size,name,signature_name)) {
            if(stype==ELF_SHT_NOBITS||bytes==0U) return NVX_GSP_ELF_SIGNATURE_INVALID;
            ++signature_matches; signature_index=i; info.signature_offset=off; info.signature_size=bytes;
        }
    }
    if(signature_matches==0U) return NVX_GSP_ELF_SIGNATURE_MISSING;
    if(signature_matches!=1U) return NVX_GSP_ELF_SIGNATURE_INVALID;
    info.signature_section_index=signature_index;
    info.type=type; info.machine=machine; info.entry=entry;
    info.program_count=phnum; info.section_count=shnum;
    for(uint32_t i=0;i<phnum;i++) {
        const uint8_t *ph=data+phoff+(uint64_t)i*phentsize;
        uint32_t ptype=le32(ph),flags=le32(ph+4U);
        uint64_t off=le64(ph+8U),vaddr=le64(ph+16U),paddr=le64(ph+24U);
        uint64_t filesz=le64(ph+32U),memsz=le64(ph+40U),align=le64(ph+48U);
        if(ptype!=ELF_PT_LOAD) continue;
        if(info.load_segment_count>=NVX_GSP_ELF_MAX_LOAD_SEGMENTS||filesz>memsz||
           !range_ok(size,off,filesz)||vaddr>UINT64_MAX-memsz||
           (align>1U&&(!power2(align)||(off%align)!=(vaddr%align))))
            return NVX_GSP_ELF_LOAD_SEGMENT_INVALID;
        nvx_gsp_elf_segment_t *seg=&info.load_segments[info.load_segment_count++];
        *seg=(nvx_gsp_elf_segment_t){off,vaddr,paddr,filesz,memsz,align,flags};
    }
    if(!info.load_segment_count) return NVX_GSP_ELF_LOAD_SEGMENT_INVALID;
    int entry_valid=0;
    for(uint32_t i=0;i<info.load_segment_count;i++) {
        const nvx_gsp_elf_segment_t *seg=&info.load_segments[i];
        if((seg->flags&ELF_PF_X)&&entry>=seg->virtual_address&&
           entry-seg->virtual_address<seg->memory_size) { entry_valid=1; break; }
    }
    if(!entry_valid) return NVX_GSP_ELF_ENTRY_INVALID;
    *out=info; return NVX_GSP_ELF_OK;
}
