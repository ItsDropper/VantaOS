# Networking architecture

VantaOS now has a usable IPv4 client path for DHCP, DNS, ICMP diagnostics, UDP, and a deliberately small TCP/HTTP client. This is an early kernel stack, not a full browser-grade or production-hardened network implementation.

## Implemented path

```
Shell: net connect / net dns / net ping / net http
                |
DHCPv4 with lease maintenance, DNS A records with TTL cache, IPv4 ICMP/UDP/TCP
                |                         IPv6 ICMPv6, Router Advertisements, SLAAC
IPv4 routing + ARP cache/resolution   IPv6 link-local + /64 SLAAC + ND responses
                |                         |
Ethernet-II + IPv4/IPv6 demultiplexing + bounded receive queue
                |
PCI network driver registry -> E1000-compatible + RTL8139 + modern/legacy VirtIO-net
```

Architecture-independent protocol code lives in `net/`; PCI drivers live in `drivers/network/`; shared contracts live in `include/net.h`. Network receive is polling-based: the driver moves frames into a fixed-capacity queue and protocol handlers run in `net_poll()` caller context. The E1000 driver supports QEMU `8086:100E` plus a defined set of legacy Intel 8254x PCI IDs that share this register/descriptor model. The VirtIO drivers support the **legacy/transitional PCI I/O transport** (`1af4:1000`) and the **modern PCI capability/notification transport** (`1af4:1041`). The modern driver validates the PCI capability chain and MMIO bounds, negotiates `VIRTIO_F_VERSION_1`, MAC, optional link status, and mergeable RX buffers, uses split virtqueues backed by DMA memory below 4 GiB, and has a modern-only QEMU TX plus ARP/ICMP/UDP RX/TX regression. The legacy QEMU regression verifies legacy device initialization and transmit completion; it is not a substitute for physical-hardware coverage. Unsupported devices remain unbound instead of being guessed from the vendor name.

## Easy connection workflow

1. Ensure the VM's virtual NIC or supported physical NIC is connected to a network.
2. Run `net connect` to request DHCPv4 configuration.
3. Run `net status` to verify link, address, gateway, DNS, and RX/TX counters. Run `net adapters` to inspect PCI network controllers and whether an in-tree driver claimed each one.
4. Run `net ping 1.1.1.1` as an optional ICMP diagnostic; some networks block ping. After IPv6 Router Advertisements and SLAAC complete, `net ping6 2606:4700:4700::1111` tests routed ICMPv6 reachability when the network permits it.
5. Run `net dns example.com` to test DNS.
6. Run `net http http://example.com/` to make a real plain-HTTP GET over TCP and print up to 4096 response bytes. `net get` is an alias.

A successful DHCP lease alone does not prove Internet access. DNS, gateway routing, upstream policy, and the selected network driver must all work. For static setups, `net config <index> <ip> <netmask> <gateway>` configures IPv4, then `net dns-server 1.1.1.1 [interface-index]` sets a DNS resolver (choose a resolver reachable from that network). DNS settings are per-interface and are not automatically encrypted.

## Experimental USB Wi-Fi status

The open RTL8188EU work in PR [#143](https://github.com/ItsDropper/VantaOS/pull/143) is **not an operational network interface**. It currently covers USB descriptor matching, bounded firmware provisioning/loading, bulk-IN RX aggregate parsing for scan-frame candidates, a low-level USB TX descriptor path, bounded PBKDF2-HMAC-SHA1 and AES-CCM primitives, and host-side tests. It does not yet perform the chipset's complete MAC/baseband/RF/eFuse initialization, active channel scanning, authentication/association, the WPA2 EAPOL four-way handshake, CCMP packet-number/replay/key management, or conversion between authenticated 802.11 data and Ethernet frames. It is not registered as a `net_device_t`, so DHCP/DNS/TCP cannot use it. No physical adapter validation has been recorded. Do not use the Wi-Fi API's generic state tracking as evidence of radio connectivity; the existing Ethernet interfaces remain the supported path.

## Protocol and resource contracts

- Ethernet-II supports one 802.1Q/802.1ad VLAN tag on receive. Outbound VLAN tagging is not implemented.
- RX frames are length-checked and copied to a bounded queue; handlers run outside the core registry lock.
- ARP uses a bounded expiring cache and bounded request/retry behavior.
- IPv4 validates header length, total length, checksum, destination, and unsupported fragments. Outbound packets are unfragmented and limited by the interface MTU.
- UDP receive validates lengths and nonzero checksums; a zero UDP checksum is accepted as valid for IPv4. UDP transmit generates the IPv4 pseudo-header checksum.
- DHCPv4 obtains address, netmask, gateway, DNS, and lease duration. It schedules T1 unicast renewal, T2 broadcast rebinding, bounded exponential retry, and lease expiry cleanup. Renewal is still a single-client state machine and needs longer-running network-emulator tests under packet loss and server restart.
- DNS supports bounded A-record lookup using the per-interface DNS address supplied by DHCP or set with `net dns-server`. Replies must match the outstanding transaction and question; positive A answers are cached per interface/server until their TTL expires. CNAME-only/AAAA resolution, negative caching, EDNS0, DNSSEC, and DNS-over-TLS/HTTPS are not implemented.
- IPv6 accepts ICMPv6, validates its pseudo-header checksum, sends Router Solicitations, processes Router Advertisements and autonomous /64 prefixes, performs basic Duplicate Address Detection before publishing a SLAAC address, responds to Neighbor Solicitations for local addresses, and answers ICMPv6 echo requests. IPv6 echo requests use a bounded neighbor cache, Neighbor Solicitation/Advertisement resolution, and an advertised default router for off-link destinations. IPv6 UDP/TCP sockets, privacy addresses, DHCPv6, extension-header chains, and broad adversarial ND hardening are not yet implemented; this is dual-stack ICMP reachability, not full IPv6 Internet access.
- TCP is currently a **single synchronous client connection** with bounded send/receive buffers, peer-MSS and advertised-window handling, segmentation of larger writes, zero-window waiting, retransmission timeout, sequence-checked RST handling, and checksum validation. It does not yet implement multiple simultaneous sockets, congestion control, SACK, out-of-order reassembly, IP fragmentation, or a full socket API.
- HTTP support is **plain HTTP only**: one bounded HTTP/1.0 GET, response display capped at 4096 bytes. HTTPS is intentionally rejected because TLS and certificate validation are not implemented; the client does not silently downgrade HTTPS to HTTP.

## Validation and remaining work

CI now includes per-interface DHCP lease state plus offer/request/ACK negotiation tests (including malformed subnet-mask options, NAK handling, and transaction-ID rejection), DNS answer-owner binding and malformed-compression tests, focused IPv6 RA/SLAAC/ICMPv6 tests, IPv4/UDP/IPv6 MTU boundary tests, and QEMU E1000/RTL8139/legacy VirtIO Ethernet regressions. These tests validate only the named assertions. TCP still needs end-to-end loss/duplicate/out-of-order tests; IPv6 needs outbound UDP/TCP and routed neighbor resolution; physical hardware compatibility still needs testing on actual adapters, and additional independently implemented NIC drivers (including more common Realtek adapters) remain future work. Modern VirtIO PCI is covered separately by a `disable-legacy=on` QEMU test that verifies initialization, transmit completion, and ARP/ICMP/UDP frame exchange. TLS and certificate validation are required before HTTPS. Wi-Fi still requires a chipset-specific driver, firmware handling, scanning, and a secure WPA2/WPA3 association path. Wi-Fi cannot be enabled by a router setting alone: VantaOS needs a supported wireless chipset driver and a secure association/authentication implementation.


## Security and compatibility boundary (2026-10)

The current implementation is an early kernel networking stack, not a secure general-purpose Internet client. In particular:

- **Wi-Fi:** the chipset-independent control API validates connection inputs and tracks driver-reported state. `net/wifi_80211.c` parses bounded Beacon/Probe Response frames and extracts SSID, BSSID, channel, RSSI, and supported RSN security information. It recognizes WPA2-PSK and only classifies WPA3-SAE when management-frame protection is both capable and required; malformed/unsupported RSN data is rejected. CI covers Beacon/Probe Response, WPA2/WPA3 classification, malformed IE bounds, wrong frame types, invalid BSSIDs, and missing WPA3 PMF. This is scan-parser groundwork, not a complete radio: no physical chipset driver, firmware loader, over-the-air scan/association, WPA2/WPA3 handshake, or supported physical wireless chipset is included yet. PCI discovery does not bind a wireless device merely because it appears on PCI.
- **TLS and HTTPS:** the TCP client is not a TLS implementation. Do not send credentials, tokens, or sensitive content over it. HTTPS must fail closed until a reviewed TLS implementation, cryptographically secure randomness, certificate-chain validation, hostname verification, and a trustworthy time/trust-store path are integrated together. No insecure certificate bypass is acceptable.
- **IPv6:** current IPv6 work covers neighbor discovery, router advertisements, SLAAC, and ICMPv6 diagnostics; it does not provide a complete IPv6 transport/application path.
- **TCP:** this is a single-client, bounded-buffer implementation intended for simple HTTP-style use and tests. It is not a general sockets API and lacks the congestion control, comprehensive option handling, concurrent connections, and full retransmission/fragmentation behavior expected of a mature stack.

### Security invariants

- Unknown PCI IDs must remain unbound; do not infer compatibility from vendor alone.
- Network input must be length-checked before field access and before subtraction or pointer arithmetic.
- Device-reported MTU values are untrusted. TCP send/connect must reject MTUs too small for the IPv4+TCP headers rather than allowing unsigned arithmetic to wrap.
- Never log wireless credentials or cryptographic key material.
- A network diagnostic is not evidence that TLS, certificate validation, or application-layer identity is secure.

### Practical next integration requirements

A real Wi-Fi implementation still needs a selected chipset with publicly documented registers and firmware redistribution terms, DMA/interrupt integration, the chipset-specific firmware/init sequence, 802.11 management and data frames, and WPA2-Personal cryptography/handshake tested against actual APs. The new API is intentionally not advertised as a device driver. Its provider must copy credentials before returning if needed asynchronously, keep them out of logs, and erase driver-owned copies when no longer required. Intel client adapters commonly depend on firmware and complex device-specific initialization; selecting a specific model and implementing that contract is required before claiming support.

TLS should be integrated as a reviewed library port, not handwritten cryptographic primitives. The port must supply entropy from a suitable kernel CSPRNG, bounded allocation, certificate parsing and path validation, hostname matching, and trusted time. The current kernel has not yet demonstrated that complete foundation, so this branch intentionally does not advertise HTTPS support.


DHCP lease negotiation and renewal state is tracked independently for each registered network device, up to `NET_DEVICE_MAX`. This avoids a second interface overwriting the first interface's lease-renewal timers or transaction state. A configured interface still needs a working supported NIC driver and a reachable DHCP server; this is not a promise of universal hardware support.


Modern VirtIO support does not make this a universal networking stack. Physical Wi-Fi association/WPA2/WPA3 and TLS/HTTPS remain separate unsupported capabilities; see [adapter support](network-adapter-support.md) and [secure networking](secure-networking.md).
