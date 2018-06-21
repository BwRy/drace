﻿// DynamoRIO client for Race-Detection

#include <dr_api.h>
#include <drmgr.h>
#include <drreg.h>
#include <drwrap.h>
#include <drutil.h>

#include <atomic>
#include <iostream>

#include "drace-client.h"
#include "memory-instr.h"
#include "function-wrapper.h"

#include <detector_if.h>

DR_EXPORT void dr_client_main(client_id_t id, int argc, const char *argv[])
{
	num_threads_active = 0;
	/* We need 2 reg slots beyond drreg's eflags slots => 3 slots */
	drreg_options_t ops = { sizeof(ops), 3, false };

	dr_set_client_name("Race-Detection Client 'drace'",
		"https://code.siemens.com/felix.moessbauer.ext/drace/issues");

	dr_enable_console_printing();

	// Init DRMGR, Reserve registers
	if (!drmgr_init() || !drwrap_init() || drreg_init(&ops) != DRREG_SUCCESS)
		DR_ASSERT(false);

	// performance tuning
	drwrap_set_global_flags((drwrap_global_flags_t)(DRWRAP_NO_FRILLS | DRWRAP_FAST_CLEANCALLS));

	// Register Events
	dr_register_exit_event(event_exit);
	drmgr_register_thread_init_event(event_thread_init);
	drmgr_register_thread_exit_event(event_thread_exit);
	drmgr_register_module_load_event(module_load_event);

	// Setup Memory Tracing
	DR_ASSERT(memory_inst::register_events());
	memory_inst::allocate_tls();

	// Initialize Detector
	detector::init();
}

static void event_exit()
{
	if (!drmgr_register_thread_init_event(event_thread_init) ||
		!drmgr_register_thread_exit_event(event_thread_exit) ||
		!drmgr_register_module_load_event(module_load_event))
		DR_ASSERT(false);

	// Cleanup Memory Tracing
	memory_inst::finalize();

	drutil_exit();
	drmgr_exit();

	// Finalize Detector
	// kills program, hence skip
	//__tsan_fini();

	dr_printf("< DR Exit\n");
}

static void event_thread_init(void *drcontext)
{
	// TODO: Start shadow thread for each app thread
	// If only one thead is running, disable detector
	thread_id_t tid = dr_get_thread_id(drcontext);
	++num_threads_active;
	dr_printf("<< [%i] Thread started\n", tid);
}

static void event_thread_exit(void *drcontext)
{
	// TODO: Cleanup and quit shadow thread
	// If only one thead is running, disable detector
	thread_id_t tid = dr_get_thread_id(drcontext);
	--num_threads_active;
	dr_printf("<< [%i] Thread exited\n", tid);
}


static void module_load_event(void *drcontext, const module_data_t *mod, bool loaded)
{
	// bind function wrappers
	funwrap::wrap_mutex_acquire(mod);
	funwrap::wrap_mutex_release(mod);
	funwrap::wrap_allocators(mod);
	funwrap::wrap_deallocs(mod);
	funwrap::wrap_main(mod);
}

// Detector Stuff
