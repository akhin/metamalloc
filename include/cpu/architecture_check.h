#ifndef _ARCHITECTURE_CHECK_H_
#define _ARCHITECTURE_CHECK_H_

//////////////////////////////////////////////////////////////////////
// ARCHITECTURE CHECK

#if defined(_MSC_VER)
#if (! defined(_M_X64))
#error "This library is supported for only x86-x64 architectures"
#endif
#elif defined(__GNUC__)
#if (! defined(__x86_64__)) && (! defined(__x86_64))
#error "This library is supported for only x86-x64 architectures"
#endif
#endif

#endif