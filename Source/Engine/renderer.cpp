#include "app_common.h"
#include "ATProfiler/atp.h"

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
		mem_buffer = MARENA_PUSH(arena, (width * height * 4), name_);
	}

	void clear_mem(MArena* arena)
	{
		MARENA_POP(arena, (width * height * 4), name);
		mem_buffer = 0;
	}
};

	struct FrameBuffer
	{
#ifdef SIMD_128
		MSlice<int64> buffer;	//NOTE: buffer is organized so that first element of each row is the Y axis coordinate and the rest of the row is just the respective X coordinate.  
#else
		MSlice<WorldPos> buffer;
#endif
		uint32 width;
		uint32 height;
	};


//Renderer memory.
struct RM	
{
	MArena rm_arena;
	MArena rm_temp_arena;
	FrameBuffer worldpos_fb;
	Bitmap main_window;
};


void draw_rectangle(Bitmap* dest, vec2ui bottom_left, vec2ui top_right, vec3f color);
void draw_bitmap(Bitmap* dest, vec2ui bottom_left, Bitmap* bitmap);
void fill_bitmap(Bitmap* dest, vec3f color);

void calculate_worldpos(AppMemory* gm, FrameBuffer& fb);

ATP_REGISTER(Render);
ATP_REGISTER(Draw_Every_Pixel);
ATP_REGISTER(Frame_Buffer_Fill);
ATP_REGISTER(Draw_Bitmap);

//Gives the main_window bitmap memory and and worldpos framebuffer memory. 
static void create_window_buffers(RM* rm)
{
	rm->main_window.init_mem(&rm->rm_arena, "Main Window Bitmap Buffer");

	rm->worldpos_fb.width = (int32)rm->main_window.width;
	rm->worldpos_fb.height = (int32)rm->main_window.height;
#ifdef SIMD_128
	//NOTE: Size to store is the total number of X coordinates = height * width and total number of distinct Y coordinates = height. ( the first element of each row is the y coordinate for the entire row.)
	//NOTE: in SIMD mode, we're storing the values as int64s instead of worldposs. This uses less space than scalar.
	rm->worldpos_fb.buffer.init_and_allocate(&rm->rm_arena, (rm->worldpos_fb.height * rm->worldpos_fb.width) + rm->worldpos_fb.height, "Frame Buffer with WorldPos");
#else
	rm->worldpos_fb.buffer.init_and_allocate(&rm->rm_arena, rm->worldpos_fb.height * rm->worldpos_fb.width, "Frame Buffer with WorldPos");
#endif

}

static void destory_window_buffers(RM* rm)
{
	//clearing stuff in the permanent render memory arena.
	rm->worldpos_fb.buffer.clear(&rm->rm_arena);
	rm->main_window.clear_mem(&rm->rm_arena);
}

void init_renderer(PL* pl, AppMemory* gm)
{

		gm->render_memory = MARENA_PUSH(&pl->memory.main_arena, sizeof(RM), "Render Memory Struct");
		RM* rm = (RM*)gm->render_memory;

		rm->rm_arena.capacity = Megabytes(100);
		init_memory_arena(&rm->rm_arena, rm->rm_arena.capacity, MARENA_PUSH(&pl->memory.main_arena, rm->rm_arena.capacity, "Render Memory Arena"));
		
		//NOTE: this is a temporary solution.
		#ifdef MONITOR_ARENA_USAGE
				rm->rm_temp_arena.allocations.front = (ArenaOwnerNode*)pl_buffer_alloc(sizeof(ArenaOwnerNode) * ARENAOWNERLIST_CAPACITY);
		#endif

		//initing and creating window with default values.
		vec2ui dim =
		{
			1920,1080
			//1280,720
			//1920, 1079
			//1919, 1079
		};
		rm->main_window.dim = dim;
		pl->window.title = (char*)"Renderer";
		pl->window.window_bitmap.width = rm->main_window.dim.x;
		pl->window.window_bitmap.height = rm->main_window.dim.y;
		pl->window.width = pl->window.window_bitmap.width;
		pl->window.height = pl->window.window_bitmap.height;
		pl->window.user_resizable = TRUE;

		pl->window.window_bitmap.bytes_per_pixel = 4;
		create_window_buffers(rm);
		pl->window.window_bitmap.buffer = rm->main_window.mem_buffer;

		PL_initialize_window(pl->window, &pl->memory.main_arena);

}

void update_renderer(PL* pl, AppMemory* gm)
{
	ATP_BLOCK(Render);
	RM* rm = (RM*)gm->render_memory;

	Bitmap& main_window = rm->main_window;
	FrameBuffer& fb = rm->worldpos_fb;

	pl_debug_print("Resolution: [%i, %i]\n", rm->main_window.width, rm->main_window.height);

	ATP_START(Frame_Buffer_Fill);
	if (gm->camera_changed)	//recalculating buffer that holds the hash of each world position for every respective pixel
	{
		calculate_worldpos(gm, fb);

		gm->camera_changed = FALSE;
	}
	ATP_END(Frame_Buffer_Fill);

	Bitmap world_bitmap;


	world_bitmap.dim = { fb.width , fb.height };
	world_bitmap.init_mem(&rm->rm_temp_arena, "World Bitmap");

	fill_bitmap(&world_bitmap, { 0.1f,0.1f,0.1f });

	//for first pixel.
	ATP_START(Draw_Every_Pixel);

	uint32* ptr = (uint32*)world_bitmap.mem_buffer;

	vec3f on_color = { .5f,.5f,0.0f };
	uint32 casted_on_color = (uint32)(on_color.r * 255.0f) << 16 | (uint32)(on_color.g * 255.0f) << 8 | (uint32)(on_color.b * 255.0f) << 0;


	if (gm->cm.scale < 0.9)
	{
#ifdef SIMD_128

#if 1	//NOTE: Using a Y row cache buffer to refer to. This is much slower in debug mode than doing a simple previous pixel check, but WAY faster in O2 mode. 

		//NOTE: Whats going on here:
		//If two rows have the same Y coords, they are both exactly the same. So, keeping a 'cached' state buffer to refer to. 
		MSlice<b8> row_state_cache;
		row_state_cache.init_and_allocate(&rm->rm_temp_arena, fb.width, "render pixel fill row state cache buffer");

		int64 prev_y_coord = -MAXINT64;	//Set to -MAXINT64 so that the first cache check will fail and will trigger to fill the cache with first row state. 
		int64* it = fb.buffer.front;

		for (uint32 y = 0; y < fb.height; y++)
		{
			int64 y_coord = *it;
			it++;	//to get to the x coordinates, it has to jump across the Y coord. 
			if (y_coord == prev_y_coord)	//Refer the previous row cache.
			{
				for (uint32 x = 0; x < fb.width; x++)
				{
					b8 state = row_state_cache[x];
					if (state)
					{
						*ptr = casted_on_color;
					}
					ptr++;
					it++;
				}
			}
			else  //Process new row and fill cache.
			{
				int64 prev_x_coord = MAXINT64;	//set to maxint64 so first check will fail. 
				b8 prev_state = 0;
				for (uint32 x = 0; x < fb.width; x++)
				{
					b8 state;
					if (*it == prev_x_coord)
					{
						state = prev_state;
					}
					else
					{
						WorldPos pos = { *it, y_coord };

						uint32 slot = hash_pos(pos, gm->table_size);
						state = (b8)lookup_cell(gm->active_table, slot, pos);

						prev_x_coord = *it;
						prev_state = state;
					}
					row_state_cache[x] = state;

					if (state)
					{
						*ptr = casted_on_color;
					}
					ptr++;
					it++;
				}
				prev_y_coord = y_coord;
			}
		}

		row_state_cache.clear(&rm->rm_temp_arena);

#else	//simple previous x coord check . 

		int64* it = fb.buffer.front;

		for (uint32 y = 0; y < fb.height; y++)
		{
			int64 y_coord = *it;
			it++;	//to get to the x coordinates, it has to jump across the Y coord. 


			int64 prev_x_coord = MAXINT64;
			b8 prev_state;

			//----This part processes the first pixel for the y row so that the next pixel can refer to it. 
			WorldPos pos = { *it, y_coord };
			uint32 slot = hash_pos(pos, gm->table_size);
			prev_state = lookup_cell(gm->active_table, slot, pos);
			if (prev_state)
			{
				*ptr = casted_on_color;
			}
			ptr++;
			it++;
			//------
			for (uint32 x = 0; x < fb.width - 1; x++)	//Processing the rest of the x coord pixels. 
			{
				b8 state;
				if (*it == *(it - 1))	//The x coord is same so uses cached state. 
				{
					state = prev_state;
				}
				else   //Different x coord so recalculating state and caching it. 
				{
					pos.x = *it;
					uint32 slot = hash_pos(pos, gm->table_size);
					prev_state = lookup_cell(gm->active_table, slot, pos);
					state = prev_state;
				}
				if (state)
				{
					*ptr = casted_on_color;
				}
				ptr++;
				it++;
			}

		}

#endif

#else	//Scalar Pixel fill
		WorldPos* next = fb.buffer.front;

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
#endif
	}
	else   //Zoomed out so not worth doing the caching of state (since each x and y pixel coordinate maps to a distinctive world coordinate. 
	{
#ifdef SIMD_128
		//NOTE: probably not worth doing a SIMD Version. 
		//Would only be able to fit 2 int64s at a time and the vector loads and unloads would probably take more time than doing the multiple multiplications from the same cache line. 

		//Basically scalar code but appropriate to the different data format used in SIMD. 
		int64* it = fb.buffer.front;

		for (uint32 y = 0; y < fb.height; y++)
		{
			int64 y_coord = *it;
			it++;	//to get to the x coordinates, it has to jump across the Y coord. 
			for (uint32 x = 0; x < fb.width; x++)
			{
				WorldPos pos = { *it, y_coord };

				b32 state;
				uint32 slot = hash_pos(pos, gm->table_size);
				state = lookup_cell(gm->active_table, slot, pos);

				if (state)
				{
					*ptr = casted_on_color;
				}
				ptr++;
				it++;

			}
		}

#else	//Scalar version
		WorldPos* it = fb.buffer.front;
		for (uint32 i = 0; i < (fb.height * fb.width - 1); i++)
		{
			b32 state;
			uint32 slot = hash_pos(*it, gm->table_size);
			state = lookup_cell(gm->active_table, slot, *it);

			if (state)
			{
				*ptr = casted_on_color;
			}
			ptr++;
			it++;
		}
#endif

	}

	ATP_END(Draw_Every_Pixel);

	ATP_START(Draw_Bitmap);
	draw_bitmap(&main_window, { 0,0 }, &world_bitmap);
	ATP_END(Draw_Bitmap);

	world_bitmap.clear_mem(&rm->rm_temp_arena);

}

void shutdown_renderer(PL* pl, AppMemory* gm)
{
	//cleanup render memory 
	RM* rm = (RM*)gm->render_memory;

#ifdef MONITOR_ARENA_USAGE
	pl_buffer_free(rm->rm_temp_arena.allocations.front);
#endif

	destory_window_buffers(rm);

#ifdef MONITOR_ARENA_USAGE
	pl_buffer_free(rm->rm_arena.allocations.front);
#endif
	MARENA_POP(&pl->memory.main_arena, rm->rm_arena.capacity, "Render Memory Arena");
	MARENA_POP(&pl->memory.main_arena, sizeof(RM), "Render Memory Struct");
}

void render(PL* pl, AppMemory* gm)
{
	RM* rm = (RM*)gm->render_memory;
	
	if (pl->window.was_altered)	//Resizing the main window bitmap to the new resolution of the window. 
	{
		if (rm->main_window.width != pl->window.width || rm->main_window.height != pl->window.height)
		{
			destory_window_buffers(rm);

			rm->main_window.dim = { pl->window.width,pl->window.height };
			pl->window.window_bitmap.width = rm->main_window.dim.x;
			pl->window.window_bitmap.height = rm->main_window.dim.y;
			create_window_buffers(rm);
			pl->window.window_bitmap.buffer = rm->main_window.mem_buffer;
			gm->camera_changed = TRUE; //to trigger the renderer to recalculate the worldpos frame buffer
		}
	}

	//initializing the temp arena every frame. 
	rm->rm_temp_arena.capacity = rm->rm_arena.capacity - rm->rm_arena.top;
	rm->rm_temp_arena.overflow_addon_size = 0;
	rm->rm_temp_arena.top = 0;
	rm->rm_temp_arena.base = MARENA_PUSH(&rm->rm_arena, rm->rm_temp_arena.capacity, "Temp Arena");

	update_renderer(pl, gm);

	//resetting/clearing the temp arena at the end of the frame. 
	MARENA_POP(&rm->rm_arena, rm->rm_temp_arena.capacity, "Temp Arena");
}

void calculate_worldpos(AppMemory* gm, FrameBuffer& fb)
{
	//TODO: Optimize the crap out of the SIMD version so that it out-performs the O2 scalar code...When in release, the compiler is able to optimize the scalar code so much that it's better than the SIMD code

#ifdef SIMD_128	//SIMD flat version
	f32 x_start = (f32)(-(int32)(fb.width / 2));
	f32 x_end = (fb.width % 2 == 0) ? ((-x_start) - 1.0f) : -x_start;

	f32 y_start = (f32)(-(int32)(fb.height / 2));
	f32 y_end = (fb.height % 2 == 0) ? ((-y_start) - 1.0f) : -y_start;

	x_end++;
	y_end++;

	__m128 adding_sequential = { 0.0f,1.0f,2.0f,3.0f };
	f32 fscale = (f32)gm->cm.scale;
	__m128 scale_4x = _mm_load1_ps(&fscale);

	__m128 x_sub_world_4x = _mm_load1_ps(&gm->cm.sub_world_center.x);

	__m128i cm_center_x_pos_4x;
	cm_center_x_pos_4x.m128i_i64[0] = { gm->cm.world_center.x };
	cm_center_x_pos_4x.m128i_i64[1] = { gm->cm.world_center.x };


	f32 zero = 0.0f;
	__m128 zero_4x = _mm_load1_ps(&zero);

	f32 half = 0.5f;
	__m128 half_4x = _mm_load1_ps(&half);

	//NOTE: To make for better use of space, the first element of each row is the y coordinate for that entire row. Everything else is the respective x coordinate

	uint32 extra_width = (uint32(x_end - x_start) % 4);
	f32 x_end_rounded_down = x_end - extra_width;

	__m128i* iterator = (__m128i*)fb.buffer.front;
	for (f32 y = y_start; y < y_end; y++)
	{

		f32 y_coord = y * fscale;
		y_coord += gm->cm.sub_world_center.y;
		int64 y_coord_fin = f32_to_int64(y_coord);
		y_coord_fin += gm->cm.world_center.y;

		int64* it_64 = (int64*)iterator;
		*it_64 = y_coord_fin;
		
		it_64++;
		iterator = (__m128i*)it_64;
		f32 x;
		for ( x = x_start; x < x_end_rounded_down; x += 4)
		{
			__m128 x_screen_coord_4x = _mm_load1_ps(&x);

			//Setting the x values to {x,x+1,x+2,x+3}
			x_screen_coord_4x = _mm_add_ps(x_screen_coord_4x, adding_sequential);

			//Multiplying x values with scale
			x_screen_coord_4x = _mm_mul_ps(x_screen_coord_4x, scale_4x);

			//adding the sub world center for x 
			x_screen_coord_4x = _mm_add_ps(x_screen_coord_4x, x_sub_world_4x);

			/*
			// proper casting x vector into 4 int32s with rounding
			__m128 compare_mask = _mm_cmpge_ps(x_screen_coord_4x, zero_4x);

			__m128 x_half_added_4x = _mm_add_ps(x_screen_coord_4x, half_4x);
			__m128 x_half_subtracted_4x = _mm_sub_ps(x_screen_coord_4x, half_4x);

			__m128 x_above_zero_4x = _mm_and_ps(compare_mask, x_half_added_4x);
			__m128 x_below_zero_4x = _mm_andnot_ps(compare_mask, x_half_subtracted_4x);

			__m128 orred_result_4x = _mm_or_ps(x_below_zero_4x, x_above_zero_4x);
			__m128i int_result = _mm_cvttps_epi32(orred_result_4x);
			*/
			//NOTE: This is ridiculous. Below is one instruction that rounds to nearest before cast to int....instead of the above. Ughhh...Why didn't I realize this sooner!!
			__m128i int_result = _mm_cvtps_epi32(x_screen_coord_4x);
			//turning those 4 int32s into 2 int64s. 
			__m128i first_two_results = _mm_cvtepi32_epi64(int_result);

			__m128i second_two_results = _mm_cvtepi32_epi64(_mm_bsrli_si128(int_result, 8));

			//adding the camera's world center values.
			first_two_results = _mm_add_epi64(cm_center_x_pos_4x, first_two_results);
			second_two_results = _mm_add_epi64(cm_center_x_pos_4x, second_two_results);

			//----------------------------------------

			*iterator = first_two_results;	
			iterator++;
			*iterator = second_two_results; 
			iterator++;
		}
		if (extra_width != 0)
		{
			int64* it_single = (int64*)iterator;
			for (; x < x_end; x++)
			{
				f32 x_coord = x * fscale;
				x_coord += gm->cm.sub_world_center.x;
				int64 x_coord_fin = f32_to_int64(x_coord);
				x_coord_fin += gm->cm.world_center.x;
				*it_single = x_coord_fin;
				it_single++;
			}
			iterator = (__m128i*) it_single;
		}
		
	}
	ASSERT((int64*)iterator == (fb.buffer.front + fb.buffer.size));

#else	//scalar flat version
	f32 x_start = (f32)(-(int32)(fb.width / 2));
	f32 x_end = (fb.width % 2 == 0) ? ((-x_start) - 1.0f) : -x_start;

	f32 y_start = (f32)(-(int32)(fb.height / 2));
	f32 y_end = (fb.height % 2 == 0) ? ((-y_start) - 1.0f) : -y_start;

	x_end++;
	y_end++;
	f32 fscale = (f32)gm->cm.scale;

	WorldPos* iterator = fb.buffer.front;
	for (f32 y = y_start; y < y_end; y++)
	{

		f32 y_coord = y * fscale;
		y_coord += gm->cm.sub_world_center.y;
		int64 y_coord_fin = f32_to_int64(y_coord);
		y_coord_fin += gm->cm.world_center.y;

		for (f32 x = x_start; x < x_end; x++)
		{
			f32 x_coord = x * fscale;
			x_coord += gm->cm.sub_world_center.x;
			int64 x_coord_fin = f32_to_int64(x_coord);
			x_coord_fin += gm->cm.world_center.x;
			*iterator = { x_coord_fin, y_coord_fin };
			iterator++;
		}
	}
	ASSERT(iterator == (fb.buffer.front + fb.buffer.size));

#endif
}

void fill_bitmap(Bitmap* dest, vec3f color)
{
#ifdef SIMD_128	//SIMD Version
	uint32 size = dest->height * dest->width;
	__m128i* ptr = (__m128i*)dest->mem_buffer;
	uint32 casted_color = (uint32)(color.r * 255.0f) << 16 | (uint32)(color.g * 255.0f) << 8 | (uint32)(color.b * 255.0f) << 0;
	__m128i casted_color_wide;
	casted_color_wide.m128i_u32[0] = { casted_color };
	casted_color_wide.m128i_u32[1] = { casted_color };
	casted_color_wide.m128i_u32[2] = { casted_color };
	casted_color_wide.m128i_u32[3] = { casted_color };

	uint32 extra = size % 4;

	size = size - extra;	//making it the nearest multiple of 4. 

	for (uint32 x = 0; x < size; x+= 4)
	{
		*ptr = casted_color_wide;
		ptr++;
	}
	uint32* ptr_single = (uint32*)ptr;
	for (uint32 x = 0; x < extra; x++)
	{
		*ptr_single = casted_color;
		ptr_single++;
	}
	ASSERT(ptr_single == ((uint32*)dest->mem_buffer + dest->height * dest->width));

#else	//Scalar Version
	uint32 size = dest->height * dest->width;
	uint32* ptr = (uint32*)dest->mem_buffer;
	uint32 casted_color = (uint32)(color.r * 255.0f) << 16 | (uint32)(color.g * 255.0f) << 8 | (uint32)(color.b * 255.0f) << 0;

	for (uint32 x = 0; x < size; x++)
	{
		*ptr = casted_color;
		ptr++;
	}
#endif
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
#ifdef SIMD_128	//SIMD Version
	__m128i* dest_ptr = (__m128i*)((uint32*)dest->mem_buffer + (bottom_left.y * dest->width) + bottom_left.x);
	__m128i* source_ptr = (__m128i*)bitmap->mem_buffer;

	uint32 width = bitmap->dim.x;
	uint32 height = bitmap->dim.y;

	ASSERT(((width * height) + bottom_left.x + bottom_left.y * dest->width) <= (dest->width * dest->height));

	uint32 width_extra = width % 4;

	uint32 rounded_down_width = width - width_extra;	//making it the nearest multiple of 4. 


	uint32 end_shift = dest->width - width;
	for (uint32 y = 0; y < height; y++)
	{
		for (uint32 x = 0; x < rounded_down_width; x+= 4)
		{
			*dest_ptr = *source_ptr;
			dest_ptr++;
			source_ptr++;
		}
		uint32* dest_ptr_single = (uint32*)dest_ptr;
		uint32* source_ptr_single = (uint32*)source_ptr;

		for (uint32 x = 0; x < width_extra; x++)
		{
			*dest_ptr_single = *source_ptr_single;
			dest_ptr_single++;
			source_ptr_single++;
		}
		dest_ptr += end_shift;
	}
#else	//Scalar version. 
	uint32* dest_ptr = (uint32*)dest->mem_buffer + (bottom_left.y * dest->width) + bottom_left.x;
	uint32* source_ptr = (uint32*)bitmap->mem_buffer;

	uint32 width = bitmap->dim.x;
	uint32 height = bitmap->dim.y;

	ASSERT(((width * height) + bottom_left.x + bottom_left.y * dest->width) <= (dest->width * dest->height));

	uint32 end_shift = dest->width - width;
	for (uint32 y = 0; y < height; y++)
	{
		for (uint32 x = 0; x < width; x++)
		{
			*dest_ptr = *source_ptr;
			dest_ptr++;
			source_ptr++;
		}
		dest_ptr += end_shift;
	}
#endif
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