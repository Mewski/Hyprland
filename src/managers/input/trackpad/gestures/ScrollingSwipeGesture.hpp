#pragma once

#include "ITrackpadGesture.hpp"

class CScrollingSwipeGesture : public ITrackpadGesture {
  public:
    CScrollingSwipeGesture()          = default;
    virtual ~CScrollingSwipeGesture() = default;

    virtual void begin(const ITrackpadGesture::STrackpadGestureBegin& e);
    virtual void update(const ITrackpadGesture::STrackpadGestureUpdate& e);
    virtual void end(const ITrackpadGesture::STrackpadGestureEnd& e);

    virtual bool isDirectionSensitive();
};
