#pragma once
#include "platform.h"

typedef Vec2<int64> WorldPos;

struct LiveCellNode
{
	WorldPos pos;
	LiveCellNode* next;
};

struct NewCellNode
{
	WorldPos pos;
	NewCellNode* next;
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
	WorldPos center;
	f64 scale;
	//------------------------

};

struct AppMemory
{
	//double buffer hashtable
	uint32 table_size;

	Hashtable table1;
	Hashtable table2;

	Hashtable* active_table;
	//------------------------

	//table update flags
	b32 update_grid_flag;

	uint64 prev_update_tick;
	uint64 update_tick_time;
	b32 cell_removed_from_table;
	b32 paused;
	//------------------------


	//renderer update flags
	b32 camera_changed;		//tells the renderer to recalculate the WorldPos for each pixel.
	//------------------------

	CameraState cm;

};

void handle_input(PL* pl, AppMemory* gm);
void cellgrid_update_step(PL* pl, AppMemory* gm);
void render(PL* pl, AppMemory* gm);

static FORCEDINLINE uint32 hash_pos(WorldPos value, uint32 table_size)
{
	//TODO: proper hash function LOL.
	uint32 hash = (uint32)(value.x * 16 + value.y * 3) & (table_size - 1);
	return hash;
}

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
 
static inline void append_new_node(Hashtable* ht, uint32 hash_index, WorldPos pos)
{
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
			iterator = iterator->next;
		}
		iterator->next = new_node;
	}
}

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

static inline WorldPos screen_to_world(WorldPos screen_coord, f64 scale, WorldPos cm_pos)
{
	Vec2<f64> screen_coordf = { (f64)screen_coord.x, (f64)screen_coord.y };
	screen_coordf = { screen_coordf.x * scale, screen_coordf.y * scale };

	WorldPos world = { f64_to_int64(screen_coordf.x),f64_to_int64(screen_coordf.y) };
	world += cm_pos;
	return world;
}