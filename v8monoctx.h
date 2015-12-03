#ifndef v8monoctx_h__
#define v8monoctx_h__

#include <v8.h>
#include <assert.h>
#include <errno.h>
#include <map>
#include <string>
#include <sstream>
#include <vector>
#include <sys/time.h>
#include <sys/stat.h>
#include <string.h>

#undef New
#undef Null
#undef do_open
#undef do_close

#define PERSISTENT_COPYABLE v8::Persistent<v8::Script, v8::CopyablePersistentTraits<v8::Script> >
#define CMD_ARGS_LEN 250

// Configuration struct
struct monocfg {
	/* config */
	unsigned int run_low_memory_notification; // call LowMemoryNotification after number of requests
	unsigned int run_idle_notification_loop;// call IdleNotification loop after number of requests
	bool watch_templates; // if template changed since last compilation - recompile it
	char cmd_args[CMD_ARGS_LEN]; // command like arguments for v8 tuning

	/* counters */
	unsigned int request_num; // requests totally processed
	double run_low_memory_notification_time;
	double run_idle_notification_loop_time;
	double compile_time;
	double exec_time;
};

// Heap statistics struct
typedef struct heapst {
	size_t heap_size_limit;
	size_t used_heap_size;
	size_t total_heap_size;
	size_t total_heap_size_executable;
	size_t total_physical_size;
} HeapSt;

// Profiler and stat functions
void StartProfile(struct timeval *t1);
double StopProfile(struct timeval *t1);
void GetHeapStat(HeapSt * st);

// Convert to simple cstring
const char* ToCString(const v8::String::Utf8Value& value);

// Reads a file into a string
std::string ReadFile(std::string name);

void ReportException(v8::TryCatch* try_catch);

// This function returns a new array with three elements, x, y, and z
void DataFetch(const v8::FunctionCallbackInfo<v8::Value>& args);

// Global console.error function
void ConsoleError(const v8::FunctionCallbackInfo<v8::Value>& args);

std::vector<std::string> GetErrors (void);

bool ExecuteFile(monocfg * cfg, std::string fname, std::string append, std::string* json, std::string* out);

// V8 gc interfaces
bool IdleNotification(int ms);
void LowMemoryNotification();

#endif
