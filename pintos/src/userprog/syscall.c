#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include <threads/malloc.h>
#include "filesys/inode.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/directory.h"
#include "lib/user/syscall.h"
#include "userprog/pagedir.h"
#include "string.h"
#include "userprog/process.h"

static void syscall_handler (struct intr_frame *);
static void syscall_halt ();
static void syscall_exit (struct intr_frame *);
static int syscall_exec (struct intr_frame *);
static int syscall_wait (struct intr_frame *);
static bool syscall_create (struct intr_frame *);
static bool syscall_remove (struct intr_frame *);
static int syscall_open (struct intr_frame *);
static int syscall_filesize (struct intr_frame *);
static int syscall_read (struct intr_frame *);
static int syscall_write (struct intr_frame *);
static void syscall_seek (struct intr_frame *);
static int syscall_tell (struct intr_frame *);
static void syscall_close (struct intr_frame *);
static bool syscall_chdir (struct intr_frame *f);
static bool syscall_mkdir (struct intr_frame *f);
static bool syscall_readdir (struct intr_frame *f);
static bool syscall_isdir (struct intr_frame *f);
static int syscall_inumber (struct intr_frame *f);


void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void*
get_paddr(void *vaddr) {
  void *ret;
  if (!is_user_vaddr(vaddr) || !(ret = pagedir_get_page(thread_current()->pagedir, vaddr))) {
    syscall_exit_helper(-1);
    return 0;
  }
  return ret;
}

static bool
is_valid_addr(void *addr) {
  if (!is_user_vaddr(addr) || !pagedir_get_page(thread_current()->pagedir, addr)) {
    syscall_exit_helper(-1);
    return 0;
  }
  return 1;
}

static void*
pop_stack(int *esp, void *dst, int offset) {
  *((int *)dst) = *((int *)get_paddr(esp + offset));
}

static void
syscall_handler (struct intr_frame *f) 
{
  is_valid_addr(f->esp);
  int sysnum = *(int*)f->esp;
//  printf ("tid %d system call %d!\n", thread_current()->tid, sysnum);
  switch (sysnum) {
    case SYS_HALT: syscall_halt(); break;
    case SYS_EXIT: syscall_exit(f); break;
    case SYS_EXEC: f->eax = syscall_exec(f); break;
    case SYS_WAIT: f->eax = syscall_wait(f); break;
    case SYS_CREATE: f->eax = syscall_create(f); break;
    case SYS_REMOVE: f->eax = syscall_remove(f); break;
    case SYS_OPEN: f->eax = syscall_open(f); break;
    case SYS_FILESIZE: f->eax = syscall_filesize(f); break;
    case SYS_READ: f->eax = syscall_read(f); break;
    case SYS_WRITE: f->eax = syscall_write(f); break;
    case SYS_SEEK: syscall_seek(f); break;
    case SYS_TELL: f->eax = syscall_tell(f); break;
    case SYS_CLOSE: syscall_close(f); break;
#ifdef FILESYS
    case SYS_CHDIR: f->eax = syscall_chdir(f); break;
    case SYS_MKDIR: f->eax = syscall_mkdir(f); break;
    case SYS_READDIR: f->eax = syscall_readdir(f); break;
    case SYS_ISDIR: f->eax = syscall_isdir(f); break;
    case SYS_INUMBER: f->eax = syscall_inumber(f); break;
#endif

    default:
      printf("Not implemented system call %d!\n", sysnum);
  }
}

void
syscall_exit_helper(int status) {
  struct thread *t = thread_current();
  t->ret_status = status;
  struct child_process *ch = get_child_by_tid(&t->parent->children, t->tid);
  ch->ret_status = status;
  thread_exit();
}

static void
syscall_halt() {
  shutdown_power_off();
}

static void
syscall_exit(struct intr_frame *f) {
  int status;
  pop_stack(f->esp, &status, 1);
  syscall_exit_helper(status);
}

static int
syscall_exec(struct intr_frame *f) {
  char *file_name;
  pop_stack(f->esp, &file_name, 1);
  if (!is_valid_addr(file_name)) {
    return -1;
  }
  lock_acquire(&filesys_lock);

  char *fn_cp = malloc(strlen(file_name) + 1);
  char *token, *save_ptr;
  strlcpy(fn_cp, file_name, strlen(file_name) + 1);
  token = strtok_r(fn_cp, " ", &save_ptr);

  struct file *fi = filesys_open(token);

  if (fi == NULL) {
    lock_release(&filesys_lock);
    return -1;
  }
  else {
    file_close(fi);
    lock_release(&filesys_lock);
    return process_execute(file_name);
  }
}

static int
syscall_wait(struct intr_frame *f) {
  tid_t tid;
  pop_stack(f->esp, &tid, 1);
  return process_wait(tid);
}

static bool
syscall_create(struct intr_frame *f) {
  int size;
  char *file_name;
  pop_stack(f->esp, &size, 2);
  pop_stack(f->esp, &file_name, 1);
  if (!is_valid_addr(file_name))
    return false;

  bool ret;
  lock_acquire(&filesys_lock);
  ret = filesys_create(file_name, size);
  lock_release(&filesys_lock);
  return ret;
}

static bool
syscall_remove(struct intr_frame *f) {
  char *file_name;
  pop_stack(f->esp, &file_name, 1);
  if (!is_valid_addr(file_name))
    return false;

  bool ret;
  lock_acquire(&filesys_lock);
  ret = filesys_remove(file_name);
  lock_release(&filesys_lock);
  return ret;
}

static int
syscall_open(struct intr_frame *f) {
  char *file_name;
  pop_stack(f->esp, &file_name, 1);
  if (!is_valid_addr(file_name))
    return -1;

  lock_acquire(&filesys_lock);
  struct file *fi = filesys_open(file_name);
  lock_release(&filesys_lock);

  if (fi == NULL) {
    return -1;
  }
  else {
    struct fd_t *fd_e = malloc(sizeof(*fd_e));
    fd_e->ptr = fi;
    struct thread *t = thread_current();
    fd_e->fd = t->file_cnt++;
#ifdef FILESYS
    if (inode_is_dir(file_get_inode(fi))) {
      fd_e->opened_dir = dir_open(inode_reopen(file_get_inode(fi)));
    }
#endif
    list_push_back(&t->files, &fd_e->elem);
    return fd_e->fd;
  }
}

static struct fd_t*
get_file_by_fd(struct list* files, int fd) {
  struct fd_t *entry;
  for (struct list_elem *e = list_begin(files); e != list_end(files); e = list_next(e)) {
    entry = list_entry(e, struct fd_t, elem);
    if (entry->fd == fd)
      return entry;
  }
  return NULL;
}

static int
syscall_filesize(struct intr_frame *f) {
  int fd;
  pop_stack(f->esp, &fd, 1);
  lock_acquire(&filesys_lock);
  int ret = file_length(get_file_by_fd(&thread_current()->files, fd)->ptr);
  lock_release(&filesys_lock);
  return ret;
}

static int
syscall_read(struct intr_frame *f) {
  int size;
  char *buffer;
  int fd;

  pop_stack(f->esp, &size, 3);
  pop_stack(f->esp, &buffer, 2);
  pop_stack(f->esp, &fd, 1);

  if (!is_valid_addr(buffer))
    return -1;

  if (fd == 0) {
    for (int i = 0; i < size; ++i) {
      buffer[i] = input_getc();
    }
    return size;
  }
  else {
    struct fd_t* fd_e = get_file_by_fd(&thread_current()->files, fd);
    if (fd_e == NULL)
      return -1;
    else {
      lock_acquire(&filesys_lock);
      int ret = file_read(fd_e->ptr, buffer, size);
      lock_release(&filesys_lock);
      return ret;
    }
  }
}

static int
syscall_write(struct intr_frame *f) {
  int size;
  char *buffer;
  int fd;

  pop_stack(f->esp, &size, 3);
  pop_stack(f->esp, &buffer, 2);
  pop_stack(f->esp, &fd, 1);

  if (!is_valid_addr(buffer)) {
    return -1;
  }

  if (fd == STDOUT_FILENO) {
    putbuf(buffer, size);
    return size;
  }
  else {
    struct fd_t* fd_e = get_file_by_fd(&thread_current()->files, fd);
    if (fd_e == NULL || inode_is_dir (file_get_inode(fd_e->ptr))) {
      return -1;
    }
    else {
      lock_acquire(&filesys_lock);
      int ret = file_write(fd_e->ptr, buffer, size);
      lock_release(&filesys_lock);
      return ret;
    }
  }
}

static void
syscall_seek(struct intr_frame *f) {
  int fd, pos;
  pop_stack(f->esp, &pos, 5);
  pop_stack(f->esp, &fd, 4);

  lock_acquire(&filesys_lock);
  file_seek(get_file_by_fd(&thread_current()->files, fd)->ptr, pos);
  lock_release(&filesys_lock);
}

static int
syscall_tell(struct intr_frame *f) {
  int fd;
  pop_stack(f->esp, &fd, 1);

  lock_acquire(&filesys_lock);
  struct fd_t* fd_e = get_file_by_fd(&thread_current()->files, fd);
  int ret;
  if (fd_e == NULL || inode_is_dir (file_get_inode(fd_e->ptr))) {
    ret = -1;
  } else {
    ret = file_tell(fd_e->ptr);
  }
  lock_release(&filesys_lock);

  return ret;
}

static void
syscall_close(struct intr_frame *f) {
  int fd;
  pop_stack(f->esp, &fd, 1);
  struct fd_t *entry = get_file_by_fd(&thread_current()->files, fd);
  if (entry != NULL) {
    lock_acquire(&filesys_lock);
#ifdef FILESYS
    if (inode_is_dir (file_get_inode(entry->ptr)))
      dir_close (entry->opened_dir);
#endif
    file_close(entry->ptr);
    list_remove(&entry->elem);
    free(entry);
    lock_release(&filesys_lock);
  }
}

#ifdef FILESYS

static bool 
syscall_chdir (struct intr_frame *f) {
  char * path_name;
  pop_stack (f->esp, &path_name, 1);
  if (path_name == NULL || strlen (path_name) == 0) {
    return false;
  }
  char * pure_name = calloc (READDIR_MAX_LEN + 1, 1);
  bool is_dir;
  struct dir* dir;
  bool success = false;
  if (filesys_is_root_dir (path_name)) {
    dir_close ( thread_current () -> dir);
    thread_current () -> dir = dir_open_root ();
    success = true;
  } else {
    if (filesys_path_parse (path_name, &dir, &pure_name, &is_dir)) {
      struct dir * subdir = dir_subdir_lookup (dir, pure_name);
      if (subdir != NULL) {
        dir_close (thread_current () -> dir);
        thread_current () -> dir = subdir;
        success = true;
      }
      dir_close (dir);
    }
  }
  free (pure_name);
  return success;
}

static bool
syscall_mkdir (struct intr_frame *f) {
  char * path_name;
  pop_stack (f->esp, &path_name, 1);
  if (path_name == NULL || strlen (path_name) == 0) {
    return false;
  }
  char * pure_name = calloc (READDIR_MAX_LEN + 1, 1);
  bool is_dir;
  struct dir* dir;
  bool success = false;
  if (! filesys_is_root_dir (path_name) && filesys_path_parse (path_name, &dir, &pure_name, &is_dir)) {
    success = dir_subdir_create (dir, pure_name);
    dir_close (dir);
  }
  free (pure_name);
  return success;
}

static bool
syscall_readdir (struct intr_frame *f) {
  int fd;
  char * name;
  pop_stack (f->esp, &fd, 1);
  pop_stack (f->esp, &name, 2);
  if (fd <= 1 || name == NULL)
    return false;
  struct fd_t * fd_e = get_file_by_fd(&thread_current()->files, fd);
  bool success = false;
  if (fd_e != NULL && dir_is_dirfile (fd_e)) {
    success = dir_readdir (fd_e->opened_dir, name);
  }
  return success;
}

static bool
syscall_isdir (struct intr_frame *f) {
  int fd;
  pop_stack (f->esp, &fd, 1);
  if (fd <= 1) return false;
  struct fd_t * fd_e = get_file_by_fd(&thread_current()->files, fd);
  return fd_e != NULL && dir_is_dirfile (fd_e);
}

static int
syscall_inumber (struct intr_frame *f) {
  int fd;
  pop_stack (f->esp, &fd, 1);
  if (fd <= 1) return -1;
  struct fd_t * fd_e = get_file_by_fd (&thread_current()->files, fd);
  if (fd_e != NULL) 
    return inode_get_inumber (file_get_inode (fd_e->ptr));
  return -1;
}


#endif