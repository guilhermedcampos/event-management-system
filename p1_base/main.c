
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>


#include "constants.h"
#include "operations.h"
#include "parser.h"
#include "jobs_processor.h" 


#define JOBS_DIR "jobs"  // name of the directory

int endsWith(const char *str, const char *suffix) {
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);

    if (str_len < suffix_len) {
        return 0;
    }

    return strcmp(str + (str_len - suffix_len), suffix) == 0;
}

void parse_jobs_file(int fd, const char *base_name) {
  while(1) {
    // Example usage of parser functions
    enum Command cmd = get_next(fd);
    switch (cmd) {
        case CMD_CREATE: {
            unsigned int event_id;
            size_t num_rows, num_cols;
            if (parse_create(fd, &event_id, &num_rows, &num_cols) == 0) {
              //printf("CREATE: event_id=%u, num_rows=%zu, num_cols=%zu\n", event_id, num_rows, num_cols);
              ems_create(event_id, num_rows, num_cols);
            }
            break;
        }
        case CMD_RESERVE: {
            unsigned int event_id;
            size_t xs[10], ys[10];  // Assuming a maximum of 10 coordinates
            size_t num_coords = parse_reserve(fd, 10, &event_id, xs, ys);
            if (num_coords > 0) {
                ems_reserve(event_id, num_coords, xs, ys);
            }
            break;
        }

        case CMD_SHOW: {
                      // Construct the output file path
          char out_file_path[PATH_MAX];
          snprintf(out_file_path, sizeof(out_file_path), "%s/%s.out", JOBS_DIR, base_name);

          // Open the output file for writing
          FILE *out_file = fdopen(open(out_file_path, O_WRONLY | O_CREAT | O_TRUNC, 0666), "w");
          if (out_file == NULL) {
              perror("Error opening output file");
              return;
          }

          // Save the current stdout
          int saved_stdout = dup(fileno(stdout));
          if (saved_stdout == -1) {
              perror("Error saving stdout");
              fclose(out_file);
              return;
          }

          // Redirect stdout to the output file
          if (dup2(fileno(out_file), STDOUT_FILENO) == -1) {
              perror("Error redirecting stdout to output file");
              fclose(out_file);
              dup2(saved_stdout, STDOUT_FILENO);  // Restore stdout
              close(saved_stdout);
              return;
          }

          unsigned int event_id;
          if (parse_show(fd, &event_id) != 0) {
              fprintf(stderr, "Invalid command. See HELP for usage\n");
          } else {
              ems_show(event_id);
          }

          // Restore stdout
          if (dup2(saved_stdout, STDOUT_FILENO) == -1) {
              perror("Error restoring stdout");
          }

          // Close file descriptor for the output file (stdout is still redirected)
          fclose(out_file);

          // Close the saved file descriptor for stdout
          close(saved_stdout);

          return;
        }

        case CMD_LIST_EVENTS:
          break;

        case CMD_WAIT:
          break;

        case CMD_INVALID:
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          break;

        case CMD_HELP:
          break;

        case CMD_BARRIER:  // Not implemented
          break;

        case CMD_EMPTY:
          break;

        case EOC: {
          fflush(stdout);  // Flush after processing each file
          break;

        default:

          break;
      }
    }
  }
}

  void process_jobs_directory() {
      DIR *dir = opendir(JOBS_DIR);

      if (dir == NULL) {
          perror("Error opening directory");
          return;
      }

      struct dirent *entry;
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

                  parse_jobs_file(fd, base_name);
                  
                  // Close the file descriptor when done
                  close(fd);
              }
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

  while (1) {
    unsigned int event_id, delay;
    size_t num_rows, num_columns, num_coords;
    size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

    printf("> ");
    fflush(stdout);

    switch (get_next(STDIN_FILENO)) {
      case CMD_CREATE:
        if (parse_create(STDIN_FILENO, &event_id, &num_rows, &num_columns) != 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }

        if (ems_create(event_id, num_rows, num_columns)) {
          fprintf(stderr, "Failed to create event\n");
        }

        break;

      case CMD_RESERVE:
        num_coords = parse_reserve(STDIN_FILENO, MAX_RESERVATION_SIZE, &event_id, xs, ys);

        if (num_coords == 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }

        if (ems_reserve(event_id, num_coords, xs, ys)) {
          fprintf(stderr, "Failed to reserve seats\n");
        }

        break;

      case CMD_SHOW:
        if (parse_show(STDIN_FILENO, &event_id) != 0) {
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }

        if (ems_show(event_id)) {
          fprintf(stderr, "Failed to show event\n");
        }

        break;

      case CMD_LIST_EVENTS:
        if (ems_list_events()) {
          fprintf(stderr, "Failed to list events\n");
        }

        break;

      case CMD_WAIT:
        if (parse_wait(STDIN_FILENO, &delay, NULL) == -1) {  // thread_id is not implemented
          fprintf(stderr, "Invalid command. See HELP for usage\n");
          continue;
        }

        if (delay > 0) {
          printf("Waiting...\n");
          ems_wait(delay);
        }

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
      case CMD_EMPTY:
        break;

      case EOC:
        ems_terminate();
        return 0;
    }
  }
}