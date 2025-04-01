#ifndef GC_H
#define GC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GC_INITIAL_CAPACITY 256
#define GC_GROWTH_FACTOR 2
#define GC_INCREMENTAL_MARK_BYTES (256 * 1024)
#define GC_FULL_COLLECTION_INTERVAL 10
#define GC_MINOR_COLLECTION_INTERVAL 10

#define TIME

typedef struct {
    void** items;
    size_t capacity;
    size_t size;
} vector_t;

typedef struct {
    vector_t gray_stack;
    vector_t roots;

    uint32_t bytes_allocated_since_collection;
    uint32_t collection_counter;
    bool collection_in_progress;
    bool is_minor_collection;
    size_t prev_root_size;
} gc_t;

typedef struct {
    double gc_time;
    double inc_time;
    double gc_time_max;
    double gc_time_min;
    double inc_time_max;
    double inc_time_min;
    size_t gc_calls;
    size_t inc_calls;
    size_t peak_before_clean;
    size_t tot_allocs;
} gc_meta_t;

/**
 * Initialize the garbage collector
 *
 * @param trace_cb Callback for tracing object references
 */
void gc_init();

/**
 * Destroy the garbage collector and free all resources
 */
void gc_destroy();

/**
 * Allocate memory with garbage collection
 *
 * @param size Size in bytes to allocate
 * @return Pointer to allocated memory or NULL on failure
 */
void* gc_allocate(uint32_t size);

/**
 * Write barrier - must be called whenever a reference field is modified
 *
 * @param obj The object whose field is being modified
 */
void gc_write_barrier(void* obj);

/**
 * Push an object to the root set (shadow stack)
 *
 * @param root The root object
 */
void gc_push_root(void* root);

/**
 * Pop objects from the root set
 *
 * @param count Number of objects to pop
 */
void gc_pop_roots(size_t count);

/**
 * Force a garbage collection cycle
 *
 * @param force_major If true, forces a major collection. Otherwise, determines
 *                    collection type based on internal GC policies.
 */
void gc_collect(bool force_major);

/**
 * Conservative object tracing - examines each word in the object
 * to see if it looks like a pointer
 *
 * @param obj The object to trace
 * @param mark_cb The callback to mark potential referenced objects
 */
void gc_conservative_trace(void* obj);

#endif  // GC_H