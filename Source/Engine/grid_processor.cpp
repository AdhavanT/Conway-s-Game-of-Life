#include "app_common.h"
#include "ATProfiler/atp.h"



//Grid Processor Memory
struct GPM
{
	MArena gpm_arena;
	MArena gpm_temp_arena; 

	//A double buffer that stores the hash table and all the live cells. Refer active_table pointer for the 'active table'. 
	Hashtable table1;
	Hashtable table2;

	b32 trigger_buffer_swap;
	int32 live_status;	//This value is a CellGridStatus used by the grid processor and can change state throughout the frame (on seperate thread)
	b32* running;

	ThreadHandle process_thread;
};
static void process_cell(WorldPos pos,CellType type, Hashtable* active_table, Hashtable* next_table, MSlice<WorldPos>& new_cells_tested, MArena* temp_arena);
static void update_cellgrid(AppMemory* gm)
{
	//---d--
	max_hash_depth = 0;
	//---d--


	GPM* gpm = (GPM*)gm->grid_processor_memory;

	Hashtable* next_table;
	if (gm->active_table == &gpm->table1)
	{
		next_table = &gpm->table2;
	}
	else
	{
		next_table = &gpm->table1;
	}

	MSlice<WorldPos> new_cells_tested;	//used to keep track of all the neighbors of lives cells that have been already processed
	new_cells_tested.init(&gpm->gpm_temp_arena, "new cells process queue");

	//Just iterating through node stack instead of table.
	LiveCellNode* it = gm->active_table->node_list.front;
	for (uint32 i = 0; i < gm->active_table->node_list.size; i++)
	{
		process_cell(it->pos,it->type, gm->active_table, next_table, new_cells_tested, &gpm->gpm_temp_arena);
		it++;
	}

	//resetting top of the arena to just having the hashtable. 
	new_cells_tested.clear(&gpm->gpm_temp_arena);
	
}
static void thread_process_cell(void* app_memory);
void init_grid_processor(PL* pl, AppMemory* gm)
{

	gm->grid_processor_memory = MARENA_PUSH(&pl->memory.main_arena, sizeof(GPM), "Grid Processor Memory Struct");

	GPM *gpm = (GPM*)gm->grid_processor_memory;

	gpm->gpm_arena.capacity = Megabytes(25);
	gpm->gpm_arena.overflow_addon_size = 0;
	gpm->gpm_arena.top = 0;
	gpm->gpm_arena.base = MARENA_PUSH(&pl->memory.main_arena, gpm->gpm_arena.capacity, "Grid Processor Memory Arena");
	add_monitoring(&gpm->gpm_arena);

	
	gpm->gpm_temp_arena.capacity = Megabytes(10);
	gpm->gpm_temp_arena.overflow_addon_size = 0;
	gpm->gpm_temp_arena.top = 0;
	gpm->gpm_temp_arena.base = MARENA_PUSH(&pl->memory.temp_arena, gpm->gpm_temp_arena.capacity, "Grid Processor temp Memory Arena");
	add_monitoring(&gpm->gpm_temp_arena);


	//hashtable stuff
	//table size needs to be a power of 2. 
	uint32 table_size = (1 << 20);

	//NOTE: THESE HAVE TO BE THE SAME SIZE!
	gpm->table1.arena.capacity = Megabytes(10);
	gpm->table1.arena.overflow_addon_size = 0;
	gpm->table1.arena.top = 0;
	gpm->table1.arena.base = MARENA_PUSH(&gpm->gpm_arena, gpm->table1.arena.capacity, "Sub Arena: HashTable-1");
	add_monitoring(&gpm->table1.arena);

	gpm->table1.table.init_and_allocate(&gpm->table1.arena, table_size, "HashTable - 1->table");
	gpm->table1.node_list.init(&gpm->table1.arena, "HashTable-1 -> live node list");

	gpm->table2.arena.capacity = gpm->table1.arena.capacity;
	gpm->table2.arena.overflow_addon_size = 0;
	gpm->table2.arena.top = 0;
	gpm->table2.arena.base = MARENA_PUSH(&gpm->gpm_arena, gpm->table2.arena.capacity, "Sub Arena: HashTable-2");
	add_monitoring(&gpm->table2.arena);

	gpm->table2.table.init_and_allocate(&gpm->table2.arena, table_size, "HashTable - 2->table");
	gpm->table2.node_list.init(&gpm->table2.arena, "HashTable-2 -> live node list");

	gm->active_table = &gpm->table1;
	//---------------
	gpm->live_status = (int32)CellGridStatus::FINISHED_PROCESSING;	//Doesn't do anything tell input handler triggers. 
	gpm->running = &pl->running;


	gpm->process_thread = pl_create_thread(thread_process_cell, (void*)gm);

}

void shutdown_grid_processor(PL* pl, AppMemory* gm)
{
	GPM* gpm = (GPM*)gm->grid_processor_memory;
	
	b32 thread_is_not_done = pl_wait_for_thread(gpm->process_thread, 30000);	//waits for the process thread to finish...waits for 30 seconds. 
	if (thread_is_not_done)
	{
		ERRORBOX("Grid Processing Thread is running for too long after shutdown initiated! Force kill the app...");
	}

	pl_close_thread(&gpm->process_thread);

	gpm->table2.node_list.clear(&gpm->table2.arena);
	gpm->table2.table.clear(&gpm->table2.arena);

	MARENA_POP(&gpm->gpm_arena, gpm->table2.arena.capacity, "Sub Arena: HashTable-2");
	remove_monitoring(&gpm->table2.arena);


	gpm->table1.node_list.clear(&gpm->table1.arena);
	gpm->table1.table.clear(&gpm->table1.arena);

	MARENA_POP(&gpm->gpm_arena, gpm->table1.arena.capacity, "Sub Arena: HashTable-1");
	remove_monitoring(&gpm->table1.arena);

	MARENA_POP(&pl->memory.temp_arena, gpm->gpm_temp_arena.capacity, "Grid Processor temp Memory Arena");
	remove_monitoring(&gpm->gpm_temp_arena);

	remove_monitoring(&gpm->gpm_arena);

	MARENA_POP(&pl->memory.main_arena, gpm->gpm_arena.capacity, "Grid Processor Memory Arena");
	MARENA_POP(&pl->memory.main_arena, sizeof(GPM), "Grid Processor Memory Struct");
}

ATP_REGISTER(process_cell_grid);
static void thread_process_cell(void* app_memory)
{
	AppMemory* gm = (AppMemory*)app_memory;
	GPM *gpm = (GPM*)gm->grid_processor_memory;
	while (*gpm->running)
	{
		CellGridStatus result = (CellGridStatus)interlocked_compare_exchange_i32(&gpm->live_status, (int32)CellGridStatus::PROCESSING, (int32)CellGridStatus::TRIGGER_PROCESSING);
		if(result == CellGridStatus::TRIGGER_PROCESSING)
		{
			ATP_BLOCK(process_cell_grid);
			update_cellgrid(gm);
			CellGridStatus finished_result = (CellGridStatus)interlocked_compare_exchange_i32(&gpm->live_status, (int32)CellGridStatus::FINISHED_PROCESSING, (int32)CellGridStatus::PROCESSING);
			ASSERT(gpm->trigger_buffer_swap == FALSE);
			gpm->trigger_buffer_swap = TRUE;
			ASSERT(finished_result == CellGridStatus::PROCESSING);
		}
		else
		{
			pl_sleep_thread(1);
		}

	}
}

CellGridStatus query_cellgrid_update_state(AppMemory* gm)
{
	GPM* gpm = (GPM*)gm->grid_processor_memory;
	if (gpm->trigger_buffer_swap)
	{
		gm->active_table->node_list.clear(&gm->active_table->arena);
		gm->active_table->node_list.front = (LiveCellNode*)MARENA_TOP(&gm->active_table->arena);

		//Clearing out previous hashtable (setting to zero to clear it out)
		pl_buffer_set(gm->active_table->table.front, 0, gm->active_table->table.size * sizeof(LiveCellNode*));
		//setting new active table.

		ASSERT(gpm->live_status == (int32)CellGridStatus::FINISHED_PROCESSING);
		Hashtable* next_table;
		if (gm->active_table == &gpm->table1)
		{
			next_table = &gpm->table2;
		}
		else
		{
			next_table = &gpm->table1;
		}
		gm->active_table = next_table;
		gpm->trigger_buffer_swap = FALSE;
	}
	CellGridStatus state = (CellGridStatus)gpm->live_status;
	return state;
}

void cellgrid_update_step(PL* pl, AppMemory* gm)
{
	GPM* gpm = (GPM*)gm->grid_processor_memory;

	if (gm->cellgrid_status == CellGridStatus::TRIGGER_PROCESSING)
	{
		ASSERT(gpm->live_status != (int32)CellGridStatus::PROCESSING);	//Triggering processing while already processing!
		interlocked_exchange_i32(&gpm->live_status, (int32)CellGridStatus::TRIGGER_PROCESSING);
	}
}


static void process_cell(WorldPos pos,CellType type,  Hashtable* active_table, Hashtable* next_table, MSlice<WorldPos>& new_cells_tested, MArena* temp_arena)
{
	if (type == CellType::EMPTY)
	{
		return;
	}

	uint32 table_size = active_table->table.size;

	WorldPos lookup_pos[8];
	lookup_pos[0] = { pos.x    , pos.y - 1 };	//bm
	lookup_pos[1] = { pos.x    , pos.y + 1 };	//tm
	lookup_pos[2] = { pos.x + 1, pos.y - 1 };	//br
	lookup_pos[3] = { pos.x + 1, pos.y };		//mr
	lookup_pos[4] = { pos.x + 1, pos.y + 1 };	//tr
	lookup_pos[5] = { pos.x - 1, pos.y - 1 };	//bl
	lookup_pos[6] = { pos.x - 1, pos.y };		//ml
	lookup_pos[7] = { pos.x - 1, pos.y + 1 };	//tl

	uint32 lookup_pos_hash[8];
	//TODO: SIMD this.
	lookup_pos_hash[0] = hash_pos(lookup_pos[0], table_size);
	lookup_pos_hash[1] = hash_pos(lookup_pos[1], table_size);
	lookup_pos_hash[2] = hash_pos(lookup_pos[2], table_size);
	lookup_pos_hash[3] = hash_pos(lookup_pos[3], table_size);
	lookup_pos_hash[4] = hash_pos(lookup_pos[4], table_size);
	lookup_pos_hash[5] = hash_pos(lookup_pos[5], table_size);
	lookup_pos_hash[6] = hash_pos(lookup_pos[6], table_size);
	lookup_pos_hash[7] = hash_pos(lookup_pos[7], table_size);

	CellType surround_state[8] = {};

	uint32 active_around = 0;
	for (uint32 i = 0; i < ArrayCount(lookup_pos); i++)
	{
		uint32 slot = lookup_pos_hash[i];
		surround_state[i] = lookup_cell(active_table, slot, lookup_pos[i]);
		active_around += surround_state[i];
	}

	if (active_around == 2 || active_around == 3)
	{
		//Cell survives! Adding to next hashmap. 
		uint32 slot = hash_pos(pos, table_size);
		append_new_node(next_table, slot, pos, CellType::CONWAY_LIVE);
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
			new_cells_tested.add(temp_arena, new_cell_pos);


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

			CellType nc_surround_state[8] = { (CellType)UINT32MAX, (CellType)UINT32MAX, (CellType)UINT32MAX, 
											  (CellType)UINT32MAX,						(CellType)UINT32MAX, 
											   (CellType)UINT32MAX,(CellType)UINT32MAX, (CellType)UINT32MAX };	// UINT32MAX means not pre-assigned
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
				if ((uint32)nc_surround_state[j] != UINT32MAX)	//found by the previous lookup 
				{
					continue;
				}
				else   //performing lookup of cell.
				{
					uint32 nc_lookup_hash = hash_pos(nc_lookup_pos[j], table_size);
					uint32 nc_slot = nc_lookup_hash;
					nc_surround_state[j] = lookup_cell(active_table, nc_slot, nc_lookup_pos[j]);
				}
			}

			//now with the completed nc_surrounding_state table, we can judge whether the cell is turned alive or not. 
			uint32 nc_active_count = 0;
			for (uint32 j = 0; j < ArrayCount(nc_surround_state); j++)
			{
				nc_active_count += (nc_surround_state[j] == CellType::CONWAY_LIVE) ? 1 : 0;
				//ASSERT(nc_surround_state[j] == 0 || nc_surround_state[j] == 1);
			}

			if (nc_active_count == 3)	//cell becomes alive!
			{
				//adding cell to next hashmap
				uint32 nc_new_cell_hash = hash_pos(new_cell_pos, table_size);
				uint32 nc_new_cell_index = nc_new_cell_hash;
				append_new_node(next_table, nc_new_cell_index, new_cell_pos, CellType::CONWAY_LIVE);
			}
		}
	SKIP_TEST:;

	}
}
