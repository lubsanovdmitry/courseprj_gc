#include "gc.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "memory.h"

gc_t gc;
extern allocator_t allocator;

#ifdef TIME
gc_meta_t gc_meta;
#endif

static void mark_roots();
static void mark_object(void* ptr);
static void sweep();
static bool is_ptr_in_heap(void* ptr);

static void v_init(vector_t* vec) {
    vec->capacity = GC_INITIAL_CAPACITY;
    vec->size = 0;
    vec->items = calloc(vec->capacity, sizeof(void*));
    assert(vec->items != NULL);
}

static void v_push(vector_t* vec, void* item) {
    if (vec->size >= vec->capacity) {
        vec->capacity *= GC_GROWTH_FACTOR;
        vec->items = realloc(vec->items, sizeof(void*) * vec->capacity);
        assert(vec->items != NULL);
    }
    vec->items[vec->size++] = item;
}

static void* v_pop(vector_t* vec) {
    if (vec->size == 0) {
        return NULL;
    }
    return vec->items[--vec->size];
}

void gc_init() {
    v_init(&gc.gray_stack);
    v_init(&gc.roots);

    gc.bytes_allocated_since_collection = 0;
    gc.collection_counter = 0;
    gc.collection_in_progress = false;
    gc.is_minor_collection = false;

    void* heap = malloc(HEAP_SIZE);
    memory_init(heap, HEAP_SIZE);
}

void gc_destroy() {
    free(gc.gray_stack.items);
    free(gc.roots.items);
    free(allocator.heap);
}

static bool is_marked(void* ptr) {
    if (!ptr)
        return false;
    color_t color = memory_get_color(ptr);
    return color == CBLK || color == CDGRAY;
}

void gc_push_root(void* root) {
    if (root) {
        v_push(&gc.roots, root);
    }
}

void gc_pop_roots(size_t count) {
    if (count > gc.roots.size) {
        gc.roots.size = 0;
        return;
    }
    gc.roots.size -= count;
}

static void mark_object(void* ptr) {
    if (!ptr)
        return;

    color_t color = memory_get_color(ptr);

    if (color == CBLK || color == CDGRAY) {
        return;
    }

    memory_set_color(ptr, CDGRAY);
    v_push(&gc.gray_stack, ptr);
}

static void process_gray_stack() {
    while (gc.gray_stack.size > 0) {
        void* obj = v_pop(&gc.gray_stack);
        if (!obj)
            break;

        memory_set_color(obj, CBLK);

        gc_conservative_trace(obj);
    }
}

static void mark_roots() {
    for (size_t i = 0; i < gc.roots.size; i++) {
        mark_object(gc.roots.items[i]);
    }

    process_gray_stack();
}

static void sweep() {
    block_header_t** pp = &allocator.large;
    while (*pp) {
        block_header_t* cur = *pp;

        if (cur->color == CWHITE || cur->color == CGRAY) {
            *pp = cur->next;
            void* ptr = (void*)(cur + 1);
            memory_free(ptr);
        } else {
            cur->color = CWHITE;
            pp = &cur->next;
        }
    }

    for (int i = 0; i < NUM_CLASSES; i++) {
        region_t* region = &allocator.size_classes[i];
        block_header_t* cur = region->start;
        block_header_t* end =
            (block_header_t*)((uintptr_t)region->start + region->region_size);

        while (cur < end) {
            if (cur->occ && (cur->color == CWHITE || cur->color == CGRAY)) {
                void* ptr = (void*)(cur + 1);
                memory_free(ptr);
            } else if (cur->occ) {
                cur->color = CWHITE;
            }
            cur = (block_header_t*)((uintptr_t)cur + region->block_size);
        }
    }

    memory_coalesce_blks();
}

void gc_conservative_trace(void* obj) {
    if (!obj)
        return;

    uint32_t size = memory_get_sz(obj);
    uintptr_t* start = (uintptr_t*)obj;
    uintptr_t* end = (uintptr_t*)((uint8_t*)obj + size);

    for (uintptr_t* p = start; p < end; p++) {
        uintptr_t value = *p;

        if (value >= (uintptr_t)allocator.heap && value < allocator.end) {
            uintptr_t aligned = value & ~(ALIGNMENT - 1);
            block_header_t* potential_hdr = ((block_header_t*)aligned) - 1;

            if ((uintptr_t)potential_hdr >= (uintptr_t)allocator.heap &&
                (uintptr_t)potential_hdr <
                    (uintptr_t)allocator.heap + allocator.heap_size &&
                potential_hdr->occ) {
                mark_object((void*)aligned);
            }
        }
    }
}

void gc_collect(bool force_major) {
#ifdef TIME
    clock_t s = clock();
    if (memory_get_allocd_sz() > gc_meta.peak_before_clean) {
        gc_meta.peak_before_clean = memory_get_allocd_sz();
    }
#endif

    gc.collection_in_progress = true;

    mark_roots();

    sweep();

    gc.collection_in_progress = false;
    gc.bytes_allocated_since_collection = 0;
    gc.collection_counter++;

#ifdef TIME
    gc_meta.gc_calls++;
    double t = (clock() - s) / (double)CLOCKS_PER_SEC;
    gc_meta.gc_time += t;
    if (gc_meta.gc_time_max < t) {
        gc_meta.gc_time_max = t;
    }
    if (gc_meta.gc_time_min > t) {
        gc_meta.gc_time_min = t;
    }
#endif
}

void gc_write_barrier(void* obj) {
    return;
}

void* gc_allocate(uint32_t size) {
    gc.bytes_allocated_since_collection += size;

    void* ptr = memory_alloc(size);

    if (!ptr) {
        gc_collect(true);
        ptr = memory_alloc(size);
    }
    if (ptr) {
        ++gc_meta.tot_allocs;
    }
    return ptr;
}

void* gc_realloc(void* obj, uint32_t new_size) {
    if (!obj) {
        return gc_allocate(new_size);
    }

    void* new_obj = memory_realloc(obj, new_size);

    if (!new_obj) {
        gc_collect(true);

        uint32_t sz = memory_get_sz(obj);
        new_obj = memory_alloc(new_size);

        if (new_obj) {
            memcpy(new_obj, obj, sz < new_size ? sz : new_size);
            memory_free(obj);
        }
    }

    return new_obj;
}