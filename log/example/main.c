#include "../log.h"

int main()
{
    log_init(1);
    INFO("Hello, world: %d", 42);
    WARN("Hello, world: %d", 42);
    WARN_ERR("Hello, world: %d", 42);
    ERROR("Hello, world: %d", 42);
    ERROR_ERR("Hello, world: %d", 42);
    FATAL("Hello, world: %d", 42);
    return 0;
}