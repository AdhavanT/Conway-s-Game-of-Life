#pragma once
#include "platform.h"

typedef Vec2<int64> WorldPos;


struct LiveCellNode
{
	WorldPos pos;
	LiveCellNode* next;
};

#define CELL_TABLE_REFACTOR 1

#define CELL_TABLE_P 4	
#define CELL_INVALID_VALUE INT64MAX

struct CellTableElement
{
	//TODO: pack this properly. 
	WorldPos cached_cell_entries[CELL_TABLE_P];	//NOTE: 4 fits in a single 64 byte cache line. 
};

struct CellTable
{
	MSlice<CellTableElement> table;
	MSlice<LiveCellNode*> extra_cell_list_list;
	MArena arena;
	MSlice<LiveCellNode> node_list;
};

struct Hashtable
{
	LiveCellNode** table_front;
	MArena arena;
	MSlice<LiveCellNode> node_list;
};

struct CameraState
{

	//Camera stuff
	WorldPos world_center;
	vec2f sub_world_center;
	f64 scale;
	//------------------------

};

struct AppMemory
{
	//double buffer hashtable
	uint32 table_size;

#if CELL_TABLE_REFACTOR
	CellTable* active_table;

#else
	Hashtable* active_table;
#endif
	//------------------------

	
	b32 update_grid_flag;	//tells the grid processor to iterate over table instead of stack 


	b32 camera_changed;		//tells the renderer to recalculate the WorldPos for each pixel.

	//------------------------
	b32 cell_removed_from_table;	//Tells the grid processor that a cell was removed from the hash table. 


	CameraState cm;

	void* grid_processor_memory;
	void* input_handling_memory;
	void* render_memory;

};

void init_input_handler(PL* pl, AppMemory* gm);
void handle_input(PL* pl, AppMemory* gm);
void shutdown_input_handler(PL* pl, AppMemory* gm);

void init_grid_processor(PL* pl, AppMemory* gm);
void cellgrid_update_step(PL* pl, AppMemory* gm);
void shutdown_grid_processor(PL* pl, AppMemory* gm);

void init_renderer(PL* pl, AppMemory* gm);
void render(PL* pl, AppMemory* gm);
void shutdown_renderer(PL* pl, AppMemory* gm);

static FORCEDINLINE uint32 hash_pos(WorldPos value, uint32 table_size)
{
	//NOTE: If hash algo is changed, respectively change the wide version used during pixel fill.

	//TODO: proper hash function LOL.
	uint32 hash = (uint32)(value.x * 16 + value.y * 3) & (table_size - 1);
	return hash;
}

#if CELL_TABLE_REFACTOR 
static inline b32 lookup_cell(CellTable* ct, uint32 slot_index, WorldPos pos)
{
	CellTableElement *elem = &ct->table[slot_index];
	WorldPos *cache_it = elem->cached_cell_entries;
	//Looking through the cached values first
	for (int32 i = 0; i < CELL_TABLE_P; i++)
	{
		if (cache_it->x == CELL_INVALID_VALUE)	//at end of cached values. Element isn't here. 
		{
			return FALSE;
		}
		else if (cache_it->x == pos.x && cache_it->y == pos.y)
		{
			return TRUE;
		}
		cache_it++;
	}
	
	//Not in cached elements. Going to go through the linked list if it's not empty (slower).
	//NOTE: Try to avoid this happening as much as possible. It's slower to go through a linked list. 
	LiveCellNode* list_it = ct->extra_cell_list_list[slot_index];
	while (list_it != 0) //there are still more nodes to look through. 
	{
		if (list_it->pos.x == pos.x && list_it->pos.y == pos.y)
		{
			return TRUE;
		}
		list_it = list_it->next;
	}
	return FALSE;	//Not in the linked list either.
}
#else
static inline b32 lookup_cell(Hashtable* ht, uint32 slot_index, WorldPos pos)
{
	LiveCellNode* front = ht->table_front[slot_index];
	while (front != 0)
	{
		if (front->pos.x == pos.x && front->pos.y == pos.y)
		{
			return TRUE;
		}
		front = front->next;
	}
	return FALSE;
}
#endif


#if CELL_TABLE_REFACTOR
static inline void clear_cell_table(CellTable* ct)
{

	__m128i uninit_cell;
	uninit_cell.m128i_i64[0] = CELL_INVALID_VALUE;
	uninit_cell.m128i_i64[1] = CELL_INVALID_VALUE;

	__m128i* it = (__m128i*)ct->table.front;
	for (uint32 i = 0; i < ct->table.size; i++)
	{
		for (int32 ij = 0; ij < CELL_TABLE_P; ij++)
		{
			*it = uninit_cell;
			it++;
		}
	}
	pl_buffer_set(ct->extra_cell_list_list.front, 0, sizeof(ct->extra_cell_list_list.front[0]) * ct->extra_cell_list_list.size);
	ASSERT(it == (__m128i*)(ct->table.front + ct->table.size));
}

static inline b32 remove_cell(CellTable* ct, uint32 hash_index, WorldPos pos)
{
	CellTableElement* elem = &ct->table[hash_index];
	for (int32 i = 0; i < CELL_TABLE_P; i++)
	{
		if (elem->cached_cell_entries[i].x == CELL_INVALID_VALUE)	//end of cache list. Element isn't in the table. 
		{
			return FALSE;
		}
		else if(elem->cached_cell_entries[i] == pos)	//element to remove found in cache.
		{
			for (int32 j = i + 1; j < CELL_TABLE_P; j++)	//left shifting the cache elements. 
			{
				elem->cached_cell_entries[j - 1] = elem->cached_cell_entries[j];
			}

			ct->extra_cell_list_list[hash_index];
			//adding the the first of the extra cell list to end of the cache. 
			if (ct->extra_cell_list_list[hash_index])
			{
				elem->cached_cell_entries[CELL_TABLE_P - 1] = ct->extra_cell_list_list[hash_index]->pos;
				ct->extra_cell_list_list[hash_index] = ct->extra_cell_list_list[hash_index]->next;	//setting the front of extra cells list as the one next in line (left shifting).
			}
			else   //there are no extra cells outside of cache. Just making the last element empty then. 
			{
				elem->cached_cell_entries[CELL_TABLE_P - 1] = { CELL_INVALID_VALUE,CELL_INVALID_VALUE };	//making the end of cache empty.
			}
			return TRUE;
		}
	}

	//element isn't in cache. must look through extra cells. 
	LiveCellNode* prev = ct->extra_cell_list_list[hash_index];
	if (prev != 0)
	{
		if (prev->pos == pos)	//first of the list is the element to remove. 
		{
			ct->extra_cell_list_list[hash_index] = ct->extra_cell_list_list[hash_index]->next;
			return TRUE;
		}
		LiveCellNode* next = prev->next;
		if (next != 0)
		{
			while (next->pos != pos)
			{
				prev = next;	//next is same as prev->next.  
				next = next->next;
				if (next == 0)
				{
					return FALSE;	//end of list
				}
			}
			prev->next = next->next;
			return TRUE;	
		}
		else
		{
			return FALSE;	//no extra cells to look thorugh after the first of list. 
		}
	}
	return FALSE;	//no extra cells to look through.

}
static inline void add_new_cell(CellTable* ct, uint32 hash_index, WorldPos pos)
{
	CellTableElement* elem = &ct->table[hash_index];

	LiveCellNode* new_node = ct->node_list.add(&ct->arena, { pos,0 });
	//looking through cache.
	for (int32 i = 0; i < CELL_TABLE_P; i++)
	{
		if (elem->cached_cell_entries[i].x == CELL_INVALID_VALUE)	//empty slot. need to fill. 
		{
			elem->cached_cell_entries[i] = pos;
			return;
		}
	}
	
	if (ct->extra_cell_list_list[hash_index] != 0)	//cache is full and the "extra" cell list hash begun. 
	{
		LiveCellNode* it = ct->extra_cell_list_list[hash_index];
		while (it->next != 0)
		{
			it = it->next;
		}
		it->next = new_node;
		return;
	}

	//cache is full and extra cell list hasn't started. Need to add as start of list. 
	ct->extra_cell_list_list[hash_index] = new_node;
	return;

}
#else
//---d--
extern int32 max_hash_depth;
//---d--
static inline void append_new_node(Hashtable* ht, uint32 hash_index, WorldPos pos)
{
	//---d--
	int32 depth = 1;
	//---d--


	LiveCellNode* new_node = ht->node_list.add(&ht->arena, { pos, 0 });
	//append to table list
	LiveCellNode* iterator = ht->table_front[hash_index];
	if (iterator == 0)
	{
		ht->table_front[hash_index] = new_node;
	}
	else
	{
		while (iterator->next != 0)
		{
			//---d--
			depth++;
			//---d--
			iterator = iterator->next;
		}
		iterator->next = new_node;
	}
	//---d--
	if (depth > max_hash_depth)
		max_hash_depth = depth;
	//---d--

}
#endif
static FORCEDINLINE int64 f64_to_int64(f64 value)
{
	if (value >= 0.0)
	{
		value += 0.5;
	}
	else
	{
		value -= 0.5;
	}
	return (int64)(value);
}

static FORCEDINLINE int64 f32_to_int64(f32 value)
{
	if (value >= 0.0f)
	{
		value += 0.5f;
	}
	else
	{
		value -= 0.5f;
	}
	return (int64)(value);
}

static inline WorldPos screen_to_world(WorldPos screen_coord, CameraState cm)
{
	Vec2<f64> screen_coordf = { (f64)screen_coord.x, (f64)screen_coord.y };	//adding the sub world position to be scaled along with 
	screen_coordf = { screen_coordf.x * cm.scale , screen_coordf.y * cm.scale };
	WorldPos world = { f64_to_int64(screen_coordf.x + (f64)cm.sub_world_center.x),f64_to_int64(screen_coordf.y + (f64)cm.sub_world_center.y) };
	world += cm.world_center;
	return world;
}