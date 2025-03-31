#ifndef GC_MEMORY_H
#define GC_MEMORY_H

#include <stdbool.h>
#include <stdint.h>

#define KBYTE 1024
#define MBYTE (1024 * KBYTE)

#define HEAP_SIZE (512 * MBYTE)
#define ALIGNMENT __alignof(void*)
#define SEARCH_LIM 1024

static const uint32_t SIZE_CLASSES[] = {16, 32, 64, 128, 256, 512};

#define NUM_CLASSES 6

typedef enum {
    CWHITE = 0,
    CGRAY = 1,
    CBLK = 2,
    CDGRAY = 3,
} color_t;

typedef struct blockheader_s {
    uint8_t color;
    uint8_t size_class;
    uint8_t occ;
    uint32_t size;
    struct blockheader_s* next;
} block_header_t;

typedef struct region_s {
    block_header_t* start;
    block_header_t* bump;
    uint32_t remaining;
    uint32_t block_size;
    uint32_t region_size;
    block_header_t* free_list;
} region_t;

typedef struct allocator_s {
    uint8_t* heap;
    uintptr_t end;
    uint32_t heap_size;
    uint32_t allocated;
    region_t size_classes[32];
    block_header_t* free;
    block_header_t* large;
} allocator_t;

/**
 * @brief Initialize the allocator
 *
 * @param heap heap to utilize
 * @param heap_size size of the heap
 */
void memory_init(void* heap, uint32_t heap_size);

/**
 * @brief Allocate memory from heap
 *
 * @param size size of chunk to be allocated
 * @return void* pointer to allocated memory
 */
void* memory_alloc(uint32_t size);

/**
 * @brief Rellocate memory in heap
 *
 * @param obj old object
 * @param new_size new size
 * @return void* new pointer
 */
void* memory_realloc(void* obj, uint32_t new_size);

/**
 * @brief Free memory from heap
 *
 * @param ptr pointer to memory to free
 */
void memory_free(void* ptr);

/**
 * @brief Get allocated memory size
 *
 * @return uint32_t allocated memory, in bytes
 */
uint32_t memory_get_allocd_sz();

/**
 * @brief Get free memory size
 *
 * @return uint32_t free memory, in bytes
 */
uint32_t memory_get_free_sz();

/**
 * @brief Get color of an object
 *
 * @param ptr pointer to object
 * @return color_t color of object
 */
color_t memory_get_color(void* ptr);

/**
 * @brief Set the color of an object
 *
 * @param ptr pointer to object
 * @param color color to be set
 */
void memory_set_color(void* ptr, color_t color);

/**
 * @brief Get size of an object
 *
 * @param ptr pointer to object
 * @return uint32_t size of the object
 */
uint32_t memory_get_sz(void* ptr);

/**
 * @brief Coalesce blocks in freelist
 *
 */
void memory_coalesce_blks();

#endif
