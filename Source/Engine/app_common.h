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
	WorldPos world_center;
	vec2f sub_world_center;
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


	//input handling memory
	uint64 prev_update_tick;
	uint64 update_tick_time;
	b32 cell_removed_from_table;
	b32 paused;
	vec2i prev_mouse_pos;
	b32 in_panning_mode;
	//------------------------

	
	b32 update_grid_flag;	//tells the grid processor to iterate over table instead of stack 

	//renderer update flags
	b32 camera_changed;		//tells the renderer to recalculate the WorldPos for each pixel.

	//------------------------

	void* render_memory;

	CameraState cm;

};

void handle_input(PL* pl, AppMemory* gm);
void cellgrid_update_step(PL* pl, AppMemory* gm);
void render(PL* pl, AppMemory* gm);

static FORCEDINLINE uint32 hash_pos(WorldPos value, uint32 table_size)
{
	//NOTE: If hash algo is changed, respectively change the wide version used during pixel fill.

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