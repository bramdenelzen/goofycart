#include "app.h"

#include <SDL3/SDL_main.h>

#include <cstdio>
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
        else if (std::strcmp(a, "--autodrive") == 0) opt.autodrive = true;
        else if (std::strcmp(a, "--karts") == 0) opt.karts = std::atoi(next());
        else if (std::strcmp(a, "--cpu") == 0) opt.cpu_level = gk::sim::parse_cpu_level(next());
        else if (std::strcmp(a, "--track") == 0) opt.track = next();
        else if (std::strcmp(a, "--mute") == 0) opt.mute = true;
        else if (std::strcmp(a, "--time-trial") == 0) opt.time_trial = true;
        else if (std::strcmp(a, "--menu") == 0) opt.menu = next();
        else if (std::strcmp(a, "--master") == 0) opt.master_url = next();
        else if (std::strcmp(a, "--region") == 0) opt.region = next();
        else if (std::strcmp(a, "--crash") == 0) opt.crash = next();
        else if (std::strcmp(a, "--connect") == 0) {
            // host or host:port
            std::string target = next();
            if (const auto colon = target.rfind(':'); colon != std::string::npos && target.find(':') == colon) {
                opt.connect_port = uint16_t(std::atoi(target.c_str() + colon + 1));
                target.resize(colon);
            }
            opt.connect_host = target;
        } else if (std::strcmp(a, "--name") == 0) opt.name = next();
        else if (std::strcmp(a, "--lag") == 0) opt.link.lag_ms = std::atoi(next());
        else if (std::strcmp(a, "--jitter") == 0) opt.link.jitter_ms = std::atoi(next());
        else if (std::strcmp(a, "--loss") == 0) opt.link.loss = float(std::atof(next())) / 100.0f;
        else if (std::strcmp(a, "--cc") == 0) {
            const int cc = std::atoi(next());
            opt.engine = cc <= 50 ? gk::sim::EngineClass::cc50 : cc <= 100 ? gk::sim::EngineClass::cc100
                       : cc <= 150 ? gk::sim::EngineClass::cc150 : gk::sim::EngineClass::cc200;
        } else if (std::strcmp(a, "--build") == 0) {
            // driver,body,tires,glider indices into data/parts.json
            std::sscanf(next(), "%d,%d,%d,%d", &opt.build.driver, &opt.build.body, &opt.build.tires, &opt.build.glider);
        }
    }
    if (const char* v = std::getenv("GK_VALIDATION"); v && *v == '1') opt.validation = true;
    gk::App app;
    return app.run(opt);
}
