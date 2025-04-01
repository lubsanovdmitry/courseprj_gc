#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../qcgc/gc.h"
#include "../qcgc/memory.h"

enum {
    INCR_IT = 10,
    FULL_IT = 10,
    ALLOC_PER_IT = 10000,
    MIN_ALLOC = 16,
    MAX_ALLOC = 4096,
};

typedef struct {
    double avg_pause;
    double min_pause;
    double max_pause;
    double stddev_pause;

    double avg_f_pause;
    double max_f_pause;
    double min_f_pause;
    double stddev_f_pause;

    size_t tot_allocs;
    double tot_exec_time;
} pause_time_result_t;

double pauses[INCR_IT];
double f_pauses[FULL_IT];

double calc_stddev(double* vals, int cnt, double avg) {
    double sum = 0.0;
    for (int i = 0; i < cnt; ++i) {
        double diff = vals[i] - avg;
        sum += diff * diff;
    }
    return sqrt(sum / cnt);
}

void perform_allocs(size_t n, size_t* alloc_cnt) {
    void** obj = calloc(n, sizeof(*obj));
    for (size_t i = 0; i < n; ++i) {
        size_t size = rand() % (MAX_ALLOC - MIN_ALLOC + 1) + MIN_ALLOC;
        obj[i] = gc_allocate(size);
        if (obj[i]) {
            (*alloc_cnt)++;

            if (rand() % 5 == 0) {
                gc_push_root(obj[i]);
            }
            if (rand() % 5 == 0) {
                gc_write_barrier(obj[i]);
            }
        }
    }
    gc_collect(true);
    free(obj);
}

extern gc_meta_t gc_meta;

pause_time_result_t run_pause_bench() {
    pause_time_result_t res = {0};
    res.min_pause = 1000000.0;
    res.min_f_pause = 1000000.0;
    memset(&gc_meta, 0, sizeof(gc_meta));
    gc_meta.gc_time_min = 10.0;
    gc_meta.inc_time_min = 10.0;

    clock_t start = 0, end = 0;

    start = clock();

    double total_incr = 0.0;
    for (int i = 0; i < INCR_IT; ++i) {
        perform_allocs(ALLOC_PER_IT, &res.tot_allocs);

        clock_t gc_s = clock();
        gc_collect(false);
        clock_t gc_e = clock();

        double pause_time = ((double)(gc_e - gc_s)) / CLOCKS_PER_SEC;
        pauses[i] = pause_time;
        total_incr += pause_time;

        if (pause_time < res.min_pause) {
            res.min_pause = pause_time;
        }
        if (pause_time > res.max_pause) {
            res.max_pause = pause_time;
        }

        if (i % 100 == 0) {
            printf("Iter: %d\n", i);
        }
    }

    res.avg_pause = total_incr / INCR_IT;

    double total_maj = 0.0;
    for (int i = 0; i < 0; ++i) {
        perform_allocs(ALLOC_PER_IT, &res.tot_allocs);

        clock_t gc_s = clock();
        gc_collect(true);
        clock_t gc_e = clock();

        double pause_time = ((double)(gc_e - gc_s)) / CLOCKS_PER_SEC;
        f_pauses[i] = pause_time;
        total_maj += pause_time;

        if (pause_time < res.min_f_pause) {
            res.min_f_pause = pause_time;
        }
        if (pause_time > res.max_f_pause) {
            res.max_f_pause = pause_time;
        }

        if (i % 100 == 0) {
            printf("Iter: %d\n", i);
        }
    }

    res.avg_f_pause = total_maj / FULL_IT;

    end = clock();
    res.stddev_pause = calc_stddev(pauses, INCR_IT, res.avg_pause);

    res.stddev_f_pause = calc_stddev(f_pauses, FULL_IT, res.avg_f_pause);

    res.tot_exec_time = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("Normal pauses:\n");
    printf("Avg: %.6f s\n Min: %.6f s\nMax: %.6f s\n std: %.6f\n",
           res.avg_pause, res.min_pause, res.max_pause, res.stddev_pause);

    printf("\nForced major pauses:\n");
    printf("Avg: %.6f s\n Min: %.6f s\nMax: %.6f s\n std: %.6f\n",
           res.avg_f_pause, res.min_f_pause, res.max_f_pause,
           res.stddev_f_pause);

    printf("Total: %.6f\n", res.tot_exec_time);
    printf("  GC Time: %.6f s\n", gc_meta.gc_time);
    printf("  GC Time max: %.6f s\n", gc_meta.gc_time_max);
    printf("  GC Time min: %.6f s\n", gc_meta.gc_time_min);
    printf("  GC Time avg: %.6f s\n",
           gc_meta.gc_time / (double)gc_meta.gc_calls);
    // printf("  INC Time: %.6f s\n", gc_meta.inc_time);
    // printf("  INC Time max: %.6f s\n", gc_meta.inc_time_max);
    // printf("  INC Time min: %.6f s\n", gc_meta.inc_time_min);
    // printf("  INC Time avg: %.6f s\n",
    //        gc_meta.inc_time /
    //            (double)((gc_meta.inc_calls > 0) ? gc_meta.inc_calls : 1));
    printf("  GC Calls: %zu\n", gc_meta.gc_calls);
    printf("  INC Calls: %zu\n", gc_meta.inc_calls);
    printf("TOT A %zu\n", gc_meta.tot_allocs);
    printf("Memory peak: %zu\n", gc_meta.peak_before_clean);
}

int main() {
    srand(time(NULL));

    printf("Becnhmark of pause time\n\n");

    gc_init();

    printf("Heap size: %d\n", HEAP_SIZE);

    run_pause_bench();
}