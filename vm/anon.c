/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "threads/malloc.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
//static struct swap_table *swap_table;
static struct bitmap *swap_map;
	
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	// disk_init은 안 해줘도 될 듯.
	// disk는 process마다 하나 씩?
	// 아니면 모든 process가 하나의 disk를 공유하나? -> 이거 인 듯
	// 그냥 이렇게 가져올 수 있는 건가.
	// 그럼 이걸 어디에 저장해 줘야 되지?
	swap_disk = disk_get(1, 1);
	//struct swap_table *swap_table = malloc(sizeof(struct swap_table));
	//struct swap_table *swap_table;
	//swap_table -> swap_map = bitmap_create(disk_size(swap_disk)/8);
    //if (swap_table -> swap_map == NULL) // bitmap_create가 fail일 때
    //    PANIC("panic while vm_init : fail to bitmap_create");
	swap_map = bitmap_create(disk_size(swap_disk)/8);

}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;
    (page->frame)->kva = kva;

	struct anon_page *anon_page = &page->anon;

    return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;

    int rest;

	size_t index = page -> swap_index;
	//printf("c\n");

	for (int i = index*8; i < index*8 + 8; i++) {
		//printf("d\n");
		rest = i%8;
		disk_read(swap_disk, i, kva + rest*DISK_SECTOR_SIZE);
	}
	page -> swapped_out = false;

/* swap_slot을 어떻게 free해줘야 될까
	void *empty = malloc(DISK_SECTOR_SIZE);
	for (int i = index*8; i < index*8; i++) {
		rest = i%8;
		disk_write(swap_disk, i, );
	}
*/
	//printf("e\n");
	bitmap_flip(swap_map, index);
	return true;
}

/* Swap out the page by writing contents to the swap disk. */
// synchornization 신경 써 줘야 할까?
// interrupt disable이라든지
static bool
anon_swap_out (struct page *page) {
	//printf("13\n");
	struct anon_page *anon_page = &page->anon;
    int rest;
	size_t empty_bit = bitmap_scan(swap_map, 0, 1, false);
	//printf("14\n");
	if (empty_bit == BITMAP_ERROR)
		PANIC("panic while anon_swap_out : there is no empty swap_slot in swap_table");
	//printf("empty_bit : %d\n", empty_bit);
	
	page -> swap_index = empty_bit;
	page -> swapped_out = true;
	for (int i = empty_bit*8; i < empty_bit*8 + 8; i++) {
		//do_nothing();
		rest = i%8;
		disk_write(swap_disk, i, (page -> frame) -> kva + rest*DISK_SECTOR_SIZE);
	}
	//free(page -> frame);
	bitmap_flip(swap_map, empty_bit);
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;

    //palloc_free_page((page -> frame) -> kva);
	if (!page -> swapped_out)
		free(page -> frame);
    free(page->info);
	
}