/* file.c: Implementation of memory mapped file object (mmaped object). */

#include "vm/vm.h"
#include "userprog/process.h"
#include "threads/vaddr.h"

static bool file_map_swap_in (struct page *page, void *kva);
static bool file_map_swap_out (struct page *page);
static void file_map_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_map_swap_in,
	.swap_out = file_map_swap_out,
	.destroy = file_map_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file mapped page */
bool
file_map_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

    (page->frame)->kva = kva;

	struct file_page *file_page = &page->file;

    return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_map_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_map_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file mapped page. PAGE will be freed by the caller. */
static void
file_map_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

static bool
lazy_load_segment2 (struct page *page, void *aux) {

    struct frame *frame = page->frame;
    if (frame == NULL) {
		//palloc_free_page(frame -> kva);
		PANIC("panic while loading file : fail to get frame.");
		return false;
	}
    
    
    struct info_for_lazy *info = page->info;

    //printf("%d\n", file_read_at(info->file, frame->kva, info->page_read_bytes, info->ofs));

    int read_num = file_read_at(info->file, frame->kva, info->page_read_bytes, info->ofs);
    if (read_num != (int) info->page_read_bytes) {
        memset(frame->kva + read_num, 0, info->page_read_bytes - read_num);
    }
    else{
        memset (frame->kva + info->page_read_bytes, 0, info->page_zero_bytes);
    }
    

    
    file_close(info->file);

    return true;
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
}





/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {

    void *addr_2 = addr;
    struct supplemental_page_table *spt = &thread_current ()->spt;

    int num_pages = length/PGSIZE;
    if( length % PGSIZE != 0){
        num_pages += 1;
    }

    for(int i = 0; i < num_pages; i++){ // 겹치지 않게, 겹친다면 null
        if(spt_find_page(spt, addr + PGSIZE * i) != NULL){
            return NULL;
        }
    }

    while (length > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = length < PGSIZE ? length : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
        struct info_for_lazy *info;
        info = (struct info_for_lazy *) malloc (sizeof (struct info_for_lazy));

        struct file *_file = file_reopen(file); // 이미 열려 있음, 닫히는거 방지

		(info->file) = _file;
        
        info->page_read_bytes = page_read_bytes;
        info->ofs = offset;
        info->page_zero_bytes = page_zero_bytes;

		if (!vm_alloc_page_with_initializer (VM_FILE, addr,
					writable, lazy_load_segment2, info)) //aux에 page_read_bytes, file, writable, page_zero_bytes passing
			return NULL;

		/* Advance. */
		length -= page_read_bytes;
		
		addr += PGSIZE;
        offset += PGSIZE;

	}

    return addr_2;

}

/* Do the munmap */
void
do_munmap (void *addr) {
}
