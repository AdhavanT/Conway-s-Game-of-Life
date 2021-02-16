#include "app_common.h"

inline bool operator==(CameraState& lhs, CameraState& rhs)
{
	b32 res = (lhs.world_center == rhs.world_center && lhs.scale == rhs.scale && lhs.sub_world_center == rhs.sub_world_center);
	return res;
}

void handle_input(PL* pl, AppMemory* gm)
{

	CameraState prev_cm_state = gm->cm;

	if (pl->input.keys[PL_KEY::SPACE].pressed)
	{
		gm->paused = !gm->paused;
	}

	if (pl->input.mouse.middle.pressed)	//in panning mode
	{
		ASSERT(gm->in_panning_mode != TRUE);
		gm->in_panning_mode = TRUE;
		gm->prev_mouse_pos = { pl->input.mouse.position_x,pl->input.mouse.position_y };
	}

	if (pl->input.mouse.middle.released)
	{
		gm->in_panning_mode = FALSE;
	}

	if (gm->in_panning_mode)
	{
		if (!pl->input.mouse.is_in_window)
		{
			gm->in_panning_mode = FALSE;
		}
		else
		{
			Vec2<f32> delta_mouse = { (f32)(pl->input.mouse.position_x - gm->prev_mouse_pos.x),(f32)(pl->input.mouse.position_y - gm->prev_mouse_pos.y) };
			delta_mouse = delta_mouse * -1;		//inverting so it's 'pull to scroll'
			delta_mouse *= (f32)gm->cm.scale;	//scaled to world position delta vector (precision loss from f64 to f32 shouldn't matter too much on deltas.)
			//now adding delta mouse to camera position
			vec2f delta_sub_world_pos;
			
			WorldPos delta_world_pos = { (int64)delta_mouse.x, (int64)delta_mouse.y };
			delta_sub_world_pos = { delta_mouse.x - (int32)delta_world_pos.x,delta_mouse.y - (int32)delta_world_pos.y };

			gm->cm.world_center += delta_world_pos;
			gm->cm.sub_world_center += delta_sub_world_pos;
			if (gm->cm.sub_world_center.x < .0f)
			{
				gm->cm.world_center.x--;
				gm->cm.sub_world_center.x = 1.0f + gm->cm.sub_world_center.x;	//wrapping down back to [0.0 to 1.0)
			}
			if (gm->cm.sub_world_center.x >= 1.0f)
			{
				gm->cm.world_center.x++;
				gm->cm.sub_world_center.x = gm->cm.sub_world_center.x - 1.0f;	//wrapping down back to [0.0 to 1.0)
			}
			if (gm->cm.sub_world_center.y < .0f)
			{
				gm->cm.world_center.y--;
				gm->cm.sub_world_center.y = 1.0f + gm->cm.sub_world_center.y;	//wrapping down back to [0.0 to 1.0)
			}
			if (gm->cm.sub_world_center.y >= 1.0f)
			{
				gm->cm.world_center.y++;
				gm->cm.sub_world_center.y = gm->cm.sub_world_center.y - 1.0f;	//wrapping down back to [0.0 to 1.0)
			}


			gm->prev_mouse_pos.x = pl->input.mouse.position_x;
			gm->prev_mouse_pos.y = pl->input.mouse.position_y;

		}
	}

	if (pl->input.mouse.scroll_delta != 0)
	{
		//TODO: Add zoom towards specific worldpos. 

		//Zoom is Fantastic! 
		f64 zoom_factor = 10;
		if (pl->input.mouse.scroll_delta > 0)
		{
			zoom_factor *= (gm->cm.scale - 0.05);	//closer it is, less the zoom factor becomes.
		}
		else
		{
			zoom_factor *= (1.0 - gm->cm.scale);
		}
		f64 scroll_delta = -pl->input.mouse.scroll_delta;
		scroll_delta *= pl->time.fdelta_seconds * zoom_factor;
		gm->cm.scale += scroll_delta;
		gm->cm.scale = clamp(gm->cm.scale, 0.05, 1.0);

	}


	if (gm->paused)
	{
		if (pl->input.mouse.left.pressed)
		{
			//Set state of cell.

			WorldPos screen_coords = { (int64)pl->input.mouse.position_x - (pl->window.window_bitmap.width / 2),(int64)pl->input.mouse.position_y - (pl->window.window_bitmap.height / 2) };
			screen_coords = screen_to_world(screen_coords, gm->cm);

			uint32 slot = hash_pos(screen_coords, gm->table_size);
			b32 state = lookup_cell(gm->active_table, slot, screen_coords);
			//add only if state is false (doesn't exist in table). 
			if (!state)
			{
				//pl_debug_print("Added: [%i, %i]\n", screen_coords.x, screen_coords.y);
				append_new_node(gm->active_table, slot, screen_coords);
			}
		}
		else if (pl->input.mouse.right.pressed)
		{
			WorldPos screen_coords = { (int64)pl->input.mouse.position_x - (pl->window.window_bitmap.width / 2),(int64)pl->input.mouse.position_y - (pl->window.window_bitmap.height / 2) };
			screen_coords = screen_to_world(screen_coords, gm->cm);

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
		gm->update_grid_flag = TRUE;
	}


	if (!(prev_cm_state == gm->cm))
	{
		gm->camera_changed = TRUE;
	}
}