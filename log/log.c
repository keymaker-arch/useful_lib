#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#define LOG_GREETING "[++] Log module loaded\n"
int __log_fd;

int log_init(int fd)
{
    if (write(fd, LOG_GREETING, sizeof(LOG_GREETING)) != sizeof(LOG_GREETING)) {
        perror("[--] log_init: write failed");
        exit(-1);
    }
    __log_fd = fd;
    return 0;
}