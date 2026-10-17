#include "net_tls.h"
#include <stdio.h>
#include <string.h>

static int expect(const char *host, int wanted)
{
    int actual = net_tls_hostname_valid(host);
    if (actual != wanted) {
        printf("hostname policy failed for '%s': got %d expected %d\n",
               host ? host : "(null)", actual, wanted);
        return 0;
    }
    return 1;
}

int main(void)
{
    char max_name[254];
    size_t used = 0U;
    for (unsigned int label = 0U; label < 4U; ++label) {
        unsigned int n = label == 3U ? 61U : 63U;
        for (unsigned int i = 0U; i < n; ++i) max_name[used++] = 'a';
        if (label != 3U) max_name[used++] = '.';
    }
    max_name[used] = 0;

    if (!expect("example.com", 1) ||
        !expect("api.example.co.uk", 1) ||
        !expect("localhost", 1) ||
        !expect("192.0.2.1", 1) ||
        !expect(max_name, 1) ||
        !expect(0, 0) ||
        !expect("", 0) ||
        !expect(".example.com", 0) ||
        !expect("example.com.", 0) ||
        !expect("example..com", 0) ||
        !expect("-example.com", 0) ||
        !expect("example-.com", 0) ||
        !expect("a.-b.example", 0) ||
        !expect("bad/name.example", 0) ||
        !expect("name with spaces", 0) ||
        !expect("a", 1))
        return 1;

    char too_long[256];
    for (size_t i = 0U; i < sizeof(too_long) - 1U; ++i) too_long[i] = 'a';
    too_long[sizeof(too_long) - 1U] = 0;
    if (!expect(too_long, 0)) return 2;

    char too_long_dotted[256];
    size_t pos = 0U;
    for (unsigned int label = 0U; label < 4U; ++label) {
        for (unsigned int i = 0U; i < 63U; ++i) too_long_dotted[pos++] = 'a';
        if (label != 3U) too_long_dotted[pos++] = '.';
    }
    too_long_dotted[pos] = 0;
    if (!expect(too_long_dotted, 0)) return 3;

    puts("TLS hostname validation tests passed.");
    return 0;
}
