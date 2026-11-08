// section informed by:
// https://danluu.com/malloc-tutorial/
// https://cs341.cs.illinois.edu/coursebook/Malloc
// https://levelup.gitconnected.com/malloc-from-scratch-dbc1bc23dfde
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * typedef struct {
 * 	unsigned int block_size : 7;
 * 	unsigned int is_free : 1;
 * } size_free;
 */

typedef struct jmem_block_meta {
  size_t size;
  struct jmem_block_meta *next;
  int free;
  bool is_mmap;
  int magic;
} jmem_block_meta_t;

const size_t JMEM_BLOCK_META_SIZE = sizeof(jmem_block_meta_t);
const size_t MMAP_THRESHOLD = 128 * 1024;

size_t jmem_align_size(size_t size) {
  return (size + (sizeof(void *) - 1)) & ~(sizeof(void *) - 1);
}

jmem_block_meta_t *_jmem_global_base = NULL;

jmem_block_meta_t *jmem_get_meta_block_ptr(void *ptr) {
  return (jmem_block_meta_t *)ptr - 1;
}

// https://users.cs.northwestern.edu/~pdinda/ics-s05/doc/dsa.pdf
void jmem_split_block(jmem_block_meta_t *block, size_t size) {
  // if current size is more than needed,
  if (block->size >= size + JMEM_BLOCK_META_SIZE + sizeof(void *)) {
    // then make a new block with the extra space and set the
    // next of this block to that free memory
    jmem_block_meta_t *new_block =
        (jmem_block_meta_t *)((char *)(block + 1) + size);
    new_block->size = block->size - size - JMEM_BLOCK_META_SIZE;
    new_block->next = block->next;
    new_block->free = true;
    new_block->is_mmap = false;

    block->size = size;
    block->next = new_block;
  }
}

// TODO (Josh): Combine while next->free == 1
void jmem_combine_free_blocks(jmem_block_meta_t *block) {
  // check next block for free
  if (block->next && block->next->free == 1) {
    block->size += JMEM_BLOCK_META_SIZE + block->next->size;
    block->next = block->next->next;
  }
  jmem_block_meta_t *curr = _jmem_global_base;
  // find previous block
  while (curr && curr->next != block) {
    curr = curr->next;
  }
  if (curr && curr->free == 1) {
    curr->size += JMEM_BLOCK_META_SIZE + block->size;
    curr->next = block->next;
  }
}

jmem_block_meta_t *find_free_block(jmem_block_meta_t **last, size_t size) {
  jmem_block_meta_t *curr = _jmem_global_base;
  while (curr != NULL && !(curr->free && (curr->size >= size))) {
    *last = curr;
    curr = curr->next;
  }
  // will be NULL if at end of list
  return curr;
}

jmem_block_meta_t *request_space(jmem_block_meta_t *last, size_t size) {
  jmem_block_meta_t *block;
  block = (jmem_block_meta_t *)sbrk(0);
  // align to next whole 16-byte unit size
  size_t aligned_size = jmem_align_size(size);
  if (size < MMAP_THRESHOLD) {
    void *request = sbrk(aligned_size + JMEM_BLOCK_META_SIZE);
    if (request == (void *)-1) {
      return NULL;
    }
    if (last) {
      last->next = block;
    }
    block->is_mmap = false;
  } else {
    void *ptr =
        mmap(NULL, aligned_size + JMEM_BLOCK_META_SIZE, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
      return NULL;
    }
    block->is_mmap = true;
  }
  block->size = size;
  block->next = NULL;
  block->free = 0;
  block->magic = 0x12345678;
  return block;
}

void *jmalloc(size_t size) {
  jmem_block_meta_t *block;
  if (size <= 0) {
    return NULL;
  }
  // if global base of list is null
  if (!_jmem_global_base) {
    // this is the first allocation
    block = request_space(NULL, size);
    if (!block) {
      return NULL;
    }
    // unless we mmap, then leave global base as null
    if (!block->is_mmap) {
      _jmem_global_base = block;
    }
  } else {
    jmem_block_meta_t *last = _jmem_global_base;
    block = find_free_block(&last, size);
    if (!block) {
      block = request_space(last, size);
      if (!block) {
        return NULL;
      }
    } else {
      jmem_split_block(block, size);
      block->free = 0;
      block->magic = 0x77777777;
    }
  }
  return (block + 1); // return a pointer to the region after the metadata
}

void jfree(void *ptr) {
  if (!ptr) {
    return;
  }
  jmem_block_meta_t *meta_block = jmem_get_meta_block_ptr(ptr);
  assert(meta_block->free == 0); // enforce block is not already free invariant
  // enforce block was created by us invariant
  assert(meta_block->magic == 0x77777777 || meta_block->magic == 0x12345678);

  if (meta_block->is_mmap) {
    munmap(meta_block, meta_block->size + JMEM_BLOCK_META_SIZE);
  } else {
    meta_block->free = 1;
    meta_block->magic = 0x55555555;
    jmem_combine_free_blocks(meta_block);
  }
}

void *jrealloc(void *ptr, size_t size) {
  if (!ptr) {
    return jmalloc(size);
  }

  if (size == 0) {
    jfree(ptr);
    return NULL;
  }

  jmem_block_meta_t *meta_block = jmem_get_meta_block_ptr(ptr);
  size_t aligned_size = jmem_align_size(size);
  // if we already have enough or more than enough space in current block
  if (meta_block->size >= aligned_size) {
    // split current block to decrease size
    jmem_split_block(meta_block, aligned_size);
    return ptr;
  }
  // otherwise, if we have enough space in the next block and the next block is
  // free
  if (!meta_block->is_mmap && meta_block->next && meta_block->next->free &&
      meta_block->size + JMEM_BLOCK_META_SIZE + meta_block->next->size >=
          aligned_size) {
    // we can combine the two blocks and split the remaining free space
    meta_block->size += JMEM_BLOCK_META_SIZE + meta_block->next->size;
    meta_block->next = meta_block->next->next;
    jmem_split_block(meta_block, aligned_size);
    return ptr;
  }

  // otherwise, need to allocate new memory
  void *new_region;
  new_region = jmalloc(size);
  if (!new_region) {
    return NULL;
  }

  memcpy(new_region, ptr, meta_block->size);
  jfree(ptr);
  return new_region;
}

void *jcalloc(size_t num_elem, size_t el_size) {
  void *new_region;
  size_t size = num_elem * el_size;
  // TODO (Josh): Hackers delight leading zeros check
  if (size / el_size != num_elem) {
    // basic underflow check
    return NULL;
  }
  new_region = jmalloc(size);
  if (!new_region) {
    return NULL;
  }

  memset(new_region, 0, size);
  return new_region;
}
