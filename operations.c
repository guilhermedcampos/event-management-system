#define _GNU_SOURCE
#include "eventlist.h"
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Create a rwlock for event list
pthread_rwlock_t event_list_rwlock = PTHREAD_RWLOCK_INITIALIZER;

// Create an reservation id lock
pthread_mutex_t reservation_id_lock = PTHREAD_MUTEX_INITIALIZER;

/// Represents an event.
static struct EventList *event_list = NULL;

static unsigned int state_access_delay_ms = 0;

/// Calculates a timespec from a delay in milliseconds.
/// @param delay_ms Delay in milliseconds.
/// @return Timespec with the given delay.
static struct timespec delay_to_timespec(unsigned int delay_ms) {
    return (struct timespec){delay_ms / 1000, (delay_ms % 1000) * 1000000};
}

/// Gets the event with the given ID from the state.
/// @note Will wait to simulate a real system accessing a costly memory
/// resource.
// @param event_id The ID of the event to get.
/// @return Pointer to the event if found, NULL otherwise.
static struct Event *get_event_with_delay(unsigned int event_id) {
    struct timespec delay = delay_to_timespec(state_access_delay_ms);
    nanosleep(&delay, NULL); // Should not be removed

    return get_event(event_list, event_id);
}

/// Gets the seat with the given index from the state.
/// @note Will wait to simulate a real system accessing a costly memory
/// resource.
/// @param event Event to get the seat from.
/// @param index Index of the seat to get.
// @return Pointer to the seat.
static unsigned int *get_seat_with_delay(struct Event *event, size_t index) {
    struct timespec delay = delay_to_timespec(state_access_delay_ms);
    nanosleep(&delay, NULL); // Should not be removed

    return &event->data[index];
}

/// Gets the index of a seat.
/// @note This function assumes that the seat exists.
/// @param event Event to get the seat index from.
/// @param row Row of the seat.
/// @param col Column of the seat.
/// @return Index of the seat.
static size_t seat_index(struct Event *event, size_t row, size_t col) {
    return (row - 1) * event->cols + col - 1;
}

// Initialize the event list
int ems_init(unsigned int delay_ms) {
    if (event_list != NULL) {
        fprintf(stderr, "EMS state has already been initialized\n");
        return 1;
    }

    event_list = create_list();
    state_access_delay_ms = delay_ms;

    return event_list == NULL;
}

// Function to reset the event list
void reset_event_list() {
    if (event_list != NULL) {
        free_list(event_list);
        event_list = create_list();
    }
}

// Terminate the event list
int ems_terminate() {
    if (event_list == NULL) {
        fprintf(stderr, "EMS state must be initialized\n");
        return 1;
    }
    free_list(event_list);
    return 0;
}

// Create an event
int ems_create(unsigned int event_id, size_t num_rows, size_t num_cols) {
    if (event_list == NULL) {
        fprintf(stderr, "EMS state must be initialized\n");
        return 1;
    }

    // Lock the event list before reading the shared data
    pthread_rwlock_wrlock(&event_list_rwlock);

    if (get_event_with_delay(event_id) != NULL) {
        fprintf(stderr, "Event already exists\n");
        pthread_rwlock_unlock(&event_list_rwlock);
        return 1;
    }

    struct Event *event = malloc(sizeof(struct Event));

    if (event == NULL) {
        fprintf(stderr, "Error allocating memory for event\n");
        pthread_rwlock_unlock(&event_list_rwlock);
        return 1;
    }

    event->id = event_id;
    event->rows = num_rows;
    event->cols = num_cols;
    event->reservations = 0;
    event->data = malloc(num_rows * num_cols * sizeof(unsigned int));

    // Initialize mutexes for each seat
    event->mutexes = malloc(num_rows * num_cols * sizeof(pthread_mutex_t));
    for (size_t i = 0; i < num_rows * num_cols; i++) {
        pthread_mutex_init(&event->mutexes[i], NULL);
    }

    if (event->data == NULL) {
        fprintf(stderr, "Error allocating memory for event data\n");
        free(event);
        pthread_rwlock_unlock(&event_list_rwlock);
        return 1;
    }

    for (size_t i = 0; i < num_rows * num_cols; i++) {
        event->data[i] = 0;
    }

    if (append_to_list(event_list, event) != 0) {
        fprintf(stderr, "Error appending event to list\n");
        free(event->data);
        free(event);
        pthread_rwlock_unlock(&event_list_rwlock);
        return 1;
    }

    pthread_rwlock_unlock(&event_list_rwlock);
    return 0;
}

// Reserve seats
int ems_reserve(unsigned int event_id, size_t num_seats, size_t *xs,
                size_t *ys) {
    if (event_list == NULL) {
        fprintf(stderr, "EMS state must be initialized\n");
        return 1;
    }

    // Lock the event list before reading the shared data
    pthread_rwlock_rdlock(&event_list_rwlock);

    struct Event *event = get_event_with_delay(event_id);

    if (event == NULL) {
        fprintf(stderr, "Event not found\n");
        pthread_rwlock_unlock(&event_list_rwlock);
        return 1;
    }

    pthread_rwlock_unlock(&event_list_rwlock);

    unsigned int reservation_id = ++event->reservations;

    // Sort the seats by row and column and lock them in that order
    for (size_t i = 0; i < num_seats; i++) {
        for (size_t j = i + 1; j < num_seats; j++) {
            if (xs[i] > xs[j] || (xs[i] == xs[j] && ys[i] > ys[j])) {
                size_t temp = xs[i];
                xs[i] = xs[j];
                xs[j] = temp;

                temp = ys[i];
                ys[i] = ys[j];
                ys[j] = temp;
            }
        }
    }

    // Look if any seat is repeated
    for (size_t i = 0; i < num_seats; i++) {
        for (size_t j = i + 1; j < num_seats; j++) {
            if (xs[i] == xs[j] && ys[i] == ys[j]) {
                return 1;
            }
        }
    }

    // Lock seat mutexes
    for (size_t i = 0; i < num_seats; i++) {
        pthread_mutex_lock(&event->mutexes[seat_index(event, xs[i], ys[i])]);
    }

    size_t i = 0;
    for (; i < num_seats; i++) {
        size_t row = xs[i];
        size_t col = ys[i];

        if (row <= 0 || row > event->rows || col <= 0 || col > event->cols) {
            fprintf(stderr, "Invalid seat\n");
            break;
        }

        if (*get_seat_with_delay(event, seat_index(event, row, col)) != 0) {
            fprintf(stderr, "Seat already reserved\n");

            break;
        }

        *get_seat_with_delay(event, seat_index(event, row, col)) =
            reservation_id;
    }

    // Lock reservation id lock before reading the shared data, needed to
    // assure a decrement if reservation fails.
    pthread_mutex_lock(&reservation_id_lock);

    // If the reservation was not successful, free the seats that were reserved.
    if (i < num_seats) {
        event->reservations--;
        for (size_t j = 0; j < i; j++) {
            *get_seat_with_delay(event, seat_index(event, xs[j], ys[j])) = 0;
        }
        pthread_mutex_unlock(&reservation_id_lock);

        // Unlock seat mutexes
        for (size_t j = 0; j < num_seats; j++) {
            pthread_mutex_unlock(
                &event->mutexes[seat_index(event, xs[j], ys[j])]);
        }
        return 1;
    }
    pthread_mutex_unlock(&reservation_id_lock);

    // Unlock seat mutexes
    for (size_t j = 0; j < num_seats; j++) {
        pthread_mutex_unlock(&event->mutexes[seat_index(event, xs[j], ys[j])]);
    }
    return 0;
}

// Show the event
int ems_show(unsigned int event_id, int fd) {
    if (event_list == NULL) {
        fprintf(stderr, "EMS state must be initialized\n");
        return 1;
    }

    // Lock the event list before reading the shared data
    pthread_rwlock_rdlock(&event_list_rwlock);

    struct Event *event = get_event_with_delay(event_id);

    if (event == NULL) {
        fprintf(stderr, "Event not found\n");
        pthread_rwlock_unlock(&event_list_rwlock);
        return 1;
    }

    pthread_rwlock_unlock(&event_list_rwlock);

    // Lock every seat mutex before reading the shared data
    for (size_t i = 0; i < event->rows * event->cols; i++) {
        pthread_mutex_lock(&event->mutexes[i]);
    }

    for (size_t i = 1; i <= event->rows; i++) {
        for (size_t j = 1; j <= event->cols; j++) {
            unsigned int *seat =
                get_seat_with_delay(event, seat_index(event, i, j));

            char seat_str[64];
            snprintf(seat_str, 64, "%u ", *seat);

            // Write the formatted seat string to the file
            write(fd, seat_str, strlen(seat_str));
        }

        // Add a newline after each row
        char newline = '\n';
        write(fd, &newline, 1);
    }

    // Unlock the seat mutex after reading the shared data
    for (size_t i = 0; i < event->rows * event->cols; i++) {
        pthread_mutex_unlock(&event->mutexes[i]);
    }
    return 0;
}

// List all events
int ems_list_events(int fd) {
    if (event_list == NULL) {
        fprintf(stderr, "EMS state must be initialized\n");
        return 1;
    }

    pthread_rwlock_rdlock(&event_list_rwlock);

    if (event_list->head == NULL) {
        write(fd, "No events\n", strlen("No events\n"));
        pthread_rwlock_unlock(&event_list_rwlock);
        return 0;
    }

    struct ListNode *current = event_list->head;
    while (current != NULL) {
        char buffer[64];
        int length = snprintf(buffer, sizeof(buffer), "Event: %u\n",
                              (current->event)->id);
        write(fd, buffer, (size_t)length);
        current = current->next;
    }
    pthread_rwlock_unlock(&event_list_rwlock);
    return 0;
}

// Wait for a delay
void ems_wait(unsigned int delay_ms) {
    struct timespec delay = delay_to_timespec(delay_ms);
    nanosleep(&delay, NULL);
}

// Print help message
int ems_help(int fd) {
    char *help_str = "Available commands:\n"
                     "  CREATE <event_id> <num_rows> <num_columns>\n"
                     "  RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]\n"
                     "  SHOW <event_id>\n"
                     "  LIST\n"
                     "  WAIT <delay_ms> [thread_id]\n"
                     "  BARRIER\n"
                     "  HELP\n";

    write(fd, help_str, strlen(help_str));

    return 0;
}