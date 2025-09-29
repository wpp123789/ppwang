// memtest_allsizes.c
// Compile: gcc -O2 -march=native -o memtest_allsizes memtest_allsizes.c
// Runs memcpy timing across multiple sizes and writes CSVs.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/mman.h>
#include <errno.h>
#include <time.h>

// Flush one cache line containing p
static inline void clflush_line(void *p) {
	asm volatile("clflush (%0)" :: "r"(p));
}

// Serialize + rdtsc
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

// rdtscp + serialize
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

// flush entire buffer by 64B steps
void flush_buffer(void *buf, size_t size) {
	const size_t line = 64;
	volatile char *p = (volatile char *)buf;
	for (size_t off = 0; off < size; off += line) {
		clflush_line((void *)(p + off));
	}
	// ensure flush visible
	asm volatile("mfence" ::: "memory");
}

int cmp_uint64(const void *a, const void *b) {
	uint64_t va = *(const uint64_t *)a;
	uint64_t vb = *(const uint64_t *)b;
	if (va < vb)
		return -1;
	if (va > vb)
		return 1;
	return 0;
}

int main(int argc, char **argv) {
	// Sizes to test: 2^6 .. 2^16, 2^20, 2^21

	int exponents[] = {6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 20, 21};
	int n_sizes = sizeof(exponents) / sizeof(exponents[0]);

	// Optional: permit override via command line to test subset or single exponent
	int start_idx = 0, end_idx = n_sizes - 1;
	if (argc >= 2) {
		// parse first arg as comma-separated exponents or single value like "6-16"
		// simple handling: if one integer given, test only that exponent.
		int x = atoi(argv[1]);
		if (x != 0) {
			// find index
			int found = -1;
			for (int i = 0; i < n_sizes; i++)
				if (exponents[i] == x) {
					found = i;
					break;
				}
			if (found >= 0) {
				start_idx = end_idx = found;
			}
		}
	}

	// Lock memory to reduce paging jitter
	if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
		fprintf(stderr, "warning: mlockall failed: %s\n", strerror(errno));
	} else {
		printf("mlockall OK\n");
	}

	// For each size, choose REPEAT adaptively (avoid too many loops for large copies)
	for (int si = start_idx; si <= end_idx; ++si) {
		int x = exponents[si];
		size_t size = (size_t)1 << x;
		size_t REPEAT;

		if (size <= 4096)
			REPEAT = 200000;          // many trials for small copies
		else if (size <= 65536)
			REPEAT = 50000;
		else if (size <= 262144)
			REPEAT = 20000;   // 256KB
		else if (size <= 1048576)
			REPEAT = 8000;   // 1MB
		else
			REPEAT = 2000;                        // 2MB etc.

		printf("=== Testing 2^%d = %zu B, REPEAT=%zu ===\n", x, size, REPEAT);

		// allocate aligned buffers
		void *buf = NULL, *bufcopy = NULL;
		if (posix_memalign(&buf, 64, size) != 0) {
			fprintf(stderr, "posix_memalign buf failed\n");
			exit(1);
		}
		if (posix_memalign(&bufcopy, 64, size) != 0) {
			fprintf(stderr, "posix_memalign bufcopy failed\n");
			exit(1);
		}
		// init
		memset(buf, 0x5A, size);
		memset(bufcopy, 0xA5, size);

		// allocate results array
		uint64_t *results = malloc(sizeof(uint64_t) * REPEAT);
		if (!results) {
			fprintf(stderr, "malloc results fail\n");
			exit(1);
		}

		// Warm-up: do a few copies to avoid cold-start anomalies
		for (int w = 0; w < 5; ++w) {
			memcpy(bufcopy, buf, size);
		}
		// flush so next reads go to DRAM
		flush_buffer(buf, size);
		flush_buffer(bufcopy, size);

		// main measurement loop
		for (size_t rep = 0; rep < REPEAT; ++rep) {
			// flush both buffers to force DRAM accesses (and not use cached data)
			flush_buffer(buf, size);
			flush_buffer(bufcopy, size);

			uint64_t t0 = rdtsc_start();
			memcpy(bufcopy, buf, size);
			uint64_t t1 = rdtsc_end();

			results[rep] = (t1 - t0);
			// minimal disturbance between iterations
		}

		// Write CSV file
		char fname[256];
		snprintf(fname, sizeof(fname), "memcpy_2pow%d_%zub.csv", x, size);
		FILE *f = fopen(fname, "w");
		if (!f) {
			fprintf(stderr, "failed to open %s for writing\n", fname);
			exit(1);
		}
		fprintf(f, "rep,cycles\n");
		for (size_t i = 0; i < REPEAT; i++) {
			fprintf(f, "%zu,%" PRIu64 "\n", i, results[i]);
		}
		fclose(f);
		printf("Wrote per-trial CSV: %s\n", fname);

		// compute stats
		// copy results for sorting
		uint64_t *sorted = malloc(sizeof(uint64_t) * REPEAT);
		if (!sorted) {
			fprintf(stderr, "malloc sorted fail\n");
			exit(1);
		}
		memcpy(sorted, results, sizeof(uint64_t) * REPEAT);
		qsort(sorted, REPEAT, sizeof(uint64_t), cmp_uint64);

		// mean, std, min, max, median
		long double sum = 0.0L;
		for (size_t i = 0; i < REPEAT; i++)
			sum += (long double)results[i];
		long double mean = sum / (long double)REPEAT;
		long double ssum = 0.0L;
		for (size_t i = 0; i < REPEAT; i++) {
			long double d = (long double)results[i] - mean;
			ssum += d * d;
		}
		long double stddev = sqrt(ssum / (long double)REPEAT);
		uint64_t minv = sorted[0];
		uint64_t maxv = sorted[REPEAT - 1];
		uint64_t median = (REPEAT % 2 == 0) ? sorted[REPEAT / 2] : sorted[REPEAT / 2];

		printf("size=%zu B: mean=%.2Lf cycles, median=%" PRIu64 ", std=%.2Lf, min=%" PRIu64 ", max=%" PRIu64 "\n",
		       size, mean, median, stddev, minv, maxv);

		free(sorted);
		free(results);
		free(buf);
		free(bufcopy);
	}

	return 0;
}
