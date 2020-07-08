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

    page -> swapped_out = false;

    file_read_at(page->file_to_write, kva, page->byte_to_write, page->offset);

    return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_map_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
    struct thread *current = thread_current();


        if (pml4_is_dirty(current -> pml4, page -> va)){ // 도대체 dirty bit은 어디서 설정해주지?
            
            if (page -> writable){ 
            
                file_write_at(page -> file_to_write, page->va, page -> byte_to_write, page -> offset);
                pml4_set_dirty(current->pml4, page->va, false);
            }
        }

        page -> swapped_out = true;

    return true;
        //file_close(page->file_to_write);
}

/* Destory the file mapped page. PAGE will be freed by the caller. */
static void
file_map_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
    struct thread *current = thread_current();


        if (pml4_is_dirty(current -> pml4, page -> va)){ // 도대체 dirty bit은 어디서 설정해주지?
            
            if (page -> writable){ 
                
             
                file_write_at(page -> file_to_write, page->va, page -> byte_to_write, page -> offset);
                
            }
        }

        file_close(page->file_to_write);

        if (!page -> swapped_out){
            free(page -> frame);
        }
		
        free(page -> info);
    
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
    
    page -> byte_to_write = read_num;
    
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

		if (!vm_alloc_page_with_initializer (VM_FILE, addr, writable, lazy_load_segment2, info)) //aux에 page_read_bytes, file, writable, page_zero_bytes passing
			return NULL;

        // munmap을 할때를 위한 정보를 저장
        struct page *page = spt_find_page(&thread_current() -> spt, addr);
        page -> num_pages = num_pages;
        page -> unmapped = false; //for implicitly unmap
        page -> offset = offset;
        page -> unmap_addr = addr_2;
        struct file *file_to_write = file_reopen(_file);
        //page -> file_to_write = file_to_write; //이렇게 그냥 넘겨줘도 되나?
        page -> file_to_write = file_to_write;
        //printf("%p\n", page -> file_to_write);
        //do_nothing2();

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
    struct supplemental_page_table spt = thread_current() -> spt;
    struct page *page = spt_find_page(&spt, addr);

    if ((page -> operations) -> type != VM_FILE){
        PANIC("panic while munmap syscall : try to munmap page which doesn't have type VM_FILE");
    }

    int num_pages = page -> num_pages;

    for (int i = 0; i < num_pages; i++) {
        struct page *p = spt_find_page(&spt, addr + PGSIZE * i); // page들은 uninit이거나 file
        if (p == NULL)
            PANIC("panic while do_munmap : spt_find_page is NULL");


        spt_remove_page(&spt, p); // hash_delete + dealloc_page


        // page안을 비우고 page를 free해주고 spt에서 삭제
        // 이 과정에서 writeback을 해야 할 수도 있음
    }


}
