#pragma once

#include "../../helpers/memory/Memory.hpp"
#include "../../desktop/DesktopTypes.hpp"

#include <deque>

class CUnifiedScrollingSwipeGesture {
  public:
    void begin();
    void update(double delta, uint32_t timeMs);
    void end();

    bool isGestureInProgress();

  private:
    struct STrackerEvent {
        double   delta     = 0;
        uint32_t timestamp = 0;
    };

    std::deque<STrackerEvent> m_history;
    double                    m_trackerPos = 0;

    void                      trackerReset();
    void                      trackerPush(double delta, uint32_t timeMs);
    double                    trackerVelocity();
    double                    trackerProjectedEndPos();
    void                      trackerTrimHistory();

    PHLMONITORREF m_monitor;
    double        m_baseOffset = 0;
    bool          m_active     = false;

    friend class CScrollingSwipeGesture;
};

inline UP<CUnifiedScrollingSwipeGesture> g_pUnifiedScrollingSwipe = makeUnique<CUnifiedScrollingSwipeGesture>();
