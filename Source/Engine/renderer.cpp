#include "renderer.h"


struct Bitmap
{
#ifdef MONITOR_ARENA_USAGE
	char* name;
#endif
	void* mem_buffer;
	union
	{
		vec2ui dim;
		struct
		{
			uint32 width;
			uint32 height;
		};
	};
	void init_mem(MArena* arena, const char* name_)
	{
#ifdef MONITOR_ARENA_USAGE
		name = (char*)name_;
#endif
		mem_buffer = MARENA_PUSH(arena, width * height * 4, name_);
	}

	void clear_mem(MArena* arena)
	{
		MARENA_POP(arena, width * height * 4, name);
		mem_buffer = 0;
	}
};

struct FrameBuffer
{
	MSlice<WorldPos> buffer;
	size_t width;
	size_t height;
};
//Renderer memory.
struct RM	
{
	FrameBuffer fb;
	Bitmap main_window;
};

void init_render_memory(PL* pl,AppMemory* gm)
{
	gm->render_memory = MARENA_PUSH(&pl->memory.main_arena, sizeof(RM), "Render Memory Struct");
	RM* rm = (RM*)gm->render_memory;

	rm->fb.width = (int32)pl->window.window_bitmap.width;
	rm->fb.height = (int32)pl->window.window_bitmap.height;
	rm->fb.buffer.init_and_allocate(&pl->memory.main_arena, rm->fb.height * rm->fb.width, "Frame Buffer with WorldPos");

	rm->main_window.mem_buffer = pl->window.window_bitmap.buffer;
	rm->main_window.height = pl->window.window_bitmap.height;
	rm->main_window.width = pl->window.window_bitmap.width;
}

void clean_render_memory(PL* pl, AppMemory* gm)
{
	RM* rm = (RM*)gm->render_memory;
	rm->fb.buffer.clear(&pl->memory.main_arena);

	MARENA_POP(&pl->memory.main_arena, sizeof(RM), "Render Memory Struct");
}


void draw_rectangle(Bitmap* dest, vec2ui bottom_left, vec2ui top_right, vec3f color);
void draw_bitmap(Bitmap* dest, vec2ui bottom_left, Bitmap* bitmap);
void fill_bitmap(Bitmap* dest, vec3f color);

void render(PL* pl, AppMemory* gm)
{
	RM* rm = (RM*)gm->render_memory;

	Bitmap& main_window = rm->main_window;
	

	FrameBuffer &fb = rm->fb;
	
	int32 x_start = -(int32)(fb.width / 2);
	int32 x_end = (fb.width % 2 == 0) ? ((-x_start) - 1) : -x_start;

	int32 y_start = -(int32)(fb.height / 2);
	int32 y_end = (fb.height % 2 == 0) ? ((-y_start) - 1) : -y_start;

	x_end++;
	y_end++;

	if (gm->camera_changed)	//recalculating buffer that holds worldpos's for every pixel
	{
		WorldPos* iterator = fb.buffer.front;
		for (int64 y = y_start; y < y_end; y++)
		{
			for (int64 x = x_start; x < x_end; x++)
			{
				WorldPos screen_coords = { x , y };
				*iterator = screen_to_world(screen_coords, gm->cm);
				iterator++;
			}
		}
		ASSERT(iterator == (fb.buffer.front + fb.buffer.size));
		gm->camera_changed = FALSE;
	}

	Bitmap world_bitmap;


	world_bitmap.dim = { fb.width , fb.height };
	world_bitmap.init_mem(&pl->memory.temp_arena, "World Bitmap");
	
	//drawing background for world bitmap
	//TODO: SIMD and optimize all this...it runs like crap. 
	//pl_buffer_set(pl->window.window_bitmap.buffer, 22, pl->window.window_bitmap.size);
	//draw_rectangle(&world_bitmap, { 0,0 }, { main_window.width , main_window.height }, { 0.1f,0.1f,0.1f });
	fill_bitmap(&world_bitmap, { 0.1f,0.1f,0.1f });


	uint32* ptr = (uint32*)world_bitmap.mem_buffer;

	WorldPos* it = fb.buffer.front;
	WorldPos* next = fb.buffer.front;
	vec3f on_color = { .5f,.5f,0.0f };
	uint32 casted_on_color = (uint32)(on_color.r * 255.0f) << 16 | (uint32)(on_color.g * 255.0f) << 8 | (uint32)(on_color.b * 255.0f) << 0;

	//for first pixel.
	b32 prev_state = { 1 };
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


		//if (*(next-1) == *(next))//	NOTE: For some reason, doing this takes WAY longer (like 15ms for 720p)...even in O2, compiler couldn't make them the same...interesting..
		if ((next->x == (next - 1)->x && next->y == (next - 1)->y))
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

	draw_bitmap(&main_window, { 0,0 }, &world_bitmap);
	world_bitmap.clear_mem(&pl->memory.temp_arena);
}

void fill_bitmap(Bitmap* dest, vec3f color)
{
	uint32 size = dest->height * dest->width;
	uint32* ptr = (uint32*)dest->mem_buffer;
	uint32 casted_color = (uint32)(color.r * 255.0f) << 16 | (uint32)(color.g * 255.0f) << 8 | (uint32)(color.b * 255.0f) << 0;

	for (uint32 x = 0; x < size; x++)
	{
		*ptr = casted_color;
		ptr++;
	}
}

void draw_rectangle(Bitmap* dest, vec2ui bottom_left, vec2ui top_right, vec3f color)
{
	int32 width = top_right.x - bottom_left.x;
	int32 height = top_right.y - bottom_left.y;
	uint32 casted_color = (uint32)(color.r * 255.0f) << 16 | (uint32)(color.g * 255.0f) << 8 | (uint32)(color.b * 255.0f) << 0;
	uint32* ptr = (uint32*)dest->mem_buffer + (bottom_left.y * dest->width) + bottom_left.x;
	ASSERT((width * height) + bottom_left.x + bottom_left.y * dest->width <= dest->width * dest->height);

	uint32 end_shift = dest->width - width;
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

void draw_bitmap(Bitmap* dest, vec2ui bottom_left, Bitmap* bitmap)
{
	uint32* dest_ptr = (uint32*)dest->mem_buffer + (bottom_left.y * dest->width) + bottom_left.x;
	uint32* source_ptr = (uint32*)bitmap->mem_buffer;

	uint32 width = bitmap->dim.x;
	uint32 height = bitmap->dim.y;

	ASSERT(((width * height) + bottom_left.x + bottom_left.y * dest->width) <= (dest->width * dest->height));

	uint32 end_shift = dest->width - width;
	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			*dest_ptr = *source_ptr;
			dest_ptr++;
			source_ptr++;
		}
		dest_ptr += end_shift;
	}
}

void draw_verticle_line(Bitmap* dest, uint32 x, uint32 from_y, uint32 to_y, vec3f color)
{
	uint32 casted_color = (uint32)(color.r * 255.0f) << 16 | (uint32)(color.g * 255.0f) << 8 | (uint32)(color.b * 255.0f) << 0;
	uint32* ptr = (uint32*)dest->mem_buffer + x + from_y * dest->width;

	uint32 width = dest->width;
	for (uint32 i = from_y; i < to_y; i++)
	{
		*ptr = casted_color;
		ptr += width;
	}
}

void draw_horizontal_line(Bitmap* dest, uint32 y, uint32 from_x, uint32 to_x, vec3f color)
{
	uint32 casted_color = (uint32)(color.r * 255.0f) << 16 | (uint32)(color.g * 255.0f) << 8 | (uint32)(color.b * 255.0f) << 0;
	uint32* ptr = (uint32*)dest->mem_buffer + y * dest->width + from_x;

	for (uint32 i = from_x; i < to_x; i++)
	{
		*ptr = casted_color;
		ptr++;
	}
}