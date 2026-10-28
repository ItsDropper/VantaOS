# Course lesson: reliable non-consuming IPC receive

Copyright (c) 2026 ItsDropper

This lesson is part of VantaOS and is licensed under the MIT License. See the repository's `LICENSE` file for the full license text.

## Problem

A receive helper that always dequeues a full 256-byte transport buffer and only then checks the application's payload capacity loses messages when a caller supplies a buffer that is too small. The error tells the caller to retry, but the message is already gone. This is a bad contract for request/reply services and should be fixed at the ABI boundary rather than left as a caller convention.

## Design

The kernel exposes syscall 30, `VANTA_SYS_IPC_PEEK_LENGTH`, through `vanta_ipc_peek_length()`. It returns the next message's byte length without consuming the queue entry. Like receive, it checks that the endpoint exists and that the calling PID owns it.

The receive helper follows a two-step sequence:

1. Peek at the next transport length.
2. If it is smaller than the 16-byte envelope header, dequeue and reject it as malformed. This prevents a malformed short message from permanently blocking the queue.
3. Calculate the maximum payload implied by the transport length.
4. If the caller's payload buffer cannot hold that payload, return `-VANTA_EMSGSIZE` without dequeuing.
5. Otherwise receive exactly that transport length, validate the envelope magic, version, type and payload-length equality, then copy the payload to the caller.

The key behavior is that a capacity error is now recoverable: the caller can allocate a larger buffer and retry the same message.

## Implementation details

The kernel implementation in `kernel/ipc.c` holds the IPC spinlock while finding the endpoint and reading the head entry's length. It releases the lock before returning. The peek does not reserve the message and must not mutate the queue.

```c
int length = (int)endpoint->queue[endpoint->head].length;
spinlock_unlock_irqrestore(&ipc_lock, flags);
return length;
```

The syscall dispatcher uses the current process snapshot's PID rather than accepting a receiver PID from userspace. This prevents callers from impersonating another endpoint owner.

The userspace helper uses the peeked transport length to decide whether to dequeue. It still validates the actual received envelope because a length alone cannot prove that the message's header is valid.

## Concurrency contract

Today, only the endpoint owner may receive and VantaOS does not expose user threads. Under that process model, the peek followed by receive is a usable non-consuming size check. The peek is not a reservation: if multiple threads or concurrent receiver contexts are added later, another receiver could consume the message between calls. At that point, replace this two-call protocol with an atomic receive-into-capacity operation that leaves undersized messages queued, or add a per-endpoint reservation protocol. Do not claim thread-safe atomicity for the current API.

## Tests

The IPC host suite in `scripts/test_ipc.c` verifies:

- Peeking an empty queue returns `-EAGAIN`.
- A non-owner cannot peek.
- Peeking does not consume a message; repeated peeks return the same length.
- An undersized kernel receive leaves the message queued for a later receive.
- Closing or cleaning up an endpoint invalidates its capability.

Run the same host suite used by CI:

```sh
gcc -std=c11 -Wall -Wextra -Werror \
  -Itests/ipc_host -Iuserspace/include -Iinclude \
  scripts/test_ipc.c -o /tmp/vantaos-ipc-test
/tmp/vantaos-ipc-test
```

The public ABI is compiled separately by `tests/user/ipc_abi_compile.c`. Kernel/QEMU tests are still needed to prove the syscall dispatch path and multi-CPU behavior; a host unit test alone does not prove those integration properties.


## Endpoint exhaustion and secret cleanup

The endpoint table is fixed-size. Creation returns `-ENOMEM` when all 32 slots are active, and fails closed with `-ENODEV` if the random-number provider cannot supply a capability token. Endpoint close and owner-process cleanup erase queued message bytes before the slot becomes reusable. Do not silently replace a random token with a predictable counter: the endpoint value is a bearer capability, and predictability would let another process guess authority it was not given.

The host regression suite now fills the endpoint table, verifies the exhaustion result, forces the RNG provider to fail, cleans up an owner with queued data, checks that the slot and payload bytes are cleared, and confirms that a reclaimed slot can be reused.


## Revocation at current-process exit

When the running process is marked terminated, the process lifecycle now revokes its IPC endpoints immediately after releasing the process-table lock and before returning to the scheduler. The current process still owns its CPU stack at that point, so its PID cannot yet be recycled. Non-current process termination continues to clean IPC as part of the reaper path, which owns the transition to resource teardown. This keeps the expensive queue-zeroing work outside the process-table critical section while avoiding a cleanup racing with PID reuse.


## Capability collision and queue-reuse regressions

The host regression suite now forces the deterministic RNG stub to return an already-live endpoint token and verifies that creation fails with the collision error instead of aliasing another process's endpoint. It also fills the bounded receive queue, checks overflow rejection, repeats non-consuming length peeks, confirms that an undersized destination does not consume the head, drains the ring through wraparound, and verifies that consumed message bytes and metadata are cleared before slot reuse.

The IPC host workflow runs the normal strict-warning test binary and a second build under AddressSanitizer and UndefinedBehaviorSanitizer. These tests validate the shared C implementation with host spinlock/RNG shims; they do not validate real SMP contention, interrupt-state restoration, syscall entry, or scheduler timing.
