// parallelization.h
#ifndef PARALLELIZATION_H
#define PARALLELIZATION_H

#include "constants.h"
#include <pthread.h>
#include <fcntl.h>

extern int max_thr;
extern int max_proc;

// Structure to hold thread-specific data
struct ThreadData {
    int id;     // Thread ID
    int fd;     // File descriptor
    int out_fd; // Output file descriptor
};

// Declare functions from parser.c
int endsWith(const char *str, const char *suffix);
int get_line_number(int fd, off_t offset);
int open_output_file(const char *base_name, char argv[]);
void parse_jobs_file(int fd, int out_fd, int id);
void *process_file_thread(void *arg);
void init_thread_list(pthread_t *threads, struct ThreadData *thread_list,
                      const char *file_path, int out_fd);
void process_directory(char argv[]);

#endif // PARALLELIZATION_H
