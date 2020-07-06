#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "userprog/syscall.h"
#include "threads/thread.h"
#include "file.h"
#include "free-map.h"

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  if (inode_create (sector, entry_cnt * sizeof (struct dir_entry))) {
    struct inode * inode = inode_open (sector);
    inode_set_dir (inode);
    struct dir * dir = dir_open (inode);
    dir_add(dir, ".", sector);
    dir_add(dir, "..", sector);
    dir_close(dir);
    return true;
  }
  return false;
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 2 * sizeof (struct dir_entry); // jump '.' and '..' for readdir
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

  /* Write .. infomation for true subdir */
  if (success && inode_sector != inode_get_inumber (dir->inode)) {
    struct inode* inode = inode_open (inode_sector);
    if(inode_is_dir (inode) || (inode_close (inode), 0)) {
      struct dir *subdir = dir_open (inode);
      lookup (subdir, "..", &e, &ofs);
      e.inode_sector = inode_get_inumber(dir->inode);
      inode_write_at(subdir->inode, &e, sizeof e, ofs);
      dir_close (subdir);
    }
  }
 done:
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (dir->inode != NULL);
  ASSERT (dir->pos > 0);

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}


bool
dir_subdir_create (struct dir* dir, const char* name) {
  block_sector_t sector = -1u;
  if (  dir != NULL
    &&  name != NULL
    &&  strlen(name) > 0
    &&  free_map_allocate(1, &sector)
    &&  dir_create(sector, 0)
    &&  dir_add(dir, name, sector)
    ) {
    return true;
  }
  if (sector != -1u) {
    free_map_release(sector, 1);
  }
  return false;
}

struct dir*
dir_subdir_lookup (struct dir* dir, const char* name) {
  struct inode* inode = NULL;
  if (  dir != NULL
    &&  name != NULL
    &&  strlen(name) > 0
    &&  dir_lookup(dir, name, &inode)
    &&  inode != NULL
    &&  (inode_is_dir(inode) || (inode_close(inode), 0))
    ) {
    return dir_open(inode);
  }
  return NULL;
}

bool
dir_subdir_delete (struct dir* dir, const char* name) {
  struct inode* inode = NULL;
  if (  dir != NULL
    &&  name != NULL
    &&  strlen(name) > 0
    &&  dir_lookup(dir, name, &inode)
    &&  inode != NULL
    &&  ( (
              inode_is_dir(inode)
          &&  inode_get_inumber(inode) != inode_get_inumber(dir_get_inode(thread_current()->dir))
          &&  inode_get_open_cnt(inode) <= 1
          )
        || (inode_close(inode), 0)
        )
    ) {
      struct dir* checker = dir_open(inode);
      char* buffer = calloc(NAME_MAX + 1, 1);
      bool not_empty = dir_readdir (checker, buffer);
      dir_close (checker);
      free (buffer);
      if (!not_empty) {
        return dir_remove (dir, name);
      }
  }
  return false;
}

bool
dir_subfile_create(struct dir* dir, const char* name, off_t initial_size) {
  block_sector_t sector = -1u;
  if (  dir != NULL
    &&  name != NULL
    &&  strlen(name) > 0
    &&  free_map_allocate(1, &sector)
    &&  inode_create(sector, initial_size)
    &&  dir_add(dir, name, sector)
    ) {
    return true;
  }
  if (sector != -1u) {
    free_map_release(sector, 1);
  }
  return false;
}

struct file*
dir_subfile_lookup(struct dir* dir, const char* name) {
  struct inode* inode = NULL;
  if (  dir != NULL
    &&  name != NULL
    &&  strlen(name) > 0
    &&  dir_lookup(dir, name, &inode)
    &&  inode != NULL
    &&  (!inode_is_dir(inode) || (inode_close(inode), 0))
    ) {
    struct file* file = file_open(inode);
    file_set_dir(file, dir_reopen(dir));
    return file;
  }
  return NULL;
}

bool
dir_subfile_delete(struct dir* dir, const char* name) {
  struct inode* inode = NULL;
  if (  dir != NULL
    &&  name != NULL
    &&  strlen(name) > 0
    &&  dir_lookup(dir, name, &inode)
    &&  inode != NULL
    &&  (!inode_is_dir(inode) || (inode_close(inode), 0))
    ) {
    inode_close(inode);
    return dir_remove(dir, name);
  }
  return false;
}

bool
dir_is_dirfile(struct fd_t* h) {
  return inode_is_dir(file_get_inode(h->ptr));
}