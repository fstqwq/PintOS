#ifndef FILESYS_DIRECTORY_H
#define FILESYS_DIRECTORY_H

#include <stdbool.h>
#include <stddef.h>
#include "devices/block.h"
#include "userprog/syscall.h"
#include "off_t.h"

/* Maximum length of a file name component.
   This is the traditional UNIX maximum length.
   After directories are implemented, this maximum length may be
   retained, but much longer full path names must be allowed. */
#define NAME_MAX 14

struct inode;

/* A directory. Moved from directory.h . */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };



/* Opening and closing directories. */
bool dir_create (block_sector_t sector, size_t entry_cnt);
struct dir *dir_open (struct inode *);
struct dir *dir_open_root (void);
struct dir *dir_reopen (struct dir *);
void dir_close (struct dir *);
struct inode *dir_get_inode (struct dir *);

/* Reading and writing. */
bool dir_lookup (const struct dir *, const char *name, struct inode **);
bool dir_add (struct dir *, const char *name, block_sector_t);
bool dir_remove (struct dir *, const char *name);
bool dir_readdir (struct dir *, char name[NAME_MAX + 1]);

bool dir_subdir_create (struct dir*, const char* name);
struct dir* dir_subdir_lookup (struct dir*, const char* name);
bool dir_subdir_delete (struct dir*, const char* name);

bool dir_subfile_create(struct dir*, const char* name, off_t initial_size);
struct file* dir_subfile_lookup(struct dir*, const char* name);
bool dir_subfile_delete(struct dir* , const char* name);

bool dir_is_dirfile(struct fd_t*);

#endif /* filesys/directory.h */
