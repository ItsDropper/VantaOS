#include "net_dns.h"
#include "net_ipv6.h"
#include "timer.h"
#include <stddef.h>
#include <stdint.h>
#define DNS_CLIENT_PORT 53000U
#define DNS_SERVER_PORT 53U
#define DNS_MAX_MESSAGE 512U
#define DNS_HEADER_SIZE 12U
#define DNS_RETRY_TICKS 100U
#define DNS_CACHE_CAPACITY 8U
typedef struct { net_device_t *device; uint32_t server,address,expires; char host[254]; uint8_t valid; } dns_cache_entry_t;
typedef struct { net_device_t* device; uint32_t server,result,ttl; uint16_t id,question_length; uint8_t question[264]; uint8_t active,done,failed; } dns_query_state_t;
static dns_query_state_t query_state;
typedef struct {
    net_device_t* device;
    uint8_t server[16], result[16];
    uint32_t ttl;
    uint16_t id, question_length;
    uint8_t question[264];
    uint8_t active, done, failed;
} dns6_query_state_t;
typedef struct {
    net_device_t* device;
    uint8_t server[16], address[16];
    uint32_t expires;
    char host[254];
    uint8_t valid;
} dns6_cache_entry_t;
static dns6_query_state_t query6_state;
static dns6_cache_entry_t cache6[DNS_CACHE_CAPACITY];
static uint32_t cache6_next;
static dns_cache_entry_t cache[DNS_CACHE_CAPACITY];
static uint32_t cache_next;
static uint16_t read_be16(const uint8_t* p){return (uint16_t)(((uint16_t)p[0]<<8)|p[1]);}
static uint32_t read_be32(const uint8_t* p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void normalize_host(const char *host,char out[254])
{
    size_t i=0;
    if(!host){out[0]=0;return;}
    while(host[i]&&i<253U){char c=host[i];out[i]=(c>='A'&&c<='Z')?(char)(c-'A'+'a'):c;++i;}
    out[i]=0;
}
static int cache_lookup(net_device_t*d,uint32_t server,const char*host,uint32_t*out)
{
    char normalized[254];normalize_host(host,normalized);uint32_t now=timer_get_ticks();
    for(uint32_t i=0;i<DNS_CACHE_CAPACITY;i++){
        if(!cache[i].valid)continue;
        if((int32_t)(now-cache[i].expires)>=0){cache[i].valid=0;continue;}
        if(cache[i].device==d&&cache[i].server==server){
            size_t j=0;while(normalized[j]&&cache[i].host[j]&&normalized[j]==cache[i].host[j])j++;
            if(!normalized[j]&&!cache[i].host[j]){*out=cache[i].address;return 1;}
        }
    }
    return 0;
}
static void cache_store(net_device_t*d,uint32_t server,const char*host,uint32_t address,uint32_t ttl)
{
    if(!ttl||!address)return;
    char normalized[254];normalize_host(host,normalized);
    uint32_t ticks=ttl>21474836U?2147483600U:ttl*100U;
    dns_cache_entry_t*e=&cache[cache_next++%DNS_CACHE_CAPACITY];
    e->device=d;e->server=server;e->address=address;e->expires=timer_get_ticks()+ticks;e->valid=1;
    size_t i=0;while(normalized[i]&&i<253U){e->host[i]=normalized[i];i++;}e->host[i]=0;
}
static void write_be16(uint8_t* p,uint16_t v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static int skip_name(const uint8_t* p,size_t n,size_t* off)
{
    if (!p || !off || *off >= n) return 0;

    size_t pos = *off;
    size_t resume = 0;
    size_t hops = 0;
    int jumped = 0;
    for (;;) {
        if (pos >= n || ++hops > n) return 0;
        uint8_t len = p[pos++];

        if ((len & 0xC0U) == 0xC0U) {
            if (pos >= n) return 0;
            size_t to = ((size_t)(len & 0x3FU) << 8) | p[pos++];
            /* DNS compression references an earlier occurrence in the message. */
            if (to >= n || to >= pos - 2U) return 0;
            if (!jumped) resume = pos;
            jumped = 1;
            pos = to;
            continue;
        }
        if (len & 0xC0U) return 0;
        if (!len) {
            *off = jumped ? resume : pos;
            return 1;
        }
        if (len > 63U || (size_t)len > n - pos) return 0;
        pos += len;
    }
}
static int name_matches_question(const uint8_t *packet, size_t packet_length,
                                 size_t owner_offset,
                                 const uint8_t *question, size_t question_length)
{
    size_t pos = owner_offset, qpos = 0, hops = 0;
    int jumped = 0;
    if (!packet || !question || question_length < 5U || owner_offset >= packet_length)
        return 0;
    for (;;) {
        if (pos >= packet_length || ++hops > packet_length || qpos >= question_length - 4U)
            return 0;
        uint8_t label = packet[pos++];
        if ((label & 0xC0U) == 0xC0U) {
            if (pos >= packet_length) return 0;
            size_t target = ((size_t)(label & 0x3FU) << 8) | packet[pos++];
            if (target >= packet_length || target >= pos - 2U) return 0;
            jumped = 1;
            pos = target;
            continue;
        }
        if (label & 0xC0U) return 0;
        if (label == 0U)
            return question[qpos] == 0U && qpos + 5U == question_length;
        if (label > 63U || (size_t)label > packet_length - pos ||
            question[qpos] != label || (size_t)label > question_length - qpos - 5U)
            return 0;
        ++qpos;
        for (uint8_t i = 0; i < label; ++i) {
            uint8_t a = packet[pos++], b = question[qpos++];
            if (a >= 'A' && a <= 'Z') a = (uint8_t)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (uint8_t)(b - 'A' + 'a');
            if (a != b) return 0;
        }
    }
    (void)jumped;
}

static void receive_dns(net_device_t* d,uint32_t src,uint16_t sport,uint16_t dport,const uint8_t* p,size_t n,void* ctx)
{
    (void)ctx;
    if(!query_state.active||d!=query_state.device||src!=query_state.server||sport!=DNS_SERVER_PORT||dport!=DNS_CLIENT_PORT||!p||n<DNS_HEADER_SIZE||read_be16(p)!=query_state.id)return;
    uint16_t flags=read_be16(p+2),questions=read_be16(p+4),answers=read_be16(p+6);
    if(!(flags&0x8000U)||(flags&0x7800U)||(flags&0x0200U)||(flags&0x000FU)||questions!=1U||answers>64U){query_state.failed=1U;query_state.done=1U;return;}
    size_t pos=DNS_HEADER_SIZE;
    if(n<DNS_HEADER_SIZE+query_state.question_length){query_state.failed=1U;query_state.done=1U;return;}
    for(uint16_t i=0;i<query_state.question_length;i++){
        uint8_t a=p[DNS_HEADER_SIZE+i],b=query_state.question[i];
        if(a>='A'&&a<='Z')a=(uint8_t)(a-'A'+'a');
        if(b>='A'&&b<='Z')b=(uint8_t)(b-'A'+'a');
        if(a!=b){query_state.failed=1U;query_state.done=1U;return;}
    }
    if(!skip_name(p,n,&pos)||pos+4U>n||read_be16(p+pos)!=1U||read_be16(p+pos+2U)!=1U){query_state.failed=1U;query_state.done=1U;return;}
    pos+=4U;
    for(uint32_t i=0;i<answers;i++){
        size_t owner_offset = pos;
        int owner_matches = name_matches_question(p,n,owner_offset,
                                                  query_state.question,
                                                  query_state.question_length);
        if(!skip_name(p,n,&pos)||pos+10U>n){query_state.failed=1U;query_state.done=1U;return;}
        uint16_t type=read_be16(p+pos),klass=read_be16(p+pos+2U),dl=read_be16(p+pos+8U);uint32_t ttl=read_be32(p+pos+4U);pos+=10U;
        if((size_t)dl>n-pos){query_state.failed=1U;query_state.done=1U;return;}
        if(owner_matches&&type==1U&&klass==1U&&dl==4U){uint32_t ip=read_be32(p+pos);uint8_t first=(uint8_t)(ip>>24);if(ip&&ip!=0xFFFFFFFFU&&first!=127U&&first<224U){query_state.result=ip;query_state.ttl=ttl;query_state.done=1U;return;}}
        pos+=dl;
    }
    query_state.failed=1U;query_state.done=1U;
}
static int build_query(const char* host,uint8_t* p,size_t cap,size_t* out_len,uint16_t id,uint16_t query_type)
{
    if(!host||!p||!out_len||cap<DNS_HEADER_SIZE+6U)return 0;
    size_t hl=0;while(host[hl]){if(hl>=253U)return 0;++hl;}if(!hl)return 0;
    for(size_t i=0;i<DNS_HEADER_SIZE;i++)p[i]=0;
    write_be16(p,id);write_be16(p+2U,0x0100U);write_be16(p+4U,1U);
    size_t out=DNS_HEADER_SIZE,start=0;
    for(size_t i=0;i<=hl;i++){char ch=host[i];if(ch=='.'||ch==0){size_t len=i-start;
        if(!len||len>63U||out+len+6U>cap||host[start]=='-'||host[i-1U]=='-')return 0;
        p[out++]=(uint8_t)len;for(size_t j=start;j<i;j++){char x=host[j];if(!((x>='a'&&x<='z')||(x>='A'&&x<='Z')||(x>='0'&&x<='9')||x=='-'))return 0;p[out++]=(uint8_t)x;}start=i+1U;}}
    p[out++]=0;write_be16(p+out,query_type);out+=2U;write_be16(p+out,1U);out+=2U;*out_len=out;return 1;
}

static int cache6_lookup(net_device_t* device, const uint8_t server[16],
                         const char* host, net_ipv6_address_t* out)
{
    char normalized[254];
    normalize_host(host, normalized);
    uint32_t now = timer_get_ticks();
    for (uint32_t i = 0; i < DNS_CACHE_CAPACITY; ++i) {
        dns6_cache_entry_t* entry = &cache6[i];
        if (!entry->valid) continue;
        if ((int32_t)(now - entry->expires) >= 0) {
            entry->valid = 0U;
            continue;
        }
        if (entry->device != device) continue;
        size_t j = 0U;
        while (j < 16U && entry->server[j] == server[j]) ++j;
        if (j != 16U) continue;
        j = 0U;
        while (normalized[j] && entry->host[j] &&
               normalized[j] == entry->host[j]) ++j;
        if (!normalized[j] && !entry->host[j]) {
            for (uint32_t k = 0; k < 16U; ++k)
                out->bytes[k] = entry->address[k];
            return 1;
        }
    }
    return 0;
}

static void cache6_store(net_device_t* device, const uint8_t server[16],
                         const char* host, const uint8_t address[16],
                         uint32_t ttl)
{
    if (!ttl || !address || address[0] == 0xFFU) return;
    uint8_t any = 0U;
    for (uint32_t i = 0; i < 16U; ++i) any |= address[i];
    if (!any) return;

    char normalized[254];
    normalize_host(host, normalized);
    dns6_cache_entry_t* entry = &cache6[cache6_next++ % DNS_CACHE_CAPACITY];
    entry->device = device;
    for (uint32_t i = 0; i < 16U; ++i) {
        entry->server[i] = server[i];
        entry->address[i] = address[i];
    }
    uint32_t ticks = ttl > 21474836U ? 2147483600U : ttl * 100U;
    entry->expires = timer_get_ticks() + ticks;
    entry->valid = 1U;
    size_t i = 0U;
    while (normalized[i] && i < 253U) {
        entry->host[i] = normalized[i];
        ++i;
    }
    entry->host[i] = 0;
}

static void receive_dns6(net_device_t* device, const uint8_t source[16],
                         const uint8_t destination[16], uint16_t source_port,
                         uint16_t destination_port, const uint8_t* packet,
                         size_t length, void* context)
{
    (void)destination;
    (void)context;
    if (!query6_state.active || device != query6_state.device || !source ||
        !packet || source_port != DNS_SERVER_PORT ||
        destination_port != DNS_CLIENT_PORT || length < DNS_HEADER_SIZE ||
        length > DNS_MAX_MESSAGE || read_be16(packet) != query6_state.id)
        return;
    for (uint32_t i = 0; i < 16U; ++i)
        if (source[i] != query6_state.server[i]) return;

    uint16_t flags = read_be16(packet + 2U);
    uint16_t questions = read_be16(packet + 4U);
    uint16_t answers = read_be16(packet + 6U);
    if (!(flags & 0x8000U) || (flags & 0x7800U) || (flags & 0x0200U) ||
        (flags & 0x000FU) || questions != 1U || answers > 64U) {
        query6_state.failed = 1U;
        query6_state.done = 1U;
        return;
    }
    if (length < DNS_HEADER_SIZE + query6_state.question_length) {
        query6_state.failed = 1U;
        query6_state.done = 1U;
        return;
    }
    for (uint16_t i = 0; i < query6_state.question_length; ++i) {
        uint8_t a = packet[DNS_HEADER_SIZE + i];
        uint8_t b = query6_state.question[i];
        if (a >= 'A' && a <= 'Z') a = (uint8_t)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (uint8_t)(b - 'A' + 'a');
        if (a != b) {
            query6_state.failed = 1U;
            query6_state.done = 1U;
            return;
        }
    }

    size_t pos = DNS_HEADER_SIZE;
    if (!skip_name(packet, length, &pos) || pos + 4U > length ||
        read_be16(packet + pos) != 28U ||
        read_be16(packet + pos + 2U) != 1U) {
        query6_state.failed = 1U;
        query6_state.done = 1U;
        return;
    }
    pos += 4U;
    for (uint32_t i = 0; i < answers; ++i) {
        size_t owner_offset = pos;
        int owner_matches = name_matches_question(packet, length, owner_offset,
                                                   query6_state.question,
                                                   query6_state.question_length);
        if (!skip_name(packet, length, &pos) || pos + 10U > length) {
            query6_state.failed = 1U;
            query6_state.done = 1U;
            return;
        }
        uint16_t type = read_be16(packet + pos);
        uint16_t klass = read_be16(packet + pos + 2U);
        uint32_t ttl = read_be32(packet + pos + 4U);
        uint16_t data_length = read_be16(packet + pos + 8U);
        pos += 10U;
        if ((size_t)data_length > length - pos) {
            query6_state.failed = 1U;
            query6_state.done = 1U;
            return;
        }
        if (owner_matches && type == 28U && klass == 1U && data_length == 16U) {
            uint8_t any = 0U;
            for (uint32_t j = 0; j < 16U; ++j) any |= packet[pos + j];
            if (any && packet[pos] != 0xFFU) {
                for (uint32_t j = 0; j < 16U; ++j)
                    query6_state.result[j] = packet[pos + j];
                query6_state.ttl = ttl;
                query6_state.done = 1U;
                return;
            }
        }
        pos += data_length;
    }
    query6_state.failed = 1U;
    query6_state.done = 1U;
}

int net_dns_resolve_aaaa(net_device_t* device, const char* host,
                         net_ipv6_address_t* out, uint32_t timeout_ms)
{
    if (!device || !host || !out || !device->link_up ||
        timeout_ms < 500U || timeout_ms > 30000U)
        return 0;

    net_ipv6_address_t server = {{0}};
    if (!net_ipv6_get_dns_server(device, &server)) return 0;
    if (cache6_lookup(device, server.bytes, host, out)) return 1;

    query6_state = (dns6_query_state_t){0};
    query6_state.device = device;
    for (uint32_t i = 0; i < 16U; ++i)
        query6_state.server[i] = server.bytes[i];
    query6_state.id = (uint16_t)(timer_get_ticks() ^ device->mac[1] ^
                                  ((uint16_t)device->mac[5] << 8));
    if (!query6_state.id) query6_state.id = 1U;
    query6_state.active = 1U;

    uint8_t packet[DNS_MAX_MESSAGE];
    size_t packet_length = 0U;
    if (!build_query(host, packet, sizeof(packet), &packet_length,
                     query6_state.id, 28U) ||
        !net_udp6_handler_register(DNS_CLIENT_PORT, receive_dns6, 0)) {
        query6_state.active = 0U;
        return 0;
    }
    query6_state.question_length =
        (uint16_t)(packet_length - DNS_HEADER_SIZE);
    for (uint16_t i = 0; i < query6_state.question_length; ++i)
        query6_state.question[i] = packet[DNS_HEADER_SIZE + i];

    int sent = net_udp6_send(device, server.bytes, DNS_CLIENT_PORT,
                             DNS_SERVER_PORT, packet, packet_length);
    uint32_t start = timer_get_ticks(), last = start;
    uint32_t limit = timeout_ms / 10U, retries = 0U;
    while (sent && !query6_state.done &&
           (uint32_t)(timer_get_ticks() - start) < limit) {
        (void)net_poll(16U);
        uint32_t now = timer_get_ticks();
        if ((uint32_t)(now - last) >= DNS_RETRY_TICKS) {
            if (retries >= 3U) break;
            ++retries;
            (void)net_udp6_send(device, server.bytes, DNS_CLIENT_PORT,
                                DNS_SERVER_PORT, packet, packet_length);
            last = now;
        }
        __asm__ volatile("pause");
    }
    query6_state.active = 0U;
    (void)net_udp6_handler_unregister(DNS_CLIENT_PORT, receive_dns6, 0);
    int ok = sent && query6_state.done && !query6_state.failed;
    if (ok) {
        for (uint32_t i = 0; i < 16U; ++i)
            out->bytes[i] = query6_state.result[i];
        cache6_store(device, server.bytes, host, query6_state.result,
                     query6_state.ttl);
    }
    query6_state.device = 0;
    return ok;
}

int net_dns_resolve(net_device_t* d,const char* host,uint32_t* out,uint32_t timeout_ms)
{
    if(!d||!host||!out||!d->link_up||timeout_ms<500U||timeout_ms>30000U||!d->ipv4_configured||!d->ipv4_dns)return 0;
    if(cache_lookup(d,d->ipv4_dns,host,out))return 1;
    query_state=(dns_query_state_t){0};query_state.device=d;query_state.server=d->ipv4_dns;
    query_state.id=(uint16_t)(timer_get_ticks()^d->mac[2]^((uint16_t)d->mac[4]<<8));if(!query_state.id)query_state.id=1U;query_state.active=1U;
    uint8_t packet[DNS_MAX_MESSAGE];size_t plen=0;
    if(!build_query(host,packet,sizeof(packet),&plen,query_state.id,1U)||!net_udp_handler_register(DNS_CLIENT_PORT,receive_dns,0)){query_state.active=0U;return 0;}
    query_state.question_length=(uint16_t)(plen-DNS_HEADER_SIZE);
    for(uint16_t i=0;i<query_state.question_length;i++)query_state.question[i]=packet[DNS_HEADER_SIZE+i];
    int sent=net_udp_send(d,query_state.server,DNS_CLIENT_PORT,DNS_SERVER_PORT,packet,plen);
    uint32_t start=timer_get_ticks(),last=start,limit=timeout_ms/10U,retries=0U;
    while(sent&&!query_state.done&&(uint32_t)(timer_get_ticks()-start)<limit){(void)net_poll(16U);uint32_t now=timer_get_ticks();if((uint32_t)(now-last)>=DNS_RETRY_TICKS){if(retries>=3U)break;++retries;(void)net_udp_send(d,query_state.server,DNS_CLIENT_PORT,DNS_SERVER_PORT,packet,plen);last=now;}__asm__ volatile("pause");}
    query_state.active=0U;(void)net_udp_handler_unregister(DNS_CLIENT_PORT,receive_dns,0);
    int ok=sent&&query_state.done&&!query_state.failed&&query_state.result!=0U;if(ok){*out=query_state.result;cache_store(d,query_state.server,host,query_state.result,query_state.ttl);}query_state.device=0;return ok;
}
