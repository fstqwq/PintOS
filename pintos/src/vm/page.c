#include "vm/page.h"
#include "vm/frame.h"
#include "threads/vaddr.h"

#define STACK_MAX (8 * 1024 * 1024)

bool
install_page(void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	return (pagedir_get_page (t->pagedir, upage) == NULL
	        && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

struct page *
page_for_addr(void *vaddr) {
	struct page p;
	struct hash_elem *e;

	p.vaddr = (void*)pg_round_down(vaddr);
	e = hash_find(thread_current()->pages, &p.elem);

	if (e != NULL) {
		return hash_entry(e, struct page, elem);
	}

	if ((p.vaddr > PHYS_BASE - STACK_MAX) && ((void *)thread_current()->esp - 32 <= vaddr)) {
		return page_alloc(p.vaddr, true);
	}
	return NULL;
}

bool
page_in(void *vaddr) {
	struct page *p = page_for_addr(vaddr);
	if (p == NULL) {
		return false;
	}
	frame_alloc(p);
	if (p->frame == NULL) {
		return false;
	}
	lock_acquire(&frame_lock);
	if (p->sector != (block_sector_t) -1) {
		lock_acquire(&filesys_lock);
		swap_in(p);
		lock_release(&filesys_lock);
	}
	else if (p->file != NULL) {
		lock_acquire(&filesys_lock);
		off_t read_bytes = file_read_at(p->file, p->frame->base, p->read_bytes, p->file_offset);
		lock_release(&filesys_lock);
		off_t zero_bytes = PGSIZE - read_bytes;
		memset (p->frame->base + read_bytes, 0, zero_bytes);
	}
	else {
		memset (p->frame->base, 0, PGSIZE);
	}
	lock_release(&frame_lock);
	return true;
}

bool
page_out (struct page *p)
{
	bool dirty;
	bool success = false;

	pagedir_clear_page(p->thread->pagedir, (void *) p->vaddr);
	dirty = pagedir_is_dirty (p->thread->pagedir, (const void *) p->vaddr);
	if (p->file == NULL) {
		success = swap_out(p);
	}
	else {
		if (dirty) {
			if (p->private) {
				lock_acquire(&filesys_lock);
				success = swap_out(p);
				lock_release(&filesys_lock);
			}
			else {
				lock_acquire(&filesys_lock);
				success = file_write_at(p->file, (const void *) p->frame->base, p->read_bytes, p->file_offset);
				lock_release(&filesys_lock);
			}
		}
		else {
			success = true;
		}
	}
	if (success) {
		p->frame = NULL;
	}
	return success;
}

struct page *
page_alloc(void *vaddr, bool writable) {
	lock_acquire(&frame_lock);
	struct thread *t = thread_current();
	struct page *p = malloc(sizeof *p);

	if (p != NULL) {
		p->vaddr = vaddr;
		p->writable = writable;
		p->frame = NULL;

		p->private = writable;
		p->file = NULL;
		p->file_offset = 0;
		p->read_bytes = 0;

		p->sector = (block_sector_t) -1;

		p->thread = t;
		if (hash_insert(t->pages, &p->elem) != NULL) {
			free (p);
			p = NULL;
		}
	}
	lock_release(&frame_lock);

	return p;
}

void
page_free(struct page *p) {
	lock_acquire(&frame_lock);
	struct thread *t = thread_current();
	if (p->frame != NULL) {
		struct frame *f = p->frame;
		if (p->file && !p->private) {
			page_out(p);
		}
		else {
			pagedir_clear_page(thread_current()->pagedir, p->vaddr);
		}
		frame_free(f);
	}
	hash_delete(t->pages, &p->elem);
	free(p);
	lock_release(&frame_lock);
}

static void
destroy_page(struct hash_elem *e, void *aux UNUSED) {
	struct page *p = hash_entry (e, struct page, elem);
	if (p->frame != NULL) {
		pagedir_clear_page(thread_current()->pagedir, p->vaddr);
		frame_free(p->frame);
	}
	hash_delete(thread_current()->pages, &p->elem);
	free(p);
}

void
page_exit() {
	struct hash *h = thread_current()->pages;
	if (h != NULL)
		hash_destroy(h, destroy_page);
}

unsigned
page_hash(const struct hash_elem *e, void *aux UNUSED) {
	const struct page *p = hash_entry(e, struct page, elem);
	return pg_no(p->vaddr);
}

bool
page_less(const struct hash_elem *lhs, const struct hash_elem *rhs, void *aux UNUSED) {
	const struct page *plhs = hash_entry(lhs, struct page, elem);
	const struct page *prhs = hash_entry(rhs, struct page, elem);

	return plhs->vaddr < prhs->vaddr;
}