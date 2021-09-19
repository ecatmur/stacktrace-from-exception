//Copyright (c) 2007 Howard Jeng
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
    sf.AddrPC.Offset = ctx.Eip;
    sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrStack.Offset = ctx.Esp;
    sf.AddrStack.Mode = AddrModeFlat;
    sf.AddrFrame.Offset = ctx.Ebp;
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
    UntypedException(const EXCEPTION_RECORD & er) :
        exception_object(reinterpret_cast<void*>(er.ExceptionInformation[1])),
        type_array(reinterpret_cast<_ThrowInfo *>(er.ExceptionInformation[2])->pCatchableTypeArray)
    {}
    void * exception_object;
    _CatchableTypeArray * type_array;
};
void * exception_cast_worker(const UntypedException & e, const type_info & ti) {
    for (int i = 0; i < e.type_array->nCatchableTypes; ++i) {
        _CatchableType & type_i = *e.type_array->arrayOfCatchableTypes[i];
        const std::type_info & ti_i = *reinterpret_cast<std::type_info const*>(type_i.pType);
        if (ti_i == ti) {
            char * base_address = reinterpret_cast<char*>(e.exception_object);
            base_address += type_i.thisDisplacement.mdisp;
            return base_address;
        }
    }
    return 0;
}
void get_exception_types(std::ostream & os, const UntypedException & e) {
    for (int i = 0; i < e.type_array->nCatchableTypes; ++i) {
        _CatchableType & type_i = *e.type_array->arrayOfCatchableTypes[i];
        const std::type_info & ti_i = *reinterpret_cast<std::type_info const*>(type_i.pType);
        os << ti_i.name() << "\n";
    }
}
template<class T> T * exception_cast(const UntypedException & e) {
    const std::type_info & ti = typeid(T);
    return reinterpret_cast<T*>(exception_cast_worker(e, ti));
}
DWORD do_filter(EXCEPTION_POINTERS * eps, std::string & buffer) {
    std::stringstream sstr;
    const EXCEPTION_RECORD & er = *eps->ExceptionRecord;
    int skip = 0;
    switch (er.ExceptionCode) {
    case 0xE06D7363: { // C++ exception
        UntypedException ue(er);
        if (std::exception * e = exception_cast<std::exception>(ue)) {
            const std::type_info & ti = typeid(*e);
            sstr << ti.name() << ":" << e->what();
        } else {
            sstr << "Unknown C++ exception thrown.\n";
            get_exception_types(sstr, ue);
        } skip = 2; // skip RaiseException and _CxxThrowException
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
    //throw std::runtime_error("I'm an exception!");
    throw 5;
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
