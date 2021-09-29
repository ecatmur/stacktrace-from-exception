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

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <vector>

#if defined _WIN32
#   include <Windows.h>
#   include <dbghelp.h>
#elif defined __linux__
#   include <functional>
#   include <memory>
#   if __has_include(<elfutils/libdwfl.h>)
#       include <elfutils/libdwfl.h>
#   endif
#   include <cxxabi.h>
#   include <execinfo.h>
#else
#   error unsupported, sorry
#endif

#if defined _WIN32
#pragma pack (push, 4)
namespace CORRECT {
struct CatchableType {
    __int32 properties;
    __int32 pType;
    _PMD    thisDisplacement;
    __int32 sizeOrOffset;
    __int32 copyFunction;
};
struct ThrowInfo {
    __int32 attributes;
    __int32 pmfnUnwind;
    __int32 pForwardCompat;
    __int32 pCatchableTypeArray;
};
}
#pragma pack (pop)

static constexpr unsigned EXCEPTION_CPP_MICROSOFT = 0xE06D7363;  // '?msc'
static constexpr unsigned EXCEPTION_CPP_MICROSOFT_EH_MAGIC_NUMBER1 = 0x19930520;  // '?msc' version magic, see ehdata.h

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
void ensureSymInit() { static SymInit sym; }

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

    void * exception_object;
    EXCEPTION_RECORD const* exc;
    HMODULE module = nullptr;
    const CORRECT::ThrowInfo* throwInfo = nullptr;
    const _CatchableTypeArray* cArray = nullptr;
#undef RVA_TO_VA_
};
template<class T> T * exception_cast(const UntypedException & e) {
    const std::type_info & ti = typeid(T);
    for (int i = 0; i < e.getNumCatchableTypes(); ++i) {
        const std::type_info& ti_i = *e.getTypeInfo(i);
        if (ti_i == ti)
            return reinterpret_cast<T*>(e.exception_object) + e.getThisDisplacement(i);
    }
    return nullptr;
}
#elif defined __linux__
struct AnyExceptionTypeInfo : std::type_info {
    bool __do_catch(std::type_info const* thr_type, void** thr_obj, unsigned outer) const override;
};
struct AnyException {
    virtual int dummy();
    using Handler = std::function<bool(std::type_info const* thr_type, void** thr_obj, unsigned outer)>;
    inline static thread_local Handler handler;
};
__asm__(R"(
	.weak	_ZTV12AnyException
	.section	.rodata._ZTV12AnyException,"aG",@progbits,_ZTV12AnyException,comdat
	.align 8
	.type	_ZTV12AnyException, @object
	.size	_ZTV12AnyException, 24
_ZTV12AnyException:
	.quad	0
	.quad	_ZTI12AnyException
	.quad	0
	.weak	_ZTI12AnyException
	.section	.rodata._ZTI12AnyException,"aG",@progbits,_ZTI12AnyException,comdat
	.align 8
	.type	_ZTI12AnyException, @object
	.size	_ZTI12AnyException, 16
_ZTI12AnyException:
	.quad	_ZTV20AnyExceptionTypeInfo+16
	.quad	_ZTS12AnyException
	.weak	_ZTS12AnyException
	.section	.rodata._ZTS12AnyException,"aG",@progbits,_ZTS12AnyException,comdat
	.align 8
	.type	_ZTS12AnyException, @object
	.size	_ZTS12AnyException, 15
_ZTS12AnyException:
	.string	"12AnyException"
)");
bool AnyExceptionTypeInfo::__do_catch(std::type_info const* thr_type, void** thr_obj, unsigned outer) const {
    return AnyException::handler(thr_type, thr_obj, outer);
}
struct Demangler {
    std::unique_ptr<char, void(*)(void*)> output{nullptr, ::free};
    std::size_t length = 0;
    std::string operator()(char const* symbol) {
        int status;
        if (auto* const result = abi::__cxa_demangle(symbol, output.get(), &length, &status)) {
            output.release();
            output.reset(result);
            return result;
        }
        return symbol;
    }
};
#endif

struct StackTrace {
    StackTrace() = default;

#if defined WIN32
    HANDLE process = GetCurrentProcess();
    std::vector<DWORD64> pc;
#elif defined __linux__
    static constexpr const std::size_t MaxSize = 128;
    std::vector<void*> pc;
#endif

#if defined WIN32
    void generate(CONTEXT ctx) {
        STACKFRAME64 sf = {};
        sf.AddrPC.Offset = ctx.Rip;
        sf.AddrPC.Mode = AddrModeFlat;
        sf.AddrStack.Offset = ctx.Rsp;
        sf.AddrStack.Mode = AddrModeFlat;
        sf.AddrFrame.Offset = ctx.Rbp;
        sf.AddrFrame.Mode = AddrModeFlat;
        HANDLE thread = GetCurrentThread();
        for (;;) {
            SetLastError(0);
            BOOL stack_walk_ok = StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &sf, &ctx, 0, &SymFunctionTableAccess64, &SymGetModuleBase64, 0);
            if (!stack_walk_ok || !sf.AddrFrame.Offset)
                return;
            pc.push_back(sf.AddrPC.Offset);
        }
    }
#elif defined __linux__
    void generate() {
        pc.resize(MaxSize);
        pc.resize(::backtrace(pc.data(), pc.size()));
    }
#endif

    friend std::ostream& operator<<(std::ostream& os, StackTrace const& st) {
        os << std::uppercase;
#if defined WIN32
        ensureSymInit();
#else
        Demangler demangler;
#   if __has_include(<elfutils/libdwfl.h>)
        struct DwflGuard {
            Dwfl* dwfl;
            DwflGuard() {
                static constexpr ::Dwfl_Callbacks callbacks = {
                    ::dwfl_linux_proc_find_elf, ::dwfl_standard_find_debuginfo, nullptr, nullptr};
                dwfl = ::dwfl_begin(&callbacks);
                ::dwfl_linux_proc_report(dwfl, ::getpid());
                ::dwfl_report_end(dwfl, nullptr, nullptr);
            }
            ~DwflGuard() { ::dwfl_end(dwfl); }
        } guard;
#   else
        std::unique_ptr<char*[], void(&)(void*)> symbols{::backtrace_symbols(st.pc.data(), st.pc.size()), ::free};
#   endif
#endif
        for (auto i = 0u; i != st.pc.size(); ++i) {
            auto const pc = st.pc[i];
            auto const addr = reinterpret_cast<std::uintptr_t>(pc);
            // write the address
            os << std::hex << addr << "|" << std::dec;
#if defined WIN32
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
#elif defined __linux__
#   if __has_include(<elfutils/libdwfl.h>)
            if (auto const module = ::dwfl_addrmodule(guard.dwfl, addr)) {
                // write module name
                os << ::dwfl_module_info(module, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) << "|";
                ::GElf_Off offset;
                ::GElf_Sym sym;
                // write function name
                if (auto const function =
                    ::dwfl_module_addrinfo(module, addr, &offset, &sym, nullptr, nullptr, nullptr))
                    os << demangler(function) << "+" << offset;
                // write source location
                int lineno, column;
                if (auto const line = ::dwfl_module_getsrc(module, addr))
                    if (auto const src = ::dwfl_lineinfo(line, nullptr, &lineno, &column, nullptr, nullptr))
                        os << "|" << src << ":" << lineno << ":" << column;
            }
#   else
            // symbol:  /path/to/file(function+0x0ff5e7) [0xadd2e55]
            // or       /path/to/file() [0xadd2e55]
            char* symbol = symbols.get()[i],* open = nullptr,* plus = nullptr,* close = nullptr;
            for (char* p = symbol; *p; ++p)
                switch (*p) {
                case '(': *(open = p) = '\0'; break;
                case '+': *(plus = p) = '\0'; break;
                case ')': *(close = p) = '\0'; break;
                }
            os << symbol;
            if (open && plus && close && open < plus && plus < close)
                os << "|" << demangler(open + 1) << "+" << std::strtoul(plus + 1, nullptr, 16);
#   endif
#endif
            os << "\n";
        }
        return os;
    }
};

template<class Ex>
auto tryCatch(auto f, auto e) {
    StackTrace st;
#if defined WIN32
    try {
        return [&] {
            __try {
                return f();
            }
            __except ([&](EXCEPTION_POINTERS* eps) -> DWORD {
                __try {
                    const EXCEPTION_RECORD& er = *eps->ExceptionRecord;
                    if (er.ExceptionCode == EXCEPTION_CPP_MICROSOFT) { // C++ exception
                        UntypedException ue(er);
                        if (exception_cast<Ex>(ue))
                            st.generate(*eps->ContextRecord);
                    }
                    return EXCEPTION_CONTINUE_SEARCH;
                }
                __except (EXCEPTION_EXECUTE_HANDLER) {
                    return EXCEPTION_CONTINUE_SEARCH;
                }
                }(GetExceptionInformation())) {
            }
        }();
    }
    catch (Ex& ex) {
        return e(ex, st);
    }
#elif defined __linux__
    struct Guard {
        AnyException::Handler prev;
        Guard(AnyException::Handler handler) : prev(std::exchange(AnyException::handler, std::move(handler))) {}
        ~Guard() { AnyException::handler = std::move(prev); }
    } guard([&](std::type_info const* thr_type, void** thr_obj, unsigned outer) -> bool {
        st.generate();
        return typeid(Ex).__do_catch(thr_type, thr_obj, outer);
    });
    try {
        return f();
    }
    catch (AnyException&) {
        try {
            std::rethrow_exception(std::current_exception());
        } catch (Ex& ex) {
            return e(ex, st);
        }
    }
#endif
}

int main(int argc, char** argv) {
    return tryCatch<std::exception>([&]() -> int {
        throw std::runtime_error("I'm an exception!");
        }, [&](std::exception& ex, StackTrace const& st) {
            std::cerr << ex.what() << "\n\nStack:\n" << st << std::endl;
            return 2;
        });
}
