#include "app_common.h"

static void init(PL* pl, void** game_memory);
static void update(PL* pl, void** game_memory);
static void shutdown(PL* pl, void** game_memory);

//---d--
int32 max_hash_depth = 0;

//---d--

void PL_entry_point(PL& pl)
{
	init_memory_arena(&pl.memory.main_arena	, Megabytes(200));

	init_memory_arena(&pl.memory.temp_arena, Megabytes(50));


	pl.initialized = FALSE;
	pl.running = TRUE;


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
			update(&pl, &game_memory);

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
	shutdown(&pl, &game_memory);
	cleanup_memory_arena(&pl.memory.main_arena);
}

static void init(PL* pl, void** game_memory)
{
	PL_initialize_timing(pl->time);
	PL_initialize_input_mouse(pl->input.mouse);
	PL_initialize_input_keyboard(pl->input.kb);

	//init common memory
	*game_memory = MARENA_PUSH(&pl->memory.main_arena, sizeof(AppMemory), "Game Memory Struct");
	AppMemory* gm = (AppMemory*)*game_memory;

	//camera stuff
	gm->camera_changed = TRUE;	//Triggers the renderer to calculate the worldpos framebuffer
	gm->cm.world_center = { 0,0 };
	gm->cm.sub_world_center = { 0,0 };
	gm->cm.scale = 0.1;

	gm->cell_removed_from_table = FALSE;

	gm->update_grid_flag = TRUE;	//allows the grid processor to initilize with everyone else one frame 1. 

	//initing the input handler
	init_input_handler(pl, gm);

	//initing the grid processor
	init_grid_processor(pl, gm);

	//initing the renderer
	//NOTE: The render is in charge of creating and initing the window too. 
	init_renderer(pl, gm);

	pl->initialized = TRUE;
}


#include "ATProfiler/atp.h"
void print_out_tests(PL& pl);
ATP_REGISTER(main_update_loop); 
ATP_REGISTER(cellgrid_update);

static void update(PL* pl, void** game_memory)
{
	ATP_START(main_update_loop);

	AppMemory* gm = (AppMemory*)*game_memory;

	handle_input(pl, gm);

	if (gm->update_grid_flag)
	{
		ATP_BLOCK(cellgrid_update);
		cellgrid_update_step(pl,gm);
		gm->update_grid_flag = FALSE;
	}

	render(pl, gm);
	ATP_END(main_update_loop);

	//stats n stuff
	pl_debug_print("No. of live cells: %i\n", gm->active_table->node_list.size);
	pl_debug_print("Max hash depth:%i\n", max_hash_depth);
	print_out_tests(*pl);
}

 
static void shutdown(PL* pl, void** game_memory)
{
	AppMemory* gm = (AppMemory*)*game_memory;
	//clean common memory
	shutdown_renderer(pl, gm);
	shutdown_grid_processor(pl, gm);
	shutdown_input_handler(pl, gm);

	MARENA_POP(&pl->memory.main_arena, sizeof(AppMemory), "Game Memory Struct");

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