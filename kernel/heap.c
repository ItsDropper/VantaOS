#include "heap.h"

#include <stdint.h>
#include <stddef.h>

#define HEAP_SIZE (64 * 1024)
#define BLOCK_FREE 0
#define BLOCK_USED 1

typedef struct heap_block
{
    size_t size;
    uint32_t status;
    struct heap_block* next;
} heap_block_t;

static uint8_t heap_area[HEAP_SIZE] __attribute__((aligned(HEAP_ALIGNMENT)));
static heap_block_t* first_block;
static size_t heap_used;
static int heap_initialized;

static size_t align_up(size_t value)
{
    return (value + (HEAP_ALIGNMENT - 1)) &
           ~(HEAP_ALIGNMENT - 1);
}

static void split_block(heap_block_t* block, size_t size)
{
    size_t remaining = block->size - size;

    if (remaining <= sizeof(heap_block_t) + HEAP_ALIGNMENT)
        return;

    heap_block_t* next =
        (heap_block_t*)((uint8_t*)block +
                        sizeof(heap_block_t) + size);

    next->size = remaining - sizeof(heap_block_t);
    next->status = BLOCK_FREE;
    next->next = block->next;

    block->size = size;
    block->next = next;
}

static void merge_free_blocks(void)
{
    heap_block_t* block = first_block;

    while (block && block->next)
    {
        heap_block_t* next = block->next;

        if (block->status == BLOCK_FREE &&
            next->status == BLOCK_FREE)
        {
            block->size += sizeof(heap_block_t) + next->size;
            block->next = next->next;
            continue;
        }

        block = next;
    }
}

void heap_initialize(void)
{
    first_block = (heap_block_t*)heap_area;
    first_block->size = HEAP_SIZE - sizeof(heap_block_t);
    first_block->status = BLOCK_FREE;
    first_block->next = NULL;

    heap_used = 0;
    heap_initialized = 1;
}

void* kmalloc(size_t size)
{
    if (!heap_initialized || size == 0)
        return NULL;

    size = align_up(size);

    heap_block_t* block = first_block;

    while (block)
    {
        if (block->status == BLOCK_FREE &&
            block->size >= size)
        {
            split_block(block, size);
            block->status = BLOCK_USED;
            heap_used += size;

            return (void*)((uint8_t*)block + sizeof(heap_block_t));
        }

        block = block->next;
    }

    return NULL;
}

void kfree(void* ptr)
{
    if (!heap_initialized || !ptr)
        return;

    if ((uintptr_t)ptr < (uintptr_t)heap_area ||
        (uintptr_t)ptr >= (uintptr_t)heap_area + HEAP_SIZE)
        return;

    heap_block_t* block =
        (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));

    if (block->status != BLOCK_USED)
        return;

    block->status = BLOCK_FREE;
    heap_used -= block->size;

    merge_free_blocks();
}

size_t heap_get_total_size(void)
{
    return HEAP_SIZE;
}

size_t heap_get_used_size(void)
{
    return heap_used;
}

size_t heap_get_free_size(void)
{
    return HEAP_SIZE - sizeof(heap_block_t) - heap_used;
}

int heap_is_initialized(void)
{
    return heap_initialized;
}
