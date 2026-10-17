#include "net_dns.h"
#include "timer.h"
#include <stddef.h>
#include <stdint.h>
#define DNS_CLIENT_PORT 53000U
#define DNS_SERVER_PORT 53U
#define DNS_MAX_MESSAGE 512U
#define DNS_HEADER_SIZE 12U
#define DNS_RETRY_TICKS 100U
typedef struct { net_device_t* device; uint32_t server,result; uint16_t id; uint8_t active,done,failed; } dns_query_state_t;
static dns_query_state_t query_state;
static uint16_t read_be16(const uint8_t* p){return (uint16_t)(((uint16_t)p[0]<<8)|p[1]);}
static uint32_t read_be32(const uint8_t* p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void write_be16(uint8_t* p,uint16_t v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static int skip_name(const uint8_t* p,size_t n,size_t* off)
{
    if(!p||!off||*off>=n)return 0;
    size_t pos=*off,resume=0,hops=0;int jumped=0;
    for(;;){if(pos>=n||++hops>n)return 0;uint8_t len=p[pos++];
        if((len&0xC0U)==0xC0U){if(pos>=n)return 0;size_t to=((size_t)(len&0x3FU)<<8)|p[pos++];if(to>=n)return 0;if(!jumped)resume=pos;jumped=1;pos=to;continue;}
        if(len&0xC0U)return 0;if(!len){*off=jumped?resume:pos;return 1;}
        if(len>63U||(size_t)len>n-pos)return 0;pos+=len;
    }
}
static void receive_dns(net_device_t* d,uint32_t src,uint16_t sport,uint16_t dport,const uint8_t* p,size_t n,void* ctx)
{
    (void)ctx;
    if(!query_state.active||d!=query_state.device||src!=query_state.server||sport!=DNS_SERVER_PORT||dport!=DNS_CLIENT_PORT||!p||n<DNS_HEADER_SIZE||read_be16(p)!=query_state.id)return;
    uint16_t flags=read_be16(p+2),questions=read_be16(p+4),answers=read_be16(p+6);
    if(!(flags&0x8000U)||(flags&0x7800U)||(flags&0x0200U)||(flags&0x000FU)||questions!=1U||answers>64U){query_state.failed=1U;query_state.done=1U;return;}
    size_t pos=DNS_HEADER_SIZE;
    if(!skip_name(p,n,&pos)||pos+4U>n||read_be16(p+pos)!=1U||read_be16(p+pos+2U)!=1U){query_state.failed=1U;query_state.done=1U;return;}
    pos+=4U;
    for(uint32_t i=0;i<answers;i++){
        if(!skip_name(p,n,&pos)||pos+10U>n){query_state.failed=1U;query_state.done=1U;return;}
        uint16_t type=read_be16(p+pos),klass=read_be16(p+pos+2U),dl=read_be16(p+pos+8U);pos+=10U;
        if((size_t)dl>n-pos){query_state.failed=1U;query_state.done=1U;return;}
        if(type==1U&&klass==1U&&dl==4U){uint32_t ip=read_be32(p+pos);uint8_t first=(uint8_t)(ip>>24);if(ip&&ip!=0xFFFFFFFFU&&first!=127U&&first<224U){query_state.result=ip;query_state.done=1U;return;}}
        pos+=dl;
    }
    query_state.failed=1U;query_state.done=1U;
}
static int build_query(const char* host,uint8_t* p,size_t cap,size_t* out_len)
{
    if(!host||!p||!out_len||cap<DNS_HEADER_SIZE+6U)return 0;
    size_t hl=0;while(host[hl]){if(hl>=253U)return 0;++hl;}if(!hl)return 0;
    for(size_t i=0;i<DNS_HEADER_SIZE;i++)p[i]=0;
    write_be16(p,query_state.id);write_be16(p+2U,0x0100U);write_be16(p+4U,1U);
    size_t out=DNS_HEADER_SIZE,start=0;
    for(size_t i=0;i<=hl;i++){char ch=host[i];if(ch=='.'||ch==0){size_t len=i-start;
        if(!len||len>63U||out+len+6U>cap||host[start]=='-'||host[i-1U]=='-')return 0;
        p[out++]=(uint8_t)len;for(size_t j=start;j<i;j++){char x=host[j];if(!((x>='a'&&x<='z')||(x>='A'&&x<='Z')||(x>='0'&&x<='9')||x=='-'))return 0;p[out++]=(uint8_t)x;}start=i+1U;}}
    p[out++]=0;write_be16(p+out,1U);out+=2U;write_be16(p+out,1U);out+=2U;*out_len=out;return 1;
}
int net_dns_resolve(net_device_t* d,const char* host,uint32_t* out,uint32_t timeout_ms)
{
    if(!d||!host||!out||!d->link_up||timeout_ms<500U||timeout_ms>30000U||!d->ipv4_configured||!d->ipv4_dns)return 0;
    query_state=(dns_query_state_t){0};query_state.device=d;query_state.server=d->ipv4_dns;
    query_state.id=(uint16_t)(timer_get_ticks()^d->mac[2]^((uint16_t)d->mac[4]<<8));if(!query_state.id)query_state.id=1U;query_state.active=1U;
    uint8_t packet[DNS_MAX_MESSAGE];size_t plen=0;
    if(!build_query(host,packet,sizeof(packet),&plen)||!net_udp_handler_register(DNS_CLIENT_PORT,receive_dns,0)){query_state.active=0U;return 0;}
    int sent=net_udp_send(d,query_state.server,DNS_CLIENT_PORT,DNS_SERVER_PORT,packet,plen);
    uint32_t start=timer_get_ticks(),last=start,limit=timeout_ms/10U;
    while(sent&&!query_state.done&&(uint32_t)(timer_get_ticks()-start)<limit){(void)net_poll(16U);uint32_t now=timer_get_ticks();if((uint32_t)(now-last)>=DNS_RETRY_TICKS){(void)net_udp_send(d,query_state.server,DNS_CLIENT_PORT,DNS_SERVER_PORT,packet,plen);last=now;}__asm__ volatile("pause");}
    query_state.active=0U;(void)net_udp_handler_unregister(DNS_CLIENT_PORT,receive_dns,0);
    int ok=sent&&query_state.done&&!query_state.failed&&query_state.result!=0U;if(ok)*out=query_state.result;query_state.device=0;return ok;
}
