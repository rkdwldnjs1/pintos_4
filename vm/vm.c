/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/vaddr.h"
#include "threads/synch.h"

#include "lib/kernel/hash.h"
#include "lib/kernel/list.h"

struct list frame_table;
struct lock frame_lock;
struct list_elem *cursor;
//struct swap_table *swap_table;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
    list_init(&frame_table);
    lock_init(&frame_lock);
    cursor = NULL;
    /*
    struct swap_table *swap_table = malloc(sizeof(struct swap_table));
    swap_table -> swap_map = bitmap_create(disk_size(swap_disk)/8);
    if (swap_table -> swap_map == NULL) // bitmap_create가 fail일 때
        PANIC("panic while vm_init : fail to bitmap_create");
    */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
        struct page *p;
        p = (struct page *) malloc (sizeof (struct page));
       
        switch(VM_TYPE(type)){
            case 1:
                //printf("%p\n", upage);
                uninit_new (p, upage, init, type, aux, anon_initializer);
                p->writable = writable;
                p->info = aux;
                p->init = init;
                p->swapped_out = false;
                break;
            case 2:
                uninit_new (p, upage, init, type, aux, file_map_initializer);
                p->writable = writable;
                p->info = aux;
                p->init = init;
                p->swapped_out = false;
                break;
            case 8:
                uninit_new (p, upage, init, type, aux, NULL);
                p->writable = writable;
                p->info = aux;
                p->init = init;
                p->swapped_out = false;
                break;
            default:
                
                goto err;
        }
/*
        uninit_new (&p, upage, init, type, aux,
		bool (*initializer)(struct page *, enum vm_type, void *))*/
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
        
        return spt_insert_page(spt, p);
		/* TODO: Insert the page into the spt. */
	}
err:
	return false;
}

/* Returns the page containing the given virtual address, or a null pointer if no such page exists. */
struct page *
page_lookup (struct hash pages, const void *address) {
    struct page p;
    struct hash_elem *e;
  
    //printf("7\n");
    p.va = address; // 비교 대상을 만드는 것
    e = hash_find (&pages, &p.hash_elem);
    //printf(e ? "true\n" : "false\n");
    return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt , void *va ) {
    //printf("6\n");

    struct hash pages = spt->pages;

	return page_lookup(pages, pg_round_down(va));
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt, struct page *page ) {
	
    struct hash pages = spt->pages;

    if( hash_insert (&pages, &page->hash_elem) == NULL ){ // 새로운거 추가 
        return true;
    }
    else{ // 이미 hash에 존재
        return false;
    }
	
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
    if (hash_delete(&spt -> pages, &page -> hash_elem) == NULL)
        PANIC("panic while spt_remove_page : fail to hash_delete");
	vm_dealloc_page (page);
	//return true;
}

struct list_elem *
next_cursor(struct list_elem *e){
    if(e==NULL){
        return list_begin(&frame_table);
    }
    else if(e==list_rbegin(&frame_table)){ // 끝이면 맨 앞으로 커서를 옮겨줌
        return list_begin(&frame_table);
    }
    else{
        return list_next(e);
    }
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {

    struct list_elem *current_cursor;

    if(list_empty(&frame_table)){
        lock_release(&frame_lock);
        PANIC("frame table empty");
        return NULL;
    }

    if (cursor == NULL){
        current_cursor = list_begin(&frame_table);
    }
    else if( cursor == list_rbegin(&frame_table)){ // 끝이면 맨 앞으로 커서를 옮겨줌
        current_cursor = list_begin(&frame_table);
    }
    else{
        current_cursor = cursor;
    }

    while(1){
        struct frame *frame = list_entry(current_cursor, struct frame, list_elem);

        if ( pml4_is_accessed (thread_current()->pml4 , (frame->page)->va) ){
            frame->counter = 0;
            pml4_set_accessed(thread_current()->pml4 , (frame->page)->va, false);
        }
        else{
            if(frame -> counter >= 1){
                frame->counter = 0;
                cursor = next_cursor(cursor); // 현재 cursor 가 가리키는 프레임 evict 될 것이므로
                //lock_release(&frame_lock);
                return frame;
            }
            else{
                frame->counter += 1;
                pml4_set_accessed(thread_current()->pml4 , (frame->page)->va, false);
            }
        }

        current_cursor = next_cursor(cursor); 
    }
	 /* TODO: The policy for eviction is up to you. */
    // Beware this func needs careful synchronization
	PANIC("no victim");
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {

    lock_acquire(&frame_lock);
	struct frame *victim = vm_get_victim ();
    //printf("10\n");
    //do_nothing();

    ASSERT(victim != NULL);

    if(swap_out(victim->page)){
        //printf("11\n");
        list_remove(&victim->list_elem);
        if(list_empty(&frame_table)){
            cursor = NULL;
        }
        //palloc_free_page(victim->kva);
        //victim->kva = NULL;
        
        pml4_clear_page(thread_current()->pml4, (victim->page)->va);


        lock_release(&frame_lock);
        return victim;
    }
	/* TODO: swap out the victim and return the evicted frame. */
    //printf("12\n");

    //pml4_clear_page : detach from page table
    lock_release(&frame_lock);
	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	//struct frame *frame = NULL;
    struct frame *frame;
    struct frame *victim;
    frame = (struct frame *) malloc (sizeof (struct frame));

    uint8_t *newpage;

    
	/* TODO: Fill this function. */
    newpage = palloc_get_page(PAL_USER | PAL_ZERO);
    if(newpage == NULL){
        //printf("8\n");
        victim = vm_evict_frame();
        //printf("9\n");
        if( victim == NULL){
            PANIC("can not evict");
        }
        return victim;
    }
    
    //printf("7\n");
    frame->kva = newpage; // kva와 매핑 맞나
    frame->counter = 0;
    frame->page = NULL;
    

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr) {

    bool success = false;
	void *stack_bottom = (void *) ((uint8_t *) pg_round_down(addr));
    
	if (!vm_alloc_page(VM_MARKER_0, stack_bottom, true)) { //이거 free는 언제 해줌?
        
		PANIC("panic during stack_growth");
	}
    
    success = vm_claim_page(stack_bottom);
    
    if(!success){
        PANIC("panic during stack_growth");
    }
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page ) {
    return page->writable == true;
}

void do_nothing(void) {
    return;
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f, void *addr,
		bool user, bool write , bool not_present ) {
	struct supplemental_page_table *spt = &thread_current ()->spt;

    bool limited_size = addr > (USER_STACK - 0x100000); //1MB
    uintptr_t rsp;

    if(!user){
        rsp = thread_current()->cur_rsp;
    }
    else{
        rsp = f->rsp;
    }
    

    if(!not_present){
        exit(-1);
    }

	struct page *page = spt_find_page (spt , addr);
    if(page == NULL && addr < USER_STACK){
        //printf("2\n");
        //printf("addr : %p\n", addr);
        
        if (!limited_size)
            exit(-1);
        
        if (user) { //user
            if (!is_user_vaddr(addr))
                exit(-1);
            
            if (write) {
                if(addr <= (rsp)-8 && addr > (rsp)-PGSIZE){
                
                    vm_stack_growth(addr);
                    return true;
                }
            }
            else if (!write) {
                if(addr <= (rsp)-8 && addr > (rsp)-PGSIZE){
                
                    vm_stack_growth(addr);
                    return true;
                }
            }
        }
        else if (!user) { //kernel
            //printf("3\n");
            if (write) {
                exit(-1);
                printf("addr : %p\n", addr);
                printf("rsp : %p\n", rsp);
                printf("rsp - addr %p\n", rsp - (uintptr_t)addr);
                printf("PGSIZE : %p\n", PGSIZE);
                
                if(addr > (rsp)-PGSIZE){
                    //printf("3\n");
                    vm_stack_growth(addr);
                    //thread_current()->cur_rsp = (void *) ((uint8_t *) pg_round_down(addr));
                    return true;
                }
            }
            else if (!write) {
                //printf("2\n");
                //printf("addr : %p\n", addr);
                //printf("rsp : %p\n", rsp);
                //printf("rsp - addr %p\n", (uintptr_t)addr - rsp);
                //printf("PGSIZE : %p\n", PGSIZE);
                
                if(addr > (rsp)-PGSIZE){
                    //printf("3\n");
                    vm_stack_growth(addr);
                    //thread_current()->cur_rsp = (void *) ((uint8_t *) pg_round_down(addr));
                    return true;
                }
            }
        }
        exit(-1);
    }
    else if (page == NULL){
        // page is null but addr is not stack region
        // 이 경우 새로운 page를 만들어주고 vm_do_claim_page를 해야되려나?
        exit(-1);
        PANIC("panic while vm_try_handle_fault : page is null but addr is not stack region.");
    }
    else{ //page가 있으면
        return vm_do_claim_page(page);
    }
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va) {
	struct page *page = NULL;
	/* TODO: Fill this function */
    page = spt_find_page(&thread_current()->spt, va);

    if(page==NULL){
        return false;
    }

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();
    struct thread *t = thread_current ();

    //printf("6\n");

	/* Set links */
	frame->page = page;
	page->frame = frame;


    if ((page -> uninit).type != VM_MARKER_0)
        list_push_back(&frame_table, &frame->list_elem); // frame table에 넣음

    //printf("a\n");
	/* TODO: Insert page table entry to map page's VA to frame's PA. */
    
    if(pml4_get_page (t->pml4, page->va) == NULL && pml4_set_page(t->pml4, page->va, frame->kva, page->writable)){
        //printf("b\n");
        return swap_in (page, frame->kva);
    }
    else{
        printf("!!!!!!!\n");
        palloc_free_page (frame->kva);
        free(frame);
        return false;
    }
            
}

/* Returns a hash value for page p. */
unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
  const struct page *p = hash_entry (p_, struct page, hash_elem);
  return hash_bytes (&p->va, sizeof p->va);
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED) {
  const struct page *a = hash_entry (a_, struct page, hash_elem);
  const struct page *b = hash_entry (b_, struct page, hash_elem);
    
  return a->va < b->va;
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt ) {
    hash_init(&spt -> pages, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst ,
		struct supplemental_page_table *src ) {
    struct hash h = src -> pages;

    size_t i;

	for (i = 0; i < h.bucket_cnt; i++) {
		struct list *bucket = &h.buckets[i];
		struct list_elem *elem, *next;

		for (elem = list_begin (bucket); elem != list_end (bucket); elem = next) {
			next = list_next (elem);
			//action (list_elem_to_hash_elem (elem), h->aux);
            struct hash_elem *hash_elem = list_elem_to_hash_elem (elem);
            struct page *page = hash_entry (hash_elem, struct page, hash_elem); //src로부터의 page
            
            if (VM_TYPE((page -> operations) -> type) == VM_UNINIT && page->uninit.type != VM_MARKER_0) {
                
                struct info_for_lazy *info2;
                info2 = (struct info_for_lazy *) malloc (sizeof (struct info_for_lazy));

                struct file *file_2 = file_reopen((page->info)->file);
                (info2->file) = file_2;
        

                info2->page_read_bytes = (page->info)->page_read_bytes;
                info2->ofs = (page->info)->ofs;
                
                info2->page_zero_bytes = (page->info)->page_zero_bytes;

                if(!vm_alloc_page_with_initializer(page_get_type(page), page->va, page->writable, page->init, info2)){
                    PANIC("panic while supplemental_page_table_copy : type uninit");
                    return false;
                }
                
            } // page가 uninit이면 vm_claim_page는 하지 않음.
            else if(VM_TYPE((page -> operations) -> type) == VM_ANON){
                if(!vm_alloc_page_with_initializer(page_get_type(page), page->va, page->writable, NULL, NULL)){
                    printf("!\n");
                    return false;
                }
                
                if(!vm_claim_page(page->va)){
                    printf("?\n");
                    return false;
                }
                memcpy((spt_find_page(dst, page -> va) -> frame) -> kva, (spt_find_page(src, page -> va) -> frame) -> kva, PGSIZE);
            }
            else{
                if(!vm_alloc_page(VM_MARKER_0, page->va, true)){
                    printf("!\n");
                    return false;
                }
                
                if(!vm_claim_page(page->va)){
                    printf("?\n");
                    return false;
                }
                memcpy((spt_find_page(dst, page -> va) -> frame) -> kva, (spt_find_page(src, page -> va) -> frame) -> kva, PGSIZE);
            }
            
		}
	}
    

    return true;
}

void 
destroy_func (struct hash_elem *e, void *aux){
    vm_dealloc_page(hash_entry (e, struct page, hash_elem));
}
/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */

    hash_destroy(&spt -> pages, destroy_func);

}
