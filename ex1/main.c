#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "constants.h"
#include "operations.h"
#include "parser.h"

// Function to check if a file has a given extension
int endsWith(const char *str, const char *suffix) {
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);

    if (str_len < suffix_len) {
        return 0;
    }

    return strcmp(str + (str_len - suffix_len), suffix) == 0;
}

// Parses the content of a .jobs file
void parse_jobs_file(int fd, const char *base_name, char argv[]) {
    char out_file_path[PATH_MAX];

    // Construct the output file path
    snprintf(out_file_path, sizeof(out_file_path), "%s/%s.out", argv, base_name);

    // Open the output file for writing
    int out_fd = open(out_file_path, O_WRONLY | O_CREAT | O_TRUNC);
    if (out_fd == -1) {
        perror("Error opening output file");
        return;
    }

    int close_out_fd = 1;  // Flag to track if out_fd needs to be closed

    while (1) {
        enum Command cmd = get_next(fd);
        switch (cmd) {
            case CMD_CREATE: {
                unsigned int event_id;
                size_t num_rows, num_cols;
                if (parse_create(fd, &event_id, &num_rows, &num_cols) == 0) {
                    ems_create(event_id, num_rows, num_cols);
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
                ems_show(event_id, out_fd);
                break;
            }

            case CMD_LIST_EVENTS:
                if (ems_list_events(out_fd)) {
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

            case EOC:
                close_out_fd = 0;  // Flag to track if out_fd needs to be closed
                break;

            default:
                break;
        }

        if (!close_out_fd) {
            break;  // Break out of the loop when EOC is encountered
        }
    }

    // Close file descriptors outside the loop
    close(fd);    // Close input file descriptor
    close(out_fd); // Close output file descriptor
    fflush(stdout);  // Flush after processing each file
}

void process_directory(char argv[]) {
    // Open the directory specified by the argv parameter
    DIR *dir = opendir(argv);

    // Check if the directory opening was successful
    if (dir == NULL) {
        perror("Error opening directory");
        return;
    }

    // Declare a struct dirent pointer to represent a directory entry
    struct dirent *entry;

    // Loop through each file in the directory
    while ((entry = readdir(dir)) != NULL) {
        // Check if the file has a ".jobs" extension
        if (endsWith(entry->d_name, ".jobs")) {
            // Print a message indicating that a ".jobs" file was found
            printf("Found .jobs file: %s\n", entry->d_name);

            // Reset the event list before processing each file
            reset_event_list();

            // Construct the full path to the ".jobs" file
            char file_path[PATH_MAX];
            snprintf(file_path, sizeof(file_path), "%s/%s", argv, entry->d_name);

            // Construct the base name (without the extension) of the file
            char base_name[PATH_MAX];
            snprintf(base_name, sizeof(base_name), "%.*s", (int)(strrchr(entry->d_name, '.') - entry->d_name), entry->d_name);

            // Open the ".jobs" file for reading
            int fd = open(file_path, O_RDONLY);
            if (fd == -1) {
                perror("Error opening job file");
                continue;  // Move on to the next file if opening fails
            }

            // Parse the content of the ".jobs" file
            parse_jobs_file(fd, base_name, argv);

            // Close the file descriptor for the ".jobs" file
            close(fd);
        }
    }

    // Close the directory
    closedir(dir);
}


int main(int argc, char *argv[]) {
    unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;

    // Check if the number of arguments is correct
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "Usage: %s <directory> [number]\n", argv[0]);
        return 1;
    }

    // Set the directory
    char *directory = argv[1];

    // Check if the optional number argument is provided
    if (argc == 3) {
        char *endptr;
        unsigned long int delay = strtoul(argv[2], &endptr, 10);

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

    // Process the directory
    process_directory(directory);

    ems_terminate();
    return 0;
}
