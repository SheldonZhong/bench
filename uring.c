#define _GNU_SOURCE
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "liburing.h"

#define CAP 64

int main(int argc, char *argv[]) {
    struct io_uring ring;
    int ret;

    ret = io_uring_queue_init(CAP, &ring, 0);
    if (ret < 0) {
        fprintf(stderr, "io_uring_queue_init: %s\n", strerror(-ret));
        return -1;
    }

    if (argc < 2) {
        fprintf(stderr, "%s <dev>", argv[0]);
        exit(1);
    }

    const int fd = open(argv[1], O_RDWR | O_DIRECT);

    struct io_uring_sqe* sqe;
    struct io_uring_cqe* cqe;
    sqe = io_uring_get_sqe(&ring);
    // TODO: writev/readv
    // io_uring_prep_writev(sqe, fd, );
    // io_uring_prep_rw(sqe, fd, )
    int size = 1lu << 12;
    char *buffer = aligned_alloc(4096, size);
    io_uring_prep_read_fixed(sqe, fd, buffer, size, 0, 0);
    io_uring_queue_exit(&ring);
    return 0;
}
