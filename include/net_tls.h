#ifndef VANTAOS_NET_TLS_H
#define VANTAOS_NET_TLS_H
#include <stddef.h>
#include <stdint.h>
#include "net.h"
#include "bearssl.h"
#include "bearssl_ssl.h"
#include "bearssl_x509.h"
/* Caller supplies maintained trust anchors and trusted UTC time. No trust-all mode. */
typedef struct { br_ssl_client_context ssl; br_x509_minimal_context x509; br_sslio_context io; net_device_t *device; uint8_t active; } net_tls_client_t;
int net_tls_connect(net_tls_client_t*,net_device_t*,const char*,uint16_t,uint32_t,const br_x509_trust_anchor*,size_t,uint32_t,uint32_t);
int net_tls_write(net_tls_client_t*,const void*,size_t);
int net_tls_read(net_tls_client_t*,void*,size_t);
void net_tls_close(net_tls_client_t*);
/* Performs a bounded HTTPS GET; response includes HTTP headers. Returns bytes or -1. */
int net_https_get(net_device_t*,const char*,const char*,uint16_t,const br_x509_trust_anchor*,size_t,uint32_t,uint32_t,void*,size_t,uint32_t);
#endif
