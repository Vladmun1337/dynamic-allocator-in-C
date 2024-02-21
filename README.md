# dynamic-allocator-in-C

This project served to help get a better understanding of the way the operating system handles memory and
helped me rethink the idea of dynamic allocations during programming.

## How it works

The main idea of the dynamic memory allocation is to reserve a **128kb memory zone** and later adjust to the
size we need through trimming and breaking apart of memory for later chunks. This is done using the **sbrk**
system call. Any memory greater than our threshold will be mapped using **mmap**.

By this neat design the ***os_malloc*** funtion is implemented. ***os_calloc*** takes an almost identical approach,
only thing being that the memory zone ready to be allocated is initialized with values of 0. ***os_realloc***
will be equivalent to malloc call if no better candidate is found, otherwise it will extend or stick to a free
memory block.

## Return Values

OS_MALLOC, OS_CALLOC, OS_REALLOC:
- NULL, for invalid entry (size out of comprehensive bounds, nonexistent block to reallocate)
- The memory zone just allocated

FREE:
- No return, used only for deallocation

## Optimisations

- **Memory Allignment**
- **Linked list structure for deallocation**
- **Preallocation through pagination**
- **Block coalescing**
- **Reuse of previously freed blocks of memory**
