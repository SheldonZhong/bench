#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#include "liburing.h"
#include "common.h"

#define CAP 64
#define GRANUNITY 100

#define URING_READ 0
#define URING_WRITE 1

static uint64_t inflight = 0;
static uint64_t ring_size = 0;
static uint64_t peek = 0;

static inline int wait(struct io_uring *ring, uint64_t cap, uint64_t bs) {
    struct io_uring_cqe *cqe;
    int ret;

    while (inflight > cap) {
        if (peek) {
            while((ret = io_uring_peek_cqe(ring, &cqe)) == -EAGAIN);
        } else {
            ret = io_uring_wait_cqe(ring, &cqe);
        }
        if (ret < 0) {
            fprintf(stderr, "io_uring_wait_cqe: %s\n", strerror(-ret));
            return -1;
        }

        if (cqe->res < 0 || cqe->res != (int64_t)bs) {
			fprintf(stderr, "ret=%s, wanted %ld\n", strerror(-cqe->res), bs);
            return cqe->res;
		}

        io_uring_cqe_seen(ring, cqe);
        inflight--;
    }
    return 0;
}

static inline int uring_rw(unsigned op, struct io_uring *ring, int fd, void *buf, uint64_t bs, uint64_t off, unsigned flags) {
    struct io_uring_sqe *sqe;
    int ret;

    sqe = io_uring_get_sqe(ring);

    switch (op)
    {
    case 0:
        io_uring_prep_read_fixed(sqe, fd, buf, bs, off, 0);
        inflight++;
        break;
    case 1:
        io_uring_prep_write_fixed(sqe, fd, buf, bs, off, 0);
        break;
    default:
        fprintf(stderr, "invalid op code");
        return -1;
    }
    sqe->flags = flags;
    ret = io_uring_submit(ring);
    if (ret < 0) {
        fprintf(stderr, "io_uring_submit: %s\n", strerror(-ret));
        return ret;
    }

    ret = wait(ring, ring_size, bs);
    if (ret < 0) {
        fprintf(stderr, "iouring_wait: %s\n", strerror(-ret));
        return ret;
    }

    return ret;
}

static inline int uring_read(struct io_uring *ring, int fd, void *buf, uint64_t bs, uint64_t off, unsigned flags) {
    return uring_rw(URING_READ, ring, fd, buf, bs, off, flags);
}

static inline int uring_write(struct io_uring *ring, int fd, void *buf, uint64_t bs, uint64_t off, unsigned flags) {
    return uring_rw(URING_WRITE, ring, fd, buf, bs, off, flags);
}

void *rw_worker(struct io_uring *ring, void *args, unsigned sqe_flags) {
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
            if (param->read_percent > ((int)random_u64() % 100)) {
                ret = uring_read(ring, param->fd, param->buf, param->block_size, offset, sqe_flags);
                if (ret < 0) {
                    fprintf(stderr, "read error %d\n", ret);
                    exit(1);
                }
                reads++;
            } else {
                ret = uring_write(ring, param->fd, param->buf, param->block_size, offset, sqe_flags);
                if (ret < 0) {
                    fprintf(stderr, "read error %d\n", ret);
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

static inline void get_setup_flags(unsigned *flags, int input) {
    switch (input)
    {
    case 0:
        printf("\t0\tno flags (default)\n");
        break;
    case 1:
        printf("1\tIORING_SETUP_IOPOLL (1 << 0)\n");
        *flags = IORING_SETUP_IOPOLL;
        break;
    case 2:
        printf("2\tIORING_SETUP_SQPOLL (1 << 1)\n");
        *flags = IORING_SETUP_SQPOLL;
        break;
    case 3:
        printf("3\tIORING_SETUP_SQPOLL | IORING_SETUP_IOPOLL\nn");
        *flags = IORING_SETUP_SQPOLL | IORING_SETUP_IOPOLL;
        break;
    case 6:
        printf("6\tIORING_SETUP_SQ_AFF | IORING_SETUP_SQPOLL\n");
        *flags = IORING_SETUP_SQ_AFF | IORING_SETUP_SQPOLL;
        break;
    case 7:
        printf("6\tIORING_SETUP_SQ_AFF | IORING_SETUP_SQPOLL | IORING_SETUP_IOPOLL\n");
        *flags = IORING_SETUP_SQ_AFF | IORING_SETUP_SQPOLL | IORING_SETUP_IOPOLL;
        break;
    default:
        printf("io_uring_setup flags\n");
        printf("\t0\tno flags (default)\n");
        printf("1\tIORING_SETUP_IOPOLL (1 << 0)\n");
        printf("2\tIORING_SETUP_SQPOLL (1 << 1)\n");
        printf("3\tIORING_SETUP_SQPOLL | IORING_SETUP_IOPOLL\n");
        printf("6\tIORING_SETUP_SQ_AFF | IORING_SETUP_SQPOLL\n");
        printf("7\tIORING_SETUP_SQ_AFF | IORING_SETUP_SQPOLL | IORING_SETUP_IOPOLL\n");
        exit(1);
        break;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "%s <dev> <read_percent> <blocksize_shift> <duration> ... (optional) <ring_size> <flags> <fixed_file> <peek>\n", argv[0]);
        exit(1);
    }

    const int fd = open(argv[1], O_RDWR | __O_DIRECT);
    if (fd < 0) {
        fprintf(stderr, "open %s error %d\n", argv[1], fd);
        exit(1);
    }

    const int read_percent = atoi(argv[2]);
    const int block_size = 1 << atoi(argv[3]);
    const int duration = atoi(argv[4]);
    if (argc > 5) {
        printf("submission queue ring size %d\n", atoi(argv[5]));
        ring_size = atoi(argv[5]);
    }
    else {
        printf("Size of io submission queue ring, must be power of 2 from [1, 4096].\n");
        printf("\tDefault %d\n", CAP);
        ring_size = CAP;
    }
    const off_t size = lseek(fd, 0, SEEK_END);
    int ret;
    struct timespec now;
    struct timespec stop;

    
    unsigned flags = 0;
    struct io_uring ring;
    if (argc > 6) {
        get_setup_flags(&flags, atoi(argv[6]));
    } else {
        printf("io_uring_setup flags\n");
        printf("\t0\tno flags (default)\n");
    }


    {
        struct io_uring_params p = {
            .flags = flags,
            .sq_thread_cpu = 0,
        };
        // io_uring_queue_init
        int fd__ = io_uring_setup(ring_size, &p);
        if (fd__ < 0)
            exit(1);

        ret = io_uring_queue_mmap(fd__, &p, &ring);
        if (ret)
            close(fd__);
        if (ret < 0) {
            fprintf(stderr, "io_uring_queue_init: %s\n", strerror(-ret));
            exit(1);
        }
    }

    unsigned sqe_flags = 0;
    int uring_fd = fd;
    if (argc > 7) {
        if (atoi(argv[7]) > 0) {
            io_uring_register_files(&ring, &fd, 1);
            sqe_flags = IOSQE_FIXED_FILE;
            uring_fd = 0;
            printf("IOSQE_FIXED_FILE, using fd %d\n", uring_fd);
        } else {
            printf("using fd %d\n", uring_fd);
        }
    }

    if (argc > 8) {
        if (atoi(argv[8]) > 0) {
            peek = 1;
            printf("io_uring_peek_cqe\n");
        } else {
        }
    }

    // legal check
    if ((flags & IORING_SETUP_SQPOLL)) {
        if(!(sqe_flags & IOSQE_FIXED_FILE)) {
            fprintf(stderr, "IORING_SETUP_SQPOLL is not compatible with not fixed file, must be fixed\n");
            exit(1);
        }
        if(!peek) {
            fprintf(stderr, "IORING_SETUP_SQPOLL is not compatible with wait, must be peek\n");
            exit(1);
        }
    } else if ((flags & IORING_SETUP_IOPOLL) && peek) {
        fprintf(stderr, "IORING_SETUP_IOPOLL is not compatible with peek, must be wait\n");
        exit(1);
    }
    struct rw_params job_args = {
        .fd = uring_fd,
        .buf = aligned_alloc(4096, block_size),
        
        .read_percent = read_percent,
        .duration = duration,
        .block_size = block_size,
        .size = size,

        .start = &now,
        .stop = &stop,
    };
    struct iovec iovecs = {
        .iov_base = job_args.buf,
        .iov_len = block_size,
    };

    io_uring_register_buffers(&ring, &iovecs, 1);

    clock_gettime(CLOCK_MONOTONIC, &now);

    rw_worker(&ring, &job_args, sqe_flags);
    wait(&ring, uring_fd, block_size);

    clock_gettime(CLOCK_MONOTONIC, &stop);

    io_uring_queue_exit(&ring);
    if (argc > 7)
        io_uring_unregister_files(&ring);
    io_uring_unregister_buffers(&ring);

    io_report(&job_args);

    return 0;
}
