#!/usr/bin/env python3
# Copyright (c) 2026 ItsDropper
#
# This file is part of VantaOS and is licensed under the MIT License.
# See the LICENSE file in the project root for the full license text.

"""QEMU socket-backend integration test for VantaOS Ethernet, ARP, and ICMP."""
import os
import socket
import struct
import time


def checksum(data):
    if len(data) & 1:
        data += b"\x00"
    total = sum((data[i] << 8) | data[i + 1] for i in range(0, len(data), 2))
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


def recv_exact(sock, count):
    chunks = bytearray()
    while len(chunks) < count:
        part = sock.recv(count - len(chunks))
        if not part:
            raise RuntimeError("QEMU closed the E1000 socket")
        chunks.extend(part)
    return bytes(chunks)


def recv_frame(sock):
    size = struct.unpack("!I", recv_exact(sock, 4))[0]
    if size < 14 or size > 1518:
        raise RuntimeError(f"Invalid QEMU Ethernet frame length: {size}")
    return recv_exact(sock, size)


def send_frame(sock, frame):
    sock.sendall(struct.pack("!I", len(frame)) + frame)


def main():
    deadline = time.monotonic() + 10
    connection = None
    while time.monotonic() < deadline:
        try:
            connection = socket.create_connection(("127.0.0.1", int(os.environ.get("VANTAOS_TEST_PORT", "1234"))), timeout=1)
            break
        except OSError:
            time.sleep(0.1)
    if connection is None:
        raise RuntimeError("Could not connect to QEMU's E1000 socket backend")

    connection.settimeout(5)
    host_mac = bytes.fromhex("020000000002")
    host_ip = socket.inet_aton("10.0.2.2")
    guest_ip = socket.inet_aton("10.0.2.15")
    try:
        # A raw Ethernet frame exercises RX descriptors, queueing, and EtherType dispatch.
        raw = bytes.fromhex("ffffffffffff02000000000288b556414e54") + bytes(42)
        send_frame(connection, raw)

        # Ask the guest to resolve its configured IPv4 address.
        arp = struct.pack("!HHBBH6s4s6s4s", 1, 0x0800, 6, 4, 1,
                          host_mac, host_ip, bytes(6), guest_ip)
        frame = b"\xff" * 6 + host_mac + struct.pack("!H", 0x0806) + arp
        send_frame(connection, frame.ljust(60, b"\x00"))

        arp_reply = None
        for _ in range(8):
            candidate = recv_frame(connection)
            if len(candidate) >= 42 and struct.unpack_from("!H", candidate, 12)[0] == 0x0806:
                if struct.unpack_from("!H", candidate, 20)[0] == 2:
                    arp_reply = candidate
                    break
        if arp_reply is None:
            raise RuntimeError("No ARP reply from VantaOS")
        arp_data = arp_reply[14:42]
        guest_mac = arp_data[8:14]
        assert arp_data[14:18] == guest_ip, "ARP reply has wrong sender IP"
        assert arp_data[18:24] == host_mac, "ARP reply has wrong target MAC"
        assert arp_data[24:28] == host_ip, "ARP reply has wrong target IP"

        # Send an IPv4 ICMP echo request and require a checksum-valid echo reply.
        payload = b"VANTAOS-ICMP"
        icmp = bytearray(struct.pack("!BBHHH", 8, 0, 0, 0x1234, 1) + payload)
        struct.pack_into("!H", icmp, 2, checksum(icmp))
        ip = bytearray(20)
        ip[0] = 0x45
        struct.pack_into("!H", ip, 2, len(ip) + len(icmp))
        struct.pack_into("!H", ip, 4, 0x4321)
        ip[8] = 64
        ip[9] = 1
        ip[12:16] = host_ip
        ip[16:20] = guest_ip
        struct.pack_into("!H", ip, 10, checksum(ip))
        frame = guest_mac + host_mac + struct.pack("!H", 0x0800) + bytes(ip) + bytes(icmp)
        send_frame(connection, frame.ljust(60, b"\x00"))

        echo_reply = None
        for _ in range(8):
            candidate = recv_frame(connection)
            if len(candidate) < 42 or struct.unpack_from("!H", candidate, 12)[0] != 0x0800:
                continue
            packet = candidate[14:]
            ihl = (packet[0] & 0x0F) * 4
            total = struct.unpack_from("!H", packet, 2)[0]
            if (packet[9] == 1 and total >= ihl + 8 and len(packet) >= total
                    and packet[ihl] == 0):
                echo_reply = packet[:total]
                break
        if echo_reply is None:
            raise RuntimeError("No ICMP echo reply from VantaOS")
        ihl = (echo_reply[0] & 0x0F) * 4
        icmp_reply = echo_reply[ihl:]
        assert checksum(echo_reply[:ihl]) == 0, "Invalid IPv4 reply checksum"
        assert checksum(icmp_reply) == 0, "Invalid ICMP reply checksum"
        assert echo_reply[12:16] == guest_ip and echo_reply[16:20] == host_ip
        assert icmp_reply[8:] == payload, "ICMP echo payload changed"

        # Exercise IPv4/UDP receive dispatch with a non-zero pseudo-header checksum.
        udp_payload = b"VANTAOS-UDP"
        udp = bytearray(struct.pack("!HHHH", 53000, 4242, 8 + len(udp_payload), 0) + udp_payload)
        pseudo = host_ip + guest_ip + struct.pack("!BBH", 0, 17, len(udp))
        udp_checksum = checksum(pseudo + bytes(udp))
        if udp_checksum == 0:
            udp_checksum = 0xFFFF
        struct.pack_into("!H", udp, 6, udp_checksum)
        udp_ip = bytearray(20)
        udp_ip[0] = 0x45
        struct.pack_into("!H", udp_ip, 2, len(udp_ip) + len(udp))
        struct.pack_into("!H", udp_ip, 4, 0x4322)
        udp_ip[8] = 64
        udp_ip[9] = 17
        udp_ip[12:16] = host_ip
        udp_ip[16:20] = guest_ip
        struct.pack_into("!H", udp_ip, 10, checksum(udp_ip))
        # A corrupt checksum and inconsistent UDP length must both be dropped.
        bad_checksum = bytearray(udp)
        bad_checksum[6] ^= 0x01
        bad_frame = guest_mac + host_mac + struct.pack("!H", 0x0800) + bytes(udp_ip) + bytes(bad_checksum)
        send_frame(connection, bad_frame.ljust(60, b"\x00"))

        bad_length = bytearray(udp)
        struct.pack_into("!H", bad_length, 4, len(bad_length) + 1)
        bad_frame = guest_mac + host_mac + struct.pack("!H", 0x0800) + bytes(udp_ip) + bytes(bad_length)
        send_frame(connection, bad_frame.ljust(60, b"\x00"))

        udp_frame = guest_mac + host_mac + struct.pack("!H", 0x0800) + bytes(udp_ip) + bytes(udp)
        send_frame(connection, udp_frame.ljust(60, b"\x00"))
    finally:
        connection.close()


if __name__ == "__main__":
    main()
