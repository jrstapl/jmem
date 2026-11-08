// this implementation is provided by ginger bill:
// https://www.gingerbill.org/article/2019/02/08/memory-allocation-strategies-002/
#include <assert.h>
#include <memory.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef DEFAULT_ALIGNMENT
#define DEFAULT_ALIGNMENT (2 * sizeof(void *))
#endif

bool jmem_is_power_of_two(uintptr_t x) { return (x & (x - 1)) == 0; }
uintptr_t jmem_align_ptr_foward(uintptr_t ptr, size_t align) {
  uintptr_t adjusted_ptr, aligned_whole, size_need_to_fill;

  // enforce alignment invariant that target alignment is power of two
  assert(jmem_is_power_of_two(align));

  adjusted_ptr = ptr;
  aligned_whole = (uintptr_t)align;

  // Bill: same as p%a but faster becuase a is known power of two
  // translation: adjusted_ptr & (aligned_whole - 1) provides accurate
  // remainder because we know aligned_whole is power of 2
  size_need_to_fill = adjusted_ptr & (aligned_whole - 1);

  // if size_need_to_fill is 0, we're already aligned
  if (size_need_to_fill != 0) {
    // else, align to next power of two address

    adjusted_ptr += (aligned_whole - size_need_to_fill);
  }
  return adjusted_ptr;
}

// TODO (Josh): Use an array of starting offsets instead of just a one previous
typedef struct JArena {
  unsigned char *arena_base_addr;
  size_t max_size;
  size_t current_number_allocations;
  size_t current_offset;
  size_t previous_allocation_start_offset;

} jarena_t;

jarena_t *jmem_arena_init(size_t arena_size) {
  size_t arena_size_aligned =
      arena_size + (arena_size & (DEFAULT_ALIGNMENT - 1));
  assert((arena_size_aligned & (arena_size_aligned - 1)) == 0);
  unsigned char *arena_buffer = (unsigned char *)malloc(arena_size);
  jarena_t *arena = malloc(sizeof(jarena_t) - 1);

  arena->arena_base_addr = arena_buffer;
  arena->max_size = arena_size;
  arena->current_number_allocations = 0;
  arena->current_offset = 0;
  arena->previous_allocation_start_offset = 0;

  return arena;
}

void *jmem_arena_alloc_aligned(jarena_t *arena, size_t size, size_t align) {

  // Bill: Align current offset forward to specified alignment

  uintptr_t curr_ptr =
      (uintptr_t)arena->arena_base_addr + (uintptr_t)arena->current_offset;
  uintptr_t offset = jmem_align_ptr_foward(curr_ptr, align);
  // make offset relative by subtracting starting addr
  offset -= (uintptr_t)arena->arena_base_addr;

  // ensure no overflow
  size_t new_offset = offset + size;
  if (new_offset > arena->max_size) {
    return NULL;
  }

  void *ptr = &arena->arena_base_addr[offset];
  arena->previous_allocation_start_offset = offset;
  arena->current_offset = new_offset;
  memset(ptr, 0, size); // zero incoming new memory
  return ptr;           // void * will be cast to desired type
}
void *jmem_arena_alloc(jarena_t *arena, size_t size) {
  return jmem_arena_alloc_aligned(arena, size, DEFAULT_ALIGNMENT);
}

// in order to do this, we would need to keep a stack of offsets
void jmem_arena_remove_last(jarena_t *arena) {
  size_t last_alloc_size =
      arena->current_offset - arena->previous_allocation_start_offset;
  void *ptr = &arena->arena_base_addr[arena->previous_allocation_start_offset];
  memset(ptr, 0, last_alloc_size);
  arena->current_offset = arena->previous_allocation_start_offset;
  arena->current_number_allocations--;
}

void jmem_arena_clear(jarena_t *arena) {
  arena->current_offset = 0;
  arena->previous_allocation_start_offset = 0;
  arena->current_number_allocations = 0;
}
