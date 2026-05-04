#pragma once

#include "ITrackpadGesture.hpp"
#include "../../../../desktop/DesktopTypes.hpp"
#include "../../../../helpers/memory/Memory.hpp"

#include <cstdint>

namespace Layout::Tiled {
    struct SColumnData;
}

class CScrollMoveTrackpadGesture : public ITrackpadGesture {
  public:
    CScrollMoveTrackpadGesture()          = default;
    virtual ~CScrollMoveTrackpadGesture() = default;

    virtual void begin(const ITrackpadGesture::STrackpadGestureBegin& e);
    virtual void update(const ITrackpadGesture::STrackpadGestureUpdate& e);
    virtual void end(const ITrackpadGesture::STrackpadGestureEnd& e);

    virtual bool isDirectionSensitive();

    static bool  isGestureInProgress();

  private:
    bool                           m_wasScrollingLayout = false;
    bool                           m_hasLastUpdate      = false;
    uint32_t                       m_lastUpdateTimeMs   = 0;
    double                         m_velocity           = 0.0;
    double                         m_startedOffset      = 0.0;
    PHLMONITORREF                  m_monitor;
    WP<Layout::Tiled::SColumnData> m_startedColumn = nullptr;

    static bool                    s_gestureInProgress;
};
