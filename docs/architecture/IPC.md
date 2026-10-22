# VantaOS inter-process communication

## First IPC ABI

The first IPC implementation provides bounded, non-blocking message queues for
ring-3 processes. It is intended as a small kernel-mediated primitive that
higher-level services can build on, not as a finished POSIX message-queue
implementation.

Userspace API: `userspace/include/vanta/syscall.h`

- `vanta_ipc_send(target_pid, payload, length)` sends up to 128 bytes to a
  live user process. It returns the payload length on success or a negative
  errno-style result.
- `vanta_ipc_receive(sender_filter, &message)` removes the first queued message
  from the selected sender, or the oldest message from any sender when
  `sender_filter == VANTA_IPC_ANY_SENDER`. It returns 1 when a message is
  received, 0 when no matching message is queued, or a negative error.
- `vanta_ipc_message_t` contains the sender PID, payload length, and a
  fixed-size 128-byte payload buffer. Only the first `length` payload bytes
  are meaningful.

## Resource limits and errors

Each process has a queue capacity of eight messages. Sends are non-blocking:
a full queue returns `-11` (try again) instead of overwriting older messages.
Payloads larger than 128 bytes are rejected. Invalid user-memory ranges are
rejected by the syscall boundary before bytes are copied into kernel memory.
The kernel validates that sender and destination PIDs refer to live user
processes. Mailboxes are cleared when process slots are reset, and queued
messages from a reset sender are purged to avoid leaking messages across PID
reuse.

The syscall copies payload bytes into kernel-owned storage before enqueueing,
and copies a received message back only after validating the destination
user-memory range. Queue mutation is serialized by a kernel spinlock.

## Deliberate limitations

This initial ABI is polling-based; it does not yet block/wake receivers,
provide named channels or endpoint capabilities, transfer handles/shared
memory, or guarantee a stable process identity across PID reuse races. PIDs
are currently the addressing mechanism. Applications that wait for messages
should yield between empty polls instead of busy-spinning. Those capabilities
should be layered in with explicit lifetime, permission, and scheduler tests
rather than simulated in the API.
