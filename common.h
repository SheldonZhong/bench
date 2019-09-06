#include <stdatomic.h>

struct rw_params {
    int fd;
    void *buf;

    int read_percent;
    __time_t duration;
    __uint64_t block_size;
    __uint64_t io_size;
    __uint64_t size;

    __uint64_t seed;

    struct timespec *start;
    struct timespec *stop;

    atomic_uint_fast64_t read_io;
    atomic_uint_fast64_t write_io;
};

static __thread __uint64_t __rs_u64 = 88172645463325252lu;

static inline __uint64_t xorshift(__uint64_t s) {
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    return s;
}

static inline __uint64_t random_u64(void) {
    __rs_u64 = xorshift(__rs_u64);
    return __rs_u64 * 2685821657736338717lu;
}

static inline __uint64_t srandom_u64(const __uint64_t seed) {
    __rs_u64 = seed ? seed : 88172645463325252lu;
    return random_u64();
}

// compares two timespec struct
// ret > 0 if t1 greater than t2, t1 is behind t2
// ret == 0 if t1 equals to t2, t1 is exactly same as t2
// ref < 0 if t1 smaller than t2, t1 is ahead t2
static inline int8_t timespec_cmp(struct timespec *t1, struct timespec *t2) {
    if (t1->tv_sec > t2->tv_sec) {
        return 1;
    }
    if (t1->tv_sec < t2->tv_sec) {
        return -1;
    }
    if (t1->tv_nsec > t2->tv_nsec) {
        return 2;
    }
    if (t1->tv_nsec < t2->tv_nsec) {
        return -2;
    }
    return 0;
}