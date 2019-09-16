#include <time.h>
#include <stdio.h>
#include "common.h"

void io_report(struct rw_params *param) {
    double dt = (double)(param->stop->tv_sec - param->start->tv_sec) +
                (double)(param->stop->tv_nsec -  param->start->tv_nsec) / 1000000000.0;
    if (param->read_io > 0) {
        printf("READ:\n\tread_io: %ld\n\ttime: %.3f seconds\n", param->read_io, dt);
        double iops = (double)param->read_io / dt;
        printf("\tIOPS: %.3f\n\tbandwidth: %.3f MiB/s\n", iops, iops * (double)param->block_size / (1024.0 * 1024.0));
        printf("\tTotal: %.3f MiB\n", param->read_io * (double)param->block_size / (1024.0 * 1024.0));
    }

    if (param->write_io> 0) {
        printf("WRITE:\n\twrite_io: %ld\n\ttime: %.3f seconds\n", param->write_io, dt);
        double iops = (double)param->write_io / dt;
        printf("\tIOPS: %.3f\n\tbandwidth: %.3f MiB/s\n", iops, iops * (double)param->block_size / (1024.0 * 1024.0));
        printf("\tTotal: %.3f MiB\n", param->write_io * (double)param->block_size / (1024.0 * 1024.0));
    }
}
