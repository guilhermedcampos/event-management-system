#include "constants.h"
#include "operations.h"
#include "parser.h"
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Create a lock when writing onto the output file
pthread_mutex_t output_file_lock = PTHREAD_MUTEX_INITIALIZER;

// Default value if not specified a maximum number of threads per process
int max_thr = 1;

// Default value if not specified a maximum number of processes active
int max_proc = 1;

// Structure to hold thread-specific data
struct ThreadData {
    int id;     // Thread ID
    int fd;     // File descriptor
    int out_fd; // Output file descriptor
};

// Check if a file name has a given extension.
int endsWith(const char *str, const char *suffix) {
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);

    if (str_len < suffix_len) {
        return 0;
    }

    return strcmp(str + (str_len - suffix_len), suffix) == 0;
}

// Count the number of lines in a file.
int numLines(const char *file_path) {
    int count = 0;
    char c;

    int fd = open(file_path, O_RDONLY);

    if (fd == -1) {
        perror("Error opening file");
        return -1;
    }

    while (read(fd, &c, 1) > 0) {
        if (c == '\n') {
            count++;
        }
    }

    close(fd);

    if (count > 0) {
        count++;
    }

    return count;
}

// Get the line number at a given file offset.
int get_line_number(int fd, off_t offset) {
    char c;
    off_t original_offset = lseek(fd, 0, SEEK_CUR);

    lseek(fd, 0, SEEK_SET);

    int line_number = 1;

    while (lseek(fd, 0, SEEK_CUR) < offset) {
        if (read(fd, &c, 1) == 0) {
            // Break if end of file is reached
            break;
        }

        if (c == '\n') {
            line_number++;
        }
    }

    lseek(fd, original_offset, SEEK_SET); // Restore the original offset

    return line_number;
}

// Function to open the output file
int open_output_file(const char *base_name, char argv[]) {
    char out_file_path[PATH_MAX];
    snprintf(out_file_path, sizeof(out_file_path), "%s/%s.out", argv,
             base_name);

    int out_fd = open(out_file_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);

    return out_fd;
}

// Parses the content of a .jobs file
void parse_jobs_file(int fd, int out_fd, int id) {
    // Open the output file for writing
    // Lock the mutex for the file descriptor (out_fd)

    // Reset the end of file flag
    int eof_flag = 0;

    int current_line = 0; // Current line number

    // Process each command in the file until the end is reached
    while (!eof_flag) {
        enum Command cmd = get_next(fd);
        current_line = get_line_number(fd, lseek(fd, 0, SEEK_CUR));

        // Process the command based on its type
        switch (cmd) {
        case CMD_CREATE: {
            unsigned int event_id;
            size_t num_rows, num_cols;

            // Parse CREATE command parameters
            if (parse_create(fd, &event_id, &num_rows, &num_cols) == 0) {
                if (current_line % max_thr == id - 1) {
                    printf("Thread %d creating on line %d.\n", id,
                           current_line);
                    ems_create(event_id, num_rows, num_cols);
                }
            }
            break;
        }
        case CMD_RESERVE: {
            unsigned int event_id;
            size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

            size_t num_coords =
                parse_reserve(fd, MAX_RESERVATION_SIZE, &event_id, xs, ys);
            if (num_coords > 0) {
                if (current_line % max_thr == id - 1) {
                    printf("Thread %d reserving on line %d.\n", id,
                           current_line);
                    ems_reserve(event_id, num_coords, xs, ys);
                }
            }
            break;
        }
        case CMD_SHOW: {
            pthread_mutex_lock(&output_file_lock);
            unsigned int event_id;
            if (parse_show(fd, &event_id) != 0) {
                fprintf(stderr, "Invalid command. See HELP for usage\n");
                break;
            }
            // Execute ems_show and write to the output file
            if (current_line % max_thr == id - 1) {
                printf("Thread %d showing on line %d.\n", id, current_line);
                ems_show(event_id, out_fd);
                fsync(out_fd);
                printf("Thread %d finished showing on line %d.\n", id, current_line);
            }
            pthread_mutex_unlock(&output_file_lock);
            break;
        }
        case CMD_LIST_EVENTS: {
            pthread_mutex_lock(&output_file_lock);
            if (current_line % max_thr == id - 1) {
                printf("Thread %d listing events on line %d.\n", id,
                       current_line);
                ems_list_events(out_fd);
                 fsync(out_fd);
                printf("Thread %d finished listing events on line %d.\n", id,
                       current_line);
            }
            pthread_mutex_unlock(&output_file_lock);
            break;
        }
        case CMD_WAIT: {
            // Process WAIT command
            unsigned int wait_delay;
            unsigned int id_index;
            pthread_mutex_lock(&output_file_lock);
            int wait_result = parse_wait(fd, &wait_delay, &id_index);
            pthread_mutex_unlock(&output_file_lock);
            if (wait_result == 0) {
                printf("Thread %d waiting %d seconds\n", id, wait_delay / 1000);
                // all threads should wait
                ems_wait(wait_delay);
            } else if (wait_result == 1) {
                // only one thread should wait
                if ((int)id_index == id) {
                    printf("Thread %d waiting %d seconds\n", id,
                           wait_delay / 1000);
                    ems_wait(wait_delay);
                }
            }
            break;
        }
        case CMD_INVALID:
            // Handle invalid command
            if (current_line % max_thr == id - 1) {
                fprintf(stderr, "Invalid command. See HELP for usage\n");
            }
            break;
        case CMD_HELP:
            // Lock the mutex for the file descriptor (out_fd)
            pthread_mutex_lock(&output_file_lock);
            if (current_line % max_thr == id - 1) {
                printf("Thread %d showing help on line %d.\n", id,
                       current_line);
                ems_help(out_fd);
            }
            pthread_mutex_unlock(&output_file_lock);
            break;
        case CMD_BARRIER:
            printf("Thread %d reached barrier.\n", id);
            pthread_exit((void *)1);
            break;
        case CMD_EMPTY:
            // Handle EMPTY command
            break;
        case EOC:
            printf("Thread %d reached end of file.\n", id);
            // Count threads that reached the end of file
            eof_flag = 1;
            break;
        default:
            break;
        }
    }

    close(fd);
    // Flush after processing each file
    fflush(stdout);
}

// The function constructs the path to the job file, parses the file using
// parse_jobs_file, and then exits the thread.
void *process_file_thread(void *arg) {
    struct ThreadData *thread_data = (struct ThreadData *)arg;

    // Parse the .jobs file
    parse_jobs_file(thread_data->fd, thread_data->out_fd, thread_data->id);

    // Exit the thread
    pthread_exit(NULL);
}

void init_thread_list(pthread_t *threads, struct ThreadData *thread_list,
                      const char *file_path, int out_fd) {
    for (int i = 0; i < max_thr; ++i) {
        // Allocate separate memory for each thread
        // Open the job file
        int fd = open(file_path, O_RDONLY);
        if (fd == -1) {
            perror("Error opening job file");
            continue;
        }

        // Initialize thread data
        thread_list[i].id = i + 1;
        thread_list[i].fd = fd;
        thread_list[i].out_fd = out_fd;

        // Create threads to process the file
        if (pthread_create(&threads[i], NULL, process_file_thread,
                           (void *)&thread_list[i]) != 0) {
            perror("Error creating thread");
            // Handle error as needed
            return;
        }
    }
}

// Function to process all files in a directory
void process_directory(char argv[]) {
    // Open the directory
    DIR *dir = opendir(argv);

    if (dir == NULL) {
        perror("Error opening directory");
        return;
    }

    struct dirent *entry;
    int active_processes = 0;

    // For each file found in the directory
    while ((entry = readdir(dir)) != NULL) {
        // Check if the file has a ".jobs" extension
        if (endsWith(entry->d_name, ".jobs")) {
            printf("Found .jobs file: %s\n", entry->d_name);

            // Reset the event list before processing each file
            reset_event_list();

            // Construct the path to the job file
            char file_path[PATH_MAX];
            snprintf(file_path, sizeof(file_path), "%s/%s", argv,
                     entry->d_name);

            // Construct the file name
            char base_name[PATH_MAX];
            snprintf(base_name, sizeof(base_name), "%.*s",
                     (int)(strrchr(entry->d_name, '.') - entry->d_name),
                     entry->d_name);

            pid_t pid = fork();

            if (pid == 0) { // Child process
                // Open the output file for writing
                int out_fd = open_output_file(base_name, argv);
                if (out_fd == -1) {
                    perror("Error opening output file");
                    return;
                }
                pthread_t threads[max_thr];
                // Create a list of threads structures
                struct ThreadData *thread_list = malloc(
                    (long unsigned int)max_thr * sizeof(struct ThreadData));
                // Create thread data and populate it
                init_thread_list(threads, thread_list, file_path, out_fd);
                // Wait for all threads to finish
                void *value = 0;
                int barrier = 0;
                while (1) {
                    for (int i = 0; i < max_thr; ++i) {
                        pthread_join(threads[i], &value);
                        if (value == (void *)1) {
                            barrier = 1;
                        }
                    }
                    if (barrier) {
                        barrier = 0;
                        for (int i = 0; i < max_thr; ++i) {
                            if (pthread_create(&threads[i], NULL,
                                               process_file_thread,
                                               (void *)&thread_list[i]) != 0) {
                                return;
                            }
                        }
                    } else {
                        break;
                    }
                }
                close(out_fd);
                free(thread_list);
                // Exit the child process
                exit(0);
            } else if (pid > 0) {
                // Parent process
                active_processes++;

                // Wait for child processes to avoid exceeding the maximum
                // allowed
                while (active_processes >= max_proc) {
                    int status;
                    pid_t child_pid = wait(&status);
                    if (child_pid > 0) {
                        active_processes--;
                    }
                }
            } else {
                // Handle fork failure
                perror("Fork failed");
            }
        }
    }

    // Wait for remaining child processes to finish
    while (active_processes > 0) {
        int status;
        pid_t child_pid = wait(&status);
        if (child_pid > 0) {
            active_processes--;
        }
    }

    // Close the jobs directory
    closedir(dir);
}

// Main function
int main(int argc, char *argv[]) {
    unsigned int state_access_delay_ms = STATE_ACCESS_DELAY_MS;

    // Check if the number of arguments is correct
    if (argc != 2 && argc != 4) {
        fprintf(stderr, "Usage: %s <directory> [max_proc] <max_thr>\n",
                argv[0]);
        return 1;
    }

    // Set the directory
    char *directory = argv[1];

    // Declare max_proc outside the if block

    // Check if the optional number argument is provided
    if (argc == 4) {
        char *endptr;
        max_proc = (int)strtoul(argv[2], &endptr, 10);
        max_thr = (int)strtoul(argv[3], &endptr, 10);
    }

    if (ems_init(state_access_delay_ms)) {
        fprintf(stderr, "Failed to initialize EMS\n");
        return 1;
    }

    // Process the directory
    process_directory(directory);

    // print status of the directory
    printf("Directory %s processed\n", directory);
    ems_terminate();
    return 0;
}