#pragma once

#include <string>

namespace dispatch_queue {

/**
 * Set current thread name.
 * Implementation is platform-dependent and supports Windows, Apple, Emscripten and POSIX APIs.
 */
void set_thread_name(const std::string& name);

}
