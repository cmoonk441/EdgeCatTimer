// 최문경 — deterministic tests for timing and monitor geometry
#include "../src/core.hpp"
#include <cassert>
#include <iostream>
using namespace edgecat;
int main() {
  Countdown t;
  t.start(1500000, 1000);
  assert(t.elapsed(751000) == 750000);
  t.pause(751000);
  assert(t.elapsed(9999999) == 750000);
  t.resume(10000000);
  assert(!t.update(10749999));
  assert(t.update(10750000));
  assert(t.state == State::Finished && t.progress(99999999) == 1);
  assert(!t.update(99999999));
  t.reset();
  assert(t.state == State::Ready && t.elapsed(99999999) == 0);
  t.start(1000, 10);
  assert(t.update(2000));  // missed drawing ticks must not extend the timer
  t.start(1000, 3000);
  t.pause(4000);
  assert(t.state == State::Finished);
  t.resume(5000);
  assert(t.state == State::Finished);
  Rect r{-1920, -240, 1920, 1080};
  int s = 72;
  double h = r.height - s, w = r.width - s, p = 2 * (h + w);
  auto a = position(r, s, 0), b = position(r, s, h / p);
  auto c = position(r, s, (h + w) / p), d = position(r, s, (2*h+w) / p);
  auto e = position(r, s, 1);
  assert(a.x == -1920 && a.y == -240 && a.quarterTurns == 1);
  assert(b.x == -1920 && b.y == 768 && b.quarterTurns == 0);
  assert(c.x == -72 && c.y == 768 && c.quarterTurns == 3);
  assert(d.x == -72 && d.y == -240 && d.quarterTurns == 2);
  assert(e.x == a.x && e.y == a.y && e.quarterTurns == a.quarterTurns);
  for (int i = 0; i <= 10000; ++i) {
    auto pose = position(r, s, i / 10000.0);
    assert(pose.x >= r.x && pose.x + s <= r.x + r.width);
    assert(pose.y >= r.y && pose.y + s <= r.y + r.height);
    assert(pose.x == r.x || pose.x + s == r.x + r.width ||
           pose.y == r.y || pose.y + s == r.y + r.height);
  }
  auto narrow = position({0, 0, 72, 72}, 72, .7);
  assert(narrow.x == 0 && narrow.y == 0);
  std::cout << "PASS: elapsed time, pause/resume, delayed tick, finish-once, reset,\n"
               "      corner directions, complete lap, negative monitor coordinates, containment.\n";
}
