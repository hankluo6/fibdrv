#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>      // Needed for mlockall()
#include <sys/resource.h>  // needed for getrusage
#include <sys/time.h>      // needed for getrusage
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define FIB_DEV "/dev/fibonacci"
#define FIB_SYS "/sys/kernel/fib_time/"

/* Copy from https://stackoverflow.com/a/11660651 */

typedef unsigned __int128 uint128_t;

#define P10_UINT64 10000000000000000000ULL /* 19 zeroes */
#define E10_UINT64 19

#define PRE_ALLOCATION_SIZE (40 * 1024 * 1024)

#define STRINGIZER(x) #x
#define TO_STRING(x) STRINGIZER(x)

static int print_u128_u(uint128_t u128)
{
    int rc;
    if (u128 > UINT64_MAX) {
        uint128_t leading = u128 / P10_UINT64;
        uint64_t trailing = u128 % P10_UINT64;
        rc = print_u128_u(leading);
        rc += printf("%." TO_STRING(E10_UINT64) PRIu64, trailing);
    } else {
        uint64_t u64 = u128;
        rc = printf("%" PRIu64, u64);
    }
    return rc;
}

/* Calculate difference of time */
static time_t diff_in_ns(struct timespec t1, struct timespec t2)
{
    struct timespec diff;
    if (t2.tv_nsec - t1.tv_nsec < 0) {
        diff.tv_sec = t2.tv_sec - t1.tv_sec - 1;
        diff.tv_nsec = t2.tv_nsec - t1.tv_nsec + 1000000000;
    } else {
        diff.tv_sec = t2.tv_sec - t1.tv_sec;
        diff.tv_nsec = t2.tv_nsec - t1.tv_nsec;
    }
    return (diff.tv_sec * 1000000000.0 + diff.tv_nsec);
}

/* fib == 1 return calculated fibonacci time in kernel
 * else return copy_to_user time in kernel
 */
static long int get_ktime(int fib)
{
    int kfd;
    if (fib == 1)
        kfd = open(FIB_SYS "fib_kt_ns", O_RDWR);
    else
        kfd = open(FIB_SYS "copy_kt_ns", O_RDWR);
    if (kfd < 0) {
        perror("Failed to open sys kobject");
        exit(1);
    }
    char buf[64];
    read(kfd, buf, 64);
    close(kfd);
    return atol(buf);
}

static void configure_malloc_behavior(void)
{
    /* Now lock all current and future pages
    from preventing of being paged */
    if (mlockall(MCL_CURRENT | MCL_FUTURE))
        perror("mlockall failed:");

    /* Turn off malloc trimming.*/
    mallopt(M_TRIM_THRESHOLD, -1);

    /* Turn off mmap usage. */
    mallopt(M_MMAP_MAX, 0);
}

static void reserve_process_memory(int size)
{
    int i;
    char *buffer;

    buffer = malloc(size);

    /* Touch each page in this piece of memory to get it mapped into RAM */
    for (i = 0; i < size; i += sysconf(_SC_PAGESIZE)) {
        /* Each write to this buffer will generate a pagefault.
            Once the pagefault is handled a page will be locked in
            memory and never given back to the system. */
        buffer[i] = 0;
    }

    /* buffer will now be released. As Glibc is configured such that it
        never gives back memory to the kernel, the memory allocated above is
        locked for this process. All malloc() and new() calls come from
        the memory pool reserved and locked above. Issuing free() and
        delete() does NOT make this locking undone. So, with this locking
        mechanism we can build C++ applications that will never run into
        a major/minor pagefault, even with swapping enabled. */
    free(buffer);
}

#ifdef DEBUG
void show_new_pagefault_count(const char *logtext,
                              const char *allowed_maj,
                              const char *allowed_min)
{
    static int last_majflt = 0, last_minflt = 0;
    struct rusage usage;

    getrusage(RUSAGE_SELF, &usage);

    printf(
        "%-30.30s: Pagefaults, Major:%ld (Allowed %s), "
        "Minor:%ld (Allowed %s)\n",
        logtext, usage.ru_majflt - last_majflt, allowed_maj,
        usage.ru_minflt - last_minflt, allowed_min);

    last_majflt = usage.ru_majflt;
    last_minflt = usage.ru_minflt;
}
#endif

int main()
{
    configure_malloc_behavior();
    reserve_process_memory(PRE_ALLOCATION_SIZE);
    long long sz;

    struct timespec t1, t2;

    uint128_t val;
    char write_buf[] = "testing writing";
    int offset = 100; /* TODO: try test something bigger than the limit */


    int fd = open(FIB_DEV, O_RDWR);
    if (fd < 0) {
        perror("Failed to open character device");
        exit(1);
    }

    for (int i = 0; i <= offset; i++) {
        sz = write(fd, write_buf, strlen(write_buf));
        printf("Writing to " FIB_DEV ", returned the sequence %lld\n", sz);
    }

    /* add these to prevent page faults or cache misses */
    clock_gettime(CLOCK_MONOTONIC, &t1);
    read(fd, &val, sizeof(val));

#ifdef DEBUG
    show_new_pagefault_count("Initial count", ">=0", ">=0");
#endif
    for (int i = 0; i <= offset; i++) {
        lseek(fd, i, SEEK_SET);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        sz = read(fd, &val, sizeof(val));
        clock_gettime(CLOCK_MONOTONIC, &t2);
        long int time = get_ktime(1) + get_ktime(0);
        printf("%d ", i);
        printf("%ld ", diff_in_ns(t1, t2));
        printf("%ld ", time);
        printf("%ld\n", diff_in_ns(t1, t2) - time);

        printf("Reading from " FIB_DEV " at offset %d, returned the sequence ",
               i);
        print_u128_u(val);
        printf(".\n");
    }
    for (int i = offset; i >= 0; i--) {
        lseek(fd, i, SEEK_SET);
        sz = read(fd, &val, sizeof(val));
        printf("Reading from " FIB_DEV " at offset %d, returned the sequence ",
               i);
        print_u128_u(val);
        printf(".\n");
    }
#ifdef DEBUG
    show_new_pagefault_count("Use generated", "=0", "=1");
#endif
    close(fd);
    return 0;
}
