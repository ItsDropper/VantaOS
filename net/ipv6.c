#include "net_ipv6.h"
#include "timer.h"
#include <stddef.h>
#include <stdint.h>
#define ETH_IPV6 0x86DDU
#define IP6_HDR 40U
#define ICMP6 58U
#define ICMP6_RS 133U
#define ICMP6_RA 134U
#define ICMP6_NS 135U
#define ICMP6_NA 136U
#define ICMP6_ECHO 128U
#define ICMP6_ECHO_REPLY 129U
#define DAD_TICKS 100U
#define RS_INTERVAL 400U
typedef struct {uint8_t address[16],mac[6];uint32_t expires;uint8_t valid;} ip6_neighbor_t;
typedef struct {net_device_t*d;net_ipv6_address_t ll,global,router,pending;uint32_t expires,router_expires,dad_started,last_rs;uint8_t init,tentative,valid,duplicate,router_valid,rs_sent,rs_count,pending_valid,neighbor_next;ip6_neighbor_t neighbors[8];} ip6_state_t;
static ip6_state_t states[NET_DEVICE_MAX];
static net_device_t *ping_device;
static net_ipv6_address_t ping_target;
static uint16_t ping_identifier,ping_sequence;
static volatile uint8_t ping_active,ping_received;
static uint16_t r16(const uint8_t*p){return(uint16_t)(((uint16_t)p[0]<<8)|p[1]);}
static uint32_t r32(const uint8_t*p){return((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void w16(uint8_t*p,uint16_t v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static int zero(const uint8_t*a){uint8_t v=0;for(uint32_t i=0;i<16;i++)v|=a[i];return v==0;}
static int eq(const uint8_t*a,const uint8_t*b){for(uint32_t i=0;i<16;i++)if(a[i]!=b[i])return 0;return 1;}
static int linklocal(const uint8_t*a){return a[0]==0xFEU&&(a[1]&0xC0U)==0x80U;}
static uint32_t sum_bytes(uint32_t sum,const uint8_t*p,size_t n){while(n>=2){sum+=((uint32_t)p[0]<<8)|p[1];p+=2;n-=2;}if(n)sum+=(uint32_t)p[0]<<8;while(sum>>16)sum=(sum&0xFFFFU)+(sum>>16);return sum;}
static uint16_t checksum(const uint8_t*s,const uint8_t*d,const uint8_t*p,size_t n,uint8_t next_header){uint32_t sum=0;sum=sum_bytes(sum,s,16);sum=sum_bytes(sum,d,16);uint8_t ph[8]={(uint8_t)(n>>24),(uint8_t)(n>>16),(uint8_t)(n>>8),(uint8_t)n,0,0,0,next_header};sum=sum_bytes(sum,ph,8);sum=sum_bytes(sum,p,n);while(sum>>16)sum=(sum&0xFFFFU)+(sum>>16);return(uint16_t)~sum;}
static ip6_state_t*get_state(net_device_t*d,int create){if(!d)return 0;ip6_state_t*free_s=0;for(uint32_t i=0;i<NET_DEVICE_MAX;i++){if(states[i].d==d)return&states[i];if(!states[i].d&&!free_s)free_s=&states[i];}if(!create||!free_s)return 0;ip6_state_t*s=free_s;*s=(ip6_state_t){0};s->d=d;s->init=1;s->ll.bytes[0]=0xFE;s->ll.bytes[1]=0x80;s->ll.bytes[8]=d->mac[0]^2U;s->ll.bytes[9]=d->mac[1];s->ll.bytes[10]=d->mac[2];s->ll.bytes[11]=0xFF;s->ll.bytes[12]=0xFE;s->ll.bytes[13]=d->mac[3];s->ll.bytes[14]=d->mac[4];s->ll.bytes[15]=d->mac[5];return s;}
static int neighbor_lookup(ip6_state_t*s,const uint8_t*address,uint8_t mac[6]){uint32_t now=timer_get_ticks();for(uint32_t i=0;i<8U;i++){ip6_neighbor_t*n=&s->neighbors[i];if(n->valid&&(int32_t)(now-n->expires)>=0)n->valid=0;if(n->valid&&eq(n->address,address)){for(uint32_t j=0;j<6U;j++)mac[j]=n->mac[j];return 1;}}return 0;}
static void neighbor_learn(ip6_state_t*s,const uint8_t*address,const uint8_t*mac){if(!s||!address||!mac||zero(address)||address[0]==0xFFU)return;uint8_t any=0;for(uint32_t i=0;i<6U;i++)any|=mac[i];if(!any||(mac[0]&1U))return;uint32_t now=timer_get_ticks();ip6_neighbor_t*slot=0;for(uint32_t i=0;i<8U;i++){ip6_neighbor_t*n=&s->neighbors[i];if(n->valid&&(int32_t)(now-n->expires)>=0)n->valid=0;if(n->valid&&eq(n->address,address)){slot=n;break;}if(!n->valid&&!slot)slot=n;}if(!slot)slot=&s->neighbors[s->neighbor_next++%8U];for(uint32_t i=0;i<16U;i++)slot->address[i]=address[i];for(uint32_t i=0;i<6U;i++)slot->mac[i]=mac[i];slot->expires=now+180000U;slot->valid=1U;}
static void mcast_mac(uint8_t*m,const uint8_t*a){m[0]=0x33;m[1]=0x33;m[2]=a[12];m[3]=a[13];m[4]=a[14];m[5]=a[15];}
static int send_packet(net_device_t*d,const uint8_t*dm,const uint8_t*s,const uint8_t*dst,uint8_t next_header,uint8_t hop,const uint8_t*p,size_t n){if(!d||!d->link_up||!dm||!s||!dst||!p||!net_ipv6_payload_fits_mtu(d->mtu,n))return 0;uint8_t f[NET_ETHERNET_HEADER_SIZE+NET_MTU_DEFAULT];for(uint32_t i=0;i<6;i++){f[i]=dm[i];f[6+i]=d->mac[i];}w16(f+12,ETH_IPV6);uint8_t*ip=f+14;ip[0]=0x60;ip[1]=ip[2]=ip[3]=0;w16(ip+4,(uint16_t)n);ip[6]=next_header;ip[7]=hop;for(uint32_t i=0;i<16;i++){ip[8+i]=s[i];ip[24+i]=dst[i];}for(size_t i=0;i<n;i++)ip[40+i]=p[i];return net_device_transmit(d,f,14U+40U+n);}
static void solicited(const uint8_t*t,uint8_t*d){for(uint32_t i=0;i<16;i++)d[i]=0;d[0]=0xFF;d[1]=2;d[11]=1;d[12]=0xFF;d[13]=t[13];d[14]=t[14];d[15]=t[15];}
static void send_rs(ip6_state_t*s){static const uint8_t dst[16]={0xFF,2,0,0,0,0,0,0,0,0,0,0,0,0,0,2};uint8_t mac[6]={0x33,0x33,0,0,0,2};uint8_t p[16]={ICMP6_RS,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0};for(uint32_t i=0;i<6;i++)p[10+i]=s->d->mac[i];w16(p+2,checksum(s->ll.bytes,dst,p,16,ICMP6_RS));(void)send_packet(s->d,mac,s->ll.bytes,dst,ICMP6,255,p,16);s->last_rs=timer_get_ticks();s->rs_sent=1;if(s->rs_count<255U)++s->rs_count;}
static void send_dad(ip6_state_t*s){uint8_t dst[16],mac[6],src[16]={0};solicited(s->global.bytes,dst);mcast_mac(mac,dst);uint8_t p[24]={ICMP6_NS,0,0,0,0,0,0,0};for(uint32_t i=0;i<16;i++)p[8+i]=s->global.bytes[i];w16(p+2,checksum(src,dst,p,24,ICMP6_NS));(void)send_packet(s->d,mac,src,dst,ICMP6,255,p,24);}
static void send_neighbor_ns(ip6_state_t*s,const uint8_t*target,const uint8_t*source){uint8_t dst[16],mac[6];solicited(target,dst);mcast_mac(mac,dst);uint8_t p[32]={ICMP6_NS,0,0,0,0,0,0,0};for(uint32_t i=0;i<16U;i++)p[8U+i]=target[i];p[24]=1U;p[25]=1U;for(uint32_t i=0;i<6U;i++)p[26U+i]=s->d->mac[i];w16(p+2,checksum(source,dst,p,sizeof(p),ICMP6_NS));(void)send_packet(s->d,mac,source,dst,ICMP6,255U,p,sizeof(p));}
static int neighbor_resolve(ip6_state_t*s,const uint8_t*target,const uint8_t*source,uint8_t mac[6],uint32_t timeout_ms){if(neighbor_lookup(s,target,mac))return 1;s->pending_valid=1U;for(uint32_t i=0;i<16U;i++)s->pending.bytes[i]=target[i];uint32_t start=timer_get_ticks(),limit=timeout_ms/10U,next=start;uint32_t retries=0;while((uint32_t)(timer_get_ticks()-start)<limit){uint32_t now=timer_get_ticks();if(retries<3U&&(int32_t)(now-next)>=0){send_neighbor_ns(s,target,source);next=now+100U;++retries;}if(neighbor_lookup(s,target,mac)){s->pending_valid=0U;return 1;}(void)net_poll(16U);__asm__ volatile("pause");}s->pending_valid=0U;return neighbor_lookup(s,target,mac);}
static int local_target(ip6_state_t*s,const uint8_t*a){return eq(a,s->ll.bytes)||(s->valid&&eq(a,s->global.bytes));}
static void send_na(ip6_state_t*s,const uint8_t*f,const uint8_t*src,const uint8_t*target){uint8_t dst[16],mac[6];if(zero(src)){static const uint8_t all[16]={0xFF,2,0,0,0,0,0,0,0,0,0,0,0,0,0,1};for(uint32_t i=0;i<16;i++)dst[i]=all[i];mcast_mac(mac,dst);}else{for(uint32_t i=0;i<16;i++)dst[i]=src[i];for(uint32_t i=0;i<6;i++)mac[i]=f[6+i];}uint8_t p[32]={ICMP6_NA,0,0,0,0x60,0,0,0};if(zero(src))p[4]=0x20U;for(uint32_t i=0;i<16;i++)p[8+i]=target[i];p[24]=2;p[25]=1;for(uint32_t i=0;i<6;i++)p[26+i]=s->d->mac[i];w16(p+2,checksum(target,dst,p,32,ICMP6_NA));(void)send_packet(s->d,mac,target,dst,ICMP6,255,p,32);}
static void prefix_update(ip6_state_t*s,const uint8_t*o,uint32_t now){uint8_t plen=o[2],flags=o[3];uint32_t life=r32(o+4);const uint8_t*pre=o+16;if(plen!=64U||!(flags&0x40U)||!life||pre[0]==0xFFU||linklocal(pre)||zero(pre))return;net_ipv6_address_t a={{0}};for(uint32_t i=0;i<8;i++)a.bytes[i]=pre[i];a.bytes[8]=s->d->mac[0]^2U;a.bytes[9]=s->d->mac[1];a.bytes[10]=s->d->mac[2];a.bytes[11]=0xFF;a.bytes[12]=0xFE;a.bytes[13]=s->d->mac[3];a.bytes[14]=s->d->mac[4];a.bytes[15]=s->d->mac[5];uint32_t ticks=life>21474836U?2147483600U:life*100U;if(s->valid&&eq(a.bytes,s->global.bytes)){s->expires=now+ticks;return;}s->global=a;s->valid=0;s->tentative=1;s->duplicate=0;s->dad_started=now;s->expires=now+ticks;send_dad(s);}
static void receive6(net_device_t*d,const uint8_t*f,size_t n,uint16_t type,size_t eh,void*ctx){(void)type;(void)ctx;if(!d||!f||(eh!=14U&&eh!=18U)||n<eh+40U||r16(f+(eh==18U?16U:12U))!=ETH_IPV6)return;const uint8_t*ip=f+eh;if((ip[0]>>4)!=6U||(ip[6]!=ICMP6&&ip[6]!=17U))return;size_t plen=r16(ip+4);if(plen<8U||plen>n-eh-40U)return;const uint8_t*src=ip+8,*dst=ip+24,*p=ip+40;ip6_state_t*s=get_state(d,1);if(!s)return;if(ip[6]==17U){if(local_target(s,dst)||dst[0]==0xFFU)net_udp_receive_ipv6(d,src,dst,p,plen);return;}if(checksum(src,dst,p,plen,ICMP6)!=0U)return;uint32_t now=timer_get_ticks();
 if(p[0]==ICMP6_RA){if(p[1]||ip[7]!=255U||plen<16U||!linklocal(src))return;neighbor_learn(s,src,f+6);uint16_t life=r16(p+6);if(life){for(uint32_t i=0;i<16;i++)s->router.bytes[i]=src[i];s->router_valid=1;uint32_t t=(uint32_t)life*100U;s->router_expires=now+(t>2147483600U?2147483600U:t);}else{s->router_valid=0;s->router=(net_ipv6_address_t){{0}};}size_t at=16;while(at<plen){if(plen-at<2U||!p[at+1])return;size_t olen=(size_t)p[at+1]*8U;if(olen>plen-at)return;if(p[at]==3U&&olen==32U)prefix_update(s,p+at,now);at+=olen;}return;}
 if(p[0]==ICMP6_NS||p[0]==ICMP6_NA){if(p[1]||ip[7]!=255U||plen<24U)return;const uint8_t*t=p+8;if(s->tentative&&eq(t,s->global.bytes)){s->duplicate=1;s->tentative=0;s->valid=0;return;}if(p[0]==ICMP6_NS){if(!zero(src))neighbor_learn(s,src,f+6);if(local_target(s,t))send_na(s,f,src,t);}else{size_t at=24U;while(at<plen){if(plen-at<2U||!p[at+1U])return;size_t olen=(size_t)p[at+1U]*8U;if(olen>plen-at)return;if(p[at]==2U&&olen==8U&&s->pending_valid&&eq(t,s->pending.bytes))neighbor_learn(s,t,p+at+2U);at+=olen;}if(s->pending_valid&&eq(t,s->pending.bytes))neighbor_learn(s,t,f+6);}return;}
 if(p[0]==ICMP6_ECHO_REPLY&&p[1]==0U&&ping_active&&ping_device==d&&eq(src,ping_target.bytes)&&plen>=8U&&r16(p+4U)==ping_identifier&&r16(p+6U)==ping_sequence){ping_received=1U;return;}
 if(p[0]==ICMP6_ECHO&&p[1]==0U&&local_target(s,dst)&&plen>=8U&&plen<=NET_MTU_DEFAULT-40U){uint8_t reply[NET_MTU_DEFAULT-40U];for(size_t i=0;i<plen;i++)reply[i]=p[i];reply[0]=ICMP6_ECHO_REPLY;reply[2]=reply[3]=0;w16(reply+2,checksum(dst,src,reply,plen,ICMP6));(void)send_packet(d,f+6,dst,src,ICMP6,64,reply,plen);}
}
int net_ipv6_initialize(void){for(uint32_t i=0;i<NET_DEVICE_MAX;i++)states[i]=(ip6_state_t){0};return net_ethernet_handler_register(ETH_IPV6,receive6,0);}

int net_ipv6_send_payload(net_device_t*d,const uint8_t*destination,const uint8_t*source,uint8_t next_header,const void*payload,size_t length){if(!d||!destination||!source||!payload||next_header!=17U||!d->link_up||!net_ipv6_payload_fits_mtu(d->mtu,length)||zero(destination)||zero(source))return 0;ip6_state_t*s=get_state(d,1);if(!s)return 0;int multicast=destination[0]==0xFFU;uint8_t mac[6],next_hop[16];if(multicast){mcast_mac(mac,destination);}else{if(linklocal(destination)){if(!eq(source,s->ll.bytes))return 0;for(uint32_t i=0;i<16U;i++)next_hop[i]=destination[i];}else{if(!s->valid||!eq(source,s->global.bytes))return 0;int on_link=1;for(uint32_t i=0;i<8U;i++)if(destination[i]!=s->global.bytes[i])on_link=0;if(on_link){for(uint32_t i=0;i<16U;i++)next_hop[i]=destination[i];}else{if(!s->router_valid)return 0;for(uint32_t i=0;i<16U;i++)next_hop[i]=s->router.bytes[i];}}if(!neighbor_resolve(s,next_hop,source,mac,3000U))return 0;}return send_packet(d,mac,source,destination,next_header,64U,(const uint8_t*)payload,length);}
void net_ipv6_poll(void){uint32_t now=timer_get_ticks();for(uint32_t i=0;i<net_device_count();i++){net_device_t*d=net_device_get(i);ip6_state_t*s=get_state(d,1);if(!s||!d->link_up)continue;if(s->tentative&&!s->duplicate&&(uint32_t)(now-s->dad_started)>=DAD_TICKS){s->tentative=0;s->valid=1;}if(s->valid&&(int32_t)(now-s->expires)>=0){s->valid=0;s->global=(net_ipv6_address_t){{0}};}if(s->router_valid&&(int32_t)(now-s->router_expires)>=0){s->router_valid=0;s->router=(net_ipv6_address_t){{0}};s->rs_count=0;s->rs_sent=0;}if(!s->router_valid&&s->rs_count<3U&&(!s->rs_sent||(uint32_t)(now-s->last_rs)>=RS_INTERVAL))send_rs(s);}}
int net_ipv6_get_status(net_device_t*d,net_ipv6_address_t*ll,net_ipv6_address_t*global,net_ipv6_address_t*router){ip6_state_t*s=get_state(d,1);if(!s)return 0;if(ll)*ll=s->ll;if(global)*global=s->valid?s->global:(net_ipv6_address_t){{0}};if(router)*router=s->router_valid?s->router:(net_ipv6_address_t){{0}};return 1;}

int net_ipv6_ping(net_device_t*d,const net_ipv6_address_t*destination,uint32_t timeout_ms,uint32_t*elapsed_ms){
 if(!d||!destination||!d->link_up||timeout_ms<500U||timeout_ms>30000U||zero(destination->bytes)||destination->bytes[0]==0xFFU)return 0;
 ip6_state_t*s=get_state(d,1);if(!s)return 0;
 const uint8_t*target=destination->bytes;const uint8_t*source=s->ll.bytes;uint8_t next_hop[16];
 if(linklocal(target)){for(uint32_t i=0;i<16U;i++)next_hop[i]=target[i];}
 else{
  if(!s->valid)return 0;
  source=s->global.bytes;
  int on_link=1;for(uint32_t i=0;i<8U;i++)if(target[i]!=s->global.bytes[i])on_link=0;
  if(on_link){for(uint32_t i=0;i<16U;i++)next_hop[i]=target[i];}
  else{if(!s->router_valid)return 0;for(uint32_t i=0;i<16U;i++)next_hop[i]=s->router.bytes[i];}
 }
 uint8_t destination_mac[6];uint32_t resolve_ms=timeout_ms/3U;if(resolve_ms<500U)resolve_ms=500U;
 if(!neighbor_resolve(s,next_hop,source,destination_mac,resolve_ms))return 0;
 uint8_t packet[24]={ICMP6_ECHO,0,0,0,0,0,0,0};ping_identifier=(uint16_t)(timer_get_ticks()^d->mac[1]^((uint16_t)d->mac[4]<<8));if(!ping_identifier)ping_identifier=1U;
 ping_sequence=(uint16_t)timer_get_ticks();w16(packet+4U,ping_identifier);w16(packet+6U,ping_sequence);
 static const uint8_t marker[]="VANTAOS-PING6";for(size_t i=0;i<sizeof(marker)-1U;i++)packet[8U+i]=marker[i];
 size_t packet_length=8U+sizeof(marker)-1U;w16(packet+2U,checksum(source,target,packet,packet_length,ICMP6));
 ping_device=d;ping_target=*destination;ping_received=0U;ping_active=1U;
 uint32_t start=timer_get_ticks();int sent=send_packet(d,destination_mac,source,target,ICMP6,64U,packet,packet_length);
 uint32_t limit=timeout_ms/10U;
 while(sent&&!ping_received&&(uint32_t)(timer_get_ticks()-start)<limit){(void)net_poll(16U);__asm__ volatile("pause");}
 uint32_t elapsed=(timer_get_ticks()-start)*10U;int success=sent&&ping_received;
 ping_active=0U;ping_device=0;if(success&&elapsed_ms)*elapsed_ms=elapsed;return success;
}
