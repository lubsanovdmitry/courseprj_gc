#include "gc.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "memory.h"

static void gc_mark_object(void* ptr);
static void gc_sweep(bool is_minor);
static bool is_marked(void* ptr);
void gc_collect(bool force_major);

gc_t gc;
#ifdef TIME
gc_meta_t gc_meta;
#endif

extern allocator_t allocator;

static void v_init(vector_t* stack) {
    stack->capacity = GC_INITIAL_CAPACITY;
    stack->size = 0;
    stack->items = calloc(stack->capacity, sizeof(void*));
    assert(stack->items != NULL);
}

static void v_push(vector_t* stack, void* item) {
    if (stack->size >= stack->capacity) {
        stack->capacity *= GC_GROWTH_FACTOR;
        stack->items = realloc(stack->items, sizeof(void*) * stack->capacity);
    }
    stack->items[stack->size++] = item;
}

static void* v_pop(vector_t* stack) {
    if (stack->size == 0) {
        return NULL;
    }
    return stack->items[--stack->size];
}

static void v_mass_pop(vector_t* roots, size_t count) {
    if (count > roots->size) {
        roots->size = 0;
        return;
    }
    roots->size -= count;
}

void gc_init() {
    v_init(&gc.gray_stack);
    v_init(&gc.roots);

    gc.bytes_allocated_since_collection = 0;
    gc.collection_counter = 0;
    gc.collection_in_progress = false;
    gc.is_minor_collection = false;
    gc.prev_root_size = 0;
    void* heap = malloc(HEAP_SIZE);
    memory_init(heap, HEAP_SIZE);
}

void gc_destroy() {
    free(gc.gray_stack.items);
    free(gc.roots.items);

    free(allocator.heap);
}

static bool is_marked(void* ptr) {
    color_t color = memory_get_color(ptr);
    return color == CBLK || color == CDGRAY;
}

static void gc_mark_object(void* ptr) {
    if (!ptr)
        return;

    color_t color = memory_get_color(ptr);

    if (color == CBLK || color == CDGRAY) {
        return;
    }

    memory_set_color(ptr, CDGRAY);
    v_push(&gc.gray_stack, ptr);
}

static void gc_process_gray_stack(size_t process_limit) {
    size_t processed = 0;
    while (gc.gray_stack.size > 0 &&
           (process_limit == 0 || processed < process_limit)) {
        void* obj = v_pop(&gc.gray_stack);
        if (!obj) {
            break;
        }

        memory_set_color(obj, CBLK);

        gc_conservative_trace(obj);

        processed++;
    }
}

extern void validate_free_list();

static void gc_sweep(bool is_minor) {
    block_header_t** pp = &allocator.large;

    while (*pp) {
        block_header_t* cur = *pp;

        if ((cur->color == CWHITE || cur->color == CGRAY)) {
            block_header_t* next = cur->next;

            if (cur->color == CWHITE || cur->color == CGRAY) {
                *pp = cur->next;
                void* ptr = (void*)(cur + 1);
                memory_free(ptr);
            } else {
                cur->color = CWHITE;
                pp = &cur->next;
            }
        } else {
            if (!is_minor && cur->color == CBLK) {
                cur->color = CWHITE;
            }
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
            } else if (!is_minor && cur->color == CBLK) {
                cur->color = CWHITE;
            }
            cur = (block_header_t*)((uintptr_t)cur + region->block_size);
        }
    }
}

void gc_write_barrier(void* obj) {
    if (!obj)
        return;

    color_t color = memory_get_color(obj);

    if (color == CGRAY || color == CDGRAY) {
        return;
    }

    if (color == CWHITE) {
        memory_set_color(obj, CGRAY);
    } else if (color == CBLK) {
        memory_set_color(obj, CDGRAY);
        v_push(&gc.gray_stack, obj);
    }
}

void gc_push_root(void* root) {
    if (root) {
        v_push(&gc.roots, root);
    }
}

void gc_pop_roots(size_t count) {
    // if (gc.prev_root_size >= count) {
    //     gc.prev_root_size -= count;
    // } else if (gc.prev_root_size < count) {
    //     gc.prev_root_size = 0;
    // }
    v_mass_pop(&gc.roots, count);
}

static void gc_start_mark_phase(bool is_minor) {
    gc.collection_in_progress = true;
    size_t start = 0;
    if (is_minor) {
        start = gc.prev_root_size;
    }
    for (size_t i = 0; i < gc.roots.size; i++) {
        gc_mark_object(gc.roots.items[i]);
    }
    gc.prev_root_size = gc.roots.size;
}

static void gc_incremental_mark_step() {
#ifdef TIME
    clock_t s = clock();
    if (memory_get_allocd_sz() > gc_meta.peak_before_clean) {
        gc_meta.peak_before_clean = memory_get_allocd_sz();
    }
#endif
    bool is_minor = gc.collection_counter % GC_MINOR_COLLECTION_INTERVAL != 0;
    gc.is_minor_collection = is_minor;
    gc_start_mark_phase(is_minor);
    size_t lim = gc.gray_stack.size / 2;
    if (lim < 128) {
        lim = 128;
    }
    gc_process_gray_stack(lim);

    // if (gc.gray_stack.size == 0) {
    //     gc_sweep(gc.is_minor_collection);
    //     gc.collection_in_progress = false;
    //     gc.bytes_allocated_since_collection = 0;
    //     gc.collection_counter++;
    // }

#ifdef TIME
    gc_meta.inc_calls++;
    double t = (clock() - s) / (double)CLOCKS_PER_SEC;
    gc_meta.inc_time += t;
    if (gc_meta.inc_time_max < t) {
        gc_meta.inc_time_max = t;
    }
    if (gc_meta.inc_time_min > t) {
        gc_meta.inc_time_min = t;
    }
#endif
}

void* gc_allocate(uint32_t size) {
    if (gc.bytes_allocated_since_collection >= GC_INCREMENTAL_MARK_BYTES) {
        gc_incremental_mark_step();
        if (gc_meta.tot_allocs % 1000 == 0) {
            if (gc.collection_counter % GC_FULL_COLLECTION_INTERVAL == 0) {
                gc_collect(true);
            } else {
                gc_collect(false);
            }
        }
    }

    void* ptr = memory_alloc(size);

    if (ptr) {
        gc.bytes_allocated_since_collection += size;
        ++gc_meta.tot_allocs;
    }
    validate_free_list();
    return ptr;
}

void* gc_realloc(void* obj, uint32_t new_size) {
    if (!obj) {
        return gc_allocate(new_size);
    }
    void* new = memory_realloc(obj, new_size);
    if (!new) {
        uint32_t sz = memory_get_sz(obj);
        new = gc_allocate(new_size);
        if (!new) {
            return NULL;
        }
        memcpy(new, obj, sz);
    }
    return new;
}

void gc_collect(bool force_major) {
#ifdef TIME
    clock_t s = clock();
    if (memory_get_allocd_sz() > gc_meta.peak_before_clean) {
        gc_meta.peak_before_clean = memory_get_allocd_sz();
    }
#endif

    bool is_minor = gc.collection_counter % GC_MINOR_COLLECTION_INTERVAL != 0;
    if (force_major) {
        is_minor = false;
    }
    gc.is_minor_collection = is_minor;
    gc_start_mark_phase(is_minor);

    gc_process_gray_stack(0);

    gc_sweep(is_minor);

    gc.collection_in_progress = false;
    gc.bytes_allocated_since_collection = 0;
    gc.collection_counter++;
    if (1) {
        memory_coalesce_blks();
    }

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

extern bool is_valid_heap_addr(void* ptr);

void gc_conservative_trace(void* obj) {
    if (!obj)
        return;

    uint32_t size = memory_get_sz(obj);
    uintptr_t* start = (uintptr_t*)obj;
    uintptr_t* end = (uintptr_t*)((uintptr_t)obj + size);

    for (uintptr_t* p = start; p < end; p++) {
        uintptr_t value = *p;

        if (value >= (uintptr_t)allocator.heap && value < allocator.end) {
            uintptr_t aligned = value & ~(ALIGNMENT - 1);
            block_header_t* potential_hdr = ((block_header_t*)aligned) - 1;

            if ((uintptr_t)potential_hdr >= (uintptr_t)allocator.heap &&
                (uintptr_t)potential_hdr <
                    (uintptr_t)allocator.heap + allocator.heap_size &&
                potential_hdr->occ) {
                gc_mark_object((void*)aligned);
            }
        }
    }
}