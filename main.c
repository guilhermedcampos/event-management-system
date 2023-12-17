/*
First Operating Systems Project
Authors: [Maria Ramos, ist1105875] [Guilherme Campos, ist1106909]
Description: 
    Multi-thread program with processes implemeting parallelization.
    This program allows processing of multiple .job files concurrently.
    The number of tasks for processing each ".jobs" file, MAX_THREADS, should be specified
    as a command-line argument at program startup. Our solution achieved parallelism while 
    ensuring atomic operations by locking the output file and seats.
*/

#include "constants.h"
#include "operations.h"
#include "parallelization.h"
#include <stdio.h>
#include <stdlib.h>

int max_thr = 1;
int max_proc =1;

int main(int argc, char *argv[]) {
    unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;

    // Check if the number of arguments is correct
    if (argc != 2 && argc != 4) {
        fprintf(stderr, "Usage: %s <directory> [max_proc] [max_thr>]\n",
                argv[0]);
        return 1;
    }

    max_thr = 1;
    max_proc = 1;

    // Set the directory
    char *directory = argv[1];

    // Check if the optional number argument is provided
    if (argc == 4) {
        char *endptr;
        max_proc = (int)strtoul(argv[2], &endptr, 10);
        max_thr = (int)strtoul(argv[3], &endptr, 10);
    } else {
        max_thr = 1;
        max_proc = 1;
    }

    if (ems_init(state_access_delay_ms)) {
        fprintf(stderr, "Failed to initialize EMS\n");
        return 1;
    }

    // Process the directory
    process_directory(directory);

    ems_terminate();
    return 0;
}