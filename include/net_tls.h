#ifndef VANTAOS_NET_TLS_H
#define VANTAOS_NET_TLS_H
#include <stddef.h>
#include <stdint.h>
#include "net.h"
#include "bearssl.h"
#include "bearssl_ssl.h"
#include "bearssl_x509.h"
/* Strict DNS hostname validation shared by TLS callers and regression tests.
 * Numeric IPv4 literals are syntactically accepted as DNS labels, but callers
 * still need certificate/IP-SAN verification supported by the TLS verifier. */
static inline int net_tls_hostname_valid(const char *host)
{
    size_t length = 0U;
    size_t label_length = 0U;
    if (!host || !host[0]) return 0;
    for (size_t i = 0U; ; ++i) {
        unsigned char c = (unsigned char)host[i];
        if (c == 0U || c == '.') {
            if (label_length == 0U || label_length > 63U ||
                host[i - 1U] == '-') return 0;
            if (c == 0U) break;
            label_length = 0U;
            continue;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-')) return 0;
        if (label_length == 0U && c == '-') return 0;
        ++label_length;
        if (++length > 253U) return 0;
    }
    return 1;
}
/* Caller supplies maintained trust anchors and trusted UTC time. No trust-all mode. */
typedef struct { br_ssl_client_context ssl; br_x509_minimal_context x509; br_sslio_context io; net_device_t *device; uint8_t active; } net_tls_client_t;
int net_tls_connect(net_tls_client_t*,net_device_t*,const char*,uint16_t,uint32_t,const br_x509_trust_anchor*,size_t,uint32_t,uint32_t);
int net_tls_write(net_tls_client_t*,const void*,size_t);
int net_tls_read(net_tls_client_t*,void*,size_t);
void net_tls_close(net_tls_client_t*);
/* Performs a bounded HTTPS GET; response includes HTTP headers. Returns bytes or -1. */
int net_https_get(net_device_t*,const char*,const char*,uint16_t,const br_x509_trust_anchor*,size_t,uint32_t,uint32_t,void*,size_t,uint32_t);
#endif
