#include "erasgf/logos.hpp"
#include <iostream>

using namespace erasgf;

#ifndef ERA_SGF_VERSION
#define ERA_SGF_VERSION "dev"
#endif
int main(int argc, char** argv) {
    try {
        const int choice = argc > 1 ? std::stoi(argv[1]) : 5;
        Scene scene; const bool launch = argc > 2 && std::string(argv[2]) == "--launch";
        if (launch) scene = launch_scene(choice, ERA_SGF_VERSION, 1264, 1680);
        else scene.drawing = logo_mark(choice, {0, 0, 1000, 1000});
        std::cout << svg(scene, launch ? 1264 : 1000, launch ? 1680 : 1000);
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
