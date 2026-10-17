#ifndef VANTAOS_TLS_POLICY_H
#define VANTAOS_TLS_POLICY_H
/* Strict ASCII DNS-name syntax validation only; this does not authenticate peers. */
static inline int net_tls_hostname_valid(const char *host)
{
    unsigned int total = 0U, label = 0U;
    if (!host || !host[0]) return 0;
    for (unsigned int i = 0U; ; ++i) {
        unsigned char c = (unsigned char)host[i];
        if (c && ++total > 253U) return 0;
        if (c == 0U || c == '.') {
            if (!label || label > 63U || host[i - 1U] == '-') return 0;
            if (!c) return 1;
            label = 0U;
            continue;
        }
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-')) return 0;
        if (!label && c == '-') return 0;
        ++label;
    }
}
#endif
