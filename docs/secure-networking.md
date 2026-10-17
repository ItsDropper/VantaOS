# TLS and HTTPS client API

VantaOS now builds the pinned BearSSL source as part of the freestanding kernel and
provides `net_tls_connect`, `net_tls_read`, `net_tls_write`, `net_tls_close`,
and a bounded `net_https_get` helper over the existing IPv4 DNS/TCP stack.

## Security contract

- The caller must supply a non-empty set of `br_x509_trust_anchor` entries from a
  maintained CA store. An empty store is rejected; there is no trust-all option.
- The caller must supply trusted UTC time as days since the Unix epoch and seconds
  within that day. VantaOS does not yet have a trusted wall-clock/time-sync service,
  so callers must not derive this value from the uptime timer.
- TLS entropy comes from `crypto_rng_fill`; connection setup fails closed if the
  CPU's RDRAND source is unavailable or fails.
- `net_https_get` is GET-only, rejects control characters in the path, limits the
  request size, returns raw HTTP response bytes including headers, and fails rather
  than silently returning a response that fills the caller's buffer.
- The current TCP implementation supports one synchronous client connection at a
  time. The TLS session API inherits that limitation and must not be called
  concurrently.
- This does not add a Wi-Fi chipset driver, 802.11 association, WPA2/WPA3, IPv6
  TLS transport, certificate-store updates, or a trusted RTC/time synchronization
  implementation. Real Wi-Fi remains hardware-specific.

## Build

CI checks out the pinned BearSSL submodule recursively. The top-level Makefile
compiles its library sources with VantaOS and links the TLS adapter into the kernel.
The build preparation script disables BearSSL's host-only OS entropy and time hooks;
VantaOS injects hardware entropy and the caller-provided time explicitly. A small
freestanding C runtime provides the memory/string primitives required by BearSSL.
