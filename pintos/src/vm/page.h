#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include "vm/frame.h"
#include "userprog/process.h"
#include "filesys/off_t.h"
#include "devices/block.h"

struct page {
	void *vaddr;
	bool writable;
	struct thread *thread;
	struct hash_elem elem;

	struct frame *frame;

	bool private;

	block_sector_t sector;

	struct file *file;
	off_t file_offset;
	off_t read_bytes;
};

void page_init();
bool page_in (void *vaddr);
bool page_out (struct page *p);
struct page *page_for_addr(void *vaddr);
struct page *page_alloc(void *vaddr, bool writable);
void page_free(struct page *p);
void page_exit();


hash_hash_func page_hash;
hash_less_func page_less;

#endif
