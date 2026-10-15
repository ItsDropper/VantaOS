#include "heap.h"
#include "paging.h"
#include "pmm.h"

#include <stddef.h>
#include <stdint.h>

#define BLOCK_FREE 0U
#define BLOCK_USED 1U
#define BLOCK_MAGIC 0x56414E54U
#define HEAP_INITIAL_PAGES 16U

typedef struct heap_block
{
    size_t size;
    uint32_t status;
    uint32_t magic;
    struct heap_block* next;
} heap_block_t;

static heap_block_t* first_block;
static size_t heap_used;
static size_t heap_size;
static uint32_t heap_pages;
static int heap_initialized;

static size_t align_up(size_t value)
{
    return (value + (HEAP_ALIGNMENT - 1U)) &
           ~(HEAP_ALIGNMENT - 1U);
}

static int block_is_valid(const heap_block_t* block)
{
    if (!block ||
        block->magic != BLOCK_MAGIC ||
        (block->status != BLOCK_FREE &&
         block->status != BLOCK_USED) ||
        block->size == 0)
        return 0;

    uintptr_t address = (uintptr_t)block;

    return address >= HEAP_VIRTUAL_BASE &&
           address < HEAP_VIRTUAL_BASE + heap_size;
}

static void split_block(heap_block_t* block, size_t size)
{
    if (!block_is_valid(block) ||
        block->status != BLOCK_FREE ||
        block->size < size)
        return;

    size_t remaining = block->size - size;

    if (remaining <= sizeof(heap_block_t) + HEAP_ALIGNMENT)
        return;

    heap_block_t* next =
        (heap_block_t*)((uint8_t*)block +
                        sizeof(heap_block_t) + size);

    next->size =
        remaining - sizeof(heap_block_t);
    next->status = BLOCK_FREE;
    next->magic = BLOCK_MAGIC;
    next->next = block->next;

    block->size = size;
    block->next = next;
}

static void merge_free_blocks(void)
{
    heap_block_t* block = first_block;

    while (block && block->next)
    {
        if (!block_is_valid(block))
            break;

        heap_block_t* next = block->next;

        if (!block_is_valid(next))
            break;

        uintptr_t expected =
            (uintptr_t)block +
            sizeof(heap_block_t) +
            block->size;

        if (block->status == BLOCK_FREE &&
            next->status == BLOCK_FREE &&
            expected == (uintptr_t)next)
        {
            block->size +=
                sizeof(heap_block_t) + next->size;
            block->next = next->next;
            continue;
        }

        block = next;
    }
}

static int heap_add_page(void)
{
    if (heap_pages >= HEAP_MAX_SIZE / PAGE_SIZE)
        return 0;

    void* physical = pmm_alloc_block();

    if (!physical)
        return 0;

    uintptr_t virtual_address =
        HEAP_VIRTUAL_BASE +
        (uintptr_t)heap_pages * PAGE_SIZE;

    if (!paging_map_page(virtual_address,
                         (uintptr_t)physical))
    {
        pmm_free_block(physical);
        return 0;
    }

    heap_pages++;
    heap_size += PAGE_SIZE;

    if (!first_block)
    {
        first_block = (heap_block_t*)virtual_address;
        first_block->size =
            PAGE_SIZE - sizeof(heap_block_t);
        first_block->status = BLOCK_FREE;
        first_block->magic = BLOCK_MAGIC;
        first_block->next = NULL;
        return 1;
    }

    heap_block_t* block = first_block;

    while (block->next)
    {
        if (!block_is_valid(block))
            return 0;

        block = block->next;
    }

    uintptr_t expected =
        (uintptr_t)block +
        sizeof(heap_block_t) +
        block->size;

    if (block->status == BLOCK_FREE &&
        expected == virtual_address)
    {
        block->size += PAGE_SIZE;
    }
    else
    {
        heap_block_t* next =
            (heap_block_t*)virtual_address;

        next->size =
            PAGE_SIZE - sizeof(heap_block_t);
        next->status = BLOCK_FREE;
        next->magic = BLOCK_MAGIC;
        next->next = NULL;
        block->next = next;
    }

    return 1;
}

void heap_initialize(void)
{
    first_block = NULL;
    heap_used = 0;
    heap_size = 0;
    heap_pages = 0;
    heap_initialized = 0;

    for (uint32_t i = 0; i < HEAP_INITIAL_PAGES; i++)
    {
        if (!heap_add_page())
            return;
    }

    heap_initialized = 1;
}

void* kmalloc(size_t size)
{
    if (!heap_initialized || size == 0)
        return NULL;

    if (size > HEAP_MAX_SIZE - sizeof(heap_block_t))
        return NULL;

    size = align_up(size);

    while (1)
    {
        heap_block_t* block = first_block;

        while (block)
        {
            if (!block_is_valid(block))
                return NULL;

            if (block->status == BLOCK_FREE &&
                block->size >= size)
            {
                split_block(block, size);
                block->status = BLOCK_USED;
                heap_used += block->size;

                return (void*)((uint8_t*)block +
                               sizeof(heap_block_t));
            }

            block = block->next;
        }

        if (!heap_add_page())
            return NULL;
    }
}

void kfree(void* ptr)
{
    if (!heap_initialized || !ptr)
        return;

    uintptr_t address = (uintptr_t)ptr;

    if (address < HEAP_VIRTUAL_BASE + sizeof(heap_block_t) ||
        address >= HEAP_VIRTUAL_BASE + heap_size)
        return;

    if ((address - HEAP_VIRTUAL_BASE) % HEAP_ALIGNMENT != 0)
        return;

    heap_block_t* block =
        (heap_block_t*)((uint8_t*)ptr -
                        sizeof(heap_block_t));

    if (!block_is_valid(block) ||
        block->status != BLOCK_USED)
        return;

    block->status = BLOCK_FREE;
    heap_used -= block->size;

    merge_free_blocks();
}

size_t heap_get_total_size(void)
{
    return heap_size;
}

size_t heap_get_used_size(void)
{
    return heap_used;
}

size_t heap_get_free_size(void)
{
    size_t free_size = 0;
    heap_block_t* block = first_block;

    while (block)
    {
        if (!block_is_valid(block))
            break;

        if (block->status == BLOCK_FREE)
            free_size += block->size;

        block = block->next;
    }

    return free_size;
}

int heap_is_initialized(void)
{
    return heap_initialized;
}
