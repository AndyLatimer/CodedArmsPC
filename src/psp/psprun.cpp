// ca_psprun: headless runner for the recompiled game (development tool).
//   ca_psprun --iso CodedArms.iso [--trace] [--seconds N]
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "psp/kernel.h"

int main(int argc, char** argv) {
    psp::kernel::BootOptions opt;
    int seconds = 0;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--iso") && i + 1 < argc) opt.isoPath = argv[++i];
        else if (!std::strcmp(argv[i], "--trace")) opt.traceHle = true;
        else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = std::atoi(argv[++i]);
    }
    if (opt.isoPath.empty()) {
        std::fprintf(stderr, "usage: ca_psprun --iso CodedArms.iso [--trace] [--seconds N]\n");
        return 2;
    }
    if (seconds > 0) {
        std::thread([seconds] {
            std::this_thread::sleep_for(std::chrono::seconds(seconds));
            std::fprintf(stderr, "[RUN] time limit reached\n");
            std::fflush(stderr);
            std::_Exit(0);
        }).detach();
    }
    return psp::kernel::boot(opt);
}
