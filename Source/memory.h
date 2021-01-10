#pragma once
#include "PL/pl_utils.h"

#define Kilobytes(n)  (n << 10)
#define Megabytes(n)  (n << 20)
#define Gigabytes(n)  (((uint64)n) << 30)
#define Terabytes(n)  (((uint64)n) << 40)

#define DEFAULT_MAX_MEMORY_RESERVED Gigabytes(4)
#define MEMORY_OVERFLOW_ADDON Gigabytes(1)

struct MArena
{
	void* base = 0;	
	uint64 capacity = DEFAULT_MAX_MEMORY_RESERVED;
	uint64 top = 0;
};

void init_memory_arena(MArena* arena)
{
	if (arena->base != 0)
	{
		ASSERT(FALSE);	//base is already pointing to something
	}
	if (arena->capacity == 0)
	{
		arena->capacity = DEFAULT_MAX_MEMORY_RESERVED;
	}
	arena->base = pl_buffer_alloc(arena->capacity);
	arena->top = 0;
}

FORCEDINLINE void* marena_alloc(MArena* arena, size_t size)
{
	if (arena->top + size > arena->capacity)
	{
		ASSERT(FALSE);	//filled the arena completely!
		arena->capacity = arena->capacity + MEMORY_OVERFLOW_ADDON;
		arena->base = pl_buffer_resize(arena->base, arena->capacity);

	}
	void* memory = (uint8*)arena->base + arena->top;
	arena->top += size;
	return memory;
}

FORCEDINLINE void* marena_free(MArena* arena, void* from, size_t buffer_size)
{
	ASSERT(((size_t)from >= (size_t)arena->base) && ((size_t)from <= (size_t)((uint8*)arena->base + arena->top)));
	//pl_buffer_copy(from, )
}