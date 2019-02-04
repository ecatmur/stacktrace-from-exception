#include <cstring>
#include <exception>
#include <iostream>
#include <optional>
#include <typeinfo>

struct AnyExceptionTypeInfo : std::type_info {
    bool __do_catch(std::type_info const* thr_type, void** thr_obj, unsigned outer) const override;
};

class AnyException {
    virtual int dummy();
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
    ::system("gdb -q --batch -p $PPID -ex where -ex gcore");
    return true;
}

struct Greeter {
    std::optional<std::string> name;
    int result();
};

int Greeter::result() {
    std::cout << "Hello, " << name.value() << std::endl;
    return 0;
}

struct Cleanup {
    ~Cleanup() { std::cout << "Did some cleanup" << std::endl; }
};

int main() {
    try {
        auto guard = Cleanup{};
        auto name = std::optional<std::string>{};
        auto g = Greeter{name};
        return g.result();
    } catch (AnyException&) {
        try {
            std::rethrow_exception(std::current_exception());
        } catch (std::exception& ex) {
            std::cerr << "Exception " << typeid(ex).name() << ": " << ex.what() << std::endl;
            return 1;
        }
    }
}