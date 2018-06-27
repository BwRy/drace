#include "drace-client.h"
#include "function-wrapper.h"
#include "memory-instr.h"

#include <vector>
#include <string>

#include <detector_if.h>
#include <dr_api.h>
#include <drwrap.h>
#include <drutil.h>

#include <unordered_map>

namespace funwrap {
	namespace internal {

		// TODO: get from config file
		static std::vector<std::string> acquire_symbols{ "_Mtx_lock",   "__gthrw_pthread_mutex_lock" };
		static std::vector<std::string> release_symbols{ "_Mtx_unlock", "__gthrw_pthread_mutex_unlock" };
#ifdef WINDOWS
		static std::vector<std::string> allocators{ "HeapAlloc" };
		static std::vector<std::string> deallocs{ "HeapFree" };
#else
		static std::vector<std::string> allocators{ "malloc" };
		static std::vector<std::string> deallocs{ "free" };
#endif
		static std::vector<std::string> main{ "_Thrd_join", "_Thrd_start", "main", "wmain", "WinMain", "wWinMain" };

		static void thread_join_pre(void *wrapctx, OUT void **user_data) {
			app_pc drcontext = drwrap_get_drcontext(wrapctx);
			per_thread_t * data = (per_thread_t*)drmgr_get_tls_field(drcontext, tls_idx);

			memory_inst::process_buffer();
			data->disabled = true;

			dr_fprintf(STDERR, "<< [%i] join threads pre\n", data->tid);
		}

		static void thread_join_post(void *wrapctx, OUT void *user_data) {
			app_pc drcontext = drwrap_get_drcontext(wrapctx);
			per_thread_t * data = (per_thread_t*)drmgr_get_tls_field(drcontext, tls_idx);

			memory_inst::clear_buffer();
			data->disabled = false;

			dr_fprintf(STDERR, "<< [%i] join threads post\n", data->tid);
		}

		static void mutex_acquire_pre(void *wrapctx, OUT void **user_data) {
			app_pc drcontext = drwrap_get_drcontext(wrapctx);
			per_thread_t * data = (per_thread_t*)drmgr_get_tls_field(drcontext, tls_idx);
			void *      mutex = drwrap_get_arg(wrapctx, 0);

			// TODO: Flush all other buffers
			memory_inst::process_buffer();
			data->disabled = true;


			detector::acquire(data->tid, mutex);

			dr_fprintf(STDERR, "<< [%i] pre mutex acquire %p\n", data->tid, mutex);
		}

		// Currently not bound as not necessary
		static void mutex_acquire_post(void *wrapctx, OUT void *user_data) {
			app_pc drcontext = drwrap_get_drcontext(wrapctx);
			per_thread_t * data = (per_thread_t*)drmgr_get_tls_field(drcontext, tls_idx);

			memory_inst::clear_buffer();
			data->disabled = false;

			dr_fprintf(STDERR, "<< [%i] post mutex acquire\n", data->tid);
		}

		static void mutex_release_pre(void *wrapctx, OUT void **user_data) {
			app_pc drcontext = drwrap_get_drcontext(wrapctx);
			per_thread_t * data = (per_thread_t*)drmgr_get_tls_field(drcontext, tls_idx);
			void * mutex = drwrap_get_arg(wrapctx, 0);

			// TODO: Flush all other buffers
			memory_inst::process_buffer();
			data->disabled = true;

			detector::release(data->tid, mutex);

			dr_fprintf(STDERR, "<< [%i] pre mutex release %p\n", data->tid, mutex);
		}

		// Currently not bound as not necessary
		static void mutex_release_post(void *wrapctx, OUT void *user_data) {
			app_pc drcontext = drwrap_get_drcontext(wrapctx);
			per_thread_t * data = (per_thread_t*)drmgr_get_tls_field(drcontext, tls_idx);

			memory_inst::clear_buffer();
			data->disabled = false;

			dr_fprintf(STDERR, "<< [%i] post mutex release\n", data->tid);
		}

		// TODO: On Linux size is arg 0
		static void alloc_pre(void *wrapctx, void **user_data) {
			app_pc drcontext = drwrap_get_drcontext(wrapctx);

			// TODO: Flush all other buffers
			memory_inst::process_buffer();

			// Save alloc size to TLS, disable detector
			per_thread_t * data = (per_thread_t*)drmgr_get_tls_field(drcontext, tls_idx);
			data->last_alloc_size = (size_t)drwrap_get_arg(wrapctx, 2);
			data->disabled = true;

			// spams logs
			//dr_fprintf(STDERR, "<< [%i] pre alloc %i\n", data->tid, data->last_alloc_size);
		}

		static void alloc_post(void *wrapctx, void *user_data) {
			app_pc drcontext = drwrap_get_drcontext(wrapctx);
			void * retval = drwrap_get_retval(wrapctx);

			// Read alloc size from TLS
			per_thread_t * data = (per_thread_t*)drmgr_get_tls_field(drcontext, tls_idx);

			// TODO: Disable Tracing during this period
			memory_inst::clear_buffer();
			data->disabled = false;

			detector::alloc(data->tid, retval, data->last_alloc_size);

			// spams logs
			//dr_fprintf(STDERR, "<< [%i] post alloc: size %i, addr %p\n", data->tid, data->last_alloc_size, retval);
		}

		// TODO: On Linux addr is arg 0
		static void free_pre(void *wrapctx, void **user_data) {
			app_pc drcontext = drwrap_get_drcontext(wrapctx);
			per_thread_t * data = (per_thread_t*)drmgr_get_tls_field(drcontext, tls_idx);
			void * addr = drwrap_get_arg(wrapctx, 2);

			memory_inst::process_buffer();
			data->disabled = false;

			detector::free(data->tid, addr);

			// spams logs
			//dr_fprintf(STDERR, "<< [%i] pre free %p\n", data->tid, addr);
		}

		static void free_post(void *wrapctx, void *user_data) {
			app_pc drcontext = drwrap_get_drcontext(wrapctx);
			per_thread_t * data = (per_thread_t*)drmgr_get_tls_field(drcontext, tls_idx);

			memory_inst::clear_buffer();
			data->disabled = false;
			//dr_fprintf(STDERR, "<< [%i] post free\n", data->tid);
		}

	} // namespace internal
} // namespace funwrap

void funwrap::wrap_mutex_acquire(const module_data_t *mod) {
	for (const auto & name : internal::acquire_symbols) {
		app_pc towrap = (app_pc)dr_get_proc_address(mod->handle, name.c_str());
		if (towrap != NULL) {
			bool ok = drwrap_wrap(towrap, internal::mutex_acquire_pre, NULL);
			if (ok) {
				std::string msg{ "< wrapped acq " };
				msg += name + "\n";
				dr_fprintf(STDERR, msg.c_str());
			}
		}
	}
}

void funwrap::wrap_mutex_release(const module_data_t *mod) {
	for (const auto & name : internal::release_symbols) {
		app_pc towrap = (app_pc)dr_get_proc_address(mod->handle, name.c_str());
		if (towrap != NULL) {
			bool ok = drwrap_wrap(towrap, internal::mutex_release_pre, NULL);
			if (ok) {
				std::string msg{ "< wrapped rel " };
				msg += name + "\n";
				dr_fprintf(STDERR, msg.c_str());
			}
		}
	}
}

void funwrap::wrap_allocators(const module_data_t *mod) {
	for (const auto & name : internal::allocators) {
		app_pc towrap = (app_pc)dr_get_proc_address(mod->handle, name.c_str());
		if (towrap != NULL) {
			bool ok = drwrap_wrap(towrap, internal::alloc_pre, internal::alloc_post);
			if (ok) {
				std::string msg{ "< wrapped alloc " };
				msg += name + "\n";
				dr_fprintf(STDERR, msg.c_str());
			}
		}
	}
}

void funwrap::wrap_deallocs(const module_data_t *mod) {
	for (const auto & name : internal::deallocs) {
		app_pc towrap = (app_pc)dr_get_proc_address(mod->handle, name.c_str());
		if (towrap != NULL) {
			bool ok = drwrap_wrap(towrap, internal::free_pre, internal::free_post);
			if (ok) {
				std::string msg{ "< wrapped deallocs " };
				msg += name + "\n";
				dr_fprintf(STDERR, msg.c_str());
			}
		}
	}
}

//TODO: Currently not working
void funwrap::wrap_main(const module_data_t *mod) {
	for (const auto & name : internal::main) {
		app_pc towrap = (app_pc)dr_get_proc_address(mod->handle, name.c_str());
		if (towrap != NULL) {
			bool ok = drwrap_wrap(towrap, internal::thread_join_pre, internal::thread_join_post);
			if (ok) {
				std::string msg{ "< wrapped main " };
				msg += name + "\n";
				dr_fprintf(STDERR, msg.c_str());
			}
		}
	}
}
