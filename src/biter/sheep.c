#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

unsigned char *sheep_base = NULL;
static unsigned char *sheep_end = NULL;

#define HEAP_NULL ((void *) sheep_base)

typedef struct block_prefix_t {
   uint32_t prev_block;
   uint32_t next_free;
   uint32_t prev_free;
   uint32_t size;
} block_prefix_t;

#define PTR(X) ((block_prefix_t *) (sheep_base + (((uint64_t) X) << 3)))
#define IS_ALLOC(X) ((X)->size & UINT32_C(1))
#define IS_FREE(X)  (!((X)->size & UINT32_C(1)))
#define NEXT_BLOCK(X) ((block_prefix_t *) ((SIZE(X) + ((unsigned char *) (X)) >= sheep_end) ? HEAP_NULL : (SIZE(X) + ((unsigned char *) (X)))))
#define PREV_BLOCK(X) (PTR(X->prev_block))
#define NEXT_FREE(X) (PTR(X->next_free))
#define PREV_FREE(X) (PTR(X->prev_free))
#define SIZE(X) ((((uint64_t) (X)->size) & ~UINT64_C(1)) << 2)
#define VAL(V) (((unsigned long) (((unsigned char *) V) - sheep_base)) >> 3)

#define SET_ALLOC(X) (X)->size |= UINT32_C(1)
#define SET_FREE(X) (X)->size &= ~UINT32_C(1)
#define SET_NEXT_BLOCK(X, V)
#define SET_PREV_BLOCK(X, V) (X)->prev_block = VAL(V)
#define SET_NEXT_FREE(X, V) (X)->next_free = VAL(V)
#define SET_PREV_FREE(X, V) (X)->prev_free = VAL(V)
#define SET_SIZE(X, V) (X)->size = ((X)->size & UINT32_C(1)) | ((V) >> 2)
#define CLEAR(X) (X)->prev_block = (X)->next_free = (X)->prev_free = (X)->size = 0

static int first_fit = 0; //If 0 then use best-fit malloc, else use first-fit malloc.

typedef struct heap_header_t {
   block_prefix_t head_block;
   uint64_t heap_size;
   uint64_t initialized;
} heap_header_t;
static heap_header_t *heap_header;
static block_prefix_t *head_block;

#define ALIGN8(X) (((X) & 7) ? ((((X) >> 3) + 1) << 3) : (X))

void init_sheep(void *mem, size_t size, int use_first_fit)
{
   block_prefix_t *first_block;
   assert(size >= 4096);
   assert(size % 4096 == 0);
   assert(size < (1 << 25)); //Maximum for shared heap
   first_fit = use_first_fit;
   

   heap_header = (heap_header_t *) mem;
   sheep_base = (unsigned char *) mem;
   sheep_end = sheep_base + size;
   head_block = &heap_header->head_block;
   first_block = (block_prefix_t *) (heap_header+1);

   if (!heap_header->initialized) {
      heap_header->heap_size = size;
      
      CLEAR(head_block);
      SET_ALLOC(head_block);
      SET_NEXT_BLOCK(head_block, first_block);
      SET_PREV_BLOCK(head_block, HEAP_NULL);
      SET_NEXT_FREE(head_block, first_block);
      SET_PREV_FREE(head_block, HEAP_NULL);
      SET_SIZE(head_block, sizeof(heap_header_t));
      
      CLEAR(first_block);
      SET_FREE(first_block);
      SET_NEXT_BLOCK(first_block, HEAP_NULL);
      SET_PREV_BLOCK(first_block, head_block);
      SET_NEXT_FREE(first_block, HEAP_NULL);
      SET_PREV_FREE(first_block, HEAP_NULL);
      SET_SIZE(first_block, size - sizeof(heap_header_t));
      heap_header->initialized = 1;
   }
}

void *malloc_sheep(size_t size)
{
   block_prefix_t *cur = NULL, *best_fit = NULL;
   block_prefix_t *prev_free, *next_free, *new_block, *next_block;
   uint32_t best_fit_diff = UINT32_MAX;
   uint32_t alloc_size;
   uint32_t min_alloc_size = sizeof(block_prefix_t) + 8;
   uint32_t cur_size, new_size;

   assert(size);
   alloc_size = ((uint32_t) ALIGN8(size));
   if (!alloc_size)
      alloc_size = 8;

   alloc_size += sizeof(block_prefix_t);

   //Find free block of sufficient size
   for (cur = NEXT_FREE(head_block); cur != HEAP_NULL; cur = NEXT_FREE(cur)) {
      uint32_t cur_size = SIZE(cur), diff;
      if (cur_size < alloc_size)
         continue;
      diff = cur_size - alloc_size;
      if (diff < best_fit_diff) {
         best_fit_diff = diff;
         best_fit = cur;
         if (diff < min_alloc_size)
            break;
      }
      if (first_fit)
         break;
   }
   
   if (!best_fit) {
      //Out of space.
      return NULL;
   }
   cur = best_fit;

   assert(IS_FREE(cur));

   prev_free = PREV_FREE(cur);
   next_free = NEXT_FREE(cur);
   next_block = NEXT_BLOCK(cur);
   cur_size = (uint32_t) SIZE(cur);
   
   //Remove cur block from free list
   SET_ALLOC(cur);
   if (prev_free != HEAP_NULL)
      SET_NEXT_FREE(prev_free, next_free);
   else
      SET_NEXT_FREE(head_block, next_free);
   if (next_free != HEAP_NULL)
      SET_PREV_FREE(next_free, prev_free);
   SET_NEXT_FREE(cur, HEAP_NULL);
   SET_PREV_FREE(cur, HEAP_NULL);

   if (best_fit_diff < min_alloc_size) {
      //Just take whole block, mark it allocated and remove from free list.
      return (void *) (cur + 1);
   }

   //Split the best fit into the block we're allocating and a new block
   // with the remaining space.
   new_block = (block_prefix_t *) (((unsigned char *) cur) + alloc_size);
   new_size = cur_size - alloc_size;
   CLEAR(new_block);
 
   SET_FREE(new_block);
   SET_NEXT_BLOCK(new_block, next_block);
   SET_PREV_BLOCK(new_block, cur);
   SET_NEXT_FREE(new_block, next_free);
   SET_PREV_FREE(new_block, prev_free);
   SET_SIZE(new_block, new_size);
   assert(new_size >= min_alloc_size);
   assert(new_size < heap_header->heap_size);
   assert((new_size & 7) == 0);

   SET_SIZE(cur, alloc_size);
   if (next_block != HEAP_NULL)
      SET_PREV_BLOCK(next_block, new_block);
   SET_NEXT_BLOCK(cur, new_block);
   
   if (prev_free != HEAP_NULL)
      SET_NEXT_FREE(prev_free, new_block);
   else 
      SET_NEXT_FREE(head_block, new_block);
   if (next_free != HEAP_NULL)
      SET_PREV_FREE(next_free, new_block);

   return (void *) (cur + 1);
}

static block_prefix_t *merge_free_blocks(block_prefix_t *a, block_prefix_t *b)
{
   block_prefix_t *b_next_block, *b_next_free;
   uint32_t a_size, b_size;
   
   a_size = SIZE(a);
   b_next_block = NEXT_BLOCK(b);
   b_next_free = NEXT_FREE(b);
   b_size = SIZE(b);

   SET_NEXT_BLOCK(a, b_next_block);
   SET_NEXT_FREE(a, b_next_free);
   
   if (b_next_block != HEAP_NULL)
      SET_PREV_BLOCK(b_next_block, a);
   if (b_next_free != HEAP_NULL)
      SET_PREV_FREE(b_next_free, a);

   SET_SIZE(a, a_size + b_size);

   return a;
}

void free_sheep(void *p)
{
   block_prefix_t *cur = ((block_prefix_t *) p) - 1;
   block_prefix_t *prev_free, *next_free, *prev_block, *next_block;
   int changed_something;

   assert(cur > head_block);
   assert(cur < (block_prefix_t *) (((unsigned char *) heap_header) + heap_header->heap_size));

   //Find the previous free block, use it to calculate the next free
   for (prev_free = cur; prev_free != HEAP_NULL && IS_ALLOC(prev_free); prev_free = PREV_BLOCK(prev_free));
   next_free = (prev_free == HEAP_NULL) ? NEXT_FREE(head_block) : NEXT_FREE(prev_free);
   assert(prev_free < cur || prev_free == HEAP_NULL);
   assert(next_free > cur || next_free == HEAP_NULL);

   //Mark cur block as free, add back into free list.
   SET_FREE(cur);
   if (prev_free != HEAP_NULL)
      SET_NEXT_FREE(prev_free, cur);
   else
      SET_NEXT_FREE(head_block, cur);
   if (next_free != HEAP_NULL)
      SET_PREV_FREE(next_free, cur);
   SET_NEXT_FREE(cur, next_free);
   SET_PREV_FREE(cur, prev_free);

   //While we're adjacent to other free blocks, join blocks.
   do {
      changed_something = 0;

      prev_block = PREV_BLOCK(cur);    
      if (IS_FREE(prev_block)) {
         cur = merge_free_blocks(prev_block, cur);
         changed_something = 1;
      }
      next_block = NEXT_BLOCK(cur);
      if (IS_FREE(next_block)) {
         cur = merge_free_blocks(cur, next_block);
         changed_something = 1;
      }
   } while (changed_something);
}

//#define UNIT_TESTS

#if defined(UNIT_TESTS)

#include <stdio.h>
#include <sys/mman.h>
#include <string.h>

//Function forms of macros--mostly useful for debugging
int is_alloc(block_prefix_t *b) { return IS_ALLOC(b); }
int is_free(block_prefix_t *b) { return IS_FREE(b); }
block_prefix_t *next_block(block_prefix_t *b) { return NEXT_BLOCK(b); }
block_prefix_t *prev_block(block_prefix_t *b) { return PREV_BLOCK(b); }
block_prefix_t *next_free(block_prefix_t *b) { return NEXT_FREE(b); }
block_prefix_t *prev_free(block_prefix_t *b) { return PREV_FREE(b); }
uint32_t bsize(block_prefix_t *b) { return SIZE(b); }

static void print_heap()
{
   uint32_t min_alloc_size = sizeof(block_prefix_t) + 8;
   block_prefix_t *cur, *prev = NULL;
   unsigned long num_frees = 0, num_frees2 = 0;

   for (cur = NEXT_BLOCK(head_block); cur != HEAP_NULL; prev = cur, cur = NEXT_BLOCK(cur)) {
#if defined(PRINT)
      {
         unsigned char *data;
         if (IS_ALLOC(cur))
            printf("A: ");
         else 
            printf("F: ");
         data = (unsigned char *) cur;
         printf("0x%lx - 0x%lx\n", data - sheep_base, (data - sheep_base) + SIZE(cur));
      }
#endif
      assert((unsigned long) cur >= (unsigned long) sheep_base);
      assert((unsigned long) cur < ((unsigned long) sheep_base) + heap_header->heap_size);
      if (prev != NULL) {
         assert(NEXT_BLOCK(prev) == cur);
         assert(PREV_BLOCK(cur) == prev);
         assert(PREV_BLOCK(cur) != HEAP_NULL);
      }
      else {
         assert(PREV_BLOCK(cur) == HEAP_NULL);
      }
      assert(SIZE(cur) < heap_header->heap_size);
      assert(SIZE(cur) >= min_alloc_size);
      assert(((unsigned long) NEXT_BLOCK(cur) > (unsigned long) cur) || (NEXT_BLOCK(cur) == HEAP_NULL));
      assert(((unsigned long) PREV_BLOCK(cur) < (unsigned long) cur) || (PREV_BLOCK(cur) == HEAP_NULL));
      if (IS_ALLOC(cur)) {
         assert(NEXT_FREE(cur) == HEAP_NULL || cur == head_block);
         assert(PREV_FREE(cur) == HEAP_NULL);
      }
      else {
         assert(IS_FREE(cur));
         if (prev)
            assert(!IS_FREE(prev));
         if (NEXT_BLOCK(cur) != HEAP_NULL)
            assert(!IS_FREE(NEXT_BLOCK(cur)));
         num_frees++;
      }
      assert(((unsigned long) NEXT_FREE(cur) > (unsigned long) cur) || (NEXT_FREE(cur) == HEAP_NULL));
      assert(((unsigned long) PREV_FREE(cur) < (unsigned long) cur) || (PREV_FREE(cur) == HEAP_NULL));
      assert(!prev || (((unsigned char *) prev) + SIZE(prev)) == ((unsigned char *) cur));
   }
   prev = NULL;
   for (cur = NEXT_FREE(head_block); cur != HEAP_NULL; prev = cur, cur = NEXT_FREE(cur)) {
      assert((unsigned long) cur >= (unsigned long) sheep_base);
      assert((unsigned long) cur < ((unsigned long) sheep_base) + heap_header->heap_size);
      assert(IS_FREE(cur));
      if (prev != NULL) {
         assert(NEXT_FREE(prev) == cur);
         assert(PREV_FREE(cur) == prev);
         assert(PREV_BLOCK(cur) != HEAP_NULL);
      }
      else {
         assert(PREV_FREE(cur) == HEAP_NULL);
      }
      assert(SIZE(cur) < heap_header->heap_size);
      assert(SIZE(cur) >= min_alloc_size);
      assert(((unsigned long) NEXT_FREE(cur) > (unsigned long) cur) || (NEXT_FREE(cur) == HEAP_NULL));
      assert(((unsigned long) PREV_FREE(cur) < (unsigned long) cur) || (PREV_FREE(cur) == HEAP_NULL));
      num_frees2++;
   }

   assert(num_frees = num_frees2);
#if defined(PRINT)
   printf("\n");
#endif
}

void test_macros()
{
   block_prefix_t block, *bp;
   void *vals[3];
   unsigned int svals[3] = {0x0, 0x1555550, 0x1fffff0};
   int a, na, pa, nf, pf, s;
   
   vals[0] = (void *) 0x0;
   vals[1] = (void *) 0xaaaaaa8;
   vals[2] = (void *) 0xffffff8;

   CLEAR(&block);
   bp = &block;

   uint32_t is_alloc, size;
   block_prefix_t *nextfree, *prevfree, *nextblock, *prevblock;

   for (a = 0; a < 2; a++)
      for (na = 0; na < 3; na++)
         for (pa = 0; pa < 3; pa++)
            for (nf = 0; nf < 3; nf++)
               for (pf = 0; pf < 3; pf++)
                  for (s = 0; s < 3; s++)
                  {
                     if (a)
                        SET_ALLOC(bp);
                     else
                        SET_FREE(bp);
                     SET_NEXT_BLOCK(bp, vals[na]);
                     SET_PREV_BLOCK(bp, vals[pa]);
                     SET_NEXT_FREE(bp, vals[nf]);
                     SET_PREV_FREE(bp, vals[pf]);
                     SET_SIZE(bp, svals[s]);

                     is_alloc = IS_ALLOC(bp);
                     size = SIZE(bp);
                     nextfree = NEXT_FREE(bp);
                     prevfree = PREV_FREE(bp);
                     nextblock = NEXT_BLOCK(bp);
                     prevblock = PREV_BLOCK(bp);

                     if ((a && !is_alloc) ||
                         (!a && is_alloc) ||
                         (nextblock != vals[na]) ||
                         (prevblock != vals[pa]) ||
                         (nextfree != vals[nf]) ||
                         (prevfree != vals[pf]) ||
                         (size != svals[s])) 
                     {
                        fprintf(stderr, "Error\n");
                        assert(0);
                     }
                  }
}

typedef struct allocations_t {
   void *addr;
   size_t size;
} allocations_t;

#define ALLOCS_SIZE 1024
allocations_t allocs[1024];
void test_heap()
{
   unsigned i, j;
   size_t total = 0, num = 0;
   size_t highest_allocation = 0;

   size_t size = 1024*1024*2;
   void *mem_region = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
   assert(mem_region != MAP_FAILED);
   memset(allocs, 0, sizeof(allocs));

   init_sheep(mem_region, size, 0);
   print_heap();
   
   for (i = 0; i < 512*1024; i++) {
      int do_malloc = 0;

      if (i < 1024) {
         if ((rand() & 3) != 0)
            do_malloc = 1;
      }
      else {
         if ((rand() & 2) != 0)
            do_malloc = 1;
      }
      if (num == 0)
         do_malloc = 1;

      if (do_malloc) {
         allocations_t *spot = NULL;
         for (j = 0; j < ALLOCS_SIZE; j++) {
            if (!allocs[j].addr) {
               spot = allocs + j;
               break;
            }
         }
         if (!spot)
            do_malloc = 0;
         else {
            size_t size;
            if (rand() % 4 == 0)
               size = 4;
            else 
               size = rand() % 8192;
            spot->addr = malloc_sheep(size);
            if (!spot->addr) {
#if defined(PRINT)
               printf("Heap full at %lu bytes allocated in %lu allocations\n", total, num);
#endif
               do_malloc = 0;
            }
            else {
               total += size;
               num += 1;
               spot->size = size;
               if (j > highest_allocation)
                  highest_allocation = j;
               memset(spot->addr, 0xef, spot->size);
#if defined(PRINT)
               printf("Allocated %lu bytes in heap at 0x%lx (total = %lu, num = %lu)\n", 
                      spot->size, ((unsigned char *) spot->addr) - sheep_base, total, num);
#endif
            }
         }
      }

      if (!do_malloc) {
         allocations_t *spot = NULL;
         int start = rand() % (highest_allocation + 1);
         int cur = start;
         unsigned char *p, *q;

         do {
            if (allocs[cur].addr) {
               spot = allocs + cur;
               break;
            }
            cur++;
            if (cur == highest_allocation+1)
               cur = 0;
         } while (start != cur);
         assert(spot);

         p = (unsigned char *) spot->addr;
         for (q = p; q != p + spot->size; q++) assert(*q == 0xef);
         
         free_sheep(spot->addr);
         total -= spot->size;
         num--;
#if defined(PRINT)
         printf("Freed %lu bytes in heap at 0x%lx (total = %lu, num = %lu)\n",
                spot->size, ((unsigned char *) spot->addr) - sheep_base, total, num);
#endif
         spot->addr = NULL;
         spot->size = 0;
      }
      print_heap();
   }

   for (i = 0; i < ALLOCS_SIZE; i++) {
      if (allocs[i].addr) {
         free_sheep(allocs[i].addr);
         allocs[i].addr = NULL;
         allocs[i].size = 0;
         print_heap();
      }
   }
}

int main(int argc, char *argv[])
{
   test_macros();
   test_heap();
   return 0;
}
#endif