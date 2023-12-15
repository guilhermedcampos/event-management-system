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
            if (parse_create(fd, &event_id, &num_rows, &num_cols) != 0) {
                fprintf(stderr, "Invalid command. See HELP for usage\n");
                continue;
            }
            if (current_line % max_thr == id - 1) {
                if (ems_create(event_id, num_rows, num_cols)) {
                    fprintf(stderr, "Failed to create event\n");
                }
            }

            break;
        }
        case CMD_RESERVE: {
            unsigned int event_id;
            size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

            size_t num_coords =
                parse_reserve(fd, MAX_RESERVATION_SIZE, &event_id, xs, ys);

            if (num_coords == 0) {
                fprintf(stderr, "Invalid command. See HELP for usage\n");
                continue;
            }

            if (current_line % max_thr == id - 1) {
                if (ems_reserve(event_id, num_coords, xs, ys)) {
                    fprintf(stderr, "Failed to reserve seats\n");
                }
            }
            break;
        }
        case CMD_SHOW: {
            // Lock the mutex for the file descriptor (out_fd)
            pthread_mutex_lock(&output_file_lock);
            unsigned int event_id;
            if (parse_show(fd, &event_id) != 0) {
                fprintf(stderr, "Invalid command. See HELP for usage\n");
                continue;
            }
            if (current_line % max_thr == id - 1) {
                if (ems_show(event_id, out_fd)) {
                    fprintf(stderr, "Failed to show event\n");
                }
            }
            pthread_mutex_unlock(&output_file_lock);
            break;
        }
        case CMD_LIST_EVENTS: {
            // Lock the mutex for the file descriptor (out_fd)
            pthread_mutex_lock(&output_file_lock);
            if (current_line % max_thr == id - 1) {
                if (ems_list_events(out_fd)) {
                    fprintf(stderr, "Failed to list events\n");
                }
            }
            pthread_mutex_unlock(&output_file_lock);
            break;
        }
        case CMD_WAIT: {
            // Initialize variables
            unsigned int wait_delay;
            unsigned int id_index;

            // Lock the mutex for the file descriptor (out_fd)
            pthread_mutex_lock(&output_file_lock);

            // Parse the wait command
            int wait_result = parse_wait(fd, &wait_delay, &id_index);
            if (wait_result == -1) {
                fprintf(stderr, "Invalid command. See HELP for usage\n");
                continue;
            }

            pthread_mutex_unlock(&output_file_lock);

            // Handle WAIT command
            if (wait_result == 0) {
                printf("Thread %d waiting...\n", id);
                // All threads should wait
                ems_wait(wait_delay);
            } else if (wait_result == 1) {
                // Only one thread should wait
                if ((int)id_index == id) {
                    printf("Thread %d waiting...\n", id);
                    ems_wait(wait_delay);
                }
            }
            break;
        }
        case CMD_INVALID:
            if (current_line % max_thr == id - 1) {
                fprintf(stderr, "Invalid command. See HELP for usage\n");
            }
            break;
        case CMD_HELP:
            // Lock the mutex for the file descriptor (out_fd)
            pthread_mutex_lock(&output_file_lock);

            if (current_line % max_thr == id - 1) {
                ems_help(out_fd);
            }

            pthread_mutex_unlock(&output_file_lock);
            break;
        case CMD_BARRIER:
            pthread_exit((void *)1);
            break;
        case CMD_EMPTY:
            break;
        case EOC:
            eof_flag = 1;
            break;
        default:
            break;
        }
    }

    // Close the file
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
                printf("Child process [%d] started\n", getpid());
                // Open the output file for writing
                int out_fd = open_output_file(base_name, argv);
                if (out_fd == -1) {
                    perror("Error opening output file");
                    return;
                }

                // Array to store each thread
                pthread_t threads[max_thr];

                // Create a list of threads structures
                struct ThreadData *thread_list = malloc(
                    (long unsigned int)max_thr * sizeof(struct ThreadData));

                // Create thread data and populate it
                init_thread_list(threads, thread_list, file_path, out_fd);

                void *value = 0;
                int barrier = 0;

                // Barrier synchronization loop
                while (1) {
                    for (int i = 0; i < max_thr; ++i) {
                        pthread_join(threads[i], &value);
                        if (value == (void *)1) {
                            barrier = 1;  // Set the barrier flag when a thread reaches the barrier
                        } 
                    }
                    if (barrier) {
                        barrier = 0;
                        // Create a new set of threads to continue processing
                        for (int i = 0; i < max_thr; ++i) {
                            if (pthread_create(&threads[i], NULL,
                                               process_file_thread,
                                               (void *)&thread_list[i]) != 0) {
                                return;
                            }
                        }
                    } else {
                        break;  // Exit the loop when no barrier is reached
                    }
                }
                // Close the output file descriptor
                close(out_fd);

                // Free allocated memory for thread's data
                free(thread_list);
                int status;
                wait(&status);
                printf("Child process [%d] exited with status[%d]\n", getpid(),
                       WEXITSTATUS(status));
                // Exit the child process
                exit(0);
            } else if (pid > 0) {
                // Parent process
                active_processes++;
                printf("Parent process [%d] created child process [%d]\n",
                       getpid(), pid);

                // Wait for child processes to avoid exceeding the maximum
                // allowed
                while (active_processes >= max_proc) {
                    int status;
                    pid_t child_pid = wait(&status);
                    if (child_pid > 0) {
                        active_processes--;
                        printf("Parent process [%d] waited for child process "
                               "[%d]\n",
                               getpid(), child_pid);
                    }
                }
            } else {
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
        fprintf(stderr, "Usage: %s <directory> [max_proc] [max_thr>]\n",
                argv[0]);
        return 1;
    }

    // Set the directory
    char *directory = argv[1];

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

    ems_terminate();
    return 0;
}