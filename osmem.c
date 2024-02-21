// SPDX-License-Identifier: BSD-3-Clause

#include "osmem.h"
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include "block_meta.h"
#include "printf.h"

#define MMAP_THRESHOLD (128 * 1024)
#define PAGE_SIZE (4 * 1024)
#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))

void *brk_head;
void *map_head;

void preallocate(void)
{
	void *top;

	// Raise top of heap for preallocation
	top = sbrk(0);
	sbrk(MMAP_THRESHOLD);

	struct block_meta *new_meta = (struct block_meta *)top;

	// Initialise new block of memory
	new_meta->size = MMAP_THRESHOLD;
	new_meta->status = 0;
	new_meta->next = new_meta->prev = NULL;

	brk_head = new_meta;
}

struct block_meta *find_best_fit(size_t size)
{
	struct block_meta *find = (struct block_meta *)brk_head;

	// Search for best fitting node in list
	while (find != NULL) {
		if (find->size >= size && find->status == 0)
			return find;

		find = find->next;
	}

	return NULL;
}

void coalesce_blocks(struct block_meta *block)
{
	// Increase block size and remove middle node after coalesce
	block->size += block->next->size;
	block->next = block->next->next;

	if (block->next != NULL)
		block->next->prev = block;
}

void *os_malloc(size_t size)
{
	void *ptr = NULL;
	unsigned long new_size;

	if (size == 0)
		return NULL;

	// Align allocated size and add size of the block meta
	new_size = ALIGN(size + sizeof(struct block_meta));

	if (size >= MMAP_THRESHOLD) {
		// Big memory blocks are mapped
		ptr = mmap(NULL, new_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		((struct block_meta *)ptr)->size = new_size;
		((struct block_meta *)ptr)->status = 2;

		if (!map_head) {
			// Initialise mapped block list
			map_head = ptr;
			((struct block_meta *)ptr)->prev = ((struct block_meta *)ptr)->next = NULL;
		} else {
			struct block_meta *last = (struct block_meta *)map_head;

			// Add new block at the end of our list
			while (last->next != NULL)
				last = last->next;

			last->next = (struct block_meta *)ptr;
			((struct block_meta *)ptr)->next = NULL;
			((struct block_meta *)ptr)->prev = last;
		}

		// Return is offset with one structure to get the payload
		return ((struct block_meta *)ptr)+1;
	}

	if (!brk_head)
		preallocate();

	struct block_meta *best = find_best_fit(new_size);


	if (best == NULL) {
		// Allocate new memory on heap
		struct block_meta *final = (struct block_meta *)brk_head;

		while (final->next != NULL)
			final = final->next;

		void *top = sbrk(0);

		if (final->status == 0) {
			// Extend last block if it's free
			size_t sign = new_size > final->size ? (new_size - final->size) : (final->size - new_size);

			sbrk(sign);
			final->size = new_size;
			best = final;
		} else {
			// Add to the end of the list
			sbrk(new_size);
			best = top;
			best->size = new_size;
			best->status = 1;

			best->prev = final;
			final->next = best;
		}

		return best+1;
	}

	best->status = 1;
	if (best->size - new_size >= 40) {
		// Split block where we can
		struct block_meta *new_meta = (struct block_meta *)((char *)best + new_size);

		new_meta->size = best->size - new_size;
		best->size = new_size;

		new_meta->status = 0;
		new_meta->next = best->next;
		best->next = new_meta;
		new_meta->prev = best;
	}

	return best+1;


	return NULL;
}

void os_free(void *ptr)
{
	if (!ptr)
		return;

	struct block_meta *node = ((struct block_meta *)ptr - 1);

	if (node->size == 0)
		return;

	if (node->status == 1) {
		node->status = 0;

		// Check for coalesce ater every free from brk
		if (node->next != NULL && node->next->status == 0)
			coalesce_blocks(node);


		if (node->prev != NULL && node->prev->status == 0)
			coalesce_blocks(node->prev);
	}

	if (node->status == 2) {
		// Remove node from list
		if (node->next != NULL)
			node->next->prev = node->prev;

		if (node->prev != NULL)
			node->prev->next = node->next;

		else
			map_head = node->next;

		// Free mapped memory
		munmap(node, node->size);
	}
}

void *os_calloc(size_t nmemb, size_t size)
{
	void *ptr = NULL;
	size_t new_size;

	if (size*nmemb == 0)
		return NULL;

	new_size = ALIGN(size*nmemb + sizeof(struct block_meta));

	if (new_size >= PAGE_SIZE) {
		ptr = mmap(NULL, new_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		((struct block_meta *)ptr)->size = new_size;
		((struct block_meta *)ptr)->status = 2;

		if (!map_head) {
			map_head = ptr;
			((struct block_meta *)ptr)->prev = ((struct block_meta *)ptr)->next = NULL;
		} else {
			struct block_meta *last = (struct block_meta *)map_head;

			while (last->next != NULL)
				last = last->next;

			last->next = (struct block_meta *)ptr;
			((struct block_meta *)ptr)->next = NULL;
			((struct block_meta *)ptr)->prev = last;
		}

		// Set payload to 0 values
		memset(((struct block_meta *)ptr)+1, 0, ((struct block_meta *)ptr)->size - sizeof(struct block_meta));
		return ((struct block_meta *)ptr)+1;
	}

	if (!brk_head)
		preallocate();

	struct block_meta *best = find_best_fit(new_size);


	if (best == NULL) {
		struct block_meta *final = (struct block_meta *)brk_head;

		while (final->next != NULL)
			final = final->next;

		void *top = sbrk(0);

		if (final->status == 0) {
			size_t sign = new_size > final->size ? (new_size - final->size) : (final->size - new_size);

			sbrk(sign);
			final->size = new_size;
			best = final;
		} else {
			sbrk(new_size);
			best = top;
			best->size = new_size;
			best->status = 1;

			best->prev = final;
			final->next = best;
		}

		memset(best+1, 0, best->size - sizeof(struct block_meta));
		return best+1;
	}

	best->status = 1;

	if (best->size - new_size >= 40) {
		struct block_meta *new_meta = (struct block_meta *)((char *)best + new_size);

		new_meta->size = best->size - new_size;
		best->size = new_size;

		new_meta->status = 0;
		new_meta->next = best->next;
		best->next = new_meta;
		new_meta->prev = best;
	}

	memset(best+1, 0, best->size - sizeof(struct block_meta));
	return best+1;
}

void *os_realloc(void *ptr, size_t size)
{
	if (ptr == NULL)
		return os_malloc(size);

	struct block_meta *block = ((struct block_meta *)ptr - 1);

	if (block->status == 0)
		return NULL;

	if (size == 0) {
		os_free(ptr);
		return NULL;
	}

	size_t new_size = (ALIGN(size + sizeof(struct block_meta)));

	if (block->status == 1) {
		// Heap memory that becomes too big is deleted and remapped
		if (new_size >= MMAP_THRESHOLD) {
			void *new_block = os_malloc(new_size);

			size_t min_size = block->size < new_size ? block->size : new_size;

			memcpy(new_block, ptr, min_size - sizeof(struct block_meta));
			os_free(ptr);

			return new_block;

		} else {
			// Block size can be split or extended
			if (block->size > new_size) {
				if (block->size - new_size > 40) {
					struct block_meta *new_meta = (struct block_meta *)((char *)block + new_size);

					new_meta->size = block->size - new_size;
					block->size = new_size;

					new_meta->status = 0;
					new_meta->next = block->next;
					block->next = new_meta;
					new_meta->prev = block;

					return block+1;
				}

				return block+1;

			} else if (block->size < new_size) {
				// Coalesce to find increase the size
				if (block->next != NULL && block->next->status == 0) {
					coalesce_blocks(block);
					return block+1;
				}

				// Extend if last node
				if (block->next == NULL) {
					sbrk(new_size - block->size);
					block->size = new_size;

					ptr = (void *)(block+1);
					return block+1;
				}

				// Allocate new block and copy contents from source
				void *new_block = os_malloc(size);

				size_t min_size = block->size < new_size ? block->size : new_size;

				memcpy(new_block, ptr, min_size);
				os_free(ptr);

				ptr = new_block;
				return new_block;
			}

			ptr = (void *)(block+1);
			return block+1;
		}
	}

	if (block->status == 2) {
		// Mapped areas are remapped separatey
		void *new_block = os_malloc(size);

		size_t min_size = block->size < new_size ? block->size : new_size;

		memcpy(new_block, ptr, min_size);
		os_free(ptr);

		ptr = new_block;
		return new_block;
	}

	return NULL;
}
