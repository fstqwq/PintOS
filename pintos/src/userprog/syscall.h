#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>

struct fd_t {
	struct file *ptr;
	int fd;
	struct list_elem elem;
};

void syscall_init (void);

#endif /* userprog/syscall.h */
