# Event Management System

This program consists in a Operating Systems project designed to explore **processes**, **multi-threading**, **synchronization**, and **file programming** with **POSIX**. The primary goal is to read inputs from files, produce outputs, and allow the configuration of the number of active processes and threads for each process. Each process reads and writes the output for a specific file.
The Event Management System provides a foundation for managing events and reservations. It supports commands to create events, reserve seats, show the current state of events, list events, introduce delays, and synchronize threads using barriers.

## Prerequisites

 **gcc:** The GNU Compiler Collection.
 
 **make**

 ## Getting Started

 Clone the repository:
 ```
git clone https://github.com/your-username/ems.git
```
Run make to generate the ems executable and the binary files required.
```
make
```
Run the executable. Choose the directory of the input files (tests/ folder), the number of processes and threads active.
```
./ems (directory) [processes] [threads]
```
## Command Syntax

The program parses the following commands in the input files:

    CREATE <event_id> <num_rows> <num_columns>
    
        Create a new event with a specified ID, number of rows, and number of columns.
        CREATE 1 10 20
    
    RESERVE <event_id> [(<x1>,<y1>) (<x2>,<y2>) ...]
    
        Reserve one or more seats in an existing event.
        RESERVE 1 [(1,1) (1,2) (1,3)]
    
    SHOW <event_id>
    
        Print the current state of all seats in an event.
        SHOW 1
    
    LIST
    
        List all created events.
        LIST
    
    WAIT <delay_ms> [thread_id]
    
        Introduce a delay to all threads or a specific thread.
        WAIT 2000
    
    BARRIER
    
        Synchronize threads at a barrier.
        BARRIER
    
    HELP
    
        Display information about available commands.
        HELP

## Testing

 The tests folder contains input files with corresponding expected output files. Due to the non-deterministic nature of thread     execution, the actual output may vary unless a BARRIER command or one thread is assigned to each process.
