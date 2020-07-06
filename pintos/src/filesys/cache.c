/* 
  Thanks Yuxuan Chen for helping us understand the algorithm.
*/

#include <lib/stdbool.h>
#include <lib/stdio.h>
#include <lib/string.h>
#include <threads/synch.h>
#include <devices/timer.h>
#include "cache.h"
#include "filesys.h"

#define CACHE_SIZE 64

struct cache_block {
  unsigned char buffer[BLOCK_SECTOR_SIZE];
  block_sector_t sector_index;
  bool used;
  bool recent;
  bool dirty;
};

static struct cache_block caches[CACHE_SIZE];
static struct lock cache_lock;

int used_cnt = 0, current_cache = 0;

#define next_cache(x) (((x) + 1) % CACHE_SIZE)
#define read_fs(cache) (block_read (fs_device, sector, cache.buffer))
#define write_fs(cache) (block_write (fs_device, cache.sector_index, cache.buffer))
#define occupy_cache(cache, sector) do {used_cnt ++; \
                                cache.used = true; \
                                cache.sector_index = sector; } while (false)
static int cache_get_free (void);

static int
cache_get_free () { // only called by locked func
  current_cache = next_cache(current_cache);
  while (caches[current_cache].used && caches[current_cache].recent) {
    caches[current_cache].recent = false;
    current_cache = next_cache(current_cache);
  }
  if (caches[current_cache].used) {
    if (caches[current_cache].dirty) {
      write_fs(caches[current_cache]);
      caches[current_cache].dirty = false;
    }
    caches[current_cache].used = false;
    used_cnt --;
  }
  return current_cache;
}

void
cache_init () {
  lock_init (&cache_lock);
  used_cnt = 0;
  current_cache = -1;
  memset(caches, 0, sizeof caches);
}

void
cache_read (block_sector_t sector, void *buffer) {
  lock_acquire (&cache_lock);
  int i;
  for (i = 0; i < CACHE_SIZE; ++i)
    if (caches[i].used && caches[i].sector_index == sector)
      break;
  if (i == CACHE_SIZE) {
    i = cache_get_free ();
    occupy_cache(caches[i], sector);
    read_fs(caches[i]);
  }
  caches[i].recent = true;
  memcpy (buffer, caches[i].buffer, BLOCK_SECTOR_SIZE);
  lock_release (&cache_lock);
}

void
cache_write (block_sector_t sector, const void *buffer) {
  lock_acquire (&cache_lock);
  int i;
  for (i = 0; i < CACHE_SIZE; ++i)
    if (caches[i].used && caches[i].sector_index == sector) 
      break;
  if (i == CACHE_SIZE) {
    i = cache_get_free ();
    occupy_cache(caches[i], sector);
  }
  caches[i].dirty = true;
  caches[i].recent = true;
  memcpy (caches[i].buffer, buffer, BLOCK_SECTOR_SIZE);
  lock_release (&cache_lock);
}

void
cache_done () {
  int i;
  for (i = 0; i < CACHE_SIZE; ++i)
    if (caches[i].used && caches[i].dirty)
      write_fs (caches[i]);
}