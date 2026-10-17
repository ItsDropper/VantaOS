# Native cryptography and TLS roadmap

VantaOS does not currently implement TLS or HTTPS. The previous BearSSL submodule and adapter have been removed. TLS will be implemented as VantaOS-owned source rather than importing a third-party TLS stack.

## Current security building blocks

- crypto_rng_fill uses CPU RDRAND when available and fails closed when unavailable or when an instruction fails. RDRAND alone is not a complete entropy design for a production OS.
- tls_policy.h validates ASCII DNS hostname syntax only. It does not validate certificates or authenticate a server.
- The existing TCP client is synchronous and limited to one client connection.

## Required before HTTPS can be enabled

A native TLS implementation still needs reviewed and tested cryptographic primitives, TLS 1.3 transcript handling and key schedule, a supported key exchange, authenticated encryption, certificate parsing and path validation, hostname matching against certificate SANs, a maintained CA store, and trusted wall-clock time. Unsupported cipher suites, malformed records, invalid certificates, missing time, and entropy failures must fail closed. No placeholder or unverified primitive may be presented as secure TLS.

Physical Wi-Fi association and WPA2/WPA3 are also unimplemented; a generic Wi-Fi control API is not a chipset driver.
