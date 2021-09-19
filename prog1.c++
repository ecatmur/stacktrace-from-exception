#include <exception>
#include <iostream>
#include <optional>

std::terminate_handler prev = std::set_terminate([] {
    ::system("gdb -q --batch -p $PPID -ex where");
    prev();
});

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
    auto guard = Cleanup{};
    auto name = std::optional<std::string>{};
    auto g = Greeter{name};
    return g.result();
}