# VantaOS userspace IPC

## Layers

VantaOS IPC has two layers:

1. **Kernel transport** in `kernel/ipc.c`: bounded, non-blocking queues addressed by 64-bit bearer capabilities. The kernel validates endpoint existence, ownership, sender PID, payload size, queue capacity, and owner cleanup. It transports opaque bytes.
2. **Userspace envelope** in `userspace/include/vanta/ipc.h`: a versioned message header with a type, request ID, and bounded payload. This provides a conventional format without coupling kernel IPC to application-specific protocols.

## Endpoint lifecycle

- Create an endpoint with `vanta_ipc_endpoint_create()`. The creating process owns receive and close authority.
- Senders need the 64-bit endpoint capability and may call `vanta_ipc_send()` or `vanta_ipc_send_message()`.
- Only the owner PID may receive from or close the endpoint.
- Each endpoint holds at most eight messages, each up to 256 bytes. Send returns `-VANTA_EAGAIN` when full; receive returns it when empty.
- Closing an endpoint or exiting its owner clears the endpoint and queued payloads. A capability is a bearer token; do not disclose it to untrusted processes.
- `vanta_ipc_peek_length()` reports the next queued message's byte length without consuming it; only the endpoint owner may peek.
- Blocking receive, timeouts, endpoint discovery, and automatic reply routing are not implemented.

## Message envelope

`vanta_ipc_message_t` is exactly 256 bytes:

| Field | Size | Meaning |
| --- | ---: | --- |
| `magic` | 4 bytes | Identifies the VantaOS IPC envelope |
| `version` | 2 bytes | Envelope version, currently 1 |
| `type` | 2 bytes | Application-defined nonzero message type |
| `request_id` | 4 bytes | Application correlation ID; zero is allowed |
| `payload_length` | 4 bytes | Number of payload bytes |
| `payload` | 240 bytes | Bounded payload storage |

The transmitted message is only the 16-byte header plus declared payload. The ABI is little-endian x86-64. Both peers must agree on message types and payload structure; validate every length and field before interpreting data.

## Example

    #include <vanta/ipc.h>

    #define APP_MESSAGE_PING 1U

    static long send_ping(uint64_t endpoint, uint32_t request_id)
    {
        static const char payload[] = "ping";
        return vanta_ipc_send_message(endpoint, APP_MESSAGE_PING,
                                      request_id, payload,
                                      sizeof(payload) - 1U);
    }

    static long receive_one(uint64_t endpoint, char *buffer, size_t capacity)
    {
        uint16_t type = 0U;
        uint32_t request_id = 0U;
        long length = vanta_ipc_receive_message(
            endpoint, &type, &request_id, buffer, capacity);
        if (length < 0)
            return length; /* Includes -VANTA_EAGAIN when empty. */
        /* Validate the payload and use request_id to correlate the reply. */
        return length;
    }

This example documents the envelope only. It does not imply that a built-in service currently uses this example message type.

## Errors and evolution

- `-VANTA_EINVAL`: invalid arguments, zero message type, or invalid payload pointer.
- `-VANTA_EAGAIN`: queue full on send or empty on receive.
- `-VANTA_EMSGSIZE`: payload exceeds the envelope limit or output capacity is too small.
- `-VANTA_EPROTO`: invalid magic/version/type or inconsistent length.
- Kernel endpoint errors such as `-VANTA_EPERM`, `-VANTA_EBADF`, and `-VANTA_ENOMEM` retain their kernel-defined meanings.

`vanta_ipc_receive_message()` first peeks at the queued transport length. If the caller's payload capacity is too small, it returns `-VANTA_EMSGSIZE` without consuming the message, allowing a retry with a larger buffer. A too-short malformed transport message is consumed and rejected with `-VANTA_EPROTO` so it cannot permanently block the queue. The peek/receive pair assumes the current single-receiver process model; revisit it if user threads or concurrent receive calls are introduced.

Do not change syscall numbers to evolve this envelope. New message types belong to the application/service layer; type values are not globally assigned. Increment the envelope version if the header layout changes, reject unknown versions, and add host plus QEMU integration tests before connecting a system service to a new protocol.
