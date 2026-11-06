#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/mmap.h>
#include <sys/types.h>
#include "utils.h"

struct meta_data {
  size_t size;
  struct meta_data *Next;
  int free;
  bool is_mmap;
  
};

#define META_SIZE sizeof(struct block_meta)
#define MMAP_THRESHOLD 128*1024;
void* global_base = NULL;
 // utilities
static void split_block(struct meta_data* curr , size_t size) {
    struct meta_data* new_block = (struct meta_data*)((char*)(curr+1) + size); 
    new_block->size = curr->size - META_SIZE - size;
    new_block->Next = curr->Next;
    new_block->free  = 1;
    new_block->is_mmap = false;
    curr->Next = new_block;
    curr->size = size;
}


static struct meta_data* find_free_block(size_t size , struct meta_data **last) {
  struct meta_data* curr = global_base;
  while(curr && !(curr->free && curr->size >= size)) {
    *last = curr;
    curr = curr->Next;
  }
  if(curr->size >= size + META_SIZE + sizeof(void*)) split_block(curr , size );
  return curr;
}

static struct meta_data *allocate_block(size_t size , struct meta_data* last) {
  struct meta_data* block;
  block = sbrk(0);
  void* request = sbrk(size+META_SIZE);
  assert(block==request);
  if(request == (void*)-1) {
    return NULL;
  }
  if(last) last->Next = block;
  block->size = size;
  block->free = 0;
  block->Next = NULL;
  block->is_mmap = false;
  return block;
}

struct meta_data* get_ptr(void* ptr) {
  return (meta_data*)ptr-1;
}

void coalesce_block(struct meta_data* block) {
    if(block->next && block->next->free) {
    block->size = block->next->size + META_SIZE;
    block->next = block->next->next;
  }
  struct meta_data* curr = global_base;
  while(curr && curr->Next != block) {
   curr = curr->Next; 
  }
  if(curr && curr->free) {
    curr->size = META_SIZE + block->size;
    curr->Next = block->Next;
  }
}


 // API for allocators
void* my_malloc(size_t size) {
  if(size <= 0) return NULL;
  size = (size - sizeof(void*)-1) & (sizeof(void*) - 1);
  struct meta_data* block;
  if(size >= MMAP_THRESHOLD) {
    void* ptr = mmap(NULL , size+META_SIZE , PROT_READ | PROT_WRITE |  , MAP_PRIVATE | MAP_ANONYMOUS , -1 , 0);
    if(!ptr) return NULL;
    struct meta_data *block = (struct meta_data*) ptr;
    block->free = 0;
    block->size = size;
    block->NEXT = NULL;
    block->is_mmap =true;
  }
  else {
  if(!global_base) {
  block = allocate_block(size , NULL);
    if(!block) return NULL;
    global_base = block;
  }
  else {
    struct meta_data *last = global_base;
    block = find_free_block(size , &last);
    if(!block) {
      block = allocate_block(size , last);
      if(!block) return NULL;
    }
    else {
      block->free = 0;
    }
      }
  }
  return (block+1);
}


void my_free(void* ptr) {
  if(!ptr) return;
  struct meta_data* block = get_ptr(ptr);
  if(block->is_mmap) 
  {
    if((munmap(block , block->size+META_SIZE))==0) {
      perror("munmap failed");
      exit(1);
    }
    return;
  }
  else {
  assert(block->free == 0);
  block->free = 1;
  coalesce_block(block);
  }
  }

void* my_realloc(void* ptr , size_t size) {
  if(size < 0 ) {
    return NULL;
  }
  if(!ptr) return my_malloc(size);
  if(size==0) my_free(ptr);
  
  size = (size + sizeof(void*) - 1) & ~(sizeof(void*) - 1);
  struct meta_data* block = get_ptr(ptr);
  if(!block->is_mmap && block->size > size) {
    split_block(block , size);
    return ptr;
  }

   if(!block->is_mmap && block->Next && block->Next->free) {
      if(block->Next->size + block->size + META_SIZE >= size) {
        block->size = block->next->size+META_SIZE+block->size;
        block->Next = block->Next->Next;
        split_block(block , size);
        return ptr;
      }
      }
  void *new_ptr = malloc(size);
  if(!new_ptr) return NULL;
  memcpy(new_ptr , ptr , block->size);
  my_free(ptr);
  return new_ptr;
}

void* calloc(size_t n , size_t size) {
  if(n != 0 && size > SIZE_MAX/n) return NULL;
  void *ptr = malloc(n*size);
  if(ptr == NULL) return NULL;
  memset(ptr , 0 , n*size);
  return ptr;
}
