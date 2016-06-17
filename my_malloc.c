/*
 * Andrew Smith - asmith379
 */

#include "my_malloc.h"

/* You *MUST* use this macro when calling my_sbrk to allocate the 
 * appropriate size. Failure to do so may result in an incorrect
 * grading!
 */
#define SBRK_SIZE 2048

/* If you want to use debugging printouts, it is HIGHLY recommended
 * to use this macro or something similar. If you produce output from
 * your code then you will receive a 20 point deduction. You have been
 * warned.
 */
#ifdef DEBUG
#define DEBUG_PRINT(x) printf x
#else
#define DEBUG_PRINT(x)
#endif


/* make sure this always points to the beginning of your current
 * heap space! if it does not, then the grader will not be able
 * to run correctly and you will receive 0 credit. remember that
 * only the _first_ call to my_malloc() returns the beginning of
 * the heap. sequential calls will return a pointer to the newly
 * added space!
 * Technically this should be declared static because we do not
 * want any program outside of this file to be able to access it
 * however, DO NOT CHANGE the way this variable is declared or
 * it will break the autograder.
 */
void* heap;

/* our freelist structure - this is where the current freelist of
 * blocks will be maintained. failure to maintain the list inside
 * of this structure will result in no credit, as the grader will
 * expect it to be maintained here.
 * Technically this should be declared static for the same reasons
 * as above, but DO NOT CHANGE the way this structure is declared
 * or it will break the autograder.
 */
metadata_t* freelist[8];
/**** SIZES FOR THE FREE LIST ****
 * freelist[0] -> 16
 * freelist[1] -> 32
 * freelist[2] -> 64
 * freelist[3] -> 128
 * freelist[4] -> 256
 * freelist[5] -> 512
 * freelist[6] -> 1024
 * freelist[7] -> 2048
 */

size_t size_needed(size_t);
int freelist_index(size_t);
void add_to_freelist(metadata_t*);
metadata_t* remove_from_freelist(metadata_t*);
void split(int, int);
metadata_t* merge_with_buddy(metadata_t*);
int my_log2(int);


void* my_malloc(size_t size)
{
	if(size + sizeof(metadata_t) > SBRK_SIZE)
	{
		ERRNO = SINGLE_REQUEST_TOO_LARGE;
		return NULL;
	}

	if(size == 0)
		return NULL;

	//instantiate heap the first time my_malloc is called
	if(heap == NULL)
	{
		heap = my_sbrk(SBRK_SIZE);
		freelist[7] = (metadata_t*)heap;
		freelist[7] -> size = SBRK_SIZE;
		freelist[7] -> in_use = 0;
		freelist[7] -> prev = NULL;
		freelist[7] -> next = NULL;
	}

	//pass along the error
	if(heap == (void*)-1)
	{
		ERRNO = OUT_OF_MEMORY;
		return NULL;
	}


	//calculate the full size of the block needed (e.g. 16, 32, 64, etc...)
	//and find the index of the freelist assosciated with that size
	size_t full_size = size_needed(size);
	int index = freelist_index(full_size);

	//freelist already has a chunk that we can return to user -- sweet!
	if(freelist[index] != NULL)
	{
		//set in use, remove from free list, and return!
		metadata_t *chunk = freelist[index];
		chunk -> in_use = 1;
		remove_from_freelist(chunk);

		ERRNO = NO_ERROR;

		//return pointer to address AFTER the metadata
		return (void*)(((char*)chunk) + sizeof(metadata_t));
	}

	//loop until we find the smallest available chunk that is big enough for the full_size
	int smallestChunkIndex = index + 1;
	while(freelist[smallestChunkIndex] == NULL && smallestChunkIndex <= 8)
	{
		smallestChunkIndex++;
	}

	//call sbrk again if no available chunks are found
	if(smallestChunkIndex > 7)
	{
		void* new_space_ptr;

		if((new_space_ptr = my_sbrk(SBRK_SIZE)) == (void*) -1)
		{
			ERRNO = OUT_OF_MEMORY;
			return NULL;
		}
		else
		{
			//add the new memory from sbrk to the freelist as an entry of size 2048
			metadata_t *new_heap = (metadata_t*) new_space_ptr;

			new_heap -> size = SBRK_SIZE;
			new_heap -> in_use = 0;
			new_heap -> next = NULL;
			new_heap -> prev = NULL;

			add_to_freelist(new_heap);
		}

		//we just added a 2048 chunk, so we know that there is a 2048 free!
		smallestChunkIndex = 7;
	}

	//recursively split the chunk as far as possible
	split(index, smallestChunkIndex);

	//there is now guaranteed to be a chunk in freelist[index] (from split)
	//set it in use, remove from free list and give to user.
	metadata_t *chunk = freelist[index];
	chunk -> in_use = 1;
	remove_from_freelist(chunk);

	ERRNO = NO_ERROR;

	//return pointer to address AFTER the metadata
	return (void*)(((char*)chunk) + sizeof(metadata_t));
}

void* my_calloc(size_t num, size_t size)
{
	size_t malloc_size = num * size;

	if(malloc_size > 2048)
	{
		ERRNO = SINGLE_REQUEST_TOO_LARGE;
		return NULL;
	}

	//get a chunk from the heap
	void *ptr = my_malloc(malloc_size);

	//number of bytes being zero'd out is the size of chunk returned by malloc, minus
	//the size of the metadata (we don't want to zero out metadata)
	int num_of_bytes = ((metadata_t*)ptr - 1) -> size - sizeof(metadata_t);

	for(int i = 0; i < num_of_bytes; i++)
	{
		//zero it out
		*( ((char*)ptr) + i ) = 0;
	}

	ERRNO = NO_ERROR;
	return ptr;
}

void my_free(void* ptr)
{
	//point to start of metadata
 	metadata_t *p = ((metadata_t*)ptr) - 1;
 	p -> in_use = 0;

 	add_to_freelist(p);	

 	//merge as many times as possible-- merge method takes care of
 	//removing both buddies from the freelist.
 	while((p = merge_with_buddy(p)) != NULL)
 	{
 		//add the merged block into the freelist
 		add_to_freelist(p);
 	}
}

void* my_memmove(void* dest, const void* src, size_t num_bytes)
{
	char *dest_ptr = (char*)dest;
	char *src_ptr = (char*)src;

	//doesn't matter if overwriting source -- that part will already be copied
	if(dest_ptr < src_ptr)
	{
		for(int i = 0; i < num_bytes; i++)
		{
			dest_ptr[i] = src_ptr[i];
		}
	}
	//copy in reverse order, so if anything gets overwritten it will already have been copied
	else if (src_ptr < dest_ptr)
	{
		for(int i = num_bytes - 1; i >= 0; i--)
		{
			dest_ptr[i] = src_ptr[i];
		}
	}

	//dest == src? do nothing!

	ERRNO = NO_ERROR;
	return (void*)dest_ptr;
}

/**
 * Takes in the size that the user is trying to allocate in memory. Returns
 * the size of the memory that actually gets allocated (the smallest needed
 * power of 2 from 16 - 2048)
 *
 */
size_t size_needed(size_t size)
{
	size_t full_size = size + sizeof(metadata_t);

	size_t bitmask = 16;

	while(bitmask < full_size)
	{
		bitmask = bitmask << 1;
	}

	return bitmask;
}

/**
 * Takes in the size you are trying to allocate, and returns the
 * appropriate index in the free list.
 */
int freelist_index(size_t size)
 {
 	//min size is 10000 (binary)
 	size = size >> 4;

 	int index = 0;

 	while(size != 1)
 	{
 		size = size >> 1;
 		index++;
 	}

 	return index;
}

/**
 * Adds a metadata pointer to the free list.
 */
void add_to_freelist(metadata_t *node)
{
	int index = freelist_index(node -> size);

	metadata_t *current = freelist[index];

	if(freelist[index] != NULL)
	{
		while(current -> next != NULL)
		{
			current = current -> next;
		}

		current -> next = node;
		node -> prev = current;
		node -> next = NULL;
	}
	else
	{
		freelist[index] = node;
		node -> prev = NULL;
		node -> next = NULL;
	}
}

/**
 * Removes the metadata pointer from the free list. Used when splitting a chunk of memory
 * into two smaller chunks, or removing something from the freelist because it is being
 * merged.
 *
 */
metadata_t* remove_from_freelist(metadata_t *node)
{
	int index = freelist_index(node -> size);

	//if not head of list, set previous node to point to the next node (or NULL if next is NULL)
	if(node -> prev != NULL)
		node -> prev -> next = node -> next;
	//if head of the list, set the head to the next node
	else
		freelist[index] = node -> next;

	//if not tail of the list, set next node to point to the previous node (or NULL of prev is NULL)
	if(node -> next != NULL)
		node -> next -> prev = node -> prev;

	//remove pointers to next and null
	node -> next = NULL;
	node -> prev = NULL;

	return node;
}

/**
 * Recursively splits the memory into smaller and smaller chunks until
 * two chunks that are the size stored in end_index are made.
 *
 * end_index: the index whose size you are trying to split the chunks into
 * current_index: the index whose chunk you are currently splitting
 */
void split(int end_index, int current_index)
{
	//base case
	if(current_index == end_index)
		return;

	//pull out first node in free list at current index.
	//this is the node that will be split
	metadata_t *current_node = freelist[current_index];

	//removes node from it's current spot in the free list
	remove_from_freelist(current_node);

	//updates nodes metadata before putting it in new spot in free list
	current_node -> size /= 2;
	current_node -> prev = NULL;
	current_node -> next = NULL;
	add_to_freelist(current_node);

	//creates new node that takes up the other half of the chunk, and adds it to freelist
	metadata_t *new_node = (metadata_t*)((char*)current_node + current_node -> size);
	new_node -> in_use = 0;
	new_node -> size = current_node -> size;
	add_to_freelist(new_node);

	//recursive call, to see if chunk needs to be split more
	split(end_index, current_index - 1);
}

/**
 * Searches for buddy1's buddy, and attempts to merge
 * with it if both buddies are free. If so, returns a
 * pointer to the merged block. If not, returns null.
 *
 */
metadata_t* merge_with_buddy(metadata_t* buddy1)
{
	//starting addresses from 0, every chunk will be buddied with one whose address
	//is identical, EXCEPT for the nth bit, which is toggled
	int n = my_log2(buddy1 -> size);

	int addressFromZero = ((char*)buddy1 - (char*)heap);
	
	//toggle nth bit
	addressFromZero = addressFromZero ^ (1 << n);

	//pointer to the actual address of the buddy
	metadata_t* buddy2 = (metadata_t*)((char*)heap + addressFromZero);

	//make sure they are actually valid buddies!
	//a lot of these checks are probably unneccessary/redundant, but better safe than sorry!
	if(buddy1 != buddy2 && buddy1 -> size == buddy2 -> size && !buddy1 -> in_use && !buddy2 -> in_use && (buddy1-> size != SBRK_SIZE) && (buddy2 -> size != SBRK_SIZE))
	{
		//get whichever buddy comes first in memory, modify it's metadata
		metadata_t* first = (buddy1 < buddy2) ? buddy1 : buddy2;
		first -> size *= 2;
		first -> in_use = 0;

		//rearrange pointers to keep the freelist accurate
		//after the removals
		if(buddy1-> next == buddy2)
			buddy1->next = buddy2-> next;
		if(buddy1-> prev == buddy2)
			buddy1-> prev = buddy2 -> prev;
		if(buddy2->next == buddy1)
			buddy2->next = buddy1-> next;
		if(buddy2->prev == buddy1)
			buddy2->prev = buddy1-> prev;

		remove_from_freelist(buddy1);
		remove_from_freelist(buddy2);

		//my_free will handle adding to freelist
		return first;
	}

	return NULL;
}

//custom log2 method that only deals in ints.
//argument should always be a valid power of 2
//to ensure it works properly.
int my_log2(int n)
{
	int counter = 0;

	while(!(n & 1))
	{
		n = n >> 1;
		counter++;
	}

	return counter;
}