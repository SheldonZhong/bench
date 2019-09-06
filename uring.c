#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "liburing.h"
#include "common.h"

#define CAP 2048
#define GRANUNITY 100

static uint64_t inflight = 0;

static inline int uring_write(struct io_uring *ring, int fd, void *buf, uint64_t bs, uint64_t off) {
    struct io_uring_cqe *cqe;
    struct io_uring_sqe *sqe;
    int ret;

    sqe = io_uring_get_sqe(ring);
    io_uring_prep_write_fixed(sqe, fd, buf, bs, off, 0);
    inflight++;
    ret = io_uring_submit(ring);
    if (ret < 0) {
        fprintf(stderr, "io_uring_submit: %s\n", strerror(-ret));
    }

    while (inflight >= CAP) {

        ret = io_uring_wait_cqe(ring, &cqe);
        if (ret < 0) {
            fprintf(stderr, "io_uring_wait_cqe: %s\n", strerror(-ret));
            return 1;
        }
        io_uring_cqe_get_data(cqe);
        io_uring_cqe_seen(ring, cqe);
        inflight--;
    }

    return 0;
}

static inline int uring_read(struct io_uring *ring, int fd, void *buf, uint64_t bs, uint64_t off) {
    struct io_uring_cqe *cqe;
    struct io_uring_sqe *sqe;
    int ret;

    sqe = io_uring_get_sqe(ring);
    if (!sqe) {
        printf("sqe == 0\n");
    }
    io_uring_prep_read_fixed(sqe, fd, buf, bs, off, 0);
    inflight++;
    ret = io_uring_submit(ring);
    if (ret < 0) {
        fprintf(stderr, "io_uring_submit: %s\n", strerror(-ret));
    }

    while (inflight >= CAP) {
        ret = io_uring_wait_cqe(ring, &cqe);
        if (ret < 0) {
            fprintf(stderr, "io_uring_wait_cqe: %s\n", strerror(-ret));
            return 1;
        }
        io_uring_cqe_get_data(cqe);
        io_uring_cqe_seen(ring, cqe);
        inflight--;
    }

    return 0;
}

static inline int wait(struct io_uring *ring) {
    struct io_uring_cqe *cqe;
    int ret;

    while (inflight > 0) {
        ret = io_uring_wait_cqe(ring, &cqe);
        if (ret < 0) {
            fprintf(stderr, "io_uring_wait_cqe: %s\n", strerror(-ret));
            return 1;
        } 
        io_uring_cqe_get_data(cqe);
        io_uring_cqe_seen(ring, cqe);
        inflight--;
    }
}

void *rw_worker(struct io_uring *ring, void *args) {
    struct rw_params *param = (struct rw_params*)args;
    uint64_t offset;
    int ret;
    struct timespec now;
    struct timespec stop;
    uint64_t reads = 0;
    uint64_t writes = 0;
    stop.tv_sec = param->start->tv_sec + param->duration;
    stop.tv_nsec = param->start->tv_nsec;
    const __uint64_t mask = ~(param->block_size - 1);
    srandom_u64(stop.tv_nsec);

    while(1) {
        for (int i = 0; i < GRANUNITY; i++) {
            offset = (random_u64() % param->size) & mask;
            if (param->read_percent > (random_u64() % 100)) {
                uring_read(ring, param->fd, param->buf, param->block_size, offset);
                reads++;
            } else {
                uring_write(ring, param->fd, param->buf, param->block_size, offset);
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
    int ret;
    struct timespec now;
    struct timespec stop;

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
    struct io_uring ring;
    ret = io_uring_queue_init(CAP, &ring, 0);
    if (ret < 0) {
        fprintf(stderr, "io_uring_queue_init: %s\n", strerror(-ret));
        return -1;
    }

    rw_worker(&ring, &job_args);
    wait(&ring);

    clock_gettime(CLOCK_MONOTONIC, &stop);
    io_uring_queue_exit(&ring);

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

    return 0;
}
