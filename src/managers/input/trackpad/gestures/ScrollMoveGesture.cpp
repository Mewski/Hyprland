#include "ScrollMoveGesture.hpp"

#include "../../../../Compositor.hpp"
#include "../../../../config/ConfigValue.hpp"
#include "../../../../desktop/Workspace.hpp"
#include "../../../../desktop/state/FocusState.hpp"
#include "../../../../helpers/Monitor.hpp"
#include "../../../../layout/LayoutManager.hpp"
#include "../../../../layout/algorithm/Algorithm.hpp"
#include "../../../../layout/algorithm/tiled/scrolling/ScrollTapeController.hpp"
#include "../../../../layout/algorithm/tiled/scrolling/ScrollingAlgorithm.hpp"
#include "../../../../layout/space/Space.hpp"
#include "../../../../managers/SessionLockManager.hpp"
#include "../../../../render/Renderer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Layout::Tiled;

static constexpr uint32_t   TRACKER_HISTORY_MS    = 150;
static constexpr double     DECELERATION_TOUCHPAD = 0.997;
static constexpr double     WORKING_AREA_MOVEMENT = 1200.0;

bool                        CScrollMoveTrackpadGesture::s_gestureInProgress = false;

static CScrollingAlgorithm* scrollingLayoutForMonitor(PHLMONITOR monitor) {
    if (!monitor)
        return nullptr;

    const auto PWORKSPACE = monitor->m_activeSpecialWorkspace ? monitor->m_activeSpecialWorkspace : monitor->m_activeWorkspace;
    if (!PWORKSPACE || !PWORKSPACE->m_space || !PWORKSPACE->m_space->algorithm())
        return nullptr;

    const auto& TILED_ALGO = PWORKSPACE->m_space->algorithm()->tiledAlgo();
    if (!TILED_ALGO)
        return nullptr;

    return dynamic_cast<CScrollingAlgorithm*>(TILED_ALGO.get());
}

static double deltaForUpdate(const ITrackpadGesture::STrackpadGestureUpdate& e) {
    if (!e.swipe)
        return 0.0;

    switch (e.direction) {
        case TRACKPAD_GESTURE_DIR_LEFT:
        case TRACKPAD_GESTURE_DIR_RIGHT:
        case TRACKPAD_GESTURE_DIR_HORIZONTAL: return e.swipe->delta.x;
        case TRACKPAD_GESTURE_DIR_UP:
        case TRACKPAD_GESTURE_DIR_DOWN:
        case TRACKPAD_GESTURE_DIR_VERTICAL: return e.swipe->delta.y;
        default: return std::abs(e.swipe->delta.x) > std::abs(e.swipe->delta.y) ? e.swipe->delta.x : e.swipe->delta.y;
    }
}

void CScrollMoveTrackpadGesture::trackerReset() {
    m_history.clear();
    m_trackerPos = 0.0;
}

void CScrollMoveTrackpadGesture::trackerPush(double delta, uint32_t timeMs) {
    if (!m_history.empty() && timeMs < m_history.back().timestamp)
        return;

    m_history.push_back({delta, timeMs});
    m_trackerPos += delta;
    trackerTrimHistory();
}

double CScrollMoveTrackpadGesture::trackerVelocity() {
    if (m_history.size() < 2)
        return 0.0;

    const double TOTAL_TIME = (m_history.back().timestamp - m_history.front().timestamp) / 1000.0;
    if (TOTAL_TIME == 0.0)
        return 0.0;

    double totalDelta = 0.0;
    for (const auto& e : m_history)
        totalDelta += e.delta;

    return totalDelta / TOTAL_TIME;
}

double CScrollMoveTrackpadGesture::trackerProjectedEndPos() {
    return m_trackerPos - trackerVelocity() / (1000.0 * std::log(DECELERATION_TOUCHPAD));
}

void CScrollMoveTrackpadGesture::trackerTrimHistory() {
    if (m_history.empty())
        return;

    const uint32_t LATEST = m_history.back().timestamp;
    while (!m_history.empty() && LATEST > m_history.front().timestamp + TRACKER_HISTORY_MS)
        m_history.pop_front();
}

bool CScrollMoveTrackpadGesture::isGestureInProgress() {
    return s_gestureInProgress;
}

void CScrollMoveTrackpadGesture::begin(const ITrackpadGesture::STrackpadGestureBegin& e) {
    ITrackpadGesture::begin(e);

    if (g_pSessionLockManager->isSessionLocked() || m_active)
        return;

    const auto PMONITOR = Desktop::focusState()->monitor();
    if (!PMONITOR)
        return;

    const auto SCROLLING = scrollingLayoutForMonitor(PMONITOR);
    if (!SCROLLING)
        return;

    const auto SDATA = SCROLLING->scrollingData();
    if (!SDATA || SDATA->columns.empty())
        return;

    m_monitor           = PMONITOR;
    m_baseOffset        = SDATA->controller->getOffset();
    m_active            = true;
    s_gestureInProgress = true;

    trackerReset();
}

void CScrollMoveTrackpadGesture::update(const ITrackpadGesture::STrackpadGestureUpdate& e) {
    if (!m_active || !e.swipe)
        return;

    const auto PMONITOR = m_monitor.lock();
    if (!PMONITOR)
        return;

    const auto SCROLLING = scrollingLayoutForMonitor(PMONITOR);
    if (!SCROLLING)
        return;

    const auto SDATA = SCROLLING->scrollingData();
    if (!SDATA || SDATA->columns.empty())
        return;

    const double DELTA = deltaForUpdate(e) * e.scale;
    if (DELTA == 0.0)
        return;

    trackerPush(DELTA, e.swipe->timeMs);

    const CBox   USABLE         = SCROLLING->usableArea();
    const double USABLE_PRIMARY = SDATA->controller->isPrimaryHorizontal() ? USABLE.w : USABLE.h;
    if (USABLE_PRIMARY <= 0.0)
        return;

    const double NORM_FACTOR = USABLE_PRIMARY / WORKING_AREA_MOVEMENT;
    const double NEW_OFFSET  = m_baseOffset - m_trackerPos * NORM_FACTOR;

    SDATA->controller->setOffset(NEW_OFFSET);
    SDATA->recalculate(true);

    g_pHyprRenderer->damageMonitor(PMONITOR);
}

void CScrollMoveTrackpadGesture::end(const ITrackpadGesture::STrackpadGestureEnd& e) {
    static const auto PSNAP = CConfigValue<Config::BOOL>("gestures:scrolling:move_snap_to_grid");

    if (!m_active) {
        m_active            = false;
        s_gestureInProgress = false;
        return;
    }

    const auto PMONITOR = m_monitor.lock();
    if (!PMONITOR) {
        trackerReset();
        m_active            = false;
        s_gestureInProgress = false;
        return;
    }

    const auto SCROLLING = scrollingLayoutForMonitor(PMONITOR);
    if (!SCROLLING) {
        trackerReset();
        m_active            = false;
        s_gestureInProgress = false;
        return;
    }

    const auto SDATA = SCROLLING->scrollingData();
    if (!SDATA || SDATA->columns.empty()) {
        trackerReset();
        m_active            = false;
        s_gestureInProgress = false;
        return;
    }

    static const auto PFSONONE   = CConfigValue<Config::INTEGER>("scrolling:fullscreen_on_one_column");
    static const auto PFITMETHOD = CConfigValue<Config::INTEGER>("scrolling:focus_fit_method");

    const CBox        USABLE         = SCROLLING->usableArea();
    const double      USABLE_PRIMARY = SDATA->controller->isPrimaryHorizontal() ? USABLE.w : USABLE.h;
    const double      NORM_FACTOR    = USABLE_PRIMARY > 0.0 ? USABLE_PRIMARY / WORKING_AREA_MOVEMENT : 0.0;

    const double      PROJECTED_END    = trackerProjectedEndPos() * NORM_FACTOR;
    const double      MAX_EXTENT       = SDATA->controller->calculateMaxExtent(USABLE, *PFSONONE);
    const double      MIN_OFFSET       = 0.0;
    const double      MAX_OFFSET       = std::max(0.0, MAX_EXTENT - USABLE_PRIMARY);
    const double      UNCLAMPED_TARGET = m_baseOffset - PROJECTED_END;

    if (!*PSNAP) {
        const double CLAMPED = std::clamp(UNCLAMPED_TARGET, MIN_OFFSET, MAX_OFFSET);
        SDATA->controller->setOffset(CLAMPED);
        SDATA->recalculate(false);

        SCROLLING->focusColumn(SCROLLING->getColumnAtViewportCenter());

        trackerReset();
        m_active            = false;
        s_gestureInProgress = false;
        g_pHyprRenderer->damageMonitor(PMONITOR);
        return;
    }

    struct SSnap {
        double offset;
        size_t colIdx;
    };

    std::vector<SSnap> snaps;
    snaps.reserve(SDATA->columns.size() * 2);

    for (size_t i = 0; i < SDATA->columns.size(); ++i) {
        const double STRIP_START = SDATA->controller->calculateStripStart(i, USABLE, *PFSONONE);
        const double STRIP_SIZE  = SDATA->controller->calculateStripSize(i, USABLE, *PFSONONE);

        if (*PFITMETHOD == 1) {
            snaps.push_back({STRIP_START - USABLE_PRIMARY + STRIP_SIZE, i});
            snaps.push_back({STRIP_START, i});
        } else
            snaps.push_back({STRIP_START - (USABLE_PRIMARY - STRIP_SIZE) / 2.0, i});
    }

    if (snaps.empty()) {
        trackerReset();
        m_active            = false;
        s_gestureInProgress = false;
        return;
    }

    size_t bestIdx  = 0;
    double bestDist = std::numeric_limits<double>::max();
    for (size_t i = 0; i < snaps.size(); ++i) {
        const double DIST = std::abs(snaps[i].offset - UNCLAMPED_TARGET);
        if (DIST < bestDist) {
            bestDist = DIST;
            bestIdx  = i;
        }
    }

    size_t       targetColIdx   = snaps[bestIdx].colIdx;
    const double TARGET_OFFSET  = std::clamp(snaps[bestIdx].offset, MIN_OFFSET, MAX_OFFSET);
    const double CURRENT_OFFSET = SDATA->controller->getOffset();

    if (*PFITMETHOD == 1) {
        if (UNCLAMPED_TARGET >= CURRENT_OFFSET) {
            for (size_t i = targetColIdx + 1; i < SDATA->columns.size(); ++i) {
                const double COL_START = SDATA->controller->calculateStripStart(i, USABLE, *PFSONONE);
                const double COL_END   = COL_START + SDATA->controller->calculateStripSize(i, USABLE, *PFSONONE);
                if (COL_END > TARGET_OFFSET + USABLE_PRIMARY)
                    break;
                targetColIdx = i;
            }
        } else {
            for (size_t i = targetColIdx; i > 0; --i) {
                const double COL_START = SDATA->controller->calculateStripStart(i - 1, USABLE, *PFSONONE);
                if (COL_START < TARGET_OFFSET)
                    break;
                targetColIdx = i - 1;
            }
        }
    }

    SDATA->controller->setOffset(TARGET_OFFSET);
    SDATA->recalculate(false);

    if (targetColIdx < SDATA->columns.size())
        SCROLLING->focusColumn(SDATA->columns[targetColIdx]);

    trackerReset();
    m_active            = false;
    s_gestureInProgress = false;
    g_pHyprRenderer->damageMonitor(PMONITOR);
}

bool CScrollMoveTrackpadGesture::isDirectionSensitive() {
    return true;
}
