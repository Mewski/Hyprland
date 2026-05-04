#include "ScrollMoveGesture.hpp"

#include "../../../../config/ConfigValue.hpp"
#include "../../../../desktop/Workspace.hpp"
#include "../../../../desktop/state/FocusState.hpp"
#include "../../../../helpers/Monitor.hpp"
#include "../../../../layout/LayoutManager.hpp"
#include "../../../../layout/algorithm/Algorithm.hpp"
#include "../../../../layout/algorithm/tiled/scrolling/ScrollingAlgorithm.hpp"
#include "../../../../layout/space/Space.hpp"
#include "../../../../managers/SessionLockManager.hpp"

#include <algorithm>
#include <cmath>

using namespace Layout::Tiled;

constexpr double            SCROLL_MOVE_VELOCITY_DECAY  = 5.0;
constexpr double            SCROLL_MOVE_MAX_PROJECTION  = 2.0;
constexpr double            SCROLL_MOVE_VELOCITY_SMOOTH = 0.35;

bool                        CScrollMoveTrackpadGesture::s_gestureInProgress = false;

static CScrollingAlgorithm* scrollingLayoutForMonitor(PHLMONITOR monitor) {
    const auto PMONITOR = monitor;
    if (!PMONITOR)
        return nullptr;

    const auto PWORKSPACE = PMONITOR->m_activeSpecialWorkspace ? PMONITOR->m_activeSpecialWorkspace : PMONITOR->m_activeWorkspace;
    if (!PWORKSPACE || !PWORKSPACE->m_space || !PWORKSPACE->m_space->algorithm())
        return nullptr;

    const auto& TILED_ALGO = PWORKSPACE->m_space->algorithm()->tiledAlgo();
    if (!TILED_ALGO)
        return nullptr;

    return dynamic_cast<CScrollingAlgorithm*>(TILED_ALGO.get());
}

static CScrollingAlgorithm* currentScrollingLayout() {
    return scrollingLayoutForMonitor(Desktop::focusState()->monitor());
}

static double deltaForUpdate(const ITrackpadGesture::STrackpadGestureUpdate& e) {
    if (!e.swipe)
        return 0.0;

    switch (e.direction) {
        case TRACKPAD_GESTURE_DIR_LEFT:
        case TRACKPAD_GESTURE_DIR_RIGHT:
        case TRACKPAD_GESTURE_DIR_HORIZONTAL: return e.swipe->delta.x * e.scale;
        case TRACKPAD_GESTURE_DIR_UP:
        case TRACKPAD_GESTURE_DIR_DOWN:
        case TRACKPAD_GESTURE_DIR_VERTICAL: return e.swipe->delta.y * e.scale;
        default: return std::abs(e.swipe->delta.x) > std::abs(e.swipe->delta.y) ? e.swipe->delta.x * e.scale : e.swipe->delta.y * e.scale;
    }
}

bool CScrollMoveTrackpadGesture::isGestureInProgress() {
    return s_gestureInProgress;
}

void CScrollMoveTrackpadGesture::begin(const ITrackpadGesture::STrackpadGestureBegin& e) {
    ITrackpadGesture::begin(e);

    if (g_pSessionLockManager->isSessionLocked() || s_gestureInProgress)
        return;

    const auto SCROLLING = currentScrollingLayout();

    m_wasScrollingLayout = !!SCROLLING;
    m_hasLastUpdate      = false;
    m_lastUpdateTimeMs   = 0;
    m_velocity           = 0.0;

    if (!SCROLLING)
        return;

    m_monitor           = Desktop::focusState()->monitor();
    m_startedOffset     = SCROLLING->normalizedTapeOffset();
    m_startedColumn     = SCROLLING->currentColumn();
    s_gestureInProgress = true;
}

void CScrollMoveTrackpadGesture::update(const ITrackpadGesture::STrackpadGestureUpdate& e) {
    if (!m_wasScrollingLayout || !s_gestureInProgress || !e.swipe)
        return;

    const auto SCROLLING = scrollingLayoutForMonitor(m_monitor.lock());
    if (!SCROLLING)
        return;

    const double DELTA   = deltaForUpdate(e);
    const double PRIMARY = SCROLLING->primaryViewportSize();

    if (DELTA == 0.0 || PRIMARY <= 0.0)
        return;

    const double NORMALIZED_DELTA        = DELTA / PRIMARY;
    const double NORMALIZED_OFFSET_DELTA = -NORMALIZED_DELTA;

    SCROLLING->moveTapeNormalized(NORMALIZED_DELTA);

    if (m_hasLastUpdate && e.swipe->timeMs > m_lastUpdateTimeMs) {
        const double DT = (e.swipe->timeMs - m_lastUpdateTimeMs) / 1000.0;

        if (DT > 0.0) {
            const double INSTANT_VELOCITY = NORMALIZED_OFFSET_DELTA / DT;

            if (std::isfinite(INSTANT_VELOCITY))
                m_velocity = m_velocity * (1.0 - SCROLL_MOVE_VELOCITY_SMOOTH) + INSTANT_VELOCITY * SCROLL_MOVE_VELOCITY_SMOOTH;
        }
    }

    m_hasLastUpdate    = true;
    m_lastUpdateTimeMs = e.swipe->timeMs;
}

void CScrollMoveTrackpadGesture::end(const ITrackpadGesture::STrackpadGestureEnd& e) {
    static auto PSNAP = CConfigValue<Config::BOOL>("gestures:scrolling:move_snap_to_grid");

    const auto  SCROLLING = scrollingLayoutForMonitor(m_monitor.lock());
    if (!m_wasScrollingLayout || !s_gestureInProgress || !SCROLLING) {
        m_wasScrollingLayout = false;
        m_hasLastUpdate      = false;
        m_velocity           = 0.0;
        s_gestureInProgress  = false;
        return;
    }

    const bool   CANCELLED = e.swipe && e.swipe->cancelled;
    const double DELTA     = CANCELLED ? 0.0 : std::clamp(m_velocity / SCROLL_MOVE_VELOCITY_DECAY, -SCROLL_MOVE_MAX_PROJECTION, SCROLL_MOVE_MAX_PROJECTION);
    const double PROJECTED = SCROLLING->normalizedTapeOffset() + DELTA;

    if (*PSNAP) {
        const auto LANDED = SCROLLING->snapToProjectedOffset(PROJECTED);

        if (LANDED == m_startedColumn.lock() && LANDED)
            SCROLLING->moveTapeNormalized(SCROLLING->normalizedTapeOffset() - m_startedOffset);
        else
            SCROLLING->focusColumn(LANDED);
    } else {
        SCROLLING->moveTapeNormalized(-DELTA);
        SCROLLING->focusColumn(SCROLLING->getColumnAtViewportCenter());
    }

    m_wasScrollingLayout = false;
    m_hasLastUpdate      = false;
    m_velocity           = 0.0;
    s_gestureInProgress  = false;
}

bool CScrollMoveTrackpadGesture::isDirectionSensitive() {
    return true;
}
