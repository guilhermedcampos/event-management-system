#ifndef JOBS_PROCESSOR_H
#define JOBS_PROCESSOR_H

#include "parser.h"  // Include the parser header

// Function to process batch commands from a file
int endsWith(const char *str, const char *suffix);

int process_jobs_directory();

#endif
