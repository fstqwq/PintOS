#include "vm/frame.h"
#include "vm/page.h"
#include <list.h>
#include "threads/palloc.h"
#include "threads/vaddr.h"


struct list frames;

void
frame_init() {
	lock_init(&frame_lock);
	list_init(&frames);
}

bool
frame_alloc(struct page *p) {
	lock_acquire (&frame_lock);

	p->frame = malloc(sizeof *p->frame);
	p->frame->base = palloc_get_page(PAL_USER);

	if (p->frame->base != NULL) {
		install_page(p->vaddr, p->frame->base, p->writable);
		p->frame->page = p;
		list_push_back(&frames, &p->frame->elem);
	}
	else {
		free(p->frame);
		p->frame = NULL;
		struct frame *entry, *victim = NULL;
		for (struct list_elem *e = list_begin(&frames); e != list_end(&frames); e = list_next(e)){
			entry = list_entry(e, struct frame, elem);
			if (entry->page == NULL) {
				victim = entry;
				break;
			}
			if (pagedir_is_accessed(entry->page->thread->pagedir, entry->page->vaddr)) {
				pagedir_set_accessed (entry->page->thread->pagedir, entry->page->vaddr, false);
				continue;
			}
			page_out(entry->page);
			victim = entry;
			break;
		}
		if (victim == NULL) {
			for (struct list_elem *e = list_begin(&frames); e != list_end(&frames); e = list_next(e)){
				entry = list_entry(e, struct frame, elem);
				if (entry->page == NULL) {
					victim = entry;
					break;
				}
				if (pagedir_is_accessed(entry->page->thread->pagedir, entry->page->vaddr)) {
					pagedir_set_accessed (entry->page->thread->pagedir, entry->page->vaddr, false);
					continue;
				}
				page_out(entry->page);
				victim = entry;
				break;
			}
		}

		p->frame = victim;
		p->frame->page = p;
		install_page(p->vaddr, p->frame->base, p->writable);
	}
	lock_release(&frame_lock);
	return true;
}

void
frame_free(struct frame *f) {
//	lock_acquire(&frame_lock);
	palloc_free_page(f->base);
	list_remove(&f->elem);
	free(f);
//	lock_release(&frame_lock);
}