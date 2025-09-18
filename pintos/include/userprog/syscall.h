#include <stdbool.h>

void syscall_init (void);
void check_address(void *addr);
void sys_halt(void);
void sys_exit(int status);
int sys_write(int fd, const void *buffer, unsigned size);
