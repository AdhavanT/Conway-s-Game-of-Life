#include "platform.h"

void update(PL* pl, void** game_memory);
void cleanup_game_memory(PL_Memory* arenas, void** game_memory);
void PL_entry_point(PL& pl)
{
	init_memory_arena(&pl.memory.main_arena	, Megabytes(20));

	init_memory_arena(&pl.memory.temp_arena, Megabytes(50));

	pl.window.title = (char*)"Renderer";
	pl.window.window_bitmap.width = 500;
	pl.window.window_bitmap.height = 500;
	pl.window.width = pl.window.window_bitmap.width;
	pl.window.height= pl.window.window_bitmap.height;
	pl.window.user_resizable = FALSE;

	pl.window.window_bitmap.bytes_per_pixel  = 4;

	pl.initialized = FALSE;
	pl.running = TRUE;
	PL_initialize_timing(pl.time);
	PL_initialize_window(pl.window, &pl.memory.main_arena);
	PL_initialize_input_mouse(pl.input.mouse);
	PL_initialize_input_keyboard(pl.input.kb);

	void* game_memory;

	while (pl.running)
	{
		PL_poll_timing(pl.time);
		PL_poll_window(pl.window);
		PL_poll_input_mouse(pl.input.mouse, pl.window);
		PL_poll_input_keyboard(pl.input.kb);

		update(&pl, &game_memory);

		if (pl.input.keys[PL_KEY::ALT].down && pl.input.keys[PL_KEY::F4].down)
		{
			pl.running = FALSE;
		}
		//Refreshing the FPS counter in the window title bar. Comment out to turn off. 
		static f64 timing_refresh = 0;
		static char buffer[256];
		if (pl.time.fcurrent_seconds - timing_refresh > 0.1)//refreshing at a tenth(0.1) of a second.
		{
			int32 frame_rate = (int32)(pl.time.cycles_per_second / pl.time.delta_cycles);
			pl_format_print(buffer, 256, "Time per frame: %.*fms , %dFPS ; Mouse Pos: [x,y]:[%i,%i]\n", 2, (f64)pl.time.fdelta_seconds * 1000, frame_rate, pl.input.mouse.position_x,pl.input.mouse.position_y);
			pl.window.title = buffer;
			timing_refresh = pl.time.fcurrent_seconds;
		}
		PL_push_window(pl.window, TRUE);
	}
	cleanup_game_memory(&pl.memory, &game_memory);
	PL_cleanup_window(pl.window, &pl.memory.main_arena);
}


void draw_rectangle(PL_Window* window, vec2ui bottom_left, vec2ui top_right, vec3f color)
{
	int32 width = top_right.x - bottom_left.x;
	int32 height = top_right.y - bottom_left.y;
	uint32 casted_color = (uint32)(color.r*255.0f) << 16 | (uint32)(color.g*255.0f) << 8 | (uint32)(color.b*255.0f) << 0;
	uint32* ptr = (uint32*)window->window_bitmap.buffer + (bottom_left.y * window->window_bitmap.width) + bottom_left.x;

	uint32 end_shift = window->window_bitmap.width - width;
	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			*ptr = casted_color;
			ptr++;
		}
		ptr += end_shift;
	}
}



void draw_verticle_line(PL_Window* window, uint32 x, uint32 from_y,uint32 to_y,vec3f color)
{
	uint32 casted_color = (uint32)(color.r * 255.0f) << 16 | (uint32)(color.g * 255.0f) << 8 | (uint32)(color.b * 255.0f) << 0;
	uint32* ptr = (uint32*)window->window_bitmap.buffer + x + from_y * window->window_bitmap.width;

	for (uint32 i = from_y; i < to_y; i++)
	{
		*ptr = casted_color;
		ptr += window->window_bitmap.width;
	}
}

void draw_horizontal_line(PL_Window* window, uint32 y,uint32 from_x,uint32 to_x, vec3f color)
{
	uint32 casted_color = (uint32)(color.r * 255.0f) << 16 | (uint32)(color.g * 255.0f) << 8 | (uint32)(color.b * 255.0f) << 0;
	uint32* ptr = (uint32*)window->window_bitmap.buffer + y * window->window_bitmap.width + from_x;

	for (uint32 i = from_x; i < to_x; i++)
	{
		*ptr = casted_color;
		ptr++;
	}
}

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

struct GameMemory
{
	//double buffer hashtable
	uint32 table_size;

	Hashtable table1;
	Hashtable table2;

	Hashtable* active_table;
	//------------------------

	//table update flags
	uint64 prev_update_tick;
	uint64 update_tick_time;
	b32 cell_removed_from_table;
	b32 paused;
	//------------------------

	//Camera stuff
	WorldPos cm_center;
	f64 cm_scale;
	//------------------------

};

FORCEDINLINE uint32 hash_pos(WorldPos value, uint32 table_size)
{
	//TODO: proper hash function LOL.
	uint32 hash = (uint32)(value.x * 16 + value.y * 3) & (table_size - 1);
	return hash;
}

inline b32 lookup_cell(Hashtable* ht, uint32 slot_index, WorldPos pos)
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



inline void append_new_node(Hashtable* ht, uint32 hash_index, WorldPos pos)
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

FORCEDINLINE int64 f64_to_int64(f64 value)
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

WorldPos screen_to_world(WorldPos screen_coord, f64 scale, WorldPos cm_pos)
{
	Vec2<f64> screen_coordf = { (f64)screen_coord.x, (f64)screen_coord.y };
	screen_coordf = { screen_coordf.x * scale, screen_coordf.y * scale};

	WorldPos world = {f64_to_int64(screen_coordf.x),f64_to_int64(screen_coordf.y)};
	world += cm_pos;
	return world;
}

void cellgrid_update_step(PL*pl,GameMemory* gm);
void render(PL* pl, GameMemory* gm);
void update(PL* pl, void** game_memory)
{
	if (pl->initialized == FALSE)
	{
		*game_memory = MARENA_PUSH(&pl->memory.main_arena, sizeof(GameMemory), "Game Memory Struct");
		GameMemory* gm = (GameMemory*)*game_memory;

		//hashtable stuff
		//table size needs to be a power of 2. 
		gm->table_size = { (2 << 10)}; 

		init_memory_arena(&gm->table1.arena, Megabytes(10));
		gm->table1.table_front = (LiveCellNode**)MARENA_PUSH(&gm->table1.arena, sizeof(LiveCellNode*) *  gm->table_size, "HashTable-1 -> table");
		gm->table1.node_list.init(&gm->table1.arena, (char*)"HashTable-1 -> live node list");


		init_memory_arena(&gm->table2.arena, Megabytes(10));
		gm->table2.table_front = (LiveCellNode**)MARENA_PUSH(&gm->table2.arena, sizeof(LiveCellNode*) * gm->table_size , "HashTable-2 -> table");
		gm->table2.node_list.init(&gm->table2.arena, (char*)"HashTable-2 -> live node list");

		gm->active_table = &gm->table1;
		//---------------

		gm->cell_removed_from_table = FALSE;

		//camera stuff
		gm->cm_center = { 0,0 };
		gm->cm_scale = 0.1;

		gm->prev_update_tick = pl->time.current_millis;
		gm->update_tick_time = 100;
		pl->initialized = TRUE;
	}
	GameMemory* gm = (GameMemory*)*game_memory;
	
	if (pl->input.mouse.scroll_delta != 0)
	{
		//TODO: implement some sort of mouse centered zooming ( zooming in towards where the mouse cursor is)
		if (pl->input.mouse.scroll_delta > 0)
		{
			//decreasing scale ( zooming in )
			gm->cm_scale -= (gm->cm_scale - 0.05) * 0.1 * (f64)pl->input.mouse.scroll_delta;
		}
		else
		{
			//increasing scale ( zooming out )
			gm->cm_scale -= (1.0 - gm->cm_scale) * 0.1 * (f64)pl->input.mouse.scroll_delta;
		}
		gm->cm_scale = min(1.0, max(gm->cm_scale, 0.05));
	}
	if (gm->paused)
	{
		if (pl->input.mouse.left.pressed)
		{
			//Set state of cell.

			WorldPos screen_coords = { (int64)pl->input.mouse.position_x - (pl->window.window_bitmap.width / 2),(int64)pl->input.mouse.position_y - (pl->window.window_bitmap.height / 2) };
			screen_coords = screen_to_world(screen_coords, gm->cm_scale, gm->cm_center);
			
			uint32 slot = hash_pos(screen_coords, gm->table_size);
			b32 state = lookup_cell(gm->active_table, slot, screen_coords);
			//add only if state is false (doesn't exist in table). 
			if (!state)
			{
				pl_debug_print("Added: [%i, %i]\n", screen_coords.x, screen_coords.y);
				append_new_node(gm->active_table, slot, screen_coords);
			}
		}
		else if (pl->input.mouse.right.pressed)
		{
			WorldPos screen_coords = { (int64)pl->input.mouse.position_x - (pl->window.window_bitmap.width / 2),(int64)pl->input.mouse.position_y - (pl->window.window_bitmap.height / 2) };
			screen_coords = screen_to_world(screen_coords, gm->cm_scale, gm->cm_center);

			uint32 slot = hash_pos(screen_coords, gm->table_size);

			LiveCellNode* front = gm->active_table->table_front[slot];
			if (front == 0)
			{
				goto ABORT_CELL_REMOVAL;
			}
			if (front->pos.x == screen_coords.x && front->pos.y == screen_coords.y)
			{
				gm->active_table->table_front[slot] = front->next;
			}
			else
			{
				LiveCellNode* prev = front;
				front = front->next;
				while ((front->pos.x != screen_coords.x && front->pos.y != screen_coords.y))
				{
					if (front == 0)
					{
						goto ABORT_CELL_REMOVAL;	//Cell doesn't exist.
					}
					prev = front;
					front = front->next;
				}
				prev->next = front->next;	//removing front from linked list. 
			}
			gm->cell_removed_from_table = TRUE;
		ABORT_CELL_REMOVAL:;
		}

	}


	if (!gm->paused && (pl->time.current_millis >= gm->prev_update_tick + gm->update_tick_time))
	{
		//update grid
		gm->prev_update_tick = pl->time.current_millis;

		cellgrid_update_step(pl,gm);
	}

	if (pl->input.keys[PL_KEY::SPACE].pressed)
	{
		gm->paused = !gm->paused;
	}

	render(pl, gm);
}

void process_cell(LiveCellNode* cell, GameMemory* gm, Hashtable* next_table, MSlice<WorldPos, uint32> &new_cells_tested)
{
	WorldPos* pos = &cell->pos;
	WorldPos lookup_pos[8];
	lookup_pos[0] = { pos->x    , pos->y - 1 };	//bm
	lookup_pos[1] = { pos->x    , pos->y + 1 };	//tm
	lookup_pos[2] = { pos->x + 1, pos->y - 1 };	//br
	lookup_pos[3] = { pos->x + 1, pos->y };		//mr
	lookup_pos[4] = { pos->x + 1, pos->y + 1 };	//tr
	lookup_pos[5] = { pos->x - 1, pos->y - 1 };	//bl
	lookup_pos[6] = { pos->x - 1, pos->y };		//ml
	lookup_pos[7] = { pos->x - 1, pos->y + 1 };	//tl

	uint32 lookup_pos_hash[8];
	//TODO: SIMD this.
	lookup_pos_hash[0] = hash_pos(lookup_pos[0], gm->table_size);
	lookup_pos_hash[1] = hash_pos(lookup_pos[1], gm->table_size);
	lookup_pos_hash[2] = hash_pos(lookup_pos[2], gm->table_size);
	lookup_pos_hash[3] = hash_pos(lookup_pos[3], gm->table_size);
	lookup_pos_hash[4] = hash_pos(lookup_pos[4], gm->table_size);
	lookup_pos_hash[5] = hash_pos(lookup_pos[5], gm->table_size);
	lookup_pos_hash[6] = hash_pos(lookup_pos[6], gm->table_size);
	lookup_pos_hash[7] = hash_pos(lookup_pos[7], gm->table_size);

	b32 surround_state[8] = {};

	uint32 active_around = 0;
	for (uint32 i = 0; i < ArrayCount(lookup_pos); i++)
	{
		uint32 slot = lookup_pos_hash[i];
		surround_state[i] = lookup_cell(gm->active_table, slot, lookup_pos[i]);
		active_around += surround_state[i];
	}

	if (active_around == 2 || active_around == 3)
	{
		//Cell survives! Adding to next hashmap. 
		uint32 slot = hash_pos(cell->pos, gm->table_size);
		append_new_node(next_table, slot, cell->pos);
	}
	//else cell doesn't survive to next state. 

	//Adding dead cells that are around the live cell to be processed at the end. 
	for (uint32 i = 0; i < ArrayCount(surround_state); i++)
	{
		if (surround_state[i] == FALSE)
		{
			//appending new cell to be processed. This is to ensure that the same surrounding 'off' cell isn't processed twice. 
			WorldPos new_cell_pos = lookup_pos[i];

			WorldPos* front = new_cells_tested.front;
			for (uint32 i = 0; i < new_cells_tested.size; i++)
			{
				if (front->x == new_cell_pos.x && front->y == new_cell_pos.y)
				{
					goto SKIP_TEST; //The cell already exists in the list to be processed. no need to add again. 
				}
				front++;
			}
			new_cells_tested.add(&gm->active_table->arena, new_cell_pos);


			//process new cell.
			WorldPos nc_lookup_pos[8];
			nc_lookup_pos[0] = { new_cell_pos.x    , new_cell_pos.y - 1 };	//bm
			nc_lookup_pos[1] = { new_cell_pos.x    , new_cell_pos.y + 1 };	//tm
			nc_lookup_pos[2] = { new_cell_pos.x + 1, new_cell_pos.y - 1 };	//br
			nc_lookup_pos[3] = { new_cell_pos.x + 1, new_cell_pos.y };		//mr
			nc_lookup_pos[4] = { new_cell_pos.x + 1, new_cell_pos.y + 1 };	//tr
			nc_lookup_pos[5] = { new_cell_pos.x - 1, new_cell_pos.y - 1 };	//bl
			nc_lookup_pos[6] = { new_cell_pos.x - 1, new_cell_pos.y };		//ml
			nc_lookup_pos[7] = { new_cell_pos.x - 1, new_cell_pos.y + 1 };	//tl

			b32 nc_surround_state[8] = { 2, 2, 2, 2, 2, 2, 2, 2 };	//not pre-assigned
			//preassigning the surrounding state with the already looked up ones. 
			for (uint32 j = 0; j < ArrayCount(nc_lookup_pos); j++)
			{
				for (uint32 ii = 0; ii < ArrayCount(lookup_pos); ii++)
				{
					if (nc_lookup_pos[j].x == lookup_pos[ii].x && nc_lookup_pos[j].y == lookup_pos[ii].y)
					{
						nc_surround_state[j] = surround_state[ii];
						break;
					}
				}
			}
			//performing lookups on the neighboring cells that aren't near the nearby live cell. 
			for (uint32 j = 0; j < ArrayCount(nc_lookup_pos); j++)
			{
				if (nc_surround_state[j] != 2)	//found by the previous lookup 
				{
					continue;
				}
				else   //performing lookup of cell.
				{
					uint32 nc_lookup_hash = hash_pos(nc_lookup_pos[j], gm->table_size);
					uint32 nc_slot = nc_lookup_hash;
					nc_surround_state[j] = lookup_cell(gm->active_table, nc_slot, nc_lookup_pos[j]);
				}
			}

			//now with the completed nc_surrounding_state table, we can judge whether the cell is turned alive or not. 
			uint32 nc_active_count = 0;
			for (uint32 j = 0; j < ArrayCount(nc_surround_state); j++)
			{
				nc_active_count += nc_surround_state[j];
				ASSERT(nc_surround_state[j] == 0 || nc_surround_state[j] == 1);
			}

			if (nc_active_count == 3)	//cell becomes alive!
			{
				//adding cell to next hashmap
				uint32 nc_new_cell_hash = hash_pos(new_cell_pos, gm->table_size);
				uint32 nc_new_cell_index = nc_new_cell_hash;
				append_new_node(next_table, nc_new_cell_index, new_cell_pos);
			}
		}
	SKIP_TEST:;

	}
}

void cellgrid_update_step(PL* pl,GameMemory* gm)
{
	Hashtable* next_table;
	if (gm->active_table == &gm->table1)
	{
		next_table = &gm->table2;
	}
	else
	{
		next_table = &gm->table1;
	}

	MSlice<WorldPos, uint32> new_cells_tested;	//used to keep track of all the neighbors of lives cells that have been already processed
	new_cells_tested.init(&gm->active_table->arena,"new cells process queue");

	if (!gm->cell_removed_from_table)
	{
		//Just iterating through node stack instead of table. Avoids going through all empty slots in table. 
		LiveCellNode* it = gm->active_table->node_list.front;
		for (uint32 i = 0; i < gm->active_table->node_list.size; i++)
		{
			process_cell(it, gm, next_table, new_cells_tested);
			it++;
		}
	}
	else
	{
		// Iterating through table and processing all nodes in table (because for cell removal, cell is removed from table but not from stack.)
		LiveCellNode** prev_table_front = gm->active_table->table_front;
		for (uint32 table_pos = 0; table_pos < gm->table_size; table_pos++)
		{
			if (prev_table_front[table_pos] != 0)
			{
				LiveCellNode* list_node = prev_table_front[table_pos];
				do
				{
					process_cell(list_node, gm, next_table, new_cells_tested);
					list_node = list_node->next;
				} while (list_node != 0);
			}
		}
		gm->cell_removed_from_table = FALSE;
	}
	//resetting top of the arena to just having the hashtable. 
	new_cells_tested.clear(&gm->active_table->arena);
	gm->active_table->node_list.clear(&gm->active_table->arena);
	gm->active_table->node_list.front = 0;
	gm->active_table->node_list.init(&gm->active_table->arena, gm->active_table->node_list.name);

	//Clearing out previous hashtable (setting to zero to clear it out)
	pl_buffer_set(gm->active_table->arena.base, 0, gm->active_table->arena.top);
	//setting new active table.
	gm->active_table = next_table;
}

struct FrameBuffer
{
	MSlice<WorldPos> buffer;
	size_t width;
	size_t height;
};

void render(PL* pl, GameMemory* gm)
{
	
	pl_buffer_set(pl->window.window_bitmap.buffer, 22, pl->window.window_bitmap.size);

	FrameBuffer fb;
	fb.width = (int32)pl->window.window_bitmap.width;
	fb.height = (int32)pl->window.window_bitmap.height;
	fb.buffer.init_and_allocate(&pl->memory.temp_arena, fb.height * fb.width, "Frame Buffer with WorldPos");

	int32 x_start = -(int32)(fb.width /2);
	int32 x_end = (fb.width % 2 == 0) ? ((-x_start) - 1) : -x_start;

	int32 y_start = -(int32)(fb.height / 2);
	int32 y_end = (fb.height % 2 == 0) ? ((-y_start) - 1) : -y_start;
	
	x_end++;
	y_end++;

	WorldPos* iterator = fb.buffer.front;
	for (int64 y = y_start; y < y_end; y++)
	{
		for (int64 x = x_start; x < x_end; x++)
		{
			WorldPos screen_coords = { x , y };
			*iterator = screen_to_world(screen_coords, gm->cm_scale, gm->cm_center);
			iterator++;
		}
	}

	ASSERT(iterator == (fb.buffer.front + fb.buffer.size ))

	uint32* ptr = (uint32*)pl->window.window_bitmap.buffer;
	WorldPos* it = fb.buffer.front;
	WorldPos* next = fb.buffer.front;
	vec3f on_color = { .5f,.5f,0.0f };
	uint32 casted_on_color = (uint32)(on_color.r * 255.0f) << 16 | (uint32)(on_color.g * 255.0f) << 8 | (uint32)(on_color.b * 255.0f) << 0;
	
	//for first pixel.
	b32 prev_state = {1};
	uint32 slot = hash_pos(*next, gm->table_size);
	prev_state = lookup_cell(gm->active_table, slot, *next);
	if (prev_state)
	{
		//set pixel to yellow (on)
		*ptr = casted_on_color;
	}
	ptr++;
	next++;
	for (uint32 i = 0; i < (fb.height * fb.width - 1); i++)	//filling the rest of them.
	{
		
		b32 state; 
			
		if ((next->x == (next - 1)->x && next->y == (next-1)->y))
		{
			state = prev_state;
		}
		else
		{
			uint32 slot = hash_pos(*next, gm->table_size);
			state = lookup_cell(gm->active_table, slot, *next);
			prev_state = state;
		}
		if (state)
		{
			//set pixel to yellow (on)
			*ptr = casted_on_color;
		}
		ptr++;
		next++;
		
	}

	fb.buffer.clear(&pl->memory.temp_arena);
	
	//WorldPos world_cm_bounds = { (int64)gm->cm_halfwidth * 2, (int64)gm->cm_halfheight * 2 };
	//if (world_cm_bounds.x > (int64)pl->window.window_bitmap.width || world_cm_bounds.y > (int64)pl->window.window_bitmap.height)
	//{
	//	gm->cm_halfheight = (pl->window.window_bitmap.width / 2);
	//	gm->cm_halfwidth = (pl->window.window_bitmap.height / 2);
	//	world_cm_bounds = { (int64)gm->cm_halfwidth * 2, (int64)gm->cm_halfheight * 2 };
	//}

	////correcting aspect ratio - this really should be an assertion. the aspect ratio should be corrected when performing zoom.  
	//f32 world_cm_ar = (f32)gm->cm_halfwidth / (f32)gm->cm_halfheight;
	//f32 screen_cm_ar = (f32)pl->window.window_bitmap.width / (f32)pl->window.window_bitmap.height;
	//if (world_cm_ar < screen_cm_ar)	//the height of world should be lower
	//{
	//	gm->cm_halfheight = (uint64)((f64)screen_cm_ar * (f64)gm->cm_halfwidth);
	//	world_cm_bounds.y = gm->cm_halfheight * 2;
	//	world_cm_ar = (f32)gm->cm_halfwidth / (f32)gm->cm_halfheight;
	//}
	//uint32 cell_size = pl->window.window_bitmap.width / (uint32)world_cm_bounds.x;


	//WorldPos translated_cm_center = { (gm->cm_center.x - (int64)gm->cm_halfwidth),(gm->cm_center.y - (int64)gm->cm_halfheight) };
	//
	//vec2f world_to_screen = {(f32)pl->window.window_bitmap.width/ (f32)(2 * gm->cm_halfwidth), (f32)pl->window.window_bitmap.height / (f32)(2 * gm->cm_halfheight) };
	//ASSERT(world_to_screen.x >= 1.f && world_to_screen.y >= 1.f);	//making sure the world bounds aren't greater than the screen resolution. 
	//
	//for (uint32 i = 0; i < gm->active_table->node_list.size; i++)
	//{
	//	WorldPos to_render = gm->active_table->node_list[i].pos;
	//	WorldPos translated_to_screen = to_render - translated_cm_center;
	//	//clip 
	//	if (translated_to_screen.x >= world_cm_bounds.x || translated_to_screen.y >= world_cm_bounds.y || translated_to_screen.x < 0 || translated_to_screen.y < 0)
	//	{
	//		//dont render
	//	}
	//	else
	//	{
	//		//translating to screen coordinates
	//		vec2ui screen_coords;
	//		screen_coords = { (uint32)((f32)translated_to_screen.x * world_to_screen.x),(uint32)((f32)translated_to_screen.y * world_to_screen.y) };

	//		vec2ui top_right = { screen_coords.x + cell_size, screen_coords.y + cell_size };
	//		//drawing.
	//		draw_rectangle(&pl->window, screen_coords, top_right, { 0.5f,0.2f,0.2f });
	//	}
	//}

	////Rendering grid lines.
	//uint32 ypos = 0;
	//uint32 x_end = cell_size * (uint32)world_cm_bounds.x;

	//for (int64 y = 0; y < world_cm_bounds.y; y++)
	//{
	//	draw_horizontal_line(&pl->window, ypos, 0, x_end, { 0.2f,0.2f,0.2f });
	//	ypos += cell_size;
	//}

	//uint32 xpos = 0;
	//uint32 y_end = cell_size * (uint32)world_cm_bounds.y;
	//for (int64 x = 0; x < world_cm_bounds.x; x++)
	//{
	//	draw_verticle_line(&pl->window, xpos, 0, y_end, { 0.2f,0.2f,0.2f });
	//	xpos += cell_size;
	//}
}

void cleanup_game_memory(PL_Memory* arenas, void** game_memory)
{
	GameMemory* gm = (GameMemory*)*game_memory;
	cleanup_memory_arena(&gm->table1.arena);
	cleanup_memory_arena(&gm->table2.arena);
	MARENA_POP(&arenas->main_arena, sizeof(GameMemory), "Game Memory Struct");
}