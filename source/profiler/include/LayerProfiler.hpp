#pragma once

#include "tensorflow/lite/micro/micro_profiler_interface.h"

#include <cstdint>

namespace arm {
namespace app {

class LayerProfiler : public tflite::MicroProfilerInterface {
public:
    uint32_t BeginEvent(const char* tag) override;
    void EndEvent(uint32_t event_handle) override;
    void Reset();


    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    uint32_t GetLayerTicks(uint32_t idx) const;
    const char* GetLayerOp(uint32_t idx) const;
    uint32_t GetNumLayers() const;

private:
    static constexpr uint32_t MAX_LAYERS = 64;
    bool m_enabled{true};        
    uint32_t m_startTicks{0};
    uint32_t m_layerTicks[MAX_LAYERS]{};
    const char* m_layerOps[MAX_LAYERS]{};
    uint32_t m_opIndex{0};
};


} // namespace app
} // namespace arm