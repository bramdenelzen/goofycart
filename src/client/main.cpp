#include "app.h"

#include <SDL3/SDL_main.h>

#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    gk::AppOptions opt;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (std::strcmp(a, "--frames") == 0) opt.max_frames = std::atoi(next());
        else if (std::strcmp(a, "--screenshot") == 0) opt.screenshot_path = next();
        else if (std::strcmp(a, "--fullres") == 0) opt.full_resolution = true;
        else if (std::strcmp(a, "--validation") == 0) opt.validation = true;
        else if (std::strcmp(a, "--width") == 0) opt.window_width = std::atoi(next());
        else if (std::strcmp(a, "--height") == 0) opt.window_height = std::atoi(next());
    }
    if (const char* v = std::getenv("GK_VALIDATION"); v && *v == '1') opt.validation = true;
    gk::App app;
    return app.run(opt);
}
