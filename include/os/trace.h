#ifndef __TRACE_H__
#define __TRACE_H__

#include <cstdarg>
#include <cstdio>
#include "../compiler/unused.h"

#ifdef __linux__ // VOLTRON_EXCLUDE
#include <unistd.h>
#elif _WIN32 // VOLTRON_EXCLUDE
#include <windows.h>
#endif // VOLTRON_EXCLUDE

#ifdef ENABLE_TRACER

// DOES NOT ALLOCATE MEMORY
void trace(const char* format, ...)
{
    const std::size_t max_length = 1024;
    char buffer[max_length];

    va_list args;
    va_start(args, format);
    std::size_t length = vsnprintf(buffer, max_length, format, args);
    va_end(args);

    if (length < 0)
    {
        return;
    }
    #ifdef __linux__
    auto ret = write(STDOUT_FILENO, buffer, length);
    #elif _WIN32
    OutputDebugStringA(buffer);
    #endif
    UNUSED(ret);
}

#define trace_message(MESSAGE) (trace(("%s\n"), (MESSAGE)));
#define trace_integer_value(VAR_NAME,VALUE); (trace(("variable name = %s , value=%zu\n"), (VAR_NAME),(VALUE)));
#define trace_double_value(VAR_NAME,VALUE); (trace(("variable name = %s , value=%f\n"), (VAR_NAME),(VALUE)));
#define trace_string_value(VAR_NAME,VALUE); (trace(("variable name = %s , value=%s\n"), (VAR_NAME),(VALUE)));

#else
// COMPILER WON'T GENERATE CODE FOR EXISTING CALLS
#define trace_message(MESSAGE);
#define trace_integer_value(VAR_NAME,VALUE);
#define trace_double_value(VAR_NAME,VALUE);
#define trace_string_value(VAR_NAME,VALUE);
#endif

#endif