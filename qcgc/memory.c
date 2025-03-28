#include "memory.h"

#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

allocator_t allocator;

static uint32_t align_sz(uint32_t size) {
    return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

void memory_init(void* heap, uint32_t heap_size) {
    memset(&allocator, 0, sizeof(allocator));
    allocator.heap = heap;
    allocator.heap_size = heap_size;
    allocator.end = (uintptr_t)heap + heap_size;
    uint32_t small_reg_sz = align_sz((heap_size / 2) / NUM_CLASSES);
    uintptr_t cur = (uintptr_t)heap;
    for (int i = 0; i < NUM_CLASSES; ++i) {
        uint32_t blk_sz = SIZE_CLASSES[i] + sizeof(block_header_t);
        allocator.size_classes[i].start = (block_header_t*)cur;
        allocator.size_classes[i].bump = (block_header_t*)cur;
        allocator.size_classes[i].block_size = blk_sz;
        allocator.size_classes[i].region_size = small_reg_sz;
        allocator.size_classes[i].remaining = small_reg_sz;
        allocator.size_classes[i].free_list = NULL;
        cur += small_reg_sz;
    }
    block_header_t* first = (block_header_t*)cur;
    first->size = ((uintptr_t)heap + heap_size) - cur - sizeof(*first);
    first->occ = 0;
    first->large = 1;
    first->size_class = 31;
    first->next = NULL;
    allocator.free = first;
}

static int get_size_class(uint16_t size) {
    for (int i = 0; i < NUM_CLASSES; ++i) {
        if (size <= SIZE_CLASSES[i]) {
            return i;
        }
    }
    return -1;
}

static void* reg_alloc(int size_class, uint32_t size) {
    region_t* reg = &allocator.size_classes[size_class];
    if (reg->remaining < reg->block_size) {
        if (reg->free_list != NULL) {
            block_header_t* blk = reg->free_list;
            reg->free_list = blk->next;
            blk->color = CGRAY;
            blk->occ = 1;
            allocator.allocated += SIZE_CLASSES[size_class];
            return (void*)(blk + 1);
        } else {
            return NULL;
        }
    }
    block_header_t* blk = reg->bump;
    blk->size = SIZE_CLASSES[size_class];
    blk->color = CGRAY;
    blk->large = 0;
    blk->size_class = size_class;
    blk->occ = 1;
    reg->bump = (block_header_t*)((uintptr_t)reg->bump + reg->block_size);
    reg->remaining -= reg->block_size;
    allocator.allocated += SIZE_CLASSES[size_class];
    return (void*)(blk + 1);
}

static void* mem_alloc_free_list(uint32_t size) {
    block_header_t* prev = NULL;
    block_header_t* cur = allocator.free;
    block_header_t* best = NULL;
    block_header_t* best_fit_prev = NULL;
    uint32_t best_sz_d = UINT32_MAX;
    int blks_chkd = 0;

    while (cur && blks_chkd < SEARCH_LIM) {
        uint32_t sz_d = cur->size - size;
        if (cur->size >= size && sz_d < best_sz_d) {
            best = cur;
            best_fit_prev = prev;
            if (sz_d < 2 * ALIGNMENT) {
                break;
            }
        }
        prev = cur;
        cur = cur->next;
        ++blks_chkd;
    }

    if (!best) {
        return NULL;
    }

    if (best_fit_prev) {
        best_fit_prev->next = best->next;
    } else {
        allocator.free = best->next;
    }

    uint32_t rem = best->size - size;
    if (rem >= sizeof(block_header_t) + 16 * ALIGNMENT) {
        block_header_t* new =
            (block_header_t*)((uint8_t*)best + sizeof(block_header_t) + size);
        new->size = rem - sizeof(block_header_t);
        new->occ = 0;
        new->size_class = 31;
        new->large = 1;
        new->next = allocator.free;
        allocator.free = new;

        best->size = size;
    }

    best->occ = 1;
    allocator.allocated += best->size;
    return (void*)(best + 1);
}

static void* mem_alloc_med(uint32_t size) {
    void* new = mem_alloc_free_list(size);
    if (new) {
        block_header_t* hdr = ((block_header_t*)new) - 1;
        hdr->size = size;
        hdr->size_class = 31;
        hdr->occ = 1;
        hdr->next = allocator.large;
        hdr->color = CGRAY;
        allocator.large = hdr;
    }
    return new;
}

void* memory_alloc(uint32_t size) {
    if (size == 0) {
        return NULL;
    }

    size = align_sz(size);

    void* new;

    if (size <= SIZE_CLASSES[NUM_CLASSES - 1]) {
        int cl = get_size_class(size);
        new = reg_alloc(cl, size);
    } else {
        new = mem_alloc_med(size);
    }

    if (!new) {
        // assert(0);
    }

    return new;
}

void memory_free(void* ptr) {
    if (!ptr) {
        return;
    }

    block_header_t* hdr = ((block_header_t*)ptr) - 1;
    allocator.allocated -= hdr->size;

    if (hdr->size_class < NUM_CLASSES) {
        hdr->occ = 0;
        hdr->next = allocator.size_classes[hdr->size_class].free_list;
        allocator.size_classes[hdr->size_class].free_list = hdr;
    } else {
        block_header_t** pp = &allocator.large;
        while (*pp && *pp != hdr) {
            pp = &(*pp)->next;
        }
        if (*pp) {
            *pp = hdr->next;
        }

        hdr->occ = 0;

        hdr->next = NULL;
        block_header_t** cur = &allocator.free;
        while (*cur && *cur < hdr) {
            if (*cur == (*cur)->next) {
                (*cur)->next = NULL;
                break;
            }
            cur = &(*cur)->next;
        }
        hdr->next = *cur;
        *cur = hdr;
    }
}

uint32_t memory_get_allocd_sz() {
    return allocator.allocated;
}

uint32_t memory_get_free_sz() {
    return HEAP_SIZE - allocator.allocated;
}

color_t memory_get_color(void* ptr) {
    if (!ptr) {
        return CWHITE;
    }
    return (((block_header_t*)ptr) - 1)->color;
}

void memory_set_color(void* ptr, color_t color) {
    if (!ptr) {
        return;
    }
    (((block_header_t*)ptr) - 1)->color = color;
}

uint32_t memory_get_sz(void* ptr) {
    return (((block_header_t*)ptr) - 1)->size;
}

void memory_coalesce_blks() {
    block_header_t* cur = allocator.free;

    while (cur) {
        if (cur == cur->next) {
            cur->next = NULL;
            break;
        }
        cur = cur->next;
    }
    cur = allocator.free;

    while (cur && cur->next) {
        if (cur == cur->next) {
            cur->next = NULL;
            break;
        }

        block_header_t* next_block = cur->next;
        uintptr_t cur_end = (uintptr_t)(cur + 1) + cur->size;

        if (cur_end == (uintptr_t)next_block) {
            cur->size += sizeof(block_header_t) + next_block->size;

            if (next_block->next == cur || next_block->next == next_block) {
                cur->next = NULL;
            } else {
                cur->next = next_block->next;
            }

        } else {
            cur = cur->next;
        }
    }
}