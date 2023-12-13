#include "constants.h"
#include "operations.h"
#include "parser.h"
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// Mutexes for synchronization
pthread_mutex_t f_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t ems_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t outf_mutex = PTHREAD_MUTEX_INITIALIZER;

// Mutex for synchronization of barrier variables
pthread_mutex_t barrier_mutex = PTHREAD_MUTEX_INITIALIZER;

// Flag to indicate end of file
bool eof_flag = false;

// flag for barrier command
bool barrier_reached = false;
int barrier_line = -1;
int barrier_count = 0;

// Structure to hold thread-specific data
struct ThreadData {
    int num_lines;
    int id;
    int fd;
    int max_thr;
    char base_name[PATH_MAX];
    char argv[PATH_MAX];
};

// Function to check if a file has a given extension
int endsWith(const char *str, const char *suffix) {
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);

    if (str_len < suffix_len) {
        return 0;
    }

    return strcmp(str + (str_len - suffix_len), suffix) == 0;
}

// Function to count the number of lines in a file
int numLines(const char *file_path) {
    int count = 0;
    char c;

    int fd = open(file_path, O_RDONLY);

    if (fd == -1) {
        // Handle file open error
        perror("Error opening file");
        return -1; // Return an error value
    }

    // Read the file character by character
    while (read(fd, &c, 1) > 0) {
        if (c == '\n') {
            count++;
        }
    }

    close(fd);

    if (count > 0) {
        // Increment count for the last line if it doesn't end with a newline
        count++;
    }

    return count;
}

// Function to get the line number at a given file offset
int get_line_number(int fd, off_t offset) {
    char c;
    off_t original_offset = lseek(fd, 0, SEEK_CUR); // Save the original offset

    lseek(fd, 0, SEEK_SET); // Move to the beginning of the file

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

void set_line(int fd, int line_number) {
    char c;
    int current_line = 1;

    lseek(fd, 0, SEEK_SET); // Move to the beginning of the file

    while (current_line < line_number) {
        if (read(fd, &c, 1) == 0) {
            // Break if end of file is reached
            break;
        }

        if (c == '\n') {
            current_line++;
        }
    }
}
// Parses the content of a .jobs file
void parse_jobs_file(int fd, const char *base_name, char argv[], int id,
                     int max_thr, int num_lines) {

    // Construct the output file path
    char out_file_path[PATH_MAX];
    snprintf(out_file_path, sizeof(out_file_path), "%s/%s.out", argv,
             base_name);

    // Open the output file for writing
    int out_fd = open(out_file_path, O_WRONLY | O_CREAT | O_TRUNC);
    if (out_fd == -1) {
        perror("Error opening output file");
        return;
    }

    // Reset the end of file flag
    eof_flag = false;

    // wait command variables
    unsigned int wait_delay;
    bool wait_flag[max_thr];

    // Initialize wait_flag array
    for (int i = 0; i < max_thr; i++) {
        wait_flag[i] = false;
    }

    // Process each command in the file until the end is reached
    while (!eof_flag) {

        pthread_mutex_lock(&f_mutex);
        // Print the current line number
        int current_line = get_line_number(fd, lseek(fd, 0, SEEK_CUR));
        printf("Thread with id %d reading line %d with command.\n", id,
               current_line);
        pthread_mutex_unlock(&f_mutex);

        printf("Barrier line: %d\n", barrier_line);
        printf("Current line: %d\n", current_line);

        if (barrier_reached) {
            pthread_mutex_lock(
                &barrier_mutex); // Lock to protect barrier variables
            if (barrier_line == current_line) {
                barrier_count++;
                printf("Thread %d reached barrier\n", id);
                printf("Barrier count: %d\n", barrier_count);
                while (barrier_count < max_thr) {
                    pthread_mutex_unlock(&barrier_mutex);
                    ems_wait(1000);
                    printf("Thread %d waiting for barrier\n", id);
                }
                barrier_reached = false;
            }
        }
        // See if thread should wait
        if (wait_flag[id - 1]) {
            printf("Thread %d waiting %d seconds\n", id, wait_delay / 1000);
            ems_wait(wait_delay);
            wait_flag[id - 1] = false;
            printf("Thread %d finished waiting\n", id);
        }

        pthread_mutex_lock(&f_mutex);
        // Get the next command from the file
        enum Command cmd = get_next(fd);
        pthread_mutex_unlock(&f_mutex);

        // Process the command based on its type
        switch (cmd) {
        case CMD_CREATE: {
            // Process CREATE command
            printf("Thread %d creating.\n", id);
            unsigned int event_id;
            size_t num_rows, num_cols;

            pthread_mutex_lock(&f_mutex);

            // Parse CREATE command parameters
            if (parse_create(fd, &event_id, &num_rows, &num_cols) == 0) {
                pthread_mutex_unlock(&f_mutex);
                pthread_mutex_lock(&ems_mutex);
                ems_create(event_id, num_rows, num_cols);
                pthread_mutex_unlock(&ems_mutex);
            } else {
                pthread_mutex_unlock(&f_mutex);
            }
            break;
        }
        case CMD_RESERVE: {
            // Process RESERVE command
            printf("Thread %d reserving.\n", id);
            unsigned int event_id;
            size_t xs[MAX_RESERVATION_SIZE], ys[MAX_RESERVATION_SIZE];

            pthread_mutex_lock(&f_mutex);

            // Parse RESERVE command parameters
            size_t num_coords =
                parse_reserve(fd, MAX_RESERVATION_SIZE, &event_id, xs, ys);
            pthread_mutex_unlock(&f_mutex);
            if (num_coords > 0) {
                pthread_mutex_lock(&ems_mutex);
                ems_reserve(event_id, num_coords, xs, ys);
                pthread_mutex_unlock(&ems_mutex);
            }

            printf("Thread %d finished reserving.\n", id);
            break;
        }
        case CMD_SHOW: {
            // Process SHOW command
            printf("Thread %d showing.\n", id);
            unsigned int event_id;

            pthread_mutex_lock(&f_mutex);

            // Parse SHOW command parameters outside ems_mutex to avoid
            // potential deadlock
            if (parse_show(fd, &event_id) != 0) {
                fprintf(stderr, "Invalid command. See HELP for usage\n");
                pthread_mutex_unlock(&f_mutex);
                break;
            }

            pthread_mutex_lock(&ems_mutex);
            pthread_mutex_lock(&outf_mutex);

            // Execute ems_show and write to the output file
            ems_show(event_id, out_fd);

            pthread_mutex_unlock(&outf_mutex);
            pthread_mutex_unlock(&ems_mutex);
            pthread_mutex_unlock(&f_mutex);
            printf("Thread %d finished showing.\n", id);
            break;
        }
        case CMD_LIST_EVENTS: {
            // Process LIST_EVENTS command
            printf("Thread %d listing.\n", id);
            pthread_mutex_lock(&ems_mutex);
            pthread_mutex_lock(&outf_mutex);
            ems_list_events(out_fd);
            pthread_mutex_unlock(&outf_mutex);
            pthread_mutex_unlock(&ems_mutex);
            printf("Thread %d finished listing.\n", id);
            break;
        }
        case CMD_WAIT: {
            // Process WAIT command
            unsigned int thread_id;
            pthread_mutex_lock(&f_mutex);

            int wait_result = parse_wait(fd, &wait_delay, &thread_id);
            printf("Wait result: %d\n", wait_result);
            printf("Delay: %d\n", wait_delay);

            if (wait_result == 1) {
                // only one thread should wait
                wait_flag[thread_id - 1] = true;
                printf("Setting wait flag for thread %d.\n", thread_id);
            } else if (wait_result == 0) {
                // all threads should wait
                for (int i = 0; i < max_thr; i++) {
                    wait_flag[i] = true;
                }
                printf("Setting wait flag for all threads.\n");
            }
            pthread_mutex_unlock(&f_mutex);
            break;
        }
        case CMD_INVALID:
            // Handle invalid command
            fprintf(stderr, "Invalid command. See HELP for usage\n");
            break;
        case CMD_HELP:
            // Display help information
            printf("Available commands:\n"
                   "  CREATE <event_id> <num_rows> <num_columns>\n"
                   "  RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n"
                   "  SHOW <event_id>\n"
                   "  LIST\n"
                   "  WAIT <delay_ms> [thread_id]\n" // thread_id is not
                                                     // implemented
                   "  BARRIER\n"                     // Not implemented
                   "  HELP\n");
            break;
        case CMD_BARRIER:
            pthread_mutex_lock(
                &barrier_mutex); // Lock to protect barrier variables
            barrier_reached = true;
            printf("Barrier line on the cmd: %d\n", barrier_line);
            // initialize barrier variables
            barrier_count = 0;
            barrier_line = current_line + 1;
            pthread_mutex_unlock(&barrier_mutex);
            break;
        case CMD_EMPTY:
            // Handle EMPTY command
            break;
        case EOC:
            // End of file reached
            eof_flag = true;
            break;
        default:
            // Handle other command types if needed
            break;
        }
    }

    // Flush after processing each file
    fflush(stdout);
    printf("Saiu do Ciclo leitura de comando.\n");
    // Close the output file
    pthread_mutex_lock(&outf_mutex);
    close(out_fd);
    pthread_mutex_unlock(&outf_mutex);
    printf("Chegueiiii\n");
}

void *process_file_thread(void *arg) {
    struct ThreadData *thread_data = (struct ThreadData *)arg;

    // Construct the path to the job file
    char file_path[8197]; // PATH_MAX * 2 (para nao dar buffer overflow)
    snprintf(file_path, sizeof(file_path), "%s/%s.jobs", thread_data->argv,
             thread_data->base_name);

    int fd = thread_data->fd;

    // Parse the .jobs file
    parse_jobs_file(fd, thread_data->base_name, thread_data->argv,
                    thread_data->id, thread_data->max_thr,
                    thread_data->num_lines);

    // Exit the thread
    pthread_exit(NULL);
}

// Function to process all files in a directory
void process_directory(char argv[], int max_proc, int max_threads) {
    // Open the directory
    DIR *dir = opendir(argv);

    if (dir == NULL) {
        perror("Error opening directory");
        return;
    }

    struct dirent *entry;
    int active_processes = 0;
    int thread_ids[max_threads]; // Array to store thread IDs

    // Initialize thread IDs
    for (int i = 0; i < max_threads; ++i) {
        thread_ids[i] = i + 1;
    }

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

            // Open the job file
            int fd = open(file_path, O_RDONLY);
            if (fd == -1) {
                perror("Error opening job file");
                continue; // Move on to the next file
            }

            pid_t pid = fork();

            if (pid == 0) { // Child process
                pthread_t threads[max_threads];

                // Create thread data and populate it
                for (int i = 0; i < max_threads; ++i) {
                    // Allocate separate memory for each thread
                    struct ThreadData *thread_data =
                        (struct ThreadData *)malloc(sizeof(struct ThreadData));
                    thread_data->num_lines = numLines(file_path);
                    thread_data->id = thread_ids[i];
                    thread_data->max_thr = max_threads;
                    thread_data->fd = fd;
                    snprintf(thread_data->base_name,
                             sizeof(thread_data->base_name), "%s", base_name);
                    snprintf(thread_data->argv, sizeof(thread_data->argv), "%s",
                             argv);

                    // Create threads to process the file
                    if (pthread_create(&threads[i], NULL, process_file_thread,
                                       (void *)thread_data) != 0) {
                        perror("Error creating thread");
                        return;
                    }
                }

                // Wait for all threads to finish
                for (int i = 0; i < max_threads; ++i) {
                    pthread_join(threads[i], NULL);
                }

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
        fprintf(stderr, "Usage: %s <directory> [number] <max_threads>\n",
                argv[0]);
        return 1;
    }

    // Set the directory
    char *directory = argv[1];

    // Declare max_proc outside the if block
    int max_proc = 1; // Initialize to a default value, or any suitable default
    int max_threads = 1;

    // Check if the optional number argument is provided
    if (argc == 4) {
        char *endptr;
        max_proc = (int)strtoul(argv[2], &endptr, 10);
        max_threads = (int)strtoul(argv[3], &endptr, 10);
    }

    if (ems_init(state_access_delay_ms)) {
        fprintf(stderr, "Failed to initialize EMS\n");
        return 1;
    }

    // Process the directory
    process_directory(directory, max_proc, max_threads);

    // print status of the directory
    printf("Directory %s processed\n", directory);
    ems_terminate();
    return 0;
}