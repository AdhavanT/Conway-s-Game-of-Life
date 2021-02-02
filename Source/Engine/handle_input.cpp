#include "app_common.h"

inline bool operator==(CameraState& lhs, CameraState& rhs)
{
	b32 res = (lhs.center == rhs.center && lhs.scale == rhs.scale);
	return res;
}

void handle_input(PL* pl, AppMemory* gm)
{

	CameraState prev_cm_state = gm->cm;

	if (pl->input.keys[PL_KEY::SPACE].pressed)
	{
		gm->paused = !gm->paused;
	}

	if (pl->input.mouse.scroll_delta != 0)
	{
		//TODO: implement some sort of mouse centered zooming ( zooming in towards where the mouse cursor is)
		if (pl->input.mouse.scroll_delta > 0)
		{
			//decreasing scale ( zooming in )
			gm->cm.scale -= (gm->cm.scale - 0.05) * 0.1 * (f64)pl->input.mouse.scroll_delta;
		}
		else
		{
			//increasing scale ( zooming out )
			gm->cm.scale -= (1.0 - gm->cm.scale) * 0.1 * (f64)pl->input.mouse.scroll_delta;
		}
		gm->cm.scale = min(1.0, max(gm->cm.scale, 0.05));
	}
	if (gm->paused)
	{
		if (pl->input.mouse.left.pressed)
		{
			//Set state of cell.

			WorldPos screen_coords = { (int64)pl->input.mouse.position_x - (pl->window.window_bitmap.width / 2),(int64)pl->input.mouse.position_y - (pl->window.window_bitmap.height / 2) };
			screen_coords = screen_to_world(screen_coords, gm->cm.scale, gm->cm.center);

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
			screen_coords = screen_to_world(screen_coords, gm->cm.scale, gm->cm.center);

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