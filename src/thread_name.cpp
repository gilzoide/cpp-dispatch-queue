#include "../include/dispatch_queue/thread_name.hpp"

#include <cstring>
#if defined(_WIN32)
	#include <cstdlib>
	#include <windows.h>
	#include <processthreadsapi.h>
#elif defined(__EMSCRIPTEN__)
	#include <emscripten.h>
#else
	#include <pthread.h>
#endif


namespace dispatch_queue {

template<size_t N>
static void copy_into(char (&buffer)[N], const std::string& s) {
	size_t size = std::min(s.size(), N - 1);
	memcpy(buffer, s.data(), size);
	buffer[size] = '\0';
}

void set_thread_name(const std::string& name) {
#if defined(_WIN32)
	wchar_t wcbuf[64];
	size_t converted = mbstowcs(wcbuf, name.c_str(), 64);
	wcbuf[converted] = L'\0';
	SetThreadDescription(GetCurrentThread(), wcbuf);
#elif defined(__APPLE__)
	char buf[64];
	copy_into(buf, name);
	pthread_setname_np(buf);
#elif defined(__EMSCRIPTEN__)
	char buf[32];
	copy_into(buf, name);
	emscripten_set_thread_name(pthread_self(), buf);
#else
	char buf[16];
	copy_into(buf, name);
	pthread_setname_np(pthread_self(), buf);
#endif
}

}
