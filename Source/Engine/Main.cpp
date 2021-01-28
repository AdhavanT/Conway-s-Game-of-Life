#include "platform.h"

void update(PL* pl, void** game_memory);
void cleanup_game_memory(PL_Memory* arenas, void** game_memory);
void PL_entry_point(PL& pl)
{
	init_memory_arena(&pl.memory.main_arena	, Megabytes(20));

	init_memory_arena(&pl.memory.temp_arena, Megabytes(10));

	pl.window.title = (char*)"Renderer";
	pl.window.window_bitmap.width = 1280;
	pl.window.window_bitmap.height = 720;
	pl.window.width = pl.window.window_bitmap.width;
	pl.window.height= pl.window.window_bitmap.height;

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
	//Camera stuff
	WorldPos cm_center;
	uint64 cm_halfwidth;
	uint64 cm_halfheight;


	uint64 prev_update_tick;
	uint64 update_tick_time;
	b32 paused;
};




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

		init_memory_arena(&gm->table1.arena, Megabytes(20));
		gm->table1.table_front = (LiveCellNode**)MARENA_PUSH(&gm->table1.arena, sizeof(LiveCellNode*) *  gm->table_size, "HashTable-1 -> table");
		gm->table1.node_list.init(&gm->table1.arena, (char*)"HashTable-1 -> live node list");


		init_memory_arena(&gm->table2.arena, Megabytes(20));
		gm->table2.table_front = (LiveCellNode**)MARENA_PUSH(&gm->table2.arena, sizeof(LiveCellNode*) * gm->table_size , "HashTable-2 -> table");
		gm->table2.node_list.init(&gm->table2.arena, (char*)"HashTable-2 -> live node list");

		gm->active_table = &gm->table1;
		//---------------

		//camera stuff
		gm->cm_center = { 0,0 };
		gm->cm_halfheight = 20;
		gm->cm_halfwidth = 20;

		gm->prev_update_tick = pl->time.current_millis;
		gm->update_tick_time = 100;
		pl->initialized = TRUE;
	}
	GameMemory* gm = (GameMemory*)*game_memory;
	
	if (gm->paused)
	{
		if (pl->input.mouse.left.pressed)
		{

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

FORCEDINLINE uint32 hash_pos(WorldPos value)
{
	//TODO: proper hash function LOL.
	uint32 hash = (uint32)(value.x * 16 + value.y * 3);
	return hash;
}

inline b32 loopup_cell(Hashtable* ht, uint32 slot_index, WorldPos pos)
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

	//looking through all live cells and their neighbors and processing them.
	LiveCellNode** prev_table_front = gm->active_table->table_front;
	for (uint32 table_pos = 0; table_pos < gm->table_size; table_pos++)
	{
		if (prev_table_front[table_pos] != 0)
		{
			LiveCellNode* list_node = prev_table_front[table_pos];
			do
			{
				//processing livecell node:
				//lookup cells around it:
				//for every cell around it: hash position, lookup value in hash
				WorldPos* pos = &list_node->pos;
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
				lookup_pos_hash[0] = hash_pos(lookup_pos[0]);
				lookup_pos_hash[1] = hash_pos(lookup_pos[1]);
				lookup_pos_hash[2] = hash_pos(lookup_pos[2]);
				lookup_pos_hash[3] = hash_pos(lookup_pos[3]);
				lookup_pos_hash[4] = hash_pos(lookup_pos[4]);
				lookup_pos_hash[5] = hash_pos(lookup_pos[5]);
				lookup_pos_hash[6] = hash_pos(lookup_pos[6]);
				lookup_pos_hash[7] = hash_pos(lookup_pos[7]);

				b32 surround_state[8] = {};

				uint32 active_around = 0;
				for (uint32 i = 0; i < ArrayCount(lookup_pos); i++)
				{
					uint32 slot = lookup_pos_hash[i] & (gm->table_size - 1);
					surround_state[i] = loopup_cell(gm->active_table, slot, lookup_pos[i]);
					active_around += surround_state[i];
				}

				if (active_around == 2 || active_around == 3)
				{
					//Cell survives! Adding to next hashmap. 

					append_new_node(next_table, table_pos, list_node->pos);
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

						b32 nc_surround_state[8] = {2};	//not pre-assigned
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
								uint32 nc_lookup_hash = hash_pos(nc_lookup_pos[j]);
								uint32 nc_slot = nc_lookup_hash & (gm->table_size - 1);
								nc_surround_state[j] = loopup_cell(gm->active_table, nc_slot, nc_lookup_pos[j]);
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
							uint32 nc_new_cell_hash = hash_pos(new_cell_pos);
							uint32 nc_new_cell_index = nc_new_cell_hash & (gm->table_size - 1);
							append_new_node(next_table, nc_new_cell_index, new_cell_pos);
						}
					}
				SKIP_TEST:;

				}

				list_node = list_node->next;
			} while (list_node != 0);
		}
	}
	//resetting top of the arena to just having the hashtable. 
	new_cells_tested.clear(&gm->active_table->arena);
	gm->active_table->node_list.clear(&gm->active_table->arena);

	//Clearing out previous hashtable (setting to zero to clear it out)
	pl_buffer_set(gm->active_table->arena.base, 0, gm->active_table->arena.top);
	//setting new active table.
	gm->active_table = next_table;
}

void render(PL* pl, GameMemory* gm)
{
	
}

void cleanup_game_memory(PL_Memory* arenas, void** game_memory)
{
	GameMemory* gm = (GameMemory*)*game_memory;
	cleanup_memory_arena(&gm->table1.arena);
	cleanup_memory_arena(&gm->table2.arena);
	MARENA_POP(&arenas->main_arena, sizeof(GameMemory), "Game Memory Struct");
}