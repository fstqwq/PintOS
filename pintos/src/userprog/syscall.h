#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <list.h>

struct fd_t {
	struct file *ptr;
	int fd;
#ifdef FILESYS
	struct dir* opened_dir;
#endif
	struct list_elem elem;
};

void syscall_init (void);

void syscall_exit_helper(int);

#endif /* userprog/syscall.h */
