/* Copyright (c) 2026 ItsDropper. MIT License; see LICENSE. */
#include "nvx_gsp_ce.h"
#include <stdint.h>
#include <limits.h>
static uint32_t header(uint32_t method,uint32_t count,uint32_t subch){return (UINT32_C(1)<<29U)|(count<<16U)|(subch<<13U)|((method>>2U)&0xFFFU);}
static void emit(volatile uint32_t *p,size_t *n,uint32_t v){__atomic_store_n(&p[*n],v,__ATOMIC_RELAXED);++*n;}
