#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define ALIGN4(s)         (((((s) - 1) >> 2) << 2) + 4)
#define BLOCK_DATA(b)     ((b) + 1)  // Skip the header to return user the actual data
#define BLOCK_HEADER(ptr) ((struct _block *)(ptr) - 1)

static int atexit_registered = 0;
static int num_mallocs       = 0;
static int num_frees         = 0;
static int num_reuses        = 0;
static int num_grows         = 0;
static int num_splits        = 0;
static int num_coalesces     = 0;
static int num_blocks        = 0;
static int num_requested     = 0;
static int max_heap          = 0;

/*
 * \brief printStatistics
 * Prints the heap statistics upon process exit.
 */
void printStatistics(void) {
    printf("\nHeap Management Statistics\n");
    printf("mallocs:\t%d\n", num_mallocs);
    printf("frees:\t\t%d\n", num_frees);
    printf("reuses:\t\t%d\n", num_reuses);
    printf("grows:\t\t%d\n", num_grows);
    printf("splits:\t\t%d\n", num_splits);
    printf("coalesces:\t%d\n", num_coalesces);
    printf("blocks:\t\t%d\n", num_blocks);
    printf("requested:\t%d\n", num_requested);
    printf("max heap:\t%d\n", max_heap);
}

struct _block {
    size_t size;            // Size of the allocated block in bytes
    struct _block *next;    // Pointer to the next block
    bool free;              // Is this block free?
    char padding[3];        // Padding for alignment
};

struct _block *heapList = NULL;  // Free list to track available blocks
struct _block *actualLast = NULL;

/*
 * \brief findFreeBlock
 * Finds a free block of memory using the defined allocation strategy.
 */
struct _block *findFreeBlock(struct _block **last, size_t size) {
    struct _block *curr = heapList;

#if defined FIT && FIT == 0
   /* First fit */
   //
   // While we haven't run off the end of the linked list and
   // while the current node we point to isn't free or isn't big enough
   // then continue to iterate over the list.  This loop ends either
   // with curr pointing to NULL, meaning we've run to the end of the list
   // without finding a node or it ends pointing to a free node that has enough
   // space for the request.
   // 
   while (curr && !(curr->free && curr->size >= size)) 
   {
      *last = curr;
      curr  = curr->next;
   }
#endif

#if defined BEST && BEST == 0
    // Best Fit strategy
    int smallest = INT_MAX;
    struct _block *bestFit = NULL;
    while (curr) {
        if (curr->free && curr->size >= size && curr->size < smallest) {
            smallest = curr->size;
            bestFit = curr;
        }
        *last = curr;
        curr = curr->next;
    }
    curr = bestFit;
#endif

#if defined WORST && WORST == 0
    // Worst Fit strategy
    int largest = 0;
    struct _block *worstFit = NULL;
    while (curr) {
        if (curr->free && curr->size >= size && curr->size > largest) {
            largest = curr->size;
            worstFit = curr;
        }
        *last = curr;
        curr = curr->next;
    }
    curr = worstFit;
#endif

#if defined NEXT && NEXT == 0
    // Next Fit strategy
    curr = actualLast;
    while (curr && !(curr->free && curr->size >= size)) {
        *last = curr;
        actualLast = curr;
        curr = curr->next;
    }
    if (!curr) {
        curr = heapList;
        while (curr && !(curr->free && curr->size >= size)) {
            *last = curr;
            curr = curr->next;
        }
    }
#endif

    return curr;
}

/*
 * \brief growHeap
 * Grows the heap by requesting memory from the OS.
 */
struct _block *growHeap(struct _block *last, size_t size) {
    struct _block *curr = (struct _block *)sbrk(0);
    struct _block *prev = (struct _block *)sbrk(sizeof(struct _block) + size);

    assert(curr == prev);

    if (curr == (struct _block *)-1) {
        return NULL; // OS allocation failed
    }

    if (!heapList) {
        heapList = curr; // Initialize heapList
    }

    if (last) {
        last->next = curr;
    }

    curr->size = size;
    curr->next = NULL;
    curr->free = false;

    max_heap += size;
    num_grows++;
    num_blocks++;
    return curr;
}

/*
 * \brief malloc
 * Allocates memory of the requested size.
 */
void *malloc(size_t size) {
    if (!atexit_registered) {
        atexit(printStatistics);
        atexit_registered = 1;
    }

    size = ALIGN4(size);
    if (size == 0) {
        return NULL;
    }

    struct _block *last = heapList;
    struct _block *next = findFreeBlock(&last, size);

    if (next && next->size > size + sizeof(struct _block)) {
        struct _block *newBlock = (struct _block *)((char *)next + sizeof(struct _block) + size);
        newBlock->size = next->size - size - sizeof(struct _block);
        newBlock->free = true;
        newBlock->next = next->next;

        next->size = size;
        next->next = newBlock;

        num_splits++;
    }

    if (!next) {
        next = growHeap(last, size);
        if (!next) {
            return NULL;
        }
    } else {
        num_reuses++;
    }

    next->free = false;
    num_requested += size;
    num_mallocs++;
    return BLOCK_DATA(next);
}

/*
 * \brief free
 * Frees the memory block and coalesces adjacent free blocks.
 */
void free(void *ptr) {
    if (!ptr) {
        return;
    }

    struct _block *curr = BLOCK_HEADER(ptr);
    assert(!curr->free);

    curr->free = true;

    struct _block *temp = heapList;
    while (temp) {
        if (temp->free && temp->next && temp->next->free) {
            temp->size += temp->next->size + sizeof(struct _block);
            temp->next = temp->next->next;
            num_coalesces++;
        }
        temp = temp->next;
    }

    num_frees++;
}

/*
 * \brief calloc
 * Allocates and zeroes memory.
 */
void *calloc(size_t nmemb, size_t size) {
    size_t total_size = nmemb * size;
    void *ptr = malloc(total_size);
    if (ptr) {
        memset(ptr, 0, total_size);
    }
    return ptr;
}

/*
 * \brief realloc
 * Reallocates memory to a new size.
 */
void *realloc(void *ptr, size_t size) {
    if (!ptr) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    void *newPtr = malloc(size);
    if (newPtr) {
        struct _block *oldBlock = BLOCK_HEADER(ptr);
        memcpy(newPtr, ptr, oldBlock->size < size ? oldBlock->size : size);
        free(ptr);
    }
    return newPtr;
}
