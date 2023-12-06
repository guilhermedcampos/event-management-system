#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <pthread.h>

#include "constants.h"
#include "operations.h"
#include "parser.h"

#define MAX_THREADS 1
#define JOBS_DIR "jobs"  // name of the directory

int endsWith(const char *str, const char *suffix) {
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);

    if (str_len < suffix_len) {
        return 0;
    }

    return strcmp(str + (str_len - suffix_len), suffix) == 0;
}

void addToArray(unsigned int** createdEvents, int* size, unsigned int eventId) {
   *size += 1;

   *createdEvents = (unsigned int*)realloc(*createdEvents, (size_t)*size * sizeof(int));
   if (*createdEvents == NULL) {
      fprintf(stdin, "Memory reallocation failed");
      exit(EXIT_FAILURE);
   }

   (*createdEvents)[*size - 1] = eventId;
}

// Parses the .jobs file given, reading its content
void parse_jobs_file(const char *file_path) {
    int fd = open(file_path, O_RDONLY);
    if (fd == -1) {
        perror("Error opening job file");
        return;
    }

    int end_of_cycle = 0;
    int size = 0;
    unsigned int* created_events = NULL;
    unsigned int tid; 

    while (!end_of_cycle) {
        enum Command cmd = get_next(fd);
        switch (cmd) {
            case CMD_CREATE: {
                unsigned int event_id;
                size_t num_rows, num_cols;
                if (parse_create(fd, &event_id, &num_rows, &num_cols) == 0) {
                    ems_create(event_id, num_rows, num_cols);
                    addToArray(&created_events, &size, event_id);
                }
                break;
            }
            case CMD_RESERVE: {
                unsigned int event_id;
                size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];
                size_t num_coords = parse_reserve(fd, MAX_RESERVATION_SIZE, &event_id, xs, ys);
                if (num_coords > 0) {
                    ems_reserve(event_id, num_coords, xs, ys);
                }
                break;
            }

            case CMD_SHOW: {
                unsigned int event_id;
                if (parse_show(fd, &event_id) != 0) {
                    fprintf(stderr, "Invalid command. See HELP for usage\n");
                }
                ems_show(event_id);
                break;
            }

            case CMD_LIST_EVENTS:
                if (ems_list_events()) {
                fprintf(stderr, "Failed to list events\n");
                }

                break;

            case CMD_WAIT: {
                    unsigned int delay;
                    parse_wait(fd, &delay, &tid);
                    sleep(delay * 1000); // Sleep for the specified delay
                    break;
                }

            case CMD_HELP:
            printf(
                "Available commands:\n"
                "  CREATE <event_id> <num_rows> <num_columns>\n"
                "  RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n"
                "  SHOW <event_id>\n"
                "  LIST\n"
                "  WAIT <delay_ms> [thread_id]\n"  // thread_id is not implemented
                "  BARRIER\n"                      // Not implemented
                "  HELP\n");
            break;

            case CMD_INVALID:
                fprintf(stderr, "Invalid command. See HELP for usage\n");
                break;

            case CMD_BARRIER:  // Not implemented
            break;

            case CMD_EMPTY:
            break;

            case EOC: {
                if (created_events == NULL) {
                    perror("No events to display.");
                    break;
                }

                for (int i = 0; i < size; i++) {
                    printf("Event: %u\n", created_events[i]);
                    ems_show(created_events[i]);
                    printf("\n");
                }

                free(created_events);
                end_of_cycle = 1;
                created_events = NULL;
                break;
            }
            default:
                break;
        }
    }

    close(fd);
}

void* process_job(void* arg) {
    const char* file_path = (const char*)arg;
    parse_jobs_file(file_path);
    return NULL;
}

void process_jobs_directory() {
    DIR* dir = opendir(JOBS_DIR);

    if (dir == NULL) {
        perror("Error opening directory");
        return;
    }

    struct dirent* entry;
    int thread_count = 0;
    pthread_t threads[MAX_THREADS];

    while ((entry = readdir(dir)) != NULL) {
        // Check if the file has a ".jobs" extension
        if (endsWith(entry->d_name, ".jobs")) {
            printf("Found .jobs file: %s\n", entry->d_name);

            // Construct the full path to the job file
            char file_path[PATH_MAX];
            snprintf(file_path, sizeof(file_path), "%s/%s", JOBS_DIR, entry->d_name);

            // Create a thread for each job file
            if (pthread_create(&threads[thread_count], NULL, process_job, (void*)file_path) != 0) {
                fprintf(stderr, "Error creating thread.\n");
                break;
            }

            // Increment the thread count
            thread_count++;

            // Check if the maximum number of threads has been reached
            if (thread_count >= MAX_THREADS) {
                // Wait for threads to finish
                for (int i = 0; i < thread_count; i++) {
                    pthread_join(threads[i], NULL);
                }
                // Reset the thread count
                thread_count = 0;
            }
        }
    }

    // Wait for remaining threads to finish
    for (int i = 0; i < thread_count; i++) {
        pthread_join(threads[i], NULL);
    }

    closedir(dir);
}

int main(int argc, char *argv[]) {
  unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;

  if (argc > 1) {
    char *endptr;
    unsigned long int delay = strtoul(argv[1], &endptr, 10);

    if (*endptr != '\0' || delay > UINT_MAX) {
      fprintf(stderr, "Invalid delay value or value too large\n");
      return 1;
    }

    state_access_delay_ms = (unsigned int)delay;
  }

  if (ems_init(state_access_delay_ms)) {
    fprintf(stderr, "Failed to initialize EMS\n");
    return 1;
  }

  process_jobs_directory();

        ems_terminate();
        return 0;
}