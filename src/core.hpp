
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace edgecat {
enum class State { Ready, Running, Paused, Finished };

// The caller supplies a monotonic clock in milliseconds. No tick counting.
class Countdown {
 public:
  State state = State::Ready;
  uint64_t duration = 25 * 60 * 1000;
  uint64_t banked = 0;
  uint64_t since = 0;
  void start(uint64_t milliseconds, uint64_t now) {
    duration = std::max<uint64_t>(1, milliseconds);
    banked = 0;
    since = now;
    state = State::Running;
  }
  uint64_t elapsed(uint64_t now) const {
    uint64_t extra = state == State::Running && now >= since ? now - since : 0;
    return std::min(duration, banked + extra);
  }
  double progress(uint64_t now) const { return double(elapsed(now)) / double(duration); }
  uint64_t remaining(uint64_t now) const { return duration - elapsed(now); }
  bool update(uint64_t now) {
    if (state == State::Running && elapsed(now) >= duration) {
      banked = duration;
      state = State::Finished;
      return true;
    }
    return false;
  }
  void pause(uint64_t now) {
    if (state != State::Running) return;
    banked = elapsed(now);
    state = banked == duration ? State::Finished : State::Paused;
  }
  void resume(uint64_t now) {
    if (state != State::Paused) return;
    since = now;
    state = State::Running;
  }
  void reset() { banked = 0; state = State::Ready; }
};

struct Rect { int x, y, width, height; };
struct Pose { int x, y, quarterTurns; };

// A square sprite stays entirely inside one monitor. Its bottom (feet) rotates
// to the active edge: left/down -> bottom/right -> right/up -> top/left.
inline Pose position(Rect r, int sprite, double progress) {
  int w = std::max(0, r.width - sprite);
  int h = std::max(0, r.height - sprite);
  double perimeter = 2.0 * (w + h);
  if (perimeter <= 0 || progress <= 0 || progress >= 1) return {r.x, r.y, 1};
  double distance = progress * perimeter;
  if (distance < h) return {r.x, r.y + int(std::lround(distance)), 1};
  distance -= h;
  if (distance < w) return {r.x + int(std::lround(distance)), r.y + h, 0};
  distance -= w;
  if (distance < h) return {r.x + w, r.y + h - int(std::lround(distance)), 3};
  distance -= h;
  return {r.x + w - int(std::lround(distance)), r.y, 2};
}
}  // namespace edgecat
