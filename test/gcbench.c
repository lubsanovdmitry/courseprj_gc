#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include "../qcgc/gc.h"
#include "../qcgc/memory.h"

extern gc_t gc;
extern gc_meta_t gc_meta;
extern allocator_t allocator;

unsigned stats_rtclock(void) {
    struct timeval t;
    struct timezone tz;

    if (gettimeofday(&t, &tz) == -1)
        return 0;
    return (t.tv_sec * 1000 + t.tv_usec / 1000);
}

#define currentTime() stats_rtclock()
#define elapsedTime(x) (x)

// Benchmark parameters
static const int kStretchTreeDepth = 16;
static const int kLongLivedTreeDepth = 16;
static const int kArraySize = 500000;
static const int kMinTreeDepth = 4;
static const int kMaxTreeDepth = 16;

typedef struct Node_struct {
    struct Node_struct* left;
    struct Node_struct* right;
    int i, j;
} Node;

static int TreeSize(int i) {
    return ((1 << (i + 1)) - 1);
}

static int NumIters(int i) {
    return 2 * TreeSize(kStretchTreeDepth) / TreeSize(i);
}

static void Populate(int iDepth, Node* thisNode) {
    if (!thisNode)
        return;
    if (iDepth <= 0) {
        thisNode->i = 0;
        thisNode->j = 0;
        thisNode->left = NULL;
        thisNode->right = NULL;
        return;
    } else {
        iDepth--;

        thisNode->left = (Node*)gc_allocate(sizeof(Node));
        thisNode->right = (Node*)gc_allocate(sizeof(Node));

        thisNode->i = iDepth;
        thisNode->j = 0;

        gc_write_barrier(thisNode);

        gc_push_root(thisNode->left);
        Populate(iDepth, thisNode->left);
        gc_pop_roots(1);

        gc_push_root(thisNode->right);
        Populate(iDepth, thisNode->right);
        gc_pop_roots(1);
    }
}

static Node* MakeTree(int iDepth) {
    Node* result;
    Node* left;
    Node* right;

    if (iDepth <= 0) {
        result = (Node*)gc_allocate(sizeof(Node));
        result->left = NULL;
        result->right = NULL;
        result->i = 0;
        result->j = 0;
        return result;
    } else {
        result = (Node*)gc_allocate(sizeof(Node));
        if (!result) {
            return result;
        }
        result->left = NULL;
        result->right = NULL;
        result->i = iDepth;
        result->j = 0;

        gc_push_root(result);

        left = MakeTree(iDepth - 1);
        result->left = left;
        gc_write_barrier(result);

        right = MakeTree(iDepth - 1);
        result->right = right;
        gc_write_barrier(result);

        gc_pop_roots(1);

        return result;
    }
}

static void PrintDiagnostics() {
    printf(" Total memory allocated: %u bytes\n", memory_get_allocd_sz());
    printf(" Free memory: %u bytes\n", memory_get_free_sz());
#ifdef TIME
    printf(" GC Statistics:\n");
    printf("  - Total GC calls: %zu\n", gc_meta.gc_calls);
    // printf("  - Total incremental calls: %zu\n", gc_meta.inc_calls);
    printf("  - Total GC time: %.2f ms\n", gc_meta.gc_time * 1000);
    printf(
        "  - Avg GC time: %.2f ms\n",
        gc_meta.gc_calls > 0 ? (gc_meta.gc_time * 1000) / gc_meta.gc_calls : 0);
    // printf("  - Total incremental time: %.2f ms\n", gc_meta.inc_time * 1000);
    printf("  - Peak memory before collection: %zu bytes\n",
           gc_meta.peak_before_clean);
    printf("  - Total allocations: %zu\n", gc_meta.tot_allocs);
#endif
}

static void TimeConstruction(int depth) {
    long tStart, tFinish;
    int iNumIters = NumIters(depth);
    Node* tempTree;
    int i;
    size_t initialGcCalls, initialIncCalls;
    double initialGcTime, initialIncTime;

    initialGcCalls = gc_meta.gc_calls;
    initialIncCalls = gc_meta.inc_calls;
    initialGcTime = gc_meta.gc_time;
    initialIncTime = gc_meta.inc_time;

    printf("Creating %d trees of depth %d\n", iNumIters, depth);

    tStart = currentTime();
    for (i = 0; i < iNumIters; ++i) {
        tempTree = (Node*)gc_allocate(sizeof(Node));
        gc_push_root(tempTree);
        Populate(depth, tempTree);
        gc_pop_roots(1);
        // No need to explicitly destroy - GC will handle it
        tempTree = NULL;
    }
    tFinish = currentTime();
    printf("\tTop down construction took %ld msec\n",
           elapsedTime(tFinish - tStart));

    printf("\tTop down construction GC stats:\n");
    printf("\t- GC calls: %zu\n", gc_meta.gc_calls - initialGcCalls);
    // printf("\t- Inc calls: %zu\n", gc_meta.inc_calls - initialIncCalls);
    printf("\t- GC time: %.2f ms\n", (gc_meta.gc_time - initialGcTime) * 1000);
    // printf("\t- Inc time: %.2f ms\n",
    //        (gc_meta.inc_time - initialIncTime) * 1000);

    initialGcCalls = gc_meta.gc_calls;
    initialIncCalls = gc_meta.inc_calls;
    initialGcTime = gc_meta.gc_time;
    initialIncTime = gc_meta.inc_time;

    tStart = currentTime();
    for (i = 0; i < iNumIters; ++i) {
        tempTree = MakeTree(depth);

        tempTree = NULL;
    }
    tFinish = currentTime();
    printf("\tBottom up construction took %ld msec\n",
           elapsedTime(tFinish - tStart));

#ifdef TIME
    printf("\tBottom up construction GC stats:\n");
    printf("\t- GC calls: %zu\n", gc_meta.gc_calls - initialGcCalls);
    // printf("\t- Inc calls: %zu\n", gc_meta.inc_calls - initialIncCalls);
    printf("\t- GC time: %.2f ms\n", (gc_meta.gc_time - initialGcTime) * 1000);
    // printf("\t- Inc time: %.2f ms\n",
    //    (gc_meta.inc_time - initialIncTime) * 1000);
#endif
}

int main() {
    Node* longLivedTree;
    Node* tempTree;
    long tStart, tFinish;
    long tElapsed;
    int i, d;
    double* array;

    gc_init();

    printf("Garbage Collector Test\n");
    // printf(" Live storage will peak at %lu bytes.\n\n",
    //        2 * sizeof(Node) * TreeSize(kLongLivedTreeDepth) +
    //            sizeof(double) * kArraySize);
    // printf(" Stretching memory with a binary tree of depth %d\n",
    //        kStretchTreeDepth);
    // PrintDiagnostics();

    tStart = currentTime();

    tempTree = MakeTree(kStretchTreeDepth);
    tempTree = NULL;

    gc_collect(true);

    printf(" Creating a long-lived binary tree of depth %d\n",
           kLongLivedTreeDepth);

    longLivedTree = (Node*)gc_allocate(sizeof(Node));
    gc_push_root(longLivedTree);
    Populate(kLongLivedTreeDepth, longLivedTree);

    printf(" Creating a long-lived array of %d doubles\n", kArraySize);

    array = gc_allocate(sizeof(double) * kArraySize);
    gc_push_root(array);

    for (i = 1; i < kArraySize / 2; ++i) {
        array[i] = 1.0 / i;
    }
    array[0] = 0.0;
    PrintDiagnostics();

    for (d = kMinTreeDepth; d <= kMaxTreeDepth; d += 2) {
        TimeConstruction(d);
    }

    if (longLivedTree == NULL || array[1000] != 1.0 / 1000)
        fprintf(stderr, "Failed\n");

    tFinish = currentTime();
    tElapsed = elapsedTime(tFinish - tStart);
    PrintDiagnostics();
    printf("Completed in %ld msec\n", tElapsed);

    printf("Memory allocated: %u bytes\n", memory_get_allocd_sz());
    printf("Memory free: %u bytes\n", memory_get_free_sz());

#ifdef TIME
    printf("\nDetailed GC Performance Metrics:\n");
    printf("================================\n");
    printf("Total collection calls:        %zu\n", gc_meta.gc_calls);
    printf("Total incremental calls:       %zu\n", gc_meta.inc_calls);
    printf("Total GC time:                 %.4f sec\n", gc_meta.gc_time);
    printf("Min GC time:                   %.4f sec\n", gc_meta.gc_time_min);
    printf("Max GC time:                   %.4f sec\n", gc_meta.gc_time_max);
    // printf("Total incremental time:        %.4f sec\n", gc_meta.inc_time);
    // printf("Min incremental time:          %.4f sec\n",
    // gc_meta.inc_time_min); printf("Max incremental time:          %.4f
    // sec\n", gc_meta.inc_time_max);
    printf("Peak memory before cleaning:   %zu bytes\n",
           gc_meta.peak_before_clean);
    printf("Total allocations:             %zu\n", gc_meta.tot_allocs);
    printf("Allocation/collection ratio:   %.2f\n",
           gc_meta.gc_calls > 0 ? (double)gc_meta.tot_allocs / gc_meta.gc_calls
                                : 0);
    printf("Avg bytes per allocation:      %.2f\n",
           gc_meta.tot_allocs > 0
               ? (double)memory_get_allocd_sz() / gc_meta.tot_allocs
               : 0);
#endif

    gc_pop_roots(2);

    gc_collect(true);

    gc_destroy();

    return 0;
}