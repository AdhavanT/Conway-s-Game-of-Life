#include "app_common.h"

void init(PL* pl, void** game_memory);
void update(PL* pl, void** game_memory);
void cleanup_game_memory(PL* pl, void** game_memory);

//---d--
int32 max_hash_depth = 0;

//---d--

void PL_entry_point(PL& pl)
{
	init_memory_arena(&pl.memory.main_arena	, Megabytes(100));

	init_memory_arena(&pl.memory.temp_arena, Megabytes(50));

	pl.window.title = (char*)"Renderer";
	vec2ui dim =
	{
		1920,1080
		//1280,720
		//1920, 1079
		//1919, 1079
		
	};
	pl.window.window_bitmap.width = dim.x;
	pl.window.window_bitmap.height = dim.y;
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

	init(&pl, &game_memory);
	while (pl.running)
	{
		PL_poll_timing(pl.time);
		PL_poll_window(pl.window);
		PL_poll_input_mouse(pl.input.mouse, pl.window);
		PL_poll_input_keyboard(pl.input.kb);

		update(&pl, &game_memory);

		if (pl.input.keys[PL_KEY::ALT].down && pl.input.keys[PL_KEY::F4].down || pl.input.keys[PL_KEY::ESCAPE].down)
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
	cleanup_game_memory(&pl, &game_memory);
	PL_cleanup_window(pl.window, &pl.memory.main_arena);
	cleanup_memory_arena(&pl.memory.main_arena);
}


#include "ATProfiler/atp.h"
void print_out_tests(PL& pl);
ATP_REGISTER(main_update_loop); 
ATP_REGISTER(cellgrid_update);

void update(PL* pl, void** game_memory)
{
	ATP_START(main_update_loop);

	AppMemory* gm = (AppMemory*)*game_memory;

	handle_input(pl, gm);

	//stats n stuff
	pl_debug_print("No. of live cells: %i\n", gm->active_table->node_list.size);
	pl_debug_print("Max hash depth:%i\n", max_hash_depth);

	if (gm->update_grid_flag)
	{
		ATP_BLOCK(cellgrid_update);
		cellgrid_update_step(pl,gm);
		gm->update_grid_flag = FALSE;
	}

	render(pl, gm);
	ATP_END(main_update_loop);

	print_out_tests(*pl);
}



void clean_render_memory(PL* pl, AppMemory* gm);

void cleanup_game_memory(PL* pl, void** game_memory)
{
	AppMemory* gm = (AppMemory*)*game_memory;

	clean_render_memory(pl, gm);

	//clean common memory
	
	
	MARENA_POP(&pl->memory.main_arena, gm->table2.arena.capacity, "Sub Arena: HashTable-2");
	MARENA_POP(&pl->memory.main_arena, gm->table1.arena.capacity, "Sub Arena: HashTable-1");

	MARENA_POP(&pl->memory.main_arena, sizeof(AppMemory), "Game Memory Struct");
}

void init_render_memory(PL* pl, AppMemory* gm);	//defined in renderer.cpp

void init(PL* pl, void** game_memory)
{
	if (pl->initialized == FALSE)
	{
		//init common memory
		*game_memory = MARENA_PUSH(&pl->memory.main_arena, sizeof(AppMemory), "Game Memory Struct");
		AppMemory* gm = (AppMemory*)*game_memory;

		//hashtable stuff
		//table size needs to be a power of 2. 
		gm->table_size = { (2 << 10) };
		
		init_memory_arena(&gm->table1.arena, Megabytes(10), MARENA_PUSH(&pl->memory.main_arena, Megabytes(10), "Sub Arena: HashTable-1"));
		gm->table1.table_front = (LiveCellNode**)MARENA_PUSH(&gm->table1.arena, sizeof(LiveCellNode*) * gm->table_size, "HashTable-1 -> table");
		gm->table1.node_list.init(&gm->table1.arena, (char*)"HashTable-1 -> live node list");


		init_memory_arena(&gm->table2.arena, Megabytes(10), MARENA_PUSH(&pl->memory.main_arena, Megabytes(10), "Sub Arena: HashTable-2"));
		gm->table2.table_front = (LiveCellNode**)MARENA_PUSH(&gm->table2.arena, sizeof(LiveCellNode*) * gm->table_size, "HashTable-2 -> table");
		gm->table2.node_list.init(&gm->table2.arena, (char*)"HashTable-2 -> live node list");

		gm->active_table = &gm->table1;
		//---------------


		gm->camera_changed = TRUE;

		gm->cell_removed_from_table = FALSE;

		//camera stuff
		gm->cm.world_center = { 0,0 };
		gm->cm.sub_world_center = { 0,0 };
		gm->cm.scale = 0.1;

		gm->prev_update_tick = pl->time.current_millis;
		gm->update_tick_time = 100;
		gm->prev_mouse_pos = { 0,0 };

		init_render_memory(pl, gm);

		pl->initialized = TRUE;
	}
}

void print_out_tests(PL& pl)
{
	f64 frequency = (f64)pl.time.cycles_per_second;
	

	int32 length = ATP::testtype_registry->no_of_testtypes;
	ATP::TestType* front = ATP::testtype_registry->front;
	for (int i = 0; i < length; i++)
	{
		if (front->type == ATP::TestTypeFormat::MULTI)
		{
			pl_debug_print("	MULTI TEST (ATP->%s):\n", front->name);
			ATP::TestInfo* index = front->tests.front;
			uint64 total = 0;
			for (uint32 i = 0; i < front->tests.size; i++)
			{
				total += index->test_run_cycles;
				f64 ms = (index->test_run_cycles * 1000 / frequency);
				pl_debug_print("		index:%i:%.*f ms (%.*f s),%I64u\n", i, 3, ms, 4, ms / 1000, index->test_run_cycles);
				index++;
			}
			f64 ms = (total * 1000 / frequency);
			pl_debug_print("	total:%.*f ms (%.*f s), %I64u\n", 3, ms, 4, ms / 1000, total);

		}
		else
		{
			f64 ms = ATP::get_ms_from_test(*front);
			pl_debug_print("	Time Elapsed(ATP->%s): %I64u, %.*f ms (%.*f s)\n", front->name, front->info.test_run_cycles,3, ms, 4, ms / 1000);
		}
		front++;
	}
	pl_debug_print("\n\n\n\n\n");

}