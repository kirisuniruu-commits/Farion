#include "../src/lod/LodCache.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>

#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c);         \
      std::exit(1);                                                            \
    }                                                                          \
  } while (0)

static dh::Tile makeTile(int x, int z, uint16_t n, int16_t h, uint32_t c) {
  dh::Tile t({x, z, 0}, n);
  std::fill(t.height.begin(), t.height.end(), h);
  std::fill(t.color.begin(), t.color.end(), c);
  return t;
}

int main() {
  { // downsample halves size, averages values, bumps level
    dh::Tile t = makeTile(3, 4, 4, 0, 0);
    for (size_t i = 0; i < t.height.size(); ++i) {
      t.height[i] = (i % 2) ? 10 : 20;
      t.color[i] = (i % 2) ? 0xFF000000u : 0xFF0A0A0Au;
    }
    dh::Tile d = dh::downsample(t);
    CHECK(d.size == 2 && d.key.level == 1 && d.key.x == 3 && d.key.z == 4);
    CHECK(d.height[0] == 15);
    CHECK(d.color[0] == 0xFF050505u);
  }
  { // odd / tiny sizes do not read out of bounds
    dh::Tile one = makeTile(0, 0, 1, 7, 0xFF112233u);
    dh::Tile d = dh::downsample(one);
    CHECK(d.size == 1 && d.height[0] == 7 && d.color[0] == 0xFF112233u);
  }
  { // cache honours its byte budget and evicts least-recently-used
    const size_t one = makeTile(0, 0, 16, 1, 1).bytes();
    dh::LodCache cache(one * 3);
    for (int i = 0; i < 3; ++i) cache.put(makeTile(i, 0, 16, 1, 1));
    CHECK(cache.size() == 3);
    CHECK(cache.get({0, 0, 0}) != nullptr); // touch 0 -> 1 is now oldest
    cache.put(makeTile(9, 9, 16, 1, 1));
    CHECK(cache.size() == 3 && cache.bytes() <= one * 3);
    CHECK(cache.get({1, 0, 0}) == nullptr);
    CHECK(cache.get({0, 0, 0}) != nullptr);
    CHECK(cache.evictions() == 1);
    cache.setBudget(one);
    CHECK(cache.size() == 1 && cache.bytes() <= one);
  }
  { // replacing a key does not leak bytes
    dh::LodCache cache(1 << 20);
    cache.put(makeTile(1, 1, 8, 1, 1));
    const size_t b = cache.bytes();
    cache.put(makeTile(1, 1, 8, 2, 2));
    CHECK(cache.bytes() == b && cache.size() == 1);
    CHECK(cache.get({1, 1, 0})->height[0] == 2);
  }
  { // worker runs jobs, drops oldest when queue is full, stops cleanly
    std::atomic<int> ran{0};
    {
      dh::ThrottledWorker w(100, 4);
      for (int i = 0; i < 50; ++i) w.submit([&] { ++ran; });
      for (int i = 0; i < 200 && w.completed() + w.dropped() < 50; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      CHECK(w.completed() + w.dropped() == 50);
      CHECK(ran.load() == static_cast<int>(w.completed()));
    }
  }
  { // throttling: 20% budget means busy time is at most ~20% of wall time
    using clk = std::chrono::steady_clock;
    dh::ThrottledWorker w(20, 16);
    const auto t0 = clk::now();
    for (int i = 0; i < 5; ++i)
      w.submit([] { std::this_thread::sleep_for(std::chrono::milliseconds(20)); });
    while (w.completed() < 5) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto wall = std::chrono::duration<double, std::milli>(clk::now() - t0).count();
    CHECK(wall >= 5 * 20 * 4.0 * 0.8); // 100ms busy at 20% => >= ~400ms
  }
  std::puts("all LOD tests passed");
  return 0;
}
