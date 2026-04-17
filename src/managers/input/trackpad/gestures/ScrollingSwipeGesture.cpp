#include "ScrollingSwipeGesture.hpp"

#include "../../../../Compositor.hpp"
#include "../../../../desktop/state/FocusState.hpp"
#include "../../../../render/Renderer.hpp"

#include "../../UnifiedScrollingSwipeGesture.hpp"

void CScrollingSwipeGesture::begin(const ITrackpadGesture::STrackpadGestureBegin& e) {
    ITrackpadGesture::begin(e);

    if (g_pSessionLockManager->isSessionLocked() || g_pUnifiedScrollingSwipe->isGestureInProgress())
        return;

    g_pUnifiedScrollingSwipe->begin();
}

void CScrollingSwipeGesture::update(const ITrackpadGesture::STrackpadGestureUpdate& e) {
    if (!g_pUnifiedScrollingSwipe->isGestureInProgress())
        return;

    if (!e.swipe)
        return;

    static auto PSWIPEINVR = CConfigValue<Hyprlang::INT>("gestures:scrolling_swipe_invert");

    const double rawDelta = e.direction == TRACKPAD_GESTURE_DIR_LEFT || e.direction == TRACKPAD_GESTURE_DIR_RIGHT || e.direction == TRACKPAD_GESTURE_DIR_HORIZONTAL
        ? e.swipe->delta.x
        : e.swipe->delta.y;

    g_pUnifiedScrollingSwipe->update(*PSWIPEINVR ? -rawDelta : rawDelta, e.swipe->timeMs);
}

void CScrollingSwipeGesture::end(const ITrackpadGesture::STrackpadGestureEnd& e) {
    if (!g_pUnifiedScrollingSwipe->isGestureInProgress())
        return;

    g_pUnifiedScrollingSwipe->end();
}

bool CScrollingSwipeGesture::isDirectionSensitive() {
    return true;
}
