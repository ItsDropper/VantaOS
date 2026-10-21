/*
 * Copyright (c) 2026 ItsDropper
 *
 * This file is part of VantaOS and is licensed under the MIT License.
 * See the LICENSE file in the project root for the full license text.
 */

/*
 * Test-only kernel regression probes. This translation unit is empty in
 * ordinary builds; VANTAOS_SELF_TEST enables the test implementation.
 */
#include "desktop_process.h"
#include "filesystem.h"
#include "fat32.h"
#include "heap.h"
#include "graphics.h"
#include "keyboard.h"
#include "mouse.h"
#include "process.h"
#include "cpu_local.h"
#include "smp.h"
#include "panic.h"
#include "spinlock.h"
#include "pmm.h"
#include "serial.h"
#include "scheduler.h"
#include "shell.h"
#include "terminal.h"
#include "terminal_process.h"
#include "vfs.h"

#ifdef VANTAOS_SELF_TEST
#include "selftest.h"

static void pmm_allocator_probe(void)
{
    if (!pmm_is_initialized())
    {
        serial_write("VANTAOS_PMM_ALLOCATOR_FAIL\n");
        return;
    }

    uint32_t total_before = pmm_get_total_blocks();
    uint32_t free_before = pmm_get_free_blocks();
    uint32_t used_before = pmm_get_used_blocks();

    if (total_before == 0 ||
        free_before > total_before ||
        used_before > total_before ||
        free_before + used_before != total_before)
    {
        serial_write("VANTAOS_PMM_ALLOCATOR_FAIL\n");
        return;
    }

    void* first = pmm_alloc_block();
    void* second = pmm_alloc_block();

    if (!first || !second)
    {
        if (first) pmm_free_block(first);
        if (second) pmm_free_block(second);
        serial_write("VANTAOS_PMM_ALLOCATOR_FAIL\n");
        return;
    }

    uintptr_t first_address = (uintptr_t)first;
    uintptr_t second_address = (uintptr_t)second;
    int ok =
        first_address != second_address &&
        (first_address % PAGE_SIZE) == 0 &&
        (second_address % PAGE_SIZE) == 0 &&
        pmm_get_free_blocks() + 2U == free_before &&
        pmm_get_used_blocks() == used_before + 2U;

    pmm_free_block((void*)(first_address + 1U));
    ok = (pmm_get_free_blocks() + 2U == free_before) && ok;

    pmm_free_block(first);
    pmm_free_block(second);
    ok = (pmm_get_free_blocks() == free_before) && ok;
    ok = (pmm_get_used_blocks() == used_before) && ok;

    /* A second free must not release the same frame twice. */
    pmm_free_block(first);
    ok = (pmm_get_free_blocks() == free_before) && ok;
    ok = (pmm_get_total_blocks() == total_before) && ok;

    serial_write(ok ?
        "VANTAOS_PMM_ALLOCATOR_OK\n" :
        "VANTAOS_PMM_ALLOCATOR_FAIL\n");
}

static void heap_allocator_probe(void)
{
    if (!heap_is_initialized())
    {
        serial_write("VANTAOS_HEAP_ALLOCATOR_FAIL\n");
        return;
    }

    size_t total_before = heap_get_total_size();
    size_t used_before = heap_get_used_size();
    size_t free_before = heap_get_free_size();

    if (total_before < 16U * 4096U ||
        used_before > total_before ||
        free_before > total_before)
    {
        serial_write("VANTAOS_HEAP_ALLOCATOR_FAIL\n");
        return;
    }

    unsigned char* first = (unsigned char*)kmalloc(64U);
    unsigned char* second = (unsigned char*)kmalloc(257U);

    if (!first || !second)
    {
        if (first) kfree(first);
        if (second) kfree(second);
        serial_write("VANTAOS_HEAP_ALLOCATOR_FAIL\n");
        return;
    }

    int ok =
        ((uintptr_t)first % HEAP_ALIGNMENT) == 0 &&
        ((uintptr_t)second % HEAP_ALIGNMENT) == 0;

    for (size_t i = 0; i < 64U; i++)
        first[i] = (unsigned char)(i ^ 0x5AU);
    for (size_t i = 0; i < 257U; i++)
        second[i] = (unsigned char)(i ^ 0xA5U);
    for (size_t i = 0; i < 64U; i++)
        ok = (first[i] == (unsigned char)(i ^ 0x5AU)) && ok;
    for (size_t i = 0; i < 257U; i++)
        ok = (second[i] == (unsigned char)(i ^ 0xA5U)) && ok;

    size_t used_after_alloc = heap_get_used_size();
    kfree((void*)((uintptr_t)first + 1U));
    ok = (heap_get_used_size() == used_after_alloc) && ok;

    kfree(first);
    kfree(second);
    size_t used_after_free = heap_get_used_size();
    kfree(first); /* A double free must not change accounting. */

    ok = (used_after_free == used_before) && ok;
    ok = (heap_get_used_size() == used_before) && ok;
    ok = (heap_get_free_size() == free_before) && ok;
    ok = (heap_get_total_size() == total_before) && ok;

    serial_write(ok ?
        "VANTAOS_HEAP_ALLOCATOR_OK\n" :
        "VANTAOS_HEAP_ALLOCATOR_FAIL\n");
}

void memory_selftest_initialize(void)
{
    pmm_allocator_probe();
    heap_allocator_probe();
}
#endif
