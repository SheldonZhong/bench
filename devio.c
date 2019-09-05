#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>
#include "common.h"

#define GRANUNITY 100

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

void *rw_worker(void *args) {
    struct rw_params *param = (struct rw_params*)args;
    __uint64_t offset;
    int ret;
    struct timespec now;
    struct timespec stop;
    __uint64_t reads = 0;
    __uint64_t writes = 0;
    stop.tv_sec = param->start->tv_sec + param->duration;
    stop.tv_nsec = param->start->tv_nsec;
    const __uint64_t mask = ~(param->block_size - 1);
    srandom_u64(pthread_self());

    while(1) {
        for (int i = 0; i < GRANUNITY; i++) {
            offset = (random_u64() % param->size) & mask;
            if (param->read_percent > (random_u64() % 100)) {
                ret = pread(param->fd, param->buf, param->block_size, offset);
                if (ret <= 0) {
                    fprintf(stderr, "read error %d\n", ret);
                    exit(1);
                }
                reads++;
            } else {
                ret = pwrite(param->fd, param->buf, param->block_size, offset);
                if (ret <= 0) {
                    fprintf(stderr, "write error %d\n", ret);
                    exit(1);
                }
                writes++;
            }
        }

        clock_gettime(CLOCK_MONOTONIC, &now);
        if (timespec_cmp(&now, &stop) >= 0) {
            break;
        }
    }

    atomic_fetch_add(&param->read_io, reads);
    atomic_fetch_add(&param->write_io, writes);

    param->stop->tv_sec = now.tv_sec;
    param->stop->tv_nsec = now.tv_nsec;

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 6) {
        fprintf(stderr, "%s <dev> <nthreads> <read_percent> <blocksize_shift> <duration> ... (optional) <stonewall>\n", argv[0]);
        exit(1);
    }

    const int fd = open(argv[1], O_RDWR | __O_DIRECT);
    if (fd < 0) {
        fprintf(stderr, "open %s error %d\n", argv[1], fd);
        exit(1);
    }

    const int nthreads = atoi(argv[2]);
    const int read_percent = atoi(argv[3]);
    const int block_size = 1 << atoi(argv[4]);
    const int duration = atoi(argv[5]);
    const off_t size = lseek(fd, 0, SEEK_END);
    struct timespec now;
    struct timespec stop;
    pthread_t thread_id[nthreads];

    struct rw_params job_args = {
        .fd = fd,
        .buf = aligned_alloc(4096, block_size),
        
        .read_percent = read_percent,
        .duration = duration,
        .block_size = block_size,
        .size = size,

        .start = &now,
        .stop = &stop,
    };
    clock_gettime(CLOCK_MONOTONIC, &now);

    for (int i = 0; i < nthreads; i++) {
        pthread_create(&thread_id[i], NULL, rw_worker, &job_args);
    }

    for (int i = 0; i < nthreads; i++) {
        pthread_join(thread_id[i], NULL);
    }

    double dt = (double)(job_args.stop->tv_sec - job_args.start->tv_sec) +
                (double)(job_args.stop->tv_nsec -  job_args.start->tv_nsec) / 1000000000.0;
    if (job_args.read_io > 0) {
        printf("READ:\n\tread_io: %d\n\ttime: %.3f seconds\n", job_args.read_io, dt);
        double iops = (double)job_args.read_io / dt;
        printf("\tIOPS: %.3f\n\tbandwidth: %.3f MiB/s\n", iops, iops * (double)block_size / (1024.0 * 1024.0));
    }

    if (job_args.write_io> 0) {
        printf("WRITE:\n\twrite_io: %d\n\ttime: %.3f seconds\n", job_args.write_io, dt);
        double iops = (double)job_args.write_io / dt;
        printf("\tIOPS: %.3f\n\tbandwidth: %.3f MiB/s\n", iops, iops * (double)block_size / (1024.0 * 1024.0));
    }
}
