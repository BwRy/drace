#pragma once

// Log up to notice
#define LOGLEVEL 3

#include "config.h"
#include "aligned-stack.h"

#include <string>
#include <unordered_map>
#include <atomic>
#include <memory>
#include <chrono>

#include <dr_api.h>

/* max number of individual mutexes per thread */
#define MUTEX_MAP_SIZE 128

/* Runtime parameters */
struct params_t {
	unsigned sampling_rate{ 1 };
	unsigned instr_rate{ 1 };
	bool     lossy{ false };
	bool     lossy_flush{ false };
	bool     exclude_master{ false };
	bool     delayed_sym_lookup{ false };
	bool     yield_on_evt{ false };
	bool     fastmode{ false };
	unsigned stack_size{ 10 };
	std::string  config_file{ "drace.ini" };
	std::string  out_file;
	std::string  xml_file;

	// Raw arguments
	int          argc;
	const char** argv;
};
extern params_t params;

/* Instrumentation Level Flags */
enum INSTR_FLAGS : uint8_t {
	NONE = 0,
	SYMBOLS = 1,
	MEMORY = 2,
	STACK = 4
};

class Statistics;

/* Per Thread data (thread-private)
* \Warning This struct is not default-constructed
*          but just allocated as a memory block and casted
*          Initialisation is done in the thread-creation event
*          in memory_instr.
*/
struct per_thread_t {
	using tls_map_t = std::vector<std::pair<thread_id_t, per_thread_t*>>;

	byte         *buf_ptr;
	ptr_int_t     buf_end;
	AlignedBuffer<byte, 64> mem_buf;

	void         *cache;
	thread_id_t   tid;
	// use ptrsize type for lea
	ptr_uint_t    enabled{ true };
	// inverse of flush pending, jmpecxz
	std::atomic<ptr_uint_t> no_flush{ false };
	// external flush is currently executed;
	std::atomic<bool> external_flush{ false };
	// Shadow Stack
	AlignedStack<void*, 64> stack;
	// Stack used to track state of detector
	uint64        event_cnt{ 0 };

	/* book-keeping of active mutexes
	 * All even indices are mutex addresses
	 * while uneven indices denote the number of
	 * references at the location in index-1.
	 * This is tuned for maximum cache-locality */
	//AlignedStack<uint64_t, 64> mutex_book;
	std::unordered_map<uint64_t, unsigned> mutex_book;
	// Used for event syncronisation procedure
	tls_map_t     th_towait;
	// Statistics
	std::unique_ptr<Statistics>   stats;
	// as the detector cannot allocate TLS,
	// use this ptr for per-thread data in detector
	void         *detector_data{ nullptr };
};

/* Global data structures */
extern reg_id_t tls_seg;
extern uint     tls_offs;
extern int      tls_idx;

// TODO check if global is better
extern std::atomic<int> num_threads_active;
extern std::atomic<uint> runtime_tid;
extern std::atomic<thread_id_t> last_th_start;
extern std::atomic<bool> th_start_pending;

// Start time of the application
extern std::chrono::system_clock::time_point app_start;
extern std::chrono::system_clock::time_point app_stop;

// Global Module Shadow Data
class ModuleTracker;
extern std::unique_ptr<ModuleTracker> module_tracker;

// TLS
extern std::unordered_map<thread_id_t, per_thread_t*> TLS_buckets;
extern void* tls_rw_mutex;

// Global mutex to synchronize threads
extern void* th_mutex;

class MemoryTracker;
extern std::unique_ptr<MemoryTracker> memory_tracker;

// Global Symbol Table
class Symbols;
extern std::shared_ptr<Symbols> symbol_table;

class RaceCollector;
extern std::unique_ptr<RaceCollector> race_collector;

// Global Configuration
extern drace::Config config;

// Global Statistics Collector
extern std::unique_ptr<Statistics> stats;

// MSR Communication Driver
template<bool SENDER>
class MsrDriverDr;
extern std::unique_ptr<MsrDriverDr<true>> msrdriver;

#undef max
#undef min