#include "memory.h"

#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

allocator_t allocator;

static uint32_t align_sz(uint32_t size) {
    return (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
}

bool is_valid_heap_addr(void* ptr) {
    if (!ptr)
        return true;
    return (uintptr_t)ptr >= (uintptr_t)allocator.heap &&
           (uintptr_t)ptr < allocator.end;
}

void validate_free_list() {
    // block_header_t* cur = allocator.free;
    // while (cur) {
    //     if (!is_valid_heap_addr(cur)) {
    //         printf("Invalid free list pointer: %p\n", cur);
    //         abort();
    //     }
    //     if (cur->next && !is_valid_heap_addr(cur->next)) {
    //         printf("Invalid next pointer: %p -> %p\n", cur, cur->next);
    //         abort();
    //     }
    //     cur = cur->next;
    // }
    // cur = allocator.large;
    // while (cur) {
    //     if (!is_valid_heap_addr(cur)) {
    //         printf("Invalid large list pointer: %p\n", cur);
    //         abort();
    //     }
    //     if (cur->next && !is_valid_heap_addr(cur->next)) {
    //         printf("Invalid large next pointer: %p -> %p\n", cur, cur->next);
    //         abort();
    //     }
    //     cur = cur->next;
    // }
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
    allocator.large = NULL;
    block_header_t* first = (block_header_t*)cur;
    first->size = ((uintptr_t)heap + heap_size) - cur - sizeof(*first);
    first->occ = 0;
    first->size_class = 31;
    first->next = NULL;
    allocator.free = first;
    validate_free_list();
}

static int get_size_class(uint16_t size) {
    for (int i = 0; i < NUM_CLASSES; ++i) {
        if (size <= SIZE_CLASSES[i]) {
            return i;
        }
    }
    return -1;
}

uint32_t memory_get_sz(void* ptr) {
    if (!ptr) {
        return 0;
    }
    return (((block_header_t*)ptr) - 1)->size;
}

static void* reg_alloc(int size_class, uint32_t size) {
    region_t* reg = &allocator.size_classes[size_class];
    if (reg->remaining < reg->block_size) {
        if (reg->free_list != NULL) {
            block_header_t* blk = reg->free_list;
            reg->free_list = blk->next;
            blk->color = CGRAY;
            blk->occ = 0xEA;
            allocator.allocated += SIZE_CLASSES[size_class];
            return (void*)(blk + 1);
        } else {
            return NULL;
        }
    }
    block_header_t* blk = reg->bump;
    blk->size = SIZE_CLASSES[size_class];
    blk->color = CGRAY;
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
    validate_free_list();
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
        new->next = NULL;
        best->size = size;
        if (allocator.free) {
            block_header_t** prev = &allocator.free;
            block_header_t* current = allocator.free;

            while (current && (uintptr_t)current < (uintptr_t) new) {
                prev = &current->next;
                current = current->next;
            }

            new->next = current;
            *prev = new;
        } else {
            allocator.free = new;
        }
    }
    validate_free_list();
    best->occ = 0xDE;
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
        validate_free_list();
        hdr->next = allocator.large;
        hdr->color = CGRAY;
        allocator.large = hdr;
        validate_free_list();
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

    return new;
}

void memory_free(void* ptr) {
    // validate_free_list();
    if (!ptr) {
        return;
    }

    block_header_t* hdr = ((block_header_t*)ptr) - 1;
    if (hdr->occ == 0) {
        return;
    }
    allocator.allocated -= hdr->size;

    if (hdr->size_class < NUM_CLASSES) {
        hdr->occ = 0;
        hdr->next = allocator.size_classes[hdr->size_class].free_list;
        allocator.size_classes[hdr->size_class].free_list = hdr;
    } else {
        validate_free_list();
        block_header_t** pp = &allocator.large;
        while (pp && *pp && *pp != hdr) {
            pp = &(*pp)->next;
        }
        if (*pp) {
            *pp = hdr->next;
        }
        validate_free_list();
        hdr->occ = 0;
        hdr->next = NULL;
        block_header_t* cur = allocator.free;

        if (allocator.free) {
            block_header_t** prev = &allocator.free;
            block_header_t* current = allocator.free;

            while (current && (uintptr_t)current < (uintptr_t)hdr) {
                prev = &current->next;
                current = current->next;
            }

            hdr->next = current;
            *prev = hdr;
        } else {
            allocator.free = hdr;
        }
        validate_free_list();
    }
}

void* memory_realloc(void* obj, uint32_t new_size) {
    block_header_t* hdr = (block_header_t*)obj - 1;
    if (hdr->size_class != 31 && SIZE_CLASSES[hdr->size_class] >= new_size) {
        hdr->size = new_size;
        return obj;
    }
    void* new = memory_alloc(new_size);
    if (!new) {
        return NULL;
    }
    memcpy(new, obj, hdr->size);
    memory_free(obj);
    return new;
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

void memory_coalesce_blks() {
    block_header_t* cur = allocator.free;

    while (cur && cur->next) {
        uintptr_t end_addr =
            (uintptr_t)cur + sizeof(block_header_t) + cur->size;
        if (end_addr == (uintptr_t)cur->next && !cur->occ && !cur->next->occ) {
            block_header_t* old = cur->next;
            cur->next = old->next;
            cur->size += old->size + sizeof(block_header_t);

            memset(old, 0xEA, sizeof(block_header_t));
        } else {
            cur = cur->next;
        }
    }

    validate_free_list();
}