/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

#include "vfs.h"
#include "spinlock.h"

#define VFS_MAX_OPEN_FILES 32U

/*
 * open_files[] is global and shared by every CPU and every user process.
 * All slot claims, releases, snapshots, and position updates run under
 * open_files_lock so two CPUs can never be handed the same handle. The lock
 * is always taken before (never inside) filesystem_lock and is never held
 * across filesystem calls, so it cannot invert with the filesystem lock.
 */
static spinlock_t open_files_lock = SPINLOCK_INITIALIZER;
static vfs_file_t open_files[VFS_MAX_OPEN_FILES];
static int initialized;

static int vfs_node_path(
    uint32_t node_id,
    char* path,
    unsigned int capacity
);

static void vfs_clear_slot_locked(unsigned int handle)
{
    open_files[handle].node_id = 0;
    open_files[handle].position = 0;
    open_files[handle].flags = 0;
    open_files[handle].used = 0;
}

/* Copy a live handle that carries required_flag; fails if it is closed. */
static int vfs_snapshot_handle(
    int handle,
    uint32_t required_flag,
    vfs_file_t* out
)
{
    uint64_t lock_flags = spinlock_lock_irqsave(&open_files_lock);
    int ok = open_files[handle].used &&
             (open_files[handle].flags & required_flag) != 0U;
    if (ok)
        *out = open_files[handle];
    spinlock_unlock_irqrestore(&open_files_lock, lock_flags);
    return ok;
}

/* Advance the position only if the same file is still open on the slot. */
static void vfs_advance_position(
    int handle,
    uint32_t node_id,
    uint32_t amount
)
{
    uint64_t lock_flags = spinlock_lock_irqsave(&open_files_lock);
    if (open_files[handle].used && open_files[handle].node_id == node_id)
        open_files[handle].position += amount;
    spinlock_unlock_irqrestore(&open_files_lock, lock_flags);
}

void vfs_initialize(void)
{
    uint64_t lock_flags = spinlock_lock_irqsave(&open_files_lock);
    for (unsigned int i = 0; i < VFS_MAX_OPEN_FILES; i++)
        vfs_clear_slot_locked(i);
    spinlock_unlock_irqrestore(&open_files_lock, lock_flags);

    initialized = filesystem_is_initialized();
}

int vfs_is_initialized(void)
{
    return initialized != 0;
}

int vfs_lookup(const char* path)
{
    if (!initialized)
        return -1;

    return filesystem_lookup(path);
}

const fs_node_t* vfs_get_node(uint32_t node_id)
{
    if (!initialized)
        return 0;

    return filesystem_get_node(node_id);
}

int vfs_list(
    uint32_t directory_id,
    uint32_t* ids,
    unsigned int capacity
)
{
    if (!initialized)
        return -1;

    return filesystem_list(
        directory_id,
        ids,
        capacity
    );
}

int vfs_open(
    const char* path,
    uint32_t flags
)
{
    if (!initialized || !path ||
        (flags & ~(VFS_OPEN_READ | VFS_OPEN_WRITE | VFS_OPEN_TRUNCATE)) != 0U ||
        (flags & (VFS_OPEN_READ | VFS_OPEN_WRITE)) == 0U ||
        ((flags & VFS_OPEN_TRUNCATE) && !(flags & VFS_OPEN_WRITE)))
        return -1;

    int node_id =
        filesystem_lookup(path);

    if (node_id < 0)
        return -1;

    const fs_node_t* node =
        filesystem_get_node((uint32_t)node_id);

    if (!node ||
        ((flags & VFS_OPEN_WRITE) && node->type != FS_NODE_FILE) ||
        (node->type != FS_NODE_FILE &&
         node->type != FS_NODE_VIRTUAL))
        return -1;

    /* Claim a free slot atomically so concurrent opens get distinct handles. */
    int slot = -1;
    uint64_t lock_flags = spinlock_lock_irqsave(&open_files_lock);
    for (unsigned int i = 0; i < VFS_MAX_OPEN_FILES; i++)
    {
        if (open_files[i].used)
            continue;

        open_files[i].node_id = (uint32_t)node_id;
        open_files[i].position = 0;
        open_files[i].flags = flags;
        open_files[i].used = 1;
        slot = (int)i;
        break;
    }
    spinlock_unlock_irqrestore(&open_files_lock, lock_flags);

    if (slot < 0)
        return -1;

    if (flags & VFS_OPEN_TRUNCATE)
    {
        /*
         * The slot is claimed but its handle has not been returned yet, so no
         * other caller can reach it while the filesystem work runs unlocked.
         */
        char truncate_path[FS_PATH_MAX];
        int truncated =
            vfs_node_path((uint32_t)node_id, truncate_path,
                          sizeof(truncate_path)) &&
            filesystem_write_file(truncate_path, "") >= 0;

        if (!truncated)
        {
            lock_flags = spinlock_lock_irqsave(&open_files_lock);
            vfs_clear_slot_locked((unsigned int)slot);
            spinlock_unlock_irqrestore(&open_files_lock, lock_flags);
            return -1;
        }
    }

    return slot;
}

int vfs_read(
    int handle,
    char* buffer,
    unsigned int length
)
{
    vfs_file_t file;

    if (!initialized || handle < 0 ||
        handle >= (int)VFS_MAX_OPEN_FILES || !buffer ||
        !vfs_snapshot_handle(handle, VFS_OPEN_READ, &file))
        return -1;

    const fs_node_t* node = filesystem_get_node(file.node_id);
    if (!node || node->type != FS_NODE_FILE)
        return -1;
    if (length == 0U || file.position >= node->size)
        return 0;

    int result = filesystem_read_range(node->id, file.position,
                                       buffer, length);
    if (result < 0)
        return -1;
    vfs_advance_position(handle, file.node_id, (uint32_t)result);
    return result;
}


static int vfs_node_path(
    uint32_t node_id,
    char* path,
    unsigned int capacity
)
{
    if (!path || capacity < 2)
        return 0;

    path[0] = 0;

    uint32_t current = node_id;

    while (current != filesystem_root())
    {
        const fs_node_t* node =
            filesystem_get_node(current);

        if (!node || node->name[0] == 0)
            return 0;

        char segment[FS_NAME_MAX + 2];
        unsigned int segment_length = 0;

        segment[segment_length++] = '/';

        while (node->name[segment_length - 1] != 0 &&
               segment_length <= FS_NAME_MAX)
        {
            segment[segment_length] =
                node->name[segment_length - 1];

            segment_length++;
        }

        segment[segment_length] = 0;

        unsigned int current_length = 0;
        while (path[current_length] != 0)
            current_length++;

        if (current_length +
            segment_length >= capacity)
            return 0;

        for (int i = (int)current_length;
             i >= 0;
             i--)
        {
            path[
                i + segment_length
            ] = path[i];
        }

        for (unsigned int i = 0;
             i < segment_length;
             i++)
        {
            path[i] = segment[i];
        }

        current = node->parent;
    }

    if (path[0] == 0)
        path[0] = '/';

    return 1;
}

int vfs_write(
    int handle,
    const char* data,
    unsigned int length
)
{
    vfs_file_t file;

    if (!initialized || handle < 0 ||
        handle >= (int)VFS_MAX_OPEN_FILES || !data ||
        !vfs_snapshot_handle(handle, VFS_OPEN_WRITE, &file))
        return -1;
    if (length == 0U)
        return 0;

    const fs_node_t* node = filesystem_get_node(file.node_id);
    if (!node || node->type != FS_NODE_FILE ||
        file.position > node->size ||
        length > UINT32_MAX - file.position)
        return -1;

    char path[FS_PATH_MAX];
    if (!vfs_node_path(node->id, path, sizeof(path)))
        return -1;

    int result = filesystem_write_file_range(path, file.position,
                                              data, length);
    if (result < 0)
        return -1;
    vfs_advance_position(handle, file.node_id, (uint32_t)result);
    return result;
}

int vfs_close(int handle)
{
    if (!initialized ||
        handle < 0 ||
        handle >= (int)VFS_MAX_OPEN_FILES)
        return -1;

    /* Check-and-clear is one critical section so a double close is detected. */
    uint64_t lock_flags = spinlock_lock_irqsave(&open_files_lock);
    int was_open = open_files[handle].used;
    if (was_open)
        vfs_clear_slot_locked((unsigned int)handle);
    spinlock_unlock_irqrestore(&open_files_lock, lock_flags);

    return was_open ? 0 : -1;
}

int vfs_create_file(const char* path)
{
    if (!initialized)
        return -1;

    return filesystem_create_file(path);
}

int vfs_create_directory(const char* path)
{
    if (!initialized)
        return -1;

    return filesystem_create_directory(path);
}

int vfs_delete_path(const char* path)
{
    if (!initialized)
        return -1;
    return filesystem_delete_path(path);
}

int vfs_rename_path(const char* old_path, const char* new_path)
{
    if (!initialized)
        return -1;
    return filesystem_rename_path(old_path, new_path);
}
