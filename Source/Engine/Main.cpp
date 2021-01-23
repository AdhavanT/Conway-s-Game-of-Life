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

struct Ball
{
	vec2f pos;
	f32 radius;
};

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

struct Cell
{
	b32 state;
};

struct CellGrid
{
	Cell* first_buffer;
	Cell* sec_buffer;
	Cell* front;
	vec2i cell_dimensions;
	int32 cell_size;
	
};

FORCEDINLINE Cell* at(Cell* front, uint32 cell_dimension_x,int32 x, int32 y)
{
	return front + (y * cell_dimension_x) + x;
}

struct GameMemory
{
	CellGrid cell_grid;
	uint64 prev_update_tick;
	uint64 update_tick_time;
	b32 paused;

	Cell* prev_altered_cell;
};

void cellgrid_update(PL*pl,GameMemory* gm);
void render(PL* pl, GameMemory* gm);
void update(PL* pl, void** game_memory)
{
	if (pl->initialized == FALSE)
	{
		*game_memory = MARENA_PUSH(&pl->memory.main_arena, sizeof(GameMemory), "Game Memory");
		GameMemory* gm = (GameMemory*)*game_memory;
		gm->cell_grid.cell_size = 10;
		gm->cell_grid.cell_dimensions.x = (pl->window.window_bitmap.width - 1) / (gm->cell_grid.cell_size);
		gm->cell_grid.cell_dimensions.y = (pl->window.window_bitmap.height - 1) / (gm->cell_grid.cell_size);
		gm->cell_grid.first_buffer = (Cell*)MARENA_PUSH(&pl->memory.main_arena, sizeof(Cell) * gm->cell_grid.cell_dimensions.x * gm->cell_grid.cell_dimensions.y, "Cell List - first buffer");
		gm->cell_grid.sec_buffer = (Cell*)MARENA_PUSH(&pl->memory.main_arena, sizeof(Cell) * gm->cell_grid.cell_dimensions.x * gm->cell_grid.cell_dimensions.y, "Cell List - sec buffer");
		gm->cell_grid.front = gm->cell_grid.first_buffer;

		gm->prev_update_tick = pl->time.current_millis;
		gm->update_tick_time = 100;
		pl->initialized = TRUE;
	}
	GameMemory* gm = (GameMemory*)*game_memory;
	
	if (gm->paused)
	{
		if (pl->input.mouse.left.pressed)
		{

			int32 cell_x;
			int32 cell_y;
			cell_x = pl->input.mouse.position_x / gm->cell_grid.cell_size;
			cell_y = pl->input.mouse.position_y / gm->cell_grid.cell_size;
			if (cell_x < gm->cell_grid.cell_dimensions.x && cell_y < gm->cell_grid.cell_dimensions.y)
			{
				Cell* active_cell;
				active_cell = at(gm->cell_grid.front, gm->cell_grid.cell_dimensions.x, cell_x, cell_y);
				//if (active_cell != gm->prev_altered_cell)
				{
					active_cell->state = !active_cell->state;
					gm->prev_altered_cell = active_cell;
				}
			}
		}
	}

	if (!gm->paused && (pl->time.current_millis >= gm->prev_update_tick + gm->update_tick_time))
	{
		//update grid
		gm->prev_update_tick = pl->time.current_millis;

		cellgrid_update(pl,gm);
	}

	if (pl->input.keys[PL_KEY::SPACE].pressed)
	{
		gm->paused = !gm->paused;
	}

	render(pl, gm);
}

void cellgrid_update(PL* pl,GameMemory* gm)
{
	Cell* temp;
	if (gm->cell_grid.front == gm->cell_grid.first_buffer)
	{
		temp = gm->cell_grid.sec_buffer;
	}
	else if(gm->cell_grid.front == gm->cell_grid.sec_buffer)
	{
		temp = gm->cell_grid.first_buffer;
	}
	else
	{
		temp = 0;
		ASSERT(FALSE);	//front isn't set to any of the double buffers. 
	}

	for (int32 y = 1; y < gm->cell_grid.cell_dimensions.y - 1; y++)
	{
		for (int32 x = 1; x < gm->cell_grid.cell_dimensions.x - 1; x++)
		{
			int32 surrounding = 0;
			surrounding += at(gm->cell_grid.front,gm->cell_grid.cell_dimensions.x,x - 1, y - 1)->state;
			surrounding += at(gm->cell_grid.front,gm->cell_grid.cell_dimensions.x,x , y - 1)->state;
			surrounding += at(gm->cell_grid.front,gm->cell_grid.cell_dimensions.x,x + 1, y - 1)->state;
			surrounding += at(gm->cell_grid.front,gm->cell_grid.cell_dimensions.x,x - 1, y )->state;
			surrounding += at(gm->cell_grid.front,gm->cell_grid.cell_dimensions.x,x + 1, y )->state;
			surrounding += at(gm->cell_grid.front,gm->cell_grid.cell_dimensions.x,x - 1, y + 1)->state;
			surrounding += at(gm->cell_grid.front,gm->cell_grid.cell_dimensions.x,x , y + 1)->state;
			surrounding += at(gm->cell_grid.front,gm->cell_grid.cell_dimensions.x,x + 1, y + 1)->state;

			b32 current_state = at(gm->cell_grid.front, gm->cell_grid.cell_dimensions.x, x, y)->state;
			b32 next_state = FALSE;
			if (current_state == TRUE)
			{
				if (surrounding == 2 || surrounding == 3)
				{
					next_state = TRUE;
				}
				else if (surrounding < 2)
				{
					next_state = FALSE;
				}
				else   //surrounding > 3
				{
					next_state = FALSE;
				}
			}
			else
			{
				if (surrounding == 3)
				{
					next_state = TRUE;
				}
				else
				{
					next_state = FALSE;
				}
			}
			at(temp, gm->cell_grid.cell_dimensions.x, x, y)->state = next_state;
		}
	}
	gm->cell_grid.front = temp;

}

void render(PL* pl, GameMemory* gm)
{
	//shading in cell grid
	for (uint32 y = 0; y < (uint32)gm->cell_grid.cell_dimensions.y; y++)
	{
		for (uint32 x = 0; x < (uint32)gm->cell_grid.cell_dimensions.x; x++)
		{
			vec3f cell_color = { 0 };
			if (at(gm->cell_grid.front,gm->cell_grid.cell_dimensions.x,x, y)->state == 0)
			{
				cell_color = { 0.1f,0.1f,0.1f };
			}
			else
			{
				cell_color = { 0.5f,0.5f,0.7f };
			}

			vec2ui bl, tr;
			bl = { x * gm->cell_grid.cell_size, y * gm->cell_grid.cell_size };
			tr = { bl.x + gm->cell_grid.cell_size, bl.y + gm->cell_grid.cell_size };
			draw_rectangle(&pl->window, bl, tr, cell_color);
		}
	}

	//drawing grid lines
	for (int32 y = 0; y <= gm->cell_grid.cell_dimensions.y; y++)
	{
		draw_horizontal_line(&pl->window, y * gm->cell_grid.cell_size, 0, gm->cell_grid.cell_size * gm->cell_grid.cell_dimensions.x, { 0.4f, 0.4f, 0.4f });
	}
	for (int32 x = 0; x <= gm->cell_grid.cell_dimensions.x; x++)
	{
		draw_verticle_line(&pl->window, x * gm->cell_grid.cell_size, 0, gm->cell_grid.cell_size * gm->cell_grid.cell_dimensions.y, { 0.4f, 0.4f, 0.4f });
	}
}

void cleanup_game_memory(PL_Memory* arenas, void** game_memory)
{
	GameMemory* gm = (GameMemory*)*game_memory;
	MARENA_POP(&arenas->main_arena, sizeof(Cell) * gm->cell_grid.cell_dimensions.x * gm->cell_grid.cell_dimensions.y, "Cell List - sec buffer");

	MARENA_POP(&arenas->main_arena, sizeof(Cell) * gm->cell_grid.cell_dimensions.x * gm->cell_grid.cell_dimensions.y, "Cell List - first buffer");

	
	MARENA_POP(&arenas->main_arena, sizeof(GameMemory), "Game Memory");
	*game_memory = NULL;
}