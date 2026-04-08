#include "LayerProfiler.hpp"
#include "tensorflow/lite/micro/micro_time.h"

namespace arm {
namespace app {

uint32_t LayerProfiler::BeginEvent(const char* tag)
{
     if (!m_enabled) {
        return 0;
    }
    if (m_opIndex < MAX_LAYERS) {
        m_layerOps[m_opIndex] = tag;   // ⬅️ OVO JE KLJUČ
        m_startTicks = tflite::GetCurrentTimeTicks();
    }
    return m_opIndex;
}

void LayerProfiler::EndEvent(uint32_t)
{
     if (!m_enabled) {
        return;
    }
    if (m_opIndex < MAX_LAYERS) {
        uint32_t end = tflite::GetCurrentTimeTicks();
        m_layerTicks[m_opIndex] = end - m_startTicks;
        ++m_opIndex;
    }
}

void LayerProfiler::Reset()
{
    m_opIndex = 0;
    m_startTicks = 0;
    for (uint32_t i = 0; i < MAX_LAYERS; ++i) {
        m_layerTicks[i] = 0;
        m_layerOps[i] = nullptr;
    }
}

uint32_t LayerProfiler::GetLayerTicks(uint32_t idx) const
{
    return (idx < m_opIndex) ? m_layerTicks[idx] : 0;
}

const char* LayerProfiler::GetLayerOp(uint32_t idx) const
{
    return (idx < m_opIndex) ? m_layerOps[idx] : nullptr;
}


uint32_t LayerProfiler::GetNumLayers() const
{
    return m_opIndex;
}

} // namespace app
} // namespace arm
