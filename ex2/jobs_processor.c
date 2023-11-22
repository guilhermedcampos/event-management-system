/*
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h> 

#define JOBS_DIR "jobs"  // name of the directory

int endsWith(const char *str, const char *suffix) {
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);

    if (str_len < suffix_len) {
        return 0;
    }

    return strcmp(str + (str_len - suffix_len), suffix) == 0;
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

                // Open the job file
                int fd = open(file_path, O_RDONLY);
                if (fd == -1) {
                    perror("Error opening job file");
                    continue;  // Move on to the next file
                }

                // TODO: Add your processing logic for the opened job file
                // ...

                // Close the file descriptor when done
                close(fd);
            }
    }

    closedir(dir);
}

int main() {
    process_jobs_directory();
    return 0;
}
*/