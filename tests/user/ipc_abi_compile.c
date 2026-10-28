/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */
#include <stddef.h>
#include <stdint.h>
#include <vanta/ipc.h>

_Static_assert(VANTA_USER_ABI_VERSION == 10U,
               "userspace ABI version must track the current syscall ABI");
_Static_assert(VANTA_SYS_IPC_PEEK_LENGTH == 30U,
               "IPC peek syscall number is part of the public ABI");

_Static_assert(sizeof(vanta_ipc_message_t) == VANTA_IPC_MESSAGE_MAX,
               "IPC envelope must match the kernel transport limit");
_Static_assert(offsetof(vanta_ipc_message_t, payload) == VANTA_IPC_HEADER_SIZE,
               "IPC envelope header layout must remain stable");

int vanta_ipc_abi_compile_probe(void)
{
    vanta_ipc_message_t message;
    return vanta_ipc_message_init(&message, 1U, 7U, 0, 0U);
}
