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
#include <vector>

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

struct StackTrace {
    StackTrace() = default;

    HANDLE process = nullptr;
    std::vector<DWORD64> pc;

    void generate(CONTEXT ctx, int skip) {
        STACKFRAME64 sf = {};
        sf.AddrPC.Offset = ctx.Rip;
        sf.AddrPC.Mode = AddrModeFlat;
        sf.AddrStack.Offset = ctx.Rsp;
        sf.AddrStack.Mode = AddrModeFlat;
        sf.AddrFrame.Offset = ctx.Rbp;
        sf.AddrFrame.Mode = AddrModeFlat;
        process = GetCurrentProcess();
        HANDLE thread = GetCurrentThread();
        for (;;) {
            SetLastError(0);
            BOOL stack_walk_ok = StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &sf, &ctx, 0, &SymFunctionTableAccess64, &SymGetModuleBase64, 0);
            if (!stack_walk_ok || !sf.AddrFrame.Offset)
                return;
            if (skip)
                --skip;
            else
                pc.push_back(sf.AddrPC.Offset);
        }
    }

    friend std::ostream& operator<<(std::ostream& os, StackTrace const& st) {
        os << std::uppercase;
        for (auto const pc : st.pc) {
            // write the address
            os << std::hex << static_cast<std::uintptr_t>(pc) << "|" << std::dec;
            // write module name
            DWORD64 module_base = SymGetModuleBase64(st.process, pc);
            if (module_base) {
                char path_name[MAX_PATH] = {};
                DWORD size = GetModuleFileNameA(reinterpret_cast<HMODULE>(module_base), path_name, MAX_PATH);
                if (size)
                    os << path_name << "|";
                else
                    os << "???|";
            }
            else {
                os << "???|";
            }
            // write function name
            SYMBOL_INFO_PACKAGE sym = { sizeof(sym.si) };
            sym.si.MaxNameLen = MAX_SYM_NAME;
            if (SymFromAddr(st.process, pc, 0, &sym.si)) {
                os << sym.si.Name << "()";
            }
            else {
                os << "???";
            }
            // write source location
            IMAGEHLP_LINE64 ih_line = { sizeof(IMAGEHLP_LINE64) };
            DWORD dummy = 0;
            if (SymGetLineFromAddr64(st.process, pc, &dummy, &ih_line)) {
                os << "|" << ih_line.FileName << ":" << ih_line.LineNumber;
            }
            os << "\n";
        }
        return os;
    }
};

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

    CORRECT::CatchableType const* cType(unsigned i) const {
        return RVA_TO_VA_(const CORRECT::CatchableType*, ((__int32*)(cArray->arrayOfCatchableTypes))[i]);
    }

    std::type_info const* getTypeInfo(unsigned i) const
    {
        return RVA_TO_VA_(const std::type_info*, cType(i)->pType);
    }
    unsigned getThisDisplacement(unsigned i) const { return cType(i)->thisDisplacement.mdisp; }

    char* copy(unsigned i) const {
        auto ct = cType(i);
        // FIXME leak, hack
        auto data = new char[ct->sizeOrOffset];
        auto copy = RVA_TO_VA_(void (*)(void*, void*), cType(i)->copyFunction);
        copy(data, exception_object);
        return data;
    }

    void * exception_object;
    EXCEPTION_RECORD const* exc;
    HMODULE module = nullptr;
    const CORRECT::ThrowInfo* throwInfo = nullptr;
    const _CatchableTypeArray* cArray = nullptr;
#undef RVA_TO_VA_
};
template<class T> T * exception_cast(const UntypedException & e, bool copy) {
    const std::type_info & ti = typeid(T);
    for (int i = 0; i < e.getNumCatchableTypes(); ++i) {
        const std::type_info& ti_i = *e.getTypeInfo(i);
        if (ti_i == ti) {
            if (copy)
                return reinterpret_cast<T*>(e.copy(i));
            else
                return reinterpret_cast<T*>(e.exception_object) + e.getThisDisplacement(i);
        }
    }
    return nullptr;    
}

template<class Ex>
auto tryCatch(auto f, auto e) {
    StackTrace st;
    Ex* ex = nullptr;
    return [&] {
        __try {
            return f();
        }
        __except ([&](EXCEPTION_POINTERS* eps) -> DWORD {
            __try {
                const EXCEPTION_RECORD& er = *eps->ExceptionRecord;
                if (er.ExceptionCode == EXCEPTION_CPP_MICROSOFT) { // C++ exception
                    UntypedException ue(er);
                    if (ex = exception_cast<Ex>(ue, true)) {
                        int skip = 2; // skip RaiseException and _CxxThrowException
                        st.generate(*eps->ContextRecord, skip);
                        return EXCEPTION_EXECUTE_HANDLER;
                    }
                }
                return EXCEPTION_CONTINUE_SEARCH;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                return EXCEPTION_CONTINUE_SEARCH;
            }
            }(GetExceptionInformation())) {
            return [&] {
                return e(*ex, st);
            }();
        }
    }();
}

int main(int argc, char** argv) {
    SymInit sym;
    return tryCatch<std::exception>([&]() -> int {
        throw std::runtime_error("I'm an exception!");
        }, [&](std::exception& ex, StackTrace const& st) {
            std::cerr << ex.what() << "\n\nStack:\n" << st << std::endl;
            return -1;
        });
}
