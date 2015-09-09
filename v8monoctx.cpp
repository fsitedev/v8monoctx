#include "v8monoctx.h"

using namespace v8;

Isolate* isolate;
Persistent<Context> context;
std::map<std::string, time_t> ScriptModified;
std::map<std::string, PERSISTENT_COPYABLE> ScriptCached;

std::string GlobalData;
std::vector<std::string> GlobalError;

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

/*
		// Print wavy underline (GetUnderline is deprecated).
		int start = message->GetStartColumn();
		for (int i = 0; i < start; i++) {
			fprintf(stderr, " ");
		}
		int end = message->GetEndColumn();
		for (int i = start; i < end; i++) {
			fprintf(stderr, "^");
		}
		fprintf(stderr, "\n");
		v8::String::Utf8Value stack_trace(try_catch->StackTrace());
		if (stack_trace.length() > 0) {
			const char* stack_trace_string = ToCString(stack_trace);
			fprintf(stderr, "%s\n", stack_trace_string);
		}
*/
	}
}

// Global function
void DataFetch(const v8::FunctionCallbackInfo<v8::Value>& args) {
	// We will be creating temporary handles so we use a handle scope.
	HandleScope handle_scope(args.GetIsolate());

	args.GetReturnValue().Set(
		String::NewFromUtf8(args.GetIsolate(), GlobalData.c_str())
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


bool ExecuteFile(monocfg * cfg, std::string fname, std::string append, std::string run, std::string* json, std::string* out) {
	GlobalError.clear();

	++cfg->request_num;

	cfg->run_idle_notification_loop_time = 0;
	cfg->run_low_memory_notification_time = 0;
	cfg->exec_time = 0;
	cfg->compile_time = 0;

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
		v8::Handle<v8::Context> ctx = Context::New(isolate, NULL, global);
		context.Reset(isolate, ctx);
	
		if (context.IsEmpty()) {
			std::string _err("Error creating context");
			GlobalError.push_back(_err);

			return false;
		}
		ctx->Enter();
	}

	v8::HandleScope handle_scope(isolate);
	v8::TryCatch try_catch;

	if (json != NULL) {
		GlobalData.assign(*json);
	}

	std::string key = fname + ' ' + append;

	// Get file stat
	struct stat stat_buf;
	if (cfg->watch_templates) {
		int rc = stat(fname.c_str(), &stat_buf);
		if (rc != 0) {
			std::string _err("Error opening file ");
			_err += fname;
			_err += ": ";
			_err += strerror(errno);
	
			GlobalError.push_back(_err);
	
			return false;
		}
	}

	Local<Script> script;
	bool run_script = true;
	if (ScriptCached.find(key) == ScriptCached.end() || (cfg->watch_templates && ScriptModified[key] != stat_buf.st_mtime)) {
/*
		if (ScriptCached.find(key) == ScriptCached.end()) {
			fprintf(stderr, "New file: %s\n", fname.c_str());
		}
		else {
			fprintf(stderr, "File modified: %s\n", fname.c_str());
		}
*/
		// Read new file
		std::string file = ReadFile(fname);
		if (file.size() == 0) {
			std::string _err("File not exists or empty: ");
			_err += fname;
	
			GlobalError.push_back(_err);
			return false;
		}

		std::string file_append = file + append;
		Handle<String> source = String::NewFromUtf8(isolate, file_append.c_str());
		Handle<String> ffn = String::NewFromUtf8(isolate, fname.c_str());

		// Origin
		v8::Handle<v8::Integer> line = v8::Integer::New(isolate, 0);
		v8::Handle<v8::Integer> column = v8::Integer::New(isolate, 0);
		v8::ScriptOrigin origin(ffn, line, column);

		struct timeval t1; StartProfile(&t1);
			script = Script::Compile(source, &origin);
		cfg->compile_time += StopProfile(&t1);

		if (script.IsEmpty()) {
			ReportException(&try_catch);
			return false;
		}

		PERSISTENT_COPYABLE pscript;

		ScriptCached.insert( std::pair<std::string, PERSISTENT_COPYABLE>(key, pscript) );
		ScriptCached[key].Reset(isolate, script);
		ScriptModified[key] = stat_buf.st_mtime;
	}
	else if (run.length() == 0) {
		script = Local<Script>::New(isolate, ScriptCached[key]);
	}
	else {
		run_script = false;
	}

	v8::Handle<v8::Value> result; 
	if (run_script == true) {
		struct timeval t1; StartProfile(&t1);
			result = script->Run();
		cfg->exec_time += StopProfile(&t1);

		if (result.IsEmpty()) {
			assert(try_catch.HasCaught());
			ReportException(&try_catch);
			return false;
		}
	}

	if (run.length() > 0) {
		if (ScriptCached.find(run) == ScriptCached.end()) {
			Handle<String> ffn = String::NewFromUtf8(isolate, run.c_str());
	
			struct timeval t1; StartProfile(&t1);
				script = Script::Compile(ffn, ffn);
			cfg->compile_time += StopProfile(&t1);
	
			if (script.IsEmpty()) {
				ReportException(&try_catch);
				return false;
			}
	
			PERSISTENT_COPYABLE pscript;
	
			ScriptCached.insert( std::pair<std::string, PERSISTENT_COPYABLE>(run, pscript) );
			ScriptCached[run].Reset(isolate, script);
		}
		else {
			script = Local<Script>::New(isolate, ScriptCached[run]);
		}

		struct timeval t1;
		StartProfile(&t1);
			result = script->Run();
		cfg->exec_time += StopProfile(&t1);

		if (result.IsEmpty()) {
			assert(try_catch.HasCaught());
			ReportException(&try_catch);
			return false;
		}
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
