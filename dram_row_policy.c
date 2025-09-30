// dram_row_policy.c
// Compile: gcc -O2 -march=native -o dram_row_policy dram_row_policy.c

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/mman.h>

#define ROW_SIZE (8 * 1024)  // Typical DRAM row size: 8KB
#define TEST_ITERATIONS 100000

// Flush one cache line
static inline void clflush_line(void* p) {
    asm volatile("clflush (%0)" :: "r"(p));
}

// Serialized rdtsc
static inline uint64_t rdtsc_start(void) {
    unsigned hi, lo;
    asm volatile(
        "cpuid\n\t"
        "rdtsc\n\t"
        : "=a"(lo), "=d"(hi)
        : "a"(0)
        : "rbx", "rcx");
    return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t rdtsc_end(void) {
    unsigned hi, lo;
    asm volatile(
        "rdtscp\n\t"
        "mov %%eax, %0\n\t"
        "mov %%edx, %1\n\t"
        "cpuid\n\t"
        : "=r"(lo), "=r"(hi)
        :
        : "rax", "rbx", "rcx", "rdx");
    return ((uint64_t)hi << 32) | lo;
}

// Flush entire buffer
void flush_buffer(void* buf, size_t size) {
    const size_t line = 64;
    volatile char* p = (volatile char*)buf;
    for (size_t off = 0; off < size; off += line) {
        clflush_line((void*)(p + off));
    }
    asm volatile("mfence" ::: "memory");
}

int main() {
    printf("Testing DRAM Row Buffer Policy (Row Size: %d bytes)\n", ROW_SIZE);

    // Allocate memory for two rows
    void* row1, * row2;
    if (posix_memalign(&row1, 64, ROW_SIZE) != 0 ||
        posix_memalign(&row2, 64, ROW_SIZE) != 0) {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }

    // Initialize memory
    memset(row1, 0x5A, ROW_SIZE);
    memset(row2, 0xA5, ROW_SIZE);

    // Lock memory to reduce jitter
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        fprintf(stderr, "Warning: mlockall failed\n");
    }

    uint64_t first_access[TEST_ITERATIONS];
    uint64_t second_access[TEST_ITERATIONS];
    uint64_t different_row_access[TEST_ITERATIONS];

    printf("Performing %d test iterations...\n", TEST_ITERATIONS);

    for (int i = 0; i < TEST_ITERATIONS; i++) {
        // Test 1: Two consecutive accesses to SAME row
        flush_buffer(row1, ROW_SIZE);  // Ensure not in cache

        // First access to row1
        uint64_t start1 = rdtsc_start();
        volatile uint64_t* data = (volatile uint64_t*)row1;
        uint64_t value = *data;  // Read operation
        asm volatile("" : "+r" (value));  // Prevent optimization
        uint64_t end1 = rdtsc_end();

        // Second access to same row1 (without flushing in between)
        uint64_t start2 = rdtsc_start();
        value = *data;
        asm volatile("" : "+r" (value));
        uint64_t end2 = rdtsc_end();

        first_access[i] = end1 - start1;
        second_access[i] = end2 - start2;

        // Test 2: Access to different row (for comparison)
        flush_buffer(row2, ROW_SIZE);
        uint64_t start3 = rdtsc_start();
        volatile uint64_t* data2 = (volatile uint64_t*)row2;
        value = *data2;
        asm volatile("" : "+r" (value));
        uint64_t end3 = rdtsc_end();

        different_row_access[i] = end3 - start3;
    }

    // Calculate statistics
    long double sum_first = 0, sum_second = 0, sum_diff = 0;
    for (int i = 0; i < TEST_ITERATIONS; i++) {
        sum_first += first_access[i];
        sum_second += second_access[i];
        sum_diff += different_row_access[i];
    }

    long double mean_first = sum_first / TEST_ITERATIONS;
    long double mean_second = sum_second / TEST_ITERATIONS;
    long double mean_diff = sum_diff / TEST_ITERATIONS;

    printf("\n=== RESULTS ===\n");
    printf("First access to row:  %.2Lf cycles\n", mean_first);
    printf("Second access to same row: %.2Lf cycles\n", mean_second);
    printf("Access to different row:   %.2Lf cycles\n", mean_diff);
    printf("Speedup ratio (first/second): %.2Lfx\n", mean_first / mean_second);

    // Determine row buffer policy
    double speedup_ratio = (double)(mean_first / mean_second);
    printf("\n=== CONCLUSION ===\n");

    if (speedup_ratio > 1.5) {
        printf("DRAM uses OPEN-ROW policy\n");
        printf("Second access is %.2fx faster - row buffer was kept open\n", speedup_ratio);
    }
    else {
        printf("DRAM uses CLOSED-ROW policy\n");
        printf("Second access shows minimal speedup (%.2fx)\n", speedup_ratio);
    }

    free(row1);
    free(row2);
    return 0;
}