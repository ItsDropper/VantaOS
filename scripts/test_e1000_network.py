#!/usr/bin/env python3
"""QEMU socket-backend integration test for VantaOS Ethernet, ARP, and ICMP."""
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
            connection = socket.create_connection(("127.0.0.1", 1234), timeout=1)
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
    finally:
        connection.close()


if __name__ == "__main__":
    main()
