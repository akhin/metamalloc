#ifndef _PACKED_H_
#define _PACKED_H_

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// PACKED

// Compilers may add additional padding zeroes for alignment
// Though those additions may increase the size of your structs/classes
// The ideal way is manually aligning data structures and minimising the memory footprint
// Compilers won`t add additional padding zeroes for "packed" data structures

#ifdef __GNUC__
#define PACKED( __Declaration__ ) __Declaration__ __attribute__((__packed__))
#elif _MSC_VER
#define PACKED( __Declaration__ ) __pragma( pack(push, 1) ) __Declaration__ __pragma( pack(pop))
#endif

#endif