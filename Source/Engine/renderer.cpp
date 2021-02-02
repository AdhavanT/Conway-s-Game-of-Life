#include "app_common.h"

struct FrameBuffer
{
	MSlice<WorldPos> buffer;
	size_t width;
	size_t height;
};

void render(PL* pl, AppMemory* gm)
{

	pl_buffer_set(pl->window.window_bitmap.buffer, 22, pl->window.window_bitmap.size);

	FrameBuffer fb;
	fb.width = (int32)pl->window.window_bitmap.width;
	fb.height = (int32)pl->window.window_bitmap.height;
	fb.buffer.init_and_allocate(&pl->memory.temp_arena, fb.height * fb.width, "Frame Buffer with WorldPos");

	int32 x_start = -(int32)(fb.width / 2);
	int32 x_end = (fb.width % 2 == 0) ? ((-x_start) - 1) : -x_start;

	int32 y_start = -(int32)(fb.height / 2);
	int32 y_end = (fb.height % 2 == 0) ? ((-y_start) - 1) : -y_start;

	x_end++;
	y_end++;

	if (gm->camera_changed)
	{
		WorldPos* iterator = fb.buffer.front;
		for (int64 y = y_start; y < y_end; y++)
		{
			for (int64 x = x_start; x < x_end; x++)
			{
				WorldPos screen_coords = { x , y };
				*iterator = screen_to_world(screen_coords, gm->cm.scale, gm->cm.center);
				iterator++;
			}
		}
		ASSERT(iterator == (fb.buffer.front + fb.buffer.size));
		gm->camera_changed = FALSE;
	}


	uint32* ptr = (uint32*)pl->window.window_bitmap.buffer;
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

	fb.buffer.clear(&pl->memory.temp_arena);

}

void draw_rectangle(PL_Window* window, vec2ui bottom_left, vec2ui top_right, vec3f color)
{
	int32 width = top_right.x - bottom_left.x;
	int32 height = top_right.y - bottom_left.y;
	uint32 casted_color = (uint32)(color.r * 255.0f) << 16 | (uint32)(color.g * 255.0f) << 8 | (uint32)(color.b * 255.0f) << 0;
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



void draw_verticle_line(PL_Window* window, uint32 x, uint32 from_y, uint32 to_y, vec3f color)
{
	uint32 casted_color = (uint32)(color.r * 255.0f) << 16 | (uint32)(color.g * 255.0f) << 8 | (uint32)(color.b * 255.0f) << 0;
	uint32* ptr = (uint32*)window->window_bitmap.buffer + x + from_y * window->window_bitmap.width;

	for (uint32 i = from_y; i < to_y; i++)
	{
		*ptr = casted_color;
		ptr += window->window_bitmap.width;
	}
}

void draw_horizontal_line(PL_Window* window, uint32 y, uint32 from_x, uint32 to_x, vec3f color)
{
	uint32 casted_color = (uint32)(color.r * 255.0f) << 16 | (uint32)(color.g * 255.0f) << 8 | (uint32)(color.b * 255.0f) << 0;
	uint32* ptr = (uint32*)window->window_bitmap.buffer + y * window->window_bitmap.width + from_x;

	for (uint32 i = from_x; i < to_x; i++)
	{
		*ptr = casted_color;
		ptr++;
	}
}