# Networking architecture

VantaOS now has a usable IPv4 client path for DHCP, DNS, ICMP diagnostics, UDP, and a deliberately small TCP/HTTP client. This is an early kernel stack, not a full browser-grade or production-hardened network implementation.

## Implemented path

```
Shell: net connect / net dns / net ping / net http
                |
DHCPv4, DNS A records, ICMP echo, bounded TCP client + HTTP/1.0 GET
                |
IPv4 input/output + next-hop routing + ARP cache/resolution
                |
Ethernet-II demultiplexer + bounded receive queue
                |
PCI network driver registry -> E1000-compatible driver
```

Architecture-independent protocol code lives in `net/`; PCI drivers live in `drivers/network/`; shared contracts live in `include/net.h`. Network receive is polling-based: the driver moves frames into a fixed-capacity queue and protocol handlers run in `net_poll()` caller context. The current E1000 driver targets Intel 82540EM / QEMU `8086:100E`; it does not imply compatibility with arbitrary physical NICs.

## Easy connection workflow

1. Ensure the VM's virtual NIC or supported physical NIC is connected to a network.
2. Run `net connect` to request DHCPv4 configuration.
3. Run `net status` to verify link, address, gateway, DNS, and RX/TX counters.
4. Run `net ping 1.1.1.1` as an optional ICMP diagnostic; some networks block ping.
5. Run `net dns example.com` to test DNS.
6. Run `net http http://example.com/` to make a real plain-HTTP GET over TCP and print up to 4096 response bytes. `net get` is an alias.

A successful DHCP lease alone does not prove Internet access. DNS, gateway routing, upstream policy, and the selected network driver must all work. For static setups, `net config <index> <ip> <netmask> <gateway>` configures IPv4; the DNS server must be configured separately through the current device configuration path.

## Protocol and resource contracts

- Ethernet-II supports one 802.1Q/802.1ad VLAN tag on receive. Outbound VLAN tagging is not implemented.
- RX frames are length-checked and copied to a bounded queue; handlers run outside the core registry lock.
- ARP uses a bounded expiring cache and bounded request/retry behavior.
- IPv4 validates header length, total length, checksum, destination, and unsupported fragments. Outbound packets are unfragmented and limited by the interface MTU.
- UDP receive validates lengths and nonzero checksums; a zero UDP checksum is accepted as valid for IPv4. UDP transmit generates the IPv4 pseudo-header checksum.
- DHCPv4 obtains address, netmask, gateway, DNS, and lease duration. Lease renewal/expiry is not yet implemented.
- DNS supports bounded A-record lookup from the DHCP-provided DNS server.
- TCP is currently a **single synchronous client connection** with bounded send/receive buffers, basic handshake/ACK/FIN/RST handling, retransmission timeout, and checksum validation. It does not yet implement multiple simultaneous sockets, congestion control, SACK, out-of-order reassembly, IP fragmentation, or a full socket API.
- HTTP support is **plain HTTP only**: one bounded HTTP/1.0 GET, response display capped at 4096 bytes. HTTPS is intentionally rejected because TLS and certificate validation are not implemented; the client does not silently downgrade HTTPS to HTTP.

## Validation and remaining work

The existing QEMU network regression covers E1000 transmit/receive, ARP, ICMP, and UDP validation. TCP/HTTP still needs dedicated end-to-end QEMU tests and real-hardware validation before being described as production-ready. Next priorities are TCP packet-level regression tests, correct behavior under loss/duplicate/out-of-order packets, TLS/certificate infrastructure before HTTPS, lease renewal, and broader NIC compatibility.
