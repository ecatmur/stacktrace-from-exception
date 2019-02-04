#include <exception>
#include <iostream>
#include <optional>

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
    } catch (std::exception& ex) {
        std::cerr << "Exception " << typeid(ex).name() << ": " << ex.what() << std::endl;
        return 1;
    }
}