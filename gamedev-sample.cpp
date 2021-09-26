//Copyright (c) 2007 Howard Jeng
//Copyright (c) 2016 Ilya Dedinsky
//Copyright (c) 2021 Ed Catmur
//Permission is hereby granted, free of charge, to any person obtaining a copy
//of this software and associated documentation files (the "Software"), to deal
//in the Software without restriction, including without limitation the rights
//to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//copies of the Software, and to permit persons to whom the Software is
//furnished to do so, subject to the following condition:
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//THE SOFTWARE.

#include <Windows.h>
#include <dbghelp.h>

#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <typeinfo>

//------------------------------------------------------------------------------------------------------------------------------
//! These definitions are based on assembly listings produded by the compiler (/FAs) rather than built-in ones
//! @{

#pragma pack (push, 4)

namespace CORRECT
{

    struct CatchableType
    {
        __int32 properties;
        __int32 pType;
        _PMD    thisDisplacement;
        __int32 sizeOrOffset;
        __int32 copyFunction;
    };

    struct ThrowInfo
    {
        __int32 attributes;
        __int32 pmfnUnwind;
        __int32 pForwardCompat;
        __int32 pCatchableTypeArray;
    };

}
#pragma pack (pop)

//! @}
//------------------------------------------------------------------------------------------------------------------------------

const unsigned EXCEPTION_CPP_MICROSOFT = 0xE06D7363;  // '?msc'
static constexpr unsigned EXCEPTION_CPP_MICROSOFT_EH_MAGIC_NUMBER1 = 0x19930520;  // '?msc' version magic, see ehdata.h

//------------------------------------------------------------------------------------------------------------------------------


class SymInit {
public:
    SymInit(void) : process_(GetCurrentProcess()) {
        SymInitialize(process_, 0, TRUE);
        DWORD options = SymGetOptions();
        options |= SYMOPT_LOAD_LINES;
        SymSetOptions(options);
    }
    ~SymInit() { SymCleanup(process_); }
private:
    HANDLE process_;
    SymInit(const SymInit &) = delete;
    SymInit & operator=(const SymInit &) = delete;
};
std::string get_module_path(HMODULE module = 0) {
    char path_name[MAX_PATH] = {};
    DWORD size = GetModuleFileNameA(module, path_name, MAX_PATH);
    return std::string(path_name, size);
}
void write_module_name(std::ostream & os, HANDLE process, DWORD64 program_counter) {
    DWORD64 module_base = SymGetModuleBase64(process, program_counter);
    if (module_base) {
        std::string module_name = get_module_path(reinterpret_cast<HMODULE>(module_base));
        if (!module_name.empty())
            os << module_name << "|";
        else
            os << "Unknown module|";
    } else {
        os << "Unknown module|";
    }
}
void write_function_name(std::ostream & os, HANDLE process, DWORD64 program_counter) {
    SYMBOL_INFO_PACKAGE sym = { sizeof(sym) };
    sym.si.MaxNameLen = MAX_SYM_NAME;
    if (SymFromAddr(process, program_counter, 0, &sym.si)) {
        os << sym.si.Name << "()";
    } else {
        os << "Unknown function";
    }
}
void write_file_and_line(std::ostream & os, HANDLE process, DWORD64 program_counter) {
    IMAGEHLP_LINE64 ih_line = { sizeof(IMAGEHLP_LINE64) };
    DWORD dummy = 0;
    if (SymGetLineFromAddr64(process, program_counter, &dummy, &ih_line)) {
        os << "|" << ih_line.FileName << ":" << ih_line.LineNumber;
    }
}
void generate_stack_trace(std::ostream & os, CONTEXT ctx, int skip) {
    STACKFRAME64 sf = {};
    sf.AddrPC.Offset = ctx.Rip;
    sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrStack.Offset = ctx.Rsp;
    sf.AddrStack.Mode = AddrModeFlat;
    sf.AddrFrame.Offset = ctx.Rbp;
    sf.AddrFrame.Mode = AddrModeFlat;
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    os << std::uppercase;
    for (;;) {
        SetLastError(0);
        BOOL stack_walk_ok = StackWalk64(IMAGE_FILE_MACHINE_I386, process, thread, &sf, &ctx, 0, &SymFunctionTableAccess64, &SymGetModuleBase64, 0);
        if (!stack_walk_ok || !sf.AddrFrame.Offset)
            return;
        if (skip) {
            --skip;
        } else {
            // write the address
            os << std::hex << static_cast<std::uintptr_t>(sf.AddrPC.Offset) << "|" << std::dec;
            write_module_name(os, process, sf.AddrPC.Offset);
            write_function_name(os, process, sf.AddrPC.Offset);
            write_file_and_line(os, process, sf.AddrPC.Offset);
            os << "\n";
        }
    }
}
struct UntypedException {
#define RVA_TO_VA_(type, addr)  ( (type) ((uintptr_t) module + (uintptr_t) (addr)) )
    UntypedException(const EXCEPTION_RECORD& er) :
        exception_object(reinterpret_cast<void*>(er.ExceptionInformation[1])),
        exc(&er)
    {
        if (exc->ExceptionInformation[0] == EXCEPTION_CPP_MICROSOFT_EH_MAGIC_NUMBER1 &&
            exc->NumberParameters >= 3)
        {
            module = (exc->NumberParameters >= 4) ? (HMODULE)exc->ExceptionInformation[3] : NULL;
            throwInfo = (const CORRECT::ThrowInfo*)exc->ExceptionInformation[2];
            if (throwInfo)
                cArray = RVA_TO_VA_(const _CatchableTypeArray*, throwInfo->pCatchableTypeArray);
        }
    }

    unsigned getNumCatchableTypes() const { return cArray ? cArray->nCatchableTypes : 0u; }

    std::type_info const* getTypeInfo(unsigned i) const
    {
        const CORRECT::CatchableType* cType = RVA_TO_VA_(const CORRECT::CatchableType*, cArray->arrayOfCatchableTypes[i]);
        return RVA_TO_VA_(const std::type_info*, cType->pType);
    }

    unsigned getThisDisplacement(unsigned i) const
    {
        const CORRECT::CatchableType* cType = RVA_TO_VA_(const CORRECT::CatchableType*, cArray->arrayOfCatchableTypes[i]);
        return cType->thisDisplacement.mdisp;
    }

    void * exception_object;
    EXCEPTION_RECORD const* exc;
    HMODULE module = nullptr;
    const CORRECT::ThrowInfo* throwInfo = nullptr;
    const _CatchableTypeArray* cArray = nullptr;
#undef RVA_TO_VA_
};
void * exception_cast_worker(const UntypedException & e, const type_info & ti) {
    for (int i = 0; i < e.getNumCatchableTypes(); ++i) {
        const std::type_info & ti_i = *e.getTypeInfo(i);
        if (ti_i == ti)
            return reinterpret_cast<char*>(e.exception_object) + e.getThisDisplacement(i);
    }
    return nullptr;
}
void get_exception_types(std::ostream & os, const UntypedException & e) {
    for (int i = 0; i < e.getNumCatchableTypes(); ++i) {
        const std::type_info& ti_i = *e.getTypeInfo(i);
        os << ti_i.name() << "\n";
    }
}
template<class T> T * exception_cast(const UntypedException & e) {
    const std::type_info & ti = typeid(T);
    return reinterpret_cast<T*>(exception_cast_worker(e, ti));
}
DWORD do_filter(EXCEPTION_POINTERS * eps, std::string & buffer) {
    std::ostringstream sstr;
    const EXCEPTION_RECORD & er = *eps->ExceptionRecord;
    int skip = 0;
    switch (er.ExceptionCode) {
    case EXCEPTION_CPP_MICROSOFT: { // C++ exception
        UntypedException ue(er);
        if (std::exception* e = exception_cast<std::exception>(ue)) {
            const std::type_info & ti = typeid(*e);
            sstr << ti.name() << ":" << e->what();
        } else {
            sstr << "Unknown C++ exception thrown.\n";
            get_exception_types(sstr, ue);
        }
        skip = 2; // skip RaiseException and _CxxThrowException
    }
    break;
    case EXCEPTION_ACCESS_VIOLATION: {
        sstr << "Access violation. Illegal " << (er.ExceptionInformation[0] ? "write" : "read") <<
            " by " << er.ExceptionAddress <<
            " at " << static_cast<std::uintptr_t>(er.ExceptionInformation[1]);
    }
    break;
    default: {
        sstr << "SEH exception thrown. Exception code: " << std::hex << std::uppercase << er.ExceptionCode <<
            " at " << er.ExceptionAddress;
    }
    }
    sstr << "\n\nStack Trace:\n";
    generate_stack_trace(sstr, *eps->ContextRecord, skip);
    buffer = sstr.str();
    return EXCEPTION_EXECUTE_HANDLER;
}
DWORD filter(EXCEPTION_POINTERS * eps, std::string & buffer) {
    __try {
        return do_filter(eps, buffer);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
}
int actual_main(int, char **) {
    // do stuff
    // cause an access violation
    //char * ptr = 0; *ptr = 0;
    // divide by zero
    //int x = 5; x = x / (x - x);
    // C++ exception
    throw std::runtime_error("I'm an exception!");
    //throw 5;
    return 0;
}
void save_buffer(const std::string & buffer) {
    std::ofstream ofs("err_log.txt");
    if (ofs)
        ofs << buffer;
}
int seh_helper(int argc, char ** argv, std::string & buffer) {
    __try {
        return actual_main(argc, argv);
    }
    __except (filter(GetExceptionInformation(), buffer)) {
        if (!buffer.empty()) {
            save_buffer(buffer);
            MessageBoxA(0, buffer.c_str(), "Abnormal Termination", MB_OK);
        }
        return -1;
    }
}
int main(int argc, char ** argv) {
    SymInit sym;
    std::string buffer;
    return seh_helper(argc, argv, buffer);
}
