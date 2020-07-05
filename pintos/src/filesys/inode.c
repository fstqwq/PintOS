#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include <stdlib.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "cache.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
/* Supporting 8MB file,
    8MB = 128 * 128 * 512
    
    inode --> L1 --> L2 --> content

    It may be a good solution if we put L1 in unused, but efficiency is not that important.
 */
#define TABLE_SIZE  128

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t table;               /* L1 Table for data sector. */
    off_t length;                       /* File size in bytes. */
    bool is_dir;                         /* is dir */
    uint8_t unused[499];               /* Not used. */
    unsigned magic;                     /* Magic number. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };

/*
   L1    L2    off
[23:17][16:10][9:0]
*/
#define byte_to_l1_table(pos) (((pos) >> 16) & (TABLE_SIZE - 1))
#define byte_to_l2_table(pos) (((pos) >>  9) & (TABLE_SIZE - 1))

static char zeros[BLOCK_SECTOR_SIZE];
static char ones [BLOCK_SECTOR_SIZE];

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (struct inode *inode, off_t pos, bool write) 
{
  ASSERT (inode != NULL);
/*
  if (pos < inode->data.length)
    return inode->data.table + pos / BLOCK_SECTOR_SIZE;
  else
    return -1;
*/
  block_sector_t *l1 = calloc(TABLE_SIZE, sizeof *l1);
  block_sector_t *l2 = calloc(TABLE_SIZE, sizeof *l2);
  block_sector_t ret = -1;
  if (pos >= inode->data.length) {
    if (!write)
      goto done;
    off_t i, j, l1_st, l1_ed, l2_st, l2_ed, l, r;
    l1_st = byte_to_l1_table(inode->data.length);
    l2_st = byte_to_l2_table(inode->data.length);
    l1_ed = byte_to_l1_table(pos);
    l2_ed = byte_to_l2_table(pos);
    
    cache_read (inode->data.table, l1);
    for (i = l1_st; i <= l1_ed; i++) {
      l = (i == l1_st ? l2_st : 0);
      r = (i == l1_ed ? l2_ed : TABLE_SIZE - 1);
      
      if (l1[i] == -1){
        if (!free_map_allocate (1, &l1[i]))
          goto done;
        cache_write (l1[i], ones); // table init to -1
      }
        
      cache_read (l1[i], l2);
      for (j = l; j <= r; j++) {
        if (l2[j] == -1) {
          if (!free_map_allocate (1, &l2[j]))
            goto done;
          cache_write (l2[j], zeros);
        }
      }
      cache_write (l1[i], l2);
    }
    cache_write (inode->data.table, l1);
    inode->data.length = pos + 1;
    cache_write (inode->sector, &inode->data);
  }

  cache_read (inode->data.table, l1);
  cache_read (l1[byte_to_l1_table (pos)], l2);
  ret = l2[byte_to_l2_table (pos)];

done:
  free(l1);
  free(l2);
  return ret;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  memset (ones, -1, sizeof ones);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->is_dir = false;
      if (free_map_allocate (1, &disk_inode->table)) 
        {
          cache_write (sector, disk_inode);
          cache_write (disk_inode->table, ones);
          if (length) {
            block_sector_t *l1 = calloc(TABLE_SIZE, sizeof *l1);
            block_sector_t *l2 = calloc(TABLE_SIZE, sizeof *l2);
            off_t i, j, l1_ed, l2_ed, l, r;

            l1_ed = byte_to_l1_table(length - 1);
            l2_ed = byte_to_l2_table(length - 1);
            
            cache_read (disk_inode->table, l1);
            for (i = 0; i <= l1_ed; i++) {
              l = 0;
              r = (i == l1_ed ? l2_ed : TABLE_SIZE - 1);
              
              if (l1[i] == -1){
                if (!free_map_allocate (1, &l1[i]))
                  goto done;
                cache_write (l1[i], ones); // table init to -1
              }
                
              cache_read (l1[i], l2);
              for (j = l; j <= r; j++) {
                if (l2[j] == -1) {
                  if (!free_map_allocate (1, &l2[j]))
                    goto done;
                  cache_write (l2[j], zeros);
                }
              }
              cache_write (l1[i], l2);
            }
            cache_write (disk_inode->table, l1);
            success = true;
          done:
            free(l1);
            free(l2);
          } else {
            success = true; 
          }
        } 
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  cache_read (inode->sector, &inode->data);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode* inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) {
        if (inode->data.length) {
          block_sector_t *l1 = calloc(TABLE_SIZE, sizeof *l1);
          block_sector_t *l2 = calloc(TABLE_SIZE, sizeof *l2);
          off_t i, j, l1_ed, l2_ed, l, r;

          l1_ed = byte_to_l1_table(inode->data.length - 1);
          l2_ed = byte_to_l2_table(inode->data.length - 1);
          
          cache_read (inode->data.table, l1);
          for (i = 0; i <= l1_ed; i++) {
            l = 0;
            r = (i == l1_ed ? l2_ed : TABLE_SIZE - 1);
            cache_read (l1[i], l2);
            for (j = l; j <= r; j++) {
              free_map_release (l2[j], 1);
            }
            free_map_release(l1[i], 1);
          }
          free(l1);
          free(l2);
        }
        free_map_release (inode->sector, 1);
        free_map_release (inode->data.table, 1);
      }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset, false);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          cache_read (sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          cache_read (sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset, true);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          cache_write (sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            cache_read (sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          cache_write ( sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

bool 
inode_is_dir (const struct inode *inode) {
  return inode->data.is_dir;
}

void
inode_set_dir (struct inode* inode) {
  inode->data.is_dir = true;
  cache_write (inode->sector, &inode->data);
}

int
inode_get_open_cnt (struct inode* inode) {
  return inode->open_cnt;
}