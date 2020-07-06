#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "vm/page.h"

struct frame {
	void *base;
	struct page *page;
	struct list_elem elem;
};

struct lock frame_lock;

void frame_init();
bool frame_alloc(struct page *p);
void frame_free(struct frame *f);
#endif