#include "app_common.h"


void update(PL* pl, void** game_memory);
void cleanup_game_memory(PL_Memory* arenas, void** game_memory);
void PL_entry_point(PL& pl)
{
	init_memory_arena(&pl.memory.main_arena	, Megabytes(20));

	init_memory_arena(&pl.memory.temp_arena, Megabytes(50));

	pl.window.title = (char*)"Renderer";
	pl.window.window_bitmap.width = 1000;
	pl.window.window_bitmap.height = 1000;
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


void update(PL* pl, void** game_memory)
{
	if (pl->initialized == FALSE)
	{
		*game_memory = MARENA_PUSH(&pl->memory.main_arena, sizeof(AppMemory), "Game Memory Struct");
		AppMemory* gm = (AppMemory*)*game_memory;

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
		gm->camera_changed = TRUE;

		gm->cell_removed_from_table = FALSE;

		//camera stuff
		gm->cm.center = { 0,0 };
		gm->cm.scale = 0.1;

		gm->prev_update_tick = pl->time.current_millis;
		gm->update_tick_time = 100;
		pl->initialized = TRUE;
	}
	AppMemory* gm = (AppMemory*)*game_memory;
	

	handle_input(pl, gm);

	if (gm->update_grid_flag)
	{
		cellgrid_update_step(pl,gm);
		gm->update_grid_flag = FALSE;
	}

	render(pl, gm);
}



void cleanup_game_memory(PL_Memory* arenas, void** game_memory)
{
	AppMemory* gm = (AppMemory*)*game_memory;
	cleanup_memory_arena(&gm->table1.arena);
	cleanup_memory_arena(&gm->table2.arena);
	MARENA_POP(&arenas->main_arena, sizeof(AppMemory), "Game Memory Struct");
}