/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

/*
 * Host regression test for the VFS open-file table.
 *
 * Several threads (standing in for CPUs running different user processes)
 * open and close files concurrently. A handle must belong to exactly one
 * opener at a time; if two openers are ever handed the same handle the test
 * fails. Single-threaded checks cover handle exhaustion, double close, and
 * position tracking.
 */

#include "vfs.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define THREADS 4
#define ITERATIONS 50000
#define MAX_HANDLES 32

static fs_node_t test_node;

int filesystem_is_initialized(void) { return 1; }
int filesystem_lookup(const char* path) { (void)path; return 1; }
const fs_node_t* filesystem_get_node(uint32_t id)
{
    return id == test_node.id ? &test_node : NULL;
}
int filesystem_list(uint32_t d, uint32_t* ids, unsigned int c)
{ (void)d; (void)ids; (void)c; return 0; }
int filesystem_read(uint32_t id, char* b, unsigned int c)
{ (void)id; (void)b; (void)c; return 0; }
int filesystem_read_range(uint32_t id, uint32_t offset, char* buffer,
                          uint32_t length)
{
    (void)id;
    if (offset >= test_node.size)
        return 0;
    uint32_t available = test_node.size - offset;
    uint32_t count = length < available ? length : available;
    memset(buffer, 'x', count);
    return (int)count;
}
int filesystem_write_file_range(const char* p, uint32_t o, const char* d,
                                uint32_t l)
{ (void)p; (void)o; (void)d; return (int)l; }
int filesystem_create_directory(const char* p) { (void)p; return 0; }
int filesystem_delete_path(const char* p) { (void)p; return 0; }
int filesystem_rename_path(const char* a, const char* b)
{ (void)a; (void)b; return 0; }
int filesystem_create_file(const char* p) { (void)p; return 0; }
int filesystem_write_file(const char* p, const char* d)
{ (void)p; (void)d; return 0; }
int filesystem_file_exists(const char* p) { (void)p; return 1; }
int filesystem_ensure_directory(const char* p) { (void)p; return 0; }
uint32_t filesystem_root(void) { return 0; }

static atomic_int owner[MAX_HANDLES];
static atomic_int duplicate_handles;
static atomic_int unexpected_failures;

static void* opener(void* argument)
{
    const int id = (int)(intptr_t)argument + 1;

    for (int i = 0; i < ITERATIONS; i++)
    {
        int handle = vfs_open("/f", VFS_OPEN_READ);
        if (handle < 0)
        {
            /* At most THREADS handles are held; the table has 32 slots. */
            atomic_fetch_add(&unexpected_failures, 1);
            continue;
        }
        assert(handle < MAX_HANDLES);

        int expected = 0;
        if (!atomic_compare_exchange_strong(&owner[handle], &expected, id))
            atomic_fetch_add(&duplicate_handles, 1);

        /* Release our claim first so a legitimate reuse is never flagged. */
        if (expected == 0)
            atomic_store(&owner[handle], 0);
        assert(vfs_close(handle) == 0);
    }
    return NULL;
}

static void test_concurrent_open_close(void)
{
    pthread_t threads[THREADS];

    for (intptr_t i = 0; i < THREADS; i++)
        assert(pthread_create(&threads[i], NULL, opener, (void*)i) == 0);
    for (int i = 0; i < THREADS; i++)
        assert(pthread_join(threads[i], NULL) == 0);

    assert(atomic_load(&duplicate_handles) == 0);
    assert(atomic_load(&unexpected_failures) == 0);
}

static void test_single_threaded_semantics(void)
{
    int handles[MAX_HANDLES];
    char buffer[8];

    for (int i = 0; i < MAX_HANDLES; i++)
    {
        handles[i] = vfs_open("/f", VFS_OPEN_READ);
        assert(handles[i] >= 0);
        for (int j = 0; j < i; j++)
            assert(handles[j] != handles[i]);
    }
    assert(vfs_open("/f", VFS_OPEN_READ) == -1);

    assert(vfs_close(handles[3]) == 0);
    assert(vfs_close(handles[3]) == -1);
    assert(vfs_close(-1) == -1);
    assert(vfs_close(MAX_HANDLES) == -1);
    assert(vfs_open("/f", VFS_OPEN_READ) == handles[3]);

    /* Reads advance the position until end of file, then return 0. */
    test_node.size = 10U;
    assert(vfs_read(handles[0], buffer, 4U) == 4);
    assert(vfs_read(handles[0], buffer, 8U) == 6);
    assert(vfs_read(handles[0], buffer, 8U) == 0);
    /* A read-only handle cannot write and a closed handle cannot read. */
    assert(vfs_write(handles[0], "a", 1U) == -1);
    assert(vfs_close(handles[1]) == 0);
    assert(vfs_read(handles[1], buffer, 1U) == -1);

    for (int i = 0; i < MAX_HANDLES; i++)
        (void)vfs_close(handles[i]);
}

int main(void)
{
    test_node.id = 1U;
    test_node.type = FS_NODE_FILE;
    test_node.size = 10U;

    vfs_initialize();
    assert(vfs_is_initialized());

    test_single_threaded_semantics();
    test_concurrent_open_close();

    puts("VFS_HANDLE_TABLE_OK");
    return 0;
}
