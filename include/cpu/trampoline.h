// 64BIT ONLY LIVE FUNCTION CODE REPLACEMENT VIA MEMORY MANIPULATION FOR ONLY WINDOWS AS THERE IS NO NEED ON LINUX THANKS TO LD_PRELOAD
#ifndef __TRAMPOLINE_H__
#define __TRAMPOLINE_H__

#ifdef _WIN32

#include <cstddef>
#include <cstdint>
#include <windows.h>
#include "../compiler/builtin_functions.h"

class ScopedReadWriteAccess
{
    public:

        ScopedReadWriteAccess(void* address)
        {
            VirtualQuery(address, &m_mbi_thunk, sizeof(MEMORY_BASIC_INFORMATION));
            VirtualProtect(m_mbi_thunk.BaseAddress, m_mbi_thunk.RegionSize, PAGE_EXECUTE_READWRITE, &m_mbi_thunk.Protect);
        };

        ~ScopedReadWriteAccess()
        {
            VirtualProtect(m_mbi_thunk.BaseAddress, m_mbi_thunk.RegionSize, m_mbi_thunk.Protect, &m_mbi_thunk.Protect);
        }
    private:
        MEMORY_BASIC_INFORMATION m_mbi_thunk;
};

class Trampoline // SUPPORTS ONLY 64 BIT
{
    public :

        static inline constexpr std::size_t INSTRUCTION_SIZE = 14; // 16bits opcode + 32 bits offset + 64 bits address
        using Bytes = char[INSTRUCTION_SIZE];

        static bool install(void* original_function_address, void* replacement_function_address, Bytes original_bytes)
        {
            if (is_address_64_bit(original_function_address) == false || is_address_64_bit(replacement_function_address) == false) return false;

            ScopedReadWriteAccess scoped_read_write(original_function_address);

            builtin_memcpy(original_bytes, original_function_address, INSTRUCTION_SIZE);
            auto* target = reinterpret_cast<char*>(original_function_address);

            // FIRST 16 BITS - FARJMP opcode 0x25ff
            uint16_t farjmp_opcode = 0x25ff;
            builtin_memcpy(target, &farjmp_opcode, 2);

            // FOLLOWING 32 BITS, OFFSET
            uint32_t offset = 0;
            builtin_memcpy(target+2, &offset, 4);

            // FOLLOWING 64 BITS REPLACEMENT ADDRESS
            builtin_memcpy(target+6, &replacement_function_address, 8);

            return true;
        }

        static void uninstall(void* original_function_address, Bytes original_bytes)
        {
            ScopedReadWriteAccess scoped_read_write(original_function_address);
            builtin_memcpy(original_function_address, original_bytes, INSTRUCTION_SIZE);
        }

    private:

        static bool is_address_64_bit(void* address)
        {
            MEMORY_BASIC_INFORMATION mbi;
            VirtualQuery(address, &mbi, sizeof(MEMORY_BASIC_INFORMATION));
            return mbi.BaseAddress < (void*)0x8000000000000000;
        }
};

#endif

#endif