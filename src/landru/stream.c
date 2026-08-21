#include <stdlib.h>
#include <string.h>

#include <landru/file.h>
#include <landru/stream.h>

/* Synchronous stream interface backed by whole-file memory buffers. */

#define MAX_CHAINS 10
#define FILENAME_SIZE 64

typedef struct {
	char filename[FILENAME_SIZE];
	uint8_t* data;
	uint32_t file_size;
	uint32_t read_offset;
	int loaded;
} StreamChain;

static StreamChain chain_list[MAX_CHAINS];
static int cur_chain;
static int dest_chain;
static int use_chain;
static int chain_count;
static int stream_active;

/* --- Lifecycle --- */

int16_t lstream_Init_Stream_Engine(int32_t buff_size, int32_t prefetch_amount) {
	(void)buff_size;
	(void)prefetch_amount;
	memset(chain_list, 0, sizeof(chain_list));
	cur_chain = 0;
	dest_chain = 0;
	use_chain = 0;
	chain_count = 0;
	stream_active = 1;
	return 1;
}

int16_t lstream_Exit_Stream_Engine(void) {
	for (int i = 0; i < MAX_CHAINS; i++) {
		free(chain_list[i].data);
		chain_list[i].data = NULL;
	}
	memset(chain_list, 0, sizeof(chain_list));
	stream_active = 0;
	return 1;
}

/* --- Chain management --- */

int16_t lstream_Chain_Stream_File(const char* filename) {
	if (chain_count >= MAX_CHAINS)
		return 0;

	strncpy(chain_list[dest_chain].filename, filename, FILENAME_SIZE - 1);
	chain_list[dest_chain].filename[FILENAME_SIZE - 1] = '\0';
	chain_list[dest_chain].data = NULL;
	chain_list[dest_chain].file_size = 0;
	chain_list[dest_chain].read_offset = 0;
	chain_list[dest_chain].loaded = 0;

	chain_count++;
	if (++dest_chain >= MAX_CHAINS)
		dest_chain = 0;
	return 1;
}

int16_t lstream_Unchain_Current_Stream_File(void) {
	if (use_chain == cur_chain) {
		free(chain_list[cur_chain].data);
		chain_list[cur_chain].data = NULL;
		chain_list[cur_chain].loaded = 0;
		chain_count--;
		if (++cur_chain >= MAX_CHAINS)
			cur_chain = 0;
	}
	if (++use_chain >= MAX_CHAINS)
		use_chain = 0;
	return 1;
}

/* --- Load and use a chained file --- */

static int load_chain_file(int idx) {
	if (chain_list[idx].loaded)
		return 1;

	LandruFile* f = lfile_Open_File(LANDRU_FILE_ROOT_ASSET, chain_list[idx].filename, "rb");
	if (!f)
		return 0;

	lfile_Seek_File(f, 0, LANDRU_SEEK_END);
	long size = lfile_Tell_File(f);
	lfile_Seek_File(f, 0, LANDRU_SEEK_SET);

	chain_list[idx].data = malloc(size);
	if (!chain_list[idx].data) {
		lfile_Close_File(f);
		return 0;
	}

	lfile_Read_Data_From_File(f, chain_list[idx].data, size);
	lfile_Close_File(f);

	chain_list[idx].file_size = size;
	chain_list[idx].read_offset = 0;
	chain_list[idx].loaded = 1;
	return 1;
}

int16_t lstream_Use_Stream_File(const char* filename) {
	/* Find the chained file by name */
	int i = use_chain;
	while (i != dest_chain) {
		if (strncmp(filename, chain_list[i].filename, FILENAME_SIZE) == 0)
			break;
		if (++i >= MAX_CHAINS)
			i = 0;
	}
	if (i == dest_chain)
		return 0;

	use_chain = i;

	/* Load if not already in memory */
	if (!chain_list[i].loaded) {
		if (!load_chain_file(i))
			return 0;
	}

	chain_list[use_chain].read_offset = 0;
	return 1;
}

/* --- Read from the loaded buffer --- */

uint32_t lstream_Read_From_Stream_Buffer(void* dest, uint32_t size, int16_t blocking) {
	(void)blocking; /* No async — data is always available */

	StreamChain* chain = &chain_list[use_chain];
	if (!chain->loaded || !chain->data)
		return 0;

	uint32_t available = chain->file_size - chain->read_offset;
	if (size > available)
		size = available;

	memcpy(dest, chain->data + chain->read_offset, size);
	chain->read_offset += size;
	return size;
}

/* --- No-ops for timing (irrelevant with synchronous I/O) --- */

int16_t lstream_Get_Stream_Flag(void) { return 0; /* STREAM_IDLE */ }

void lstream_Adjust_For_Penalty(uint32_t size) { (void)size; }

void lstream_Set_Stream_Tick_Counts(void) { /* CD timing calibration — not needed */ }
