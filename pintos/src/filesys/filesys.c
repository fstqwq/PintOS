#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "cache.h"
#include "threads/malloc.h"
#include "lib/user/syscall.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();
  cache_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
  cache_done ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) 
{
#ifndef FILESYS
  block_sector_t inode_sector = 0;
  struct dir *dir = dir_open_root ();
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size)
                  && dir_add (dir, name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);

  return success;
#else
  struct dir* dir;
  char *pure_name = calloc (READDIR_MAX_LEN + 1, 1);
  bool is_dir;
  bool success = false;
  if (name != NULL && strlen(name) > 0 && !filesys_is_root_dir (name) && filesys_path_parse (name, &dir, &pure_name, &is_dir)) {
    success = !is_dir && dir_subfile_create (dir, pure_name, initial_size);
    dir_close (dir);
  }
  free (pure_name);
  return success;
#endif
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
#ifndef FILESYS
  struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);

  return file_open (inode);
#else
  struct dir* dir;
  char *pure_name = calloc (READDIR_MAX_LEN + 1, 1);
  bool is_dir;
  struct file * file = NULL;
  if (name != NULL && strlen (name) > 0 && filesys_is_root_dir(name)) {
    file = file_open(inode_open(ROOT_DIR_SECTOR));
    file_set_dir(file, dir_open_root());
  }
  else if (name != NULL  && strlen (name) > 0 && !filesys_is_root_dir (name) && filesys_path_parse (name, &dir, &pure_name, &is_dir)) {
    struct dir * retdir;
    struct file * retfile;
    retdir = dir_subdir_lookup (dir, pure_name);
    retfile = dir_subfile_lookup (dir, pure_name);
    
    if (retdir != NULL) {
      file = file_open (inode_reopen (dir_get_inode (retdir)));
      file_set_dir (file, dir_reopen (dir));
      dir_close (retdir);
    } else {
      if (!is_dir) {
        file = retfile;
      }
    }
    dir_close (dir);
  }
  free (pure_name);
  return file;
#endif
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
#ifndef FILESYS
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir); 

  return success;
#else
  struct dir* dir;
  char *pure_name = calloc (READDIR_MAX_LEN + 1, 1);
  bool is_dir;
  bool success = false;
  if (name != NULL  && strlen (name) > 0 && !filesys_is_root_dir (name) && filesys_path_parse (name, &dir, &pure_name, &is_dir)) {
    success = dir_subdir_delete (dir, pure_name) || (!is_dir && dir_subfile_delete (dir, pure_name));
    dir_close (dir);
  }
  free (pure_name);
  return success;
#endif
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}


#define DELIM "/"

/* length and proper name check */
static bool
check_name(const char *name) {
  if (name == NULL)
    return false;
  for (int i = 0; i < READDIR_MAX_LEN + 1; i++) {
    switch (name[i]) {
      case '/':
        return false;
      case '\0':
        return true;
      default:
        break;
    }
  }
  return false;
}

/* name == "/" ? */
bool
filesys_is_root_dir (const char * name) {
  return name != NULL && name[0] == '/' && name[1] == '\0';
}

/*
name      : Path name that needed to parse.
dir       : On success, return the directory the file belong to. Should be closed by caller.
pure_name : On success, copy the name of file into *pure_name.
is_dir    : On success, indicate if path name is ended with slash.
*/
bool
filesys_path_parse (const char *name, struct dir **dir, char **pure_name, bool *is_dir) {
  int len;
  if (name == NULL || (len = strlen (name)) == 0 || (len == 1 && name[0] == '/'))
    return false;
  char *path = calloc (len + 1, 1);
  strlcpy (path, name, len + 1);
  if (path[len - 1] == '/') {
    *is_dir = true;
    path[len--] = 0;
  } else {
    *is_dir = false;
  }
  if (path[0] == '/') {
    *dir = dir_open_root ();
  } else {
    ASSERT (thread_current () -> dir != NULL);
    *dir = dir_reopen (thread_current ()->dir); // NPE for start up
  }
  char *token, *saveptr, *next;
  bool success = false;
  token = strtok_r (path, DELIM, &saveptr);
  for ( ; ; ) {
    if (!check_name (token)) {
      dir_close (*dir);
      break;
    }
    next = strtok_r (NULL, DELIM, &saveptr);
    if (next == NULL) {
      strlcpy (*pure_name, token, READDIR_MAX_LEN + 1);
      success = true;
      break;
    }
    struct dir *nxt = dir_subdir_lookup (*dir, token);
    dir_close (*dir);
    if (nxt == NULL) {
      break;
    }
    *dir = nxt;
    token = next;
  }
  free (path);
  return success;
}
