#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern int __log_fd;

// General logging function
#define PRINT_LOG(level, includeErrno, x...) \
  do { \
    dprintf(__log_fd, "[%s] [%s:%d] ", level, __FILE__, __LINE__); \
    dprintf(__log_fd, x); \
    if (includeErrno) { \
      dprintf(__log_fd, ": %s", strerror(errno)); \
    } \
    dprintf(__log_fd, "\n"); \
  } while (0)

// Specific log level macros
#ifdef DEBUG
#define DEBUGFD(x...)   PRINT_LOG("DEBUG", 0, x)
#else
#define DEBUGFD(x...)   do {} while (0)
#endif

#define INFO(x...)     PRINT_LOG("LOG", 0, x)
#define WARN(x...)    PRINT_LOG("WARN", 0, x)
#define WARN_ERR(x...) PRINT_LOG("WARN", 1, x)
#define ERROR(x...)   PRINT_LOG("ERROR", 0, x)
#define ERROR_ERR(x...) PRINT_LOG("ERROR", 1, x)
#define FATAL(x...)   { PRINT_LOG("FATAL", 0, x); exit(-1); }
#define FATAL_ERR(x...) { PRINT_LOG("FATAL", 1, x); exit(-1); }


/**
 * initialize the log file descriptor
 * @fd: file descriptor of the log file
 * @return: 0 on success, -1 on error
*/
int log_init(int fd);