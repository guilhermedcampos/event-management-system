
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "constants.h"
#include "operations.h"
#include "parser.h"

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
void parse_jobs_file(int fd, const char *base_name) {
  int end_of_cycle = 0;

  int size = 0;
  unsigned int* created_events = NULL;

  while(!end_of_cycle) {
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

        case CMD_WAIT:
          break;

        case CMD_INVALID:
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          break;

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

        case CMD_BARRIER:  // Not implemented
          break;

        case CMD_EMPTY:
          break;

        case EOC: {
          if (created_events == NULL) {
              perror("No events to display.");
            break;
          }

          // Construct the output file path
          char out_file_path[PATH_MAX];
          snprintf(out_file_path, sizeof(out_file_path), "%s/%s.out", JOBS_DIR, base_name);

          // Open the output file for writing
          int out_fd = open(out_file_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
          if (out_fd == -1) {
              perror("Error opening output file");
              free(created_events);
              break;
          }

          // Save the current stdout
          int saved_stdout = dup(STDOUT_FILENO);

          // Redirect stdout to the output file
          if (dup2(out_fd, STDOUT_FILENO) == -1) {
              perror("Error redirecting stdout to output file");
              close(out_fd);
              free(created_events);
              break;
          }

          for (int i = 0; i < size; i++) {
            printf("Event: %u\n", created_events[i]);
            ems_show(created_events[i]);
            printf("\n");
          }


          // Restore stdout
          if (dup2(saved_stdout, STDOUT_FILENO) == -1) {
              perror("Error restoring stdout");
          }

          // Close file descriptors
          close(out_fd);
          close(saved_stdout);

          fflush(stdout);  // Flush after processing each file

          // Free the memory allocated for event_ids
          free(created_events);
          end_of_cycle = 1;
          created_events = NULL;
          break;
        }
        default:

          break;
      }
    }
  }

void process_jobs_directory(int max_proc) {
    DIR *dir = opendir(JOBS_DIR);

    if (dir == NULL) {
        perror("Error opening directory");
        return;
    }

    struct dirent *entry;
    int active_processes = 0; // Counter for active child processes

    while ((entry = readdir(dir)) != NULL) {
        // Check if the file has a ".jobs" extension
        if (endsWith(entry->d_name, ".jobs")) {
            printf("Found .jobs file: %s\n", entry->d_name);

            // Construct the full path to the job file
            char file_path[PATH_MAX];
            snprintf(file_path, sizeof(file_path), "%s/%s", JOBS_DIR, entry->d_name);

            char base_name[PATH_MAX];
            snprintf(base_name, sizeof(base_name), "%.*s", (int)(strrchr(entry->d_name, '.') - entry->d_name), entry->d_name);

            // Open the job file
            int fd = open(file_path, O_RDONLY);
            if (fd == -1) {
                perror("Error opening job file");
                continue;  // Move on to the next file
            }

            // New child process
            pid_t pid = fork();

            if (pid == 0) {
                // Child process
                parse_jobs_file(fd, base_name);
                close(fd);
                exit(0);
            } else if (pid > 0) {
                // Parent process
                active_processes++;

                // Check if the maximum number of processes has been reached
                while (active_processes >= max_proc) {
                    int status;
                    pid_t child_pid = waitpid(-1, &status, WNOHANG);
                    if (child_pid > 0) {
                        active_processes--;
                    }
                }
                close(fd);
            } else {
                // Fork failed
                perror("Fork failed");
            }
        }
    }

    // Wait for remaining child processes to finish
    while (active_processes > 0) {
        int status;
        pid_t child_pid = waitpid(-1, &status, 0);
        if (child_pid > 0) {
            active_processes--;
        }
    }

    closedir(dir);
}



int main(int argc, char *argv[]) {
  unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;
  int max_proc;

  printf("Enter the maximum number of processes (MAX_PROC): ");
  scanf("%d", &max_proc);

  if (max_proc <= 0) {
    fprintf(stderr, "Invalid value for MAX_PROC\n");
    return 1;
  }

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

  process_jobs_directory(max_proc);

        ems_terminate();
        return 0;
}