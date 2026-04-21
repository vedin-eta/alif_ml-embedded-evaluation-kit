#pragma once

#include "tensorflow/lite/micro/micro_profiler_interface.h"

namespace arm {
namespace app {

class NullProfiler final : public tflite::MicroProfilerInterface {
public:
    uint32_t BeginEvent(const char*) override { return 0; }
    void EndEvent(uint32_t) override {}
};

} // namespace app
} // namespace arm
