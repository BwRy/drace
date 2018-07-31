#pragma once

#include "util.h"

#include <dr_api.h>
#include <drutil.h>
#include <tinyxml2.h>

namespace sink {
	/* A race exporter which creates a valgrind valkyrie compatible xml output */
	template<typename Stream>
	class Valkyrie {
	public:
		using TimePoint = std::chrono::system_clock::time_point;

	private:
		Stream &     _stream;
		int          _argc;
		const char** _argv;
		const char*  _app;
		TimePoint    _start_time;
		TimePoint    _end_time;


	private:
		void print_header(tinyxml2::XMLPrinter & p) const {
			p.OpenElement("protocolversion"); p.PushText(4); p.CloseElement();

			p.OpenElement("protocoltool");	p.PushText("helgrind");	p.CloseElement();

			p.OpenElement("preamble");
			p.OpenElement("line"); p.PushText("Drace, a thread error detector"); p.CloseElement();
			p.CloseElement();

			p.OpenElement("pid"); p.PushText((int)dr_get_process_id()); p.CloseElement();
#ifndef WINDOWS
			p.OpenElement("ppid"); p.PushText((int)dr_get_parent_id()); p.CloseElement();
#endif

			p.OpenElement("tool"); p.PushText("drace"); p.CloseElement();
		}

		void print_params(tinyxml2::XMLPrinter & p) const {
			p.OpenElement("args");
			// Detector
			p.OpenElement("vargv");
			p.OpenElement("exe");
			p.PushText(_argv[0]);
			p.CloseElement();
			for (int i = 1; i < _argc; ++i) {
				p.OpenElement("arg");
				p.PushText(_argv[i]);
				p.CloseElement();
			}
			p.CloseElement();

			// Application
			p.OpenElement("argv");
			p.OpenElement("exe");
			p.PushText(_app);
			p.CloseElement();
			p.CloseElement();
			p.CloseElement();
		}

		void print_stack(tinyxml2::XMLPrinter & p, const std::vector<SymbolLocation> & stack) const {
			p.OpenElement("stack");
			for (const auto & f : stack) {
				p.OpenElement("frame");
			
				std::stringstream pc;
				pc << "0x" << std::hex << (uint64_t)f.pc;
				p.OpenElement("ip"); p.PushText(pc.str().c_str()); p.CloseElement();
				p.OpenElement("obj"); p.PushText(f.mod_name.c_str()); p.CloseElement();
				if (!f.sym_name.empty()) {
					p.OpenElement("fn"); p.PushText(f.sym_name.c_str()); p.CloseElement();
				}
				if (!f.file.empty()) {
					p.OpenElement("dir"); p.PushText(f.file.substr(0, f.file.find_last_of("/\\")).c_str()); p.CloseElement();
					p.OpenElement("file"); p.PushText(f.file.substr(f.file.find_last_of("/\\") + 1).c_str()); p.CloseElement();
				}
				if (f.line) {
					p.OpenElement("line"); p.PushText(std::to_string(f.line).c_str()); p.CloseElement();
				}
				p.CloseElement();
			}
			p.CloseElement();
		}

		void print_races(tinyxml2::XMLPrinter & p, const RaceCollector::RaceCollectionT & races) const {
			for (unsigned i = 0; i < races.size(); ++i) {
				const ResolvedAccess & r = races[i].second.first;
				const ResolvedAccess & r2 = races[i].second.second;

				p.OpenElement("error");
				std::stringstream unique;
				unique << "0x" << std::hex << i;
				p.OpenElement("unique"); p.PushText(unique.str().c_str()); p.CloseElement();
				p.OpenElement("tid"); p.PushText(r2.thread_id); p.CloseElement();
				p.OpenElement("threadname"); p.PushText("Thread"); p.CloseElement();
				p.OpenElement("kind"); p.PushText("Race"); p.CloseElement();

				{
					p.OpenElement("xwhat");
					p.OpenElement("text");
					std::stringstream text;
					text << "Possible data race during ";
					text << (r2.write ? "write" : "read") << " of size "
						 << r2.access_size << " at 0x" << std::hex << r2.accessed_memory
						 << " by thread #" << std::dec << r2.thread_id;
					p.PushText(text.str().c_str());
					p.CloseElement();
					p.OpenElement("hthreadid"); p.PushText(r2.thread_id); p.CloseElement();
					p.CloseElement();
					print_stack(p, r2.resolved_stack);
				}
				{
					p.OpenElement("xwhat");
					p.OpenElement("text");
					std::stringstream text;
					text << "This conflicts with a previous ";
					text << (r.write ? "write" : "read") << " of size "
						<< r.access_size << " at 0x" << std::hex << r.accessed_memory
						<< " by thread #" << std::dec << r.thread_id;
					p.PushText(text.str().c_str());
					p.CloseElement();
					p.OpenElement("hthreadid"); p.PushText(r.thread_id); p.CloseElement();
					p.CloseElement();
					print_stack(p, r.resolved_stack);
				}

				p.CloseElement();
			}
		}

	public:
		Valkyrie() = delete;
		Valkyrie(const Valkyrie &) = delete;
		Valkyrie(Valkyrie &&) = default;

		Valkyrie(Stream & stream,
			int argc,
			const char** argv,
			const char* app,
			TimePoint start,
			TimePoint stop)
			: _stream(stream),
			  _argc(argc),
			  _argv(argv),
			  _app(app),
			  _start_time(start),
			  _end_time(stop){ }

		Valkyrie & operator= (const Valkyrie &) = delete;
		Valkyrie & operator= (Valkyrie &&) = default;

		template<typename RaceEntry>
		void process_all(const RaceEntry & races) const {
			tinyxml2::XMLPrinter p(0,true);
			p.PushHeader(true, true);
			p.OpenElement("valgrindoutput");
			print_header(p);
			print_params(p);

			p.OpenElement("status");
			p.OpenElement("state"); p.PushText("RUNNING"); p.CloseElement();

			p.OpenElement("time");
			p.PushText(util::to_iso_time(_start_time).c_str());
			p.CloseElement();
			p.CloseElement();

			// Announce Threads

			print_races(p, races);

			p.OpenElement("status");
			p.OpenElement("state"); p.PushText("FINISHED"); p.CloseElement();
			p.OpenElement("time");
			p.PushText(util::to_iso_time(_end_time).c_str());
			p.CloseElement();
			p.CloseElement();

			// ERROR COUNTS
			// SUPPCOUNTS

			p.CloseElement();

			_stream << p.CStr();
		}
	};
}