#include "v8monoctx.h"

static v8::Isolate* isolate;
static v8::Persistent<v8::Context> context;

/* maps for caching templates (ExecuteFile) */
static std::map<std::string, time_t> ExecuteScriptModified;
static std::map<std::string, time_t> LoadConfigModified;

/* maps for caching utilites (LoadFile) */
static std::map<std::string, time_t> LoadScriptModified;

static std::string GlobalData;
static std::vector<std::string> GlobalError;


// Profiler functions
void StartProfile(struct timeval *t1) {
   	gettimeofday(t1, NULL);
}

double StopProfile(struct timeval *t1) {
	struct timeval t2;
  	gettimeofday(&t2, NULL);
  	return (((t2.tv_sec - t1->tv_sec) * 1000000) + (t2.tv_usec - t1->tv_usec)) / 1000000.0;
}

// Just convert to string
const char* ToCString(const v8::String::Utf8Value& value) {
	return *value ? *value : "<string conversion failed>";
}

// Reads a file into a string.
std::string ReadFile(std::string name) {
	FILE* file = fopen(name.c_str(), "rb");
	if (file == NULL) return std::string();

	fseek(file, 0, SEEK_END);
	int size = ftell(file);
	rewind(file);

	char* chars = new char[size + 1];
	chars[size] = '\0';
	for (int i = 0; i < size;) {
		int read = static_cast<int>(fread(&chars[i], 1, size - i, file));
		i += read;
	}
	fclose(file);

	std::string result(chars);
	delete[] chars;
	return result;
}

void ReportException(v8::TryCatch* try_catch) {
	v8::HandleScope handle_scope(isolate);
	v8::String::Utf8Value exception(try_catch->Exception());

	std::string exception_string( ToCString(exception) );
	v8::Handle<v8::Message> message = try_catch->Message();

	if (message.IsEmpty()) {
		// V8 didn't provide any extra information about this error; just
		// print the exception.

		GlobalError.push_back(exception_string);
	} else {
		// Print (filename):(line number): (message)\n(sourceline)
		v8::String::Utf8Value filename(message->GetScriptResourceName());
		v8::String::Utf8Value sourceline(message->GetSourceLine());

		std::string _err( ToCString(filename) );
		std::string _source( ToCString(sourceline) );

		std::ostringstream linenum;
		linenum << message->GetLineNumber();

		_err += ":";
		_err += linenum.str();
		_err += ": ";
		_err += exception_string;
		_err += "\n";
		_err += _source;

		GlobalError.push_back(_err);
	}
}

// Global function
void DataFetch(const v8::FunctionCallbackInfo<v8::Value>& args) {
	// We will be creating temporary handles so we use a handle scope.
	v8::HandleScope handle_scope(args.GetIsolate());

	args.GetReturnValue().Set(
		v8::String::NewFromUtf8(args.GetIsolate(), GlobalData.c_str())
	);
}

// Global function
void ConsoleError(const v8::FunctionCallbackInfo<v8::Value>& args) {
	for (int i = 0; i < args.Length(); i++) {
		v8::HandleScope handle_scope(args.GetIsolate());
		v8::String::Utf8Value str(args[i]);

		std::string _err( ToCString(str) );
		GlobalError.push_back(_err);
	}
}

std::vector<std::string> GetErrors (void) {
	return GlobalError;
}

bool InitIsolate(monocfg *cfg) {
	if (isolate == NULL) {
		// Get the default Isolate created at startup
		isolate = v8::Isolate::GetCurrent();

		// Create a stack-allocated handle scope
		v8::HandleScope handle_scope(isolate);

		if (strlen(cfg->cmd_args) > 0) {
			v8::V8::SetFlagsFromString(cfg->cmd_args, strlen(cfg->cmd_args));
		}

		// Global objects
		v8::Handle<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate);
		global->Set(v8::String::NewFromUtf8(isolate, "__dataFetch"), v8::FunctionTemplate::New(isolate, DataFetch));
		global->Set(v8::String::NewFromUtf8(isolate, "__errorLog"), v8::FunctionTemplate::New(isolate, ConsoleError));

		// Create a new context
		v8::Handle<v8::Context> ctx = v8::Context::New(isolate, NULL, global);
		context.Reset(isolate, ctx);

		if (context.IsEmpty()) {
			std::string _err("Error creating context");
			GlobalError.push_back(_err);
			return false;
		}
		ctx->Enter();
	}
	return true;
}


bool CompileFile(monocfg *cfg, std::string fname, v8::Local<v8::Script> *script, v8::TryCatch *try_catch) {
	// Read new file
	std::string file = ReadFile(fname);
	if (file.size() == 0) {
		std::string _err("File not exists or empty: ");
		_err += fname;

		GlobalError.push_back(_err);
		return false;
	}

	return CompileSource(cfg, file, script, try_catch);
}

bool CompileSource(monocfg *cfg, std::string source_text, v8::Local<v8::Script> *script, v8::TryCatch *try_catch) {
	v8::Handle<v8::String> source = v8::String::NewFromUtf8(isolate, source_text.c_str());

	// Origin
	v8::Handle<v8::Integer> line = v8::Integer::New(isolate, 0);
	v8::Handle<v8::Integer> column = v8::Integer::New(isolate, 0);
	v8::ScriptOrigin origin(source, line, column);

	struct timeval t1; StartProfile(&t1);
		*script = v8::Script::Compile(source, &origin);
	cfg->compile_time += StopProfile(&t1);

	if (script->IsEmpty()) {
		ReportException(try_catch);
		return false;
	}
	return true;
}

bool LoadFile(monocfg *cfg, std::string fname) {
	GlobalError.clear();

	cfg->run_idle_notification_loop_time = 0;
	cfg->run_low_memory_notification_time = 0;
	cfg->exec_time = 0;
	cfg->compile_time = 0;

	if (!InitIsolate(cfg))
		return false;

	v8::HandleScope handle_scope(isolate);
	v8::TryCatch try_catch;

	// Get file stat
	struct stat stat_buf;
	if (cfg->watch_templates) {
		if (stat(fname.c_str(), &stat_buf) != 0) {
			std::string _err("Error opening file ");
			_err += fname;
			_err += ": ";
			_err += strerror(errno);

			GlobalError.push_back(_err);

			return false;
		}
	}

	if (LoadScriptModified.find(fname) == LoadScriptModified.end() || (cfg->watch_templates && LoadScriptModified[fname] != stat_buf.st_mtime)) {
		v8::Local<v8::Script> script;
		if (!CompileFile(cfg, fname, &script, &try_catch))
			return false;

		LoadScriptModified[fname] = stat_buf.st_mtime;

		struct timeval t1; StartProfile(&t1);
			v8::Local<v8::Value> result = script->Run();
		cfg->exec_time += StopProfile(&t1);

		if (result.IsEmpty()) {
			assert(try_catch.HasCaught());
			ReportException(&try_catch);
			return false;
		}

		// No errors
		assert(!try_catch.HasCaught());
	}

	return true;
}

bool ExecuteFile(monocfg *cfg, std::string fname, std::string run, std::string* json, std::string* out) {
	GlobalError.clear();

	++cfg->request_num;

	cfg->run_idle_notification_loop_time = 0;
	cfg->run_low_memory_notification_time = 0;
	cfg->exec_time = 0;
	cfg->compile_time = 0;

	if (!InitIsolate(cfg))
		return false;

	v8::HandleScope handle_scope(isolate);
	v8::TryCatch try_catch;

	if (json != NULL) {
		GlobalData.assign(*json);
	}

	// Get file stat
	struct stat stat_buf;
	if (cfg->watch_templates) {
		if (stat(fname.c_str(), &stat_buf) != 0) {
			std::string _err("Error opening file ");
			_err += fname;
			_err += ": ";
			_err += strerror(errno);

			GlobalError.push_back(_err);
			return false;
		}
	}

	// Reload context if has changed
	if (cfg->watch_templates) {
		for (std::map<std::string, time_t>::iterator it=LoadScriptModified.begin(); it!=LoadScriptModified.end(); ++it) {
			if (LoadScriptModified[it->first] != stat_buf.st_mtime) {
				if (!LoadFile(cfg, it->first.c_str()))
					return false;
			}
		}
	}

	if (ExecuteScriptModified.find(fname) == ExecuteScriptModified.end() || (cfg->watch_templates && ExecuteScriptModified[fname] != stat_buf.st_mtime)) {
		if (!LoadFile(cfg, fname))
			return false;

		ExecuteScriptModified[ fname ] = stat_buf.st_mtime;
	}

	v8::Local<v8::Script> script;
	if (!CompileSource(cfg, run, &script, &try_catch))
		return false;

	struct timeval t1; StartProfile(&t1);
		v8::Local<v8::Value> result = script->Run();
	cfg->exec_time += StopProfile(&t1);

	if (result.IsEmpty()) {
		assert(try_catch.HasCaught());
		ReportException(&try_catch);
		return false;
	}

	// No errors
	assert(!try_catch.HasCaught());
	if (!result->IsUndefined() && out != NULL) {
		v8::String::Utf8Value utf8(result);
		out->assign(*utf8, utf8.length());
	}

	// Try to call GC in a very simple way
	if (cfg->run_low_memory_notification > 0 && cfg->request_num % cfg->run_low_memory_notification == 0) {
		struct timeval t1;
		StartProfile(&t1);
			v8::V8::LowMemoryNotification();
		cfg->run_low_memory_notification_time = StopProfile(&t1);
	}

	// Another gc calling mechanism
	if (cfg->run_idle_notification_loop > 0 && cfg->request_num % cfg->run_idle_notification_loop == 0) {
		struct timeval t1;
		StartProfile(&t1);
			while(!v8::V8::IdleNotification()) {}
		cfg->run_idle_notification_loop_time = StopProfile(&t1);
	}

	return true;
}

bool LoadConfig(monocfg *cfg, std::string fname) {
	GlobalError.clear();

	++cfg->request_num;

	cfg->run_idle_notification_loop_time = 0;
	cfg->run_low_memory_notification_time = 0;
	cfg->exec_time = 0;
	cfg->compile_time = 0;

	if (!InitIsolate(cfg))
		return false;

	v8::HandleScope handle_scope(isolate);
	v8::TryCatch try_catch;

	// Get file stat
	struct stat stat_buf;
	if (cfg->watch_templates) {
		if (stat(fname.c_str(), &stat_buf) != 0) {
			std::string _err("Error opening file ");
			_err += fname;
			_err += ": ";
			_err += strerror(errno);

			GlobalError.push_back(_err);
			return false;
		}
	}

	// Reload context if has changed
    if (LoadConfigModified.find(fname) == LoadConfigModified.end() || (cfg->watch_templates && LoadConfigModified[fname] != stat_buf.st_mtime)) {
		std::string file = ReadFile(fname);
		if (file.size() == 0) {
			std::string _err("File not exists or empty: ");
			_err += fname;
	
			GlobalError.push_back(_err);
			return false;
		}

		std::string str;
		v8::Local<v8::Script> script;

		str = ";(function(g){g.config=g.config||{};g.config.buildCatalog=" + file + "})((0,eval)('this'));";

		if (!CompileSource(cfg, str, &script, &try_catch))
			return false;

        LoadConfigModified[fname] = stat_buf.st_mtime;

		struct timeval t1; StartProfile(&t1);
			v8::Local<v8::Value> result = script->Run();
		cfg->exec_time += StopProfile(&t1);

		if (result.IsEmpty()) {
			assert(try_catch.HasCaught());
			ReportException(&try_catch);
			return false;
		}

        assert(!try_catch.HasCaught());
    }

	return true;
}

void GetHeapStat(HeapSt * st) {
	v8::HeapStatistics hs;
	if (isolate == NULL) {return;}

	isolate->GetHeapStatistics(&hs);

	st->total_heap_size = hs.total_heap_size();
	st->total_heap_size_executable = hs.total_heap_size_executable();
	st->total_physical_size = hs.total_physical_size();
	st->used_heap_size = hs.used_heap_size();
	st->heap_size_limit = hs.heap_size_limit();
}

bool IdleNotification(int ms) {
	if (isolate == NULL) {return false;}
	return v8::V8::IdleNotification(ms);
}

void LowMemoryNotification() {
	if (isolate == NULL) {return;}
	v8::V8::LowMemoryNotification();
}

