#pragma once

#include "ITrackpadGesture.hpp"
#include "../../../../desktop/DesktopTypes.hpp"
#include "../../../../helpers/memory/Memory.hpp"

#include <cstdint>
#include <deque>

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
    struct STrackerEvent {
        double   delta     = 0.0;
        uint32_t timestamp = 0;
    };

    void                      trackerReset();
    void                      trackerPush(double delta, uint32_t timeMs);
    double                    trackerVelocity();
    double                    trackerProjectedEndPos();
    void                      trackerTrimHistory();

    std::deque<STrackerEvent> m_history;
    double                    m_trackerPos = 0.0;

    PHLMONITORREF             m_monitor;
    double                    m_baseOffset = 0.0;
    bool                      m_active     = false;

    static bool               s_gestureInProgress;
};
