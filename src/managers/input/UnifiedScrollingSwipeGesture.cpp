#include "UnifiedScrollingSwipeGesture.hpp"

#include "../../Compositor.hpp"
#include "../../desktop/state/FocusState.hpp"
#include "../../render/Renderer.hpp"
#include "../../layout/algorithm/Algorithm.hpp"
#include "../../layout/algorithm/tiled/scrolling/ScrollingAlgorithm.hpp"
#include "../../layout/space/Space.hpp"
#include "InputManager.hpp"

using namespace Layout::Tiled;

static constexpr uint32_t TRACKER_HISTORY_MS     = 150;
static constexpr double   DECELERATION_TOUCHPAD  = 0.997;
static constexpr double   WORKING_AREA_MOVEMENT  = 1200.0;

void CUnifiedScrollingSwipeGesture::trackerReset() {
    m_history.clear();
    m_trackerPos = 0;
}

void CUnifiedScrollingSwipeGesture::trackerPush(double delta, uint32_t timeMs) {
    if (!m_history.empty() && timeMs < m_history.back().timestamp)
        return;

    m_history.push_back({delta, timeMs});
    m_trackerPos += delta;
    trackerTrimHistory();
}

double CUnifiedScrollingSwipeGesture::trackerVelocity() {
    if (m_history.size() < 2)
        return 0.0;

    const double totalTime = (m_history.back().timestamp - m_history.front().timestamp) / 1000.0;
    if (totalTime == 0.0)
        return 0.0;

    double totalDelta = 0.0;
    for (const auto& e : m_history)
        totalDelta += e.delta;

    return totalDelta / totalTime;
}

double CUnifiedScrollingSwipeGesture::trackerProjectedEndPos() {
    return m_trackerPos - trackerVelocity() / (1000.0 * std::log(DECELERATION_TOUCHPAD));
}

void CUnifiedScrollingSwipeGesture::trackerTrimHistory() {
    if (m_history.empty())
        return;

    const uint32_t latest = m_history.back().timestamp;
    while (!m_history.empty() && latest > m_history.front().timestamp + TRACKER_HISTORY_MS)
        m_history.pop_front();
}

bool CUnifiedScrollingSwipeGesture::isGestureInProgress() {
    return m_active;
}

void CUnifiedScrollingSwipeGesture::begin() {
    if (m_active)
        return;

    const auto PMONITOR   = Desktop::focusState()->monitor();
    const auto PWORKSPACE = PMONITOR->m_activeWorkspace;

    if (!PWORKSPACE || !PWORKSPACE->m_space || !PWORKSPACE->m_space->algorithm())
        return;

    const auto& TILED_ALGO = PWORKSPACE->m_space->algorithm()->tiledAlgo();
    auto*       SCROLLING  = dynamic_cast<CScrollingAlgorithm*>(TILED_ALGO.get());

    if (!SCROLLING)
        return;

    const auto SDATA = SCROLLING->scrollingData();
    if (!SDATA || SDATA->columns.empty())
        return;

    m_monitor    = PMONITOR;
    m_baseOffset = SDATA->controller->getOffset();
    m_active     = true;

    trackerReset();
}

void CUnifiedScrollingSwipeGesture::update(double delta, uint32_t timeMs) {
    if (!m_active)
        return;

    const auto PMONITOR = m_monitor.lock();
    if (!PMONITOR)
        return;

    const auto PWORKSPACE = PMONITOR->m_activeWorkspace;
    if (!PWORKSPACE || !PWORKSPACE->m_space || !PWORKSPACE->m_space->algorithm())
        return;

    const auto& TILED_ALGO = PWORKSPACE->m_space->algorithm()->tiledAlgo();
    auto*       SCROLLING  = dynamic_cast<CScrollingAlgorithm*>(TILED_ALGO.get());
    if (!SCROLLING)
        return;

    const auto SDATA = SCROLLING->scrollingData();
    if (!SDATA || SDATA->columns.empty())
        return;

    trackerPush(delta, timeMs);

    const CBox   USABLE        = SCROLLING->usableArea();
    const double usablePrimary = SDATA->controller->isPrimaryHorizontal() ? USABLE.w : USABLE.h;
    const double normFactor    = usablePrimary / WORKING_AREA_MOVEMENT;

    double newOffset = m_baseOffset - m_trackerPos * normFactor;

    SDATA->controller->setOffset(newOffset);
    SDATA->recalculate(true);

    g_pHyprRenderer->damageMonitor(PMONITOR);
}

void CUnifiedScrollingSwipeGesture::end() {
    if (!m_active)
        return;

    const auto PMONITOR = m_monitor.lock();
    if (!PMONITOR) {
        m_active = false;
        return;
    }

    const auto PWORKSPACE = PMONITOR->m_activeWorkspace;
    if (!PWORKSPACE || !PWORKSPACE->m_space || !PWORKSPACE->m_space->algorithm()) {
        m_active = false;
        return;
    }

    const auto& TILED_ALGO = PWORKSPACE->m_space->algorithm()->tiledAlgo();
    auto*       SCROLLING  = dynamic_cast<CScrollingAlgorithm*>(TILED_ALGO.get());
    if (!SCROLLING) {
        m_active = false;
        return;
    }

    const auto SDATA = SCROLLING->scrollingData();
    if (!SDATA || SDATA->columns.empty()) {
        m_active = false;
        return;
    }

    static const auto PFSONONE   = CConfigValue<Hyprlang::INT>("scrolling:fullscreen_on_one_column");
    static const auto PFITMETHOD = CConfigValue<Hyprlang::INT>("scrolling:focus_fit_method");
    const CBox        USABLE     = SCROLLING->usableArea();
    const double      usablePrimary = SDATA->controller->isPrimaryHorizontal() ? USABLE.w : USABLE.h;
    const double      normFactor    = usablePrimary / WORKING_AREA_MOVEMENT;

    const double projectedEnd  = trackerProjectedEndPos() * normFactor;
    const double maxExtent     = SDATA->controller->calculateMaxExtent(USABLE, *PFSONONE);
    const double minOffset     = 0.0;
    const double maxOffset     = std::max(0.0, maxExtent - usablePrimary);
    const double targetOffset  = std::clamp(m_baseOffset - projectedEnd, minOffset, maxOffset);

    struct SSnap {
        double offset;
        size_t colIdx;
    };

    std::vector<SSnap> snaps;

    for (size_t i = 0; i < SDATA->columns.size(); ++i) {
        const double stripStart = SDATA->controller->calculateStripStart(i, USABLE, *PFSONONE);
        const double stripSize  = SDATA->controller->calculateStripSize(i, USABLE, *PFSONONE);

        if (*PFITMETHOD == 1) {
            const double lo = stripStart - usablePrimary + stripSize;
            const double hi = stripStart;

            if (lo <= maxOffset)
                snaps.push_back({std::clamp(lo, minOffset, maxOffset), i});
            if (hi >= minOffset)
                snaps.push_back({std::clamp(hi, minOffset, maxOffset), i});
        } else {
            const double centered = stripStart - (usablePrimary - stripSize) / 2.0;
            snaps.push_back({std::clamp(centered, minOffset, maxOffset), i});
        }
    }

    if (snaps.empty()) {
        m_active = false;
        return;
    }

    size_t bestIdx  = 0;
    double bestDist = std::numeric_limits<double>::max();
    for (size_t i = 0; i < snaps.size(); ++i) {
        const double dist = std::abs(snaps[i].offset - targetOffset);
        if (dist < bestDist) {
            bestDist = dist;
            bestIdx  = i;
        }
    }

    const size_t targetColIdx  = snaps[bestIdx].colIdx;
    const double targetSnapOff = snaps[bestIdx].offset;

    m_active = false;

    SDATA->controller->setOffset(targetSnapOff);
    SDATA->recalculate(false);

    if (!SDATA->columns[targetColIdx]->targetDatas.empty()) {
        const auto TARGET = SDATA->columns[targetColIdx]->targetDatas.front()->target.lock();
        if (TARGET)
            SCROLLING->focusTargetUpdate(TARGET);
    }

    g_pInputManager->refocus();
    g_pHyprRenderer->damageMonitor(PMONITOR);
}
