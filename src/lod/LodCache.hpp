// Host-testable LOD core: tiles, downsampling, byte-budgeted LRU cache and a
// CPU-throttled background worker. No Android / preloader dependencies.
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace dh {

struct TileKey {
  int32_t x = 0;
  int32_t z = 0;
  uint8_t level = 0;
  bool operator==(const TileKey &o) const {
    return x == o.x && z == o.z && level == o.level;
  }
};

struct TileKeyHash {
  size_t operator()(const TileKey &k) const noexcept {
    uint64_t h = static_cast<uint32_t>(k.x);
    h = h * 0x9E3779B97F4A7C15ull ^ static_cast<uint32_t>(k.z);
    h = h * 0x9E3779B97F4A7C15ull ^ k.level;
    return static_cast<size_t>(h ^ (h >> 29));
  }
};

// A square grid of column samples: top height and ARGB top color.
struct Tile {
  TileKey key;
  uint16_t size = 0;
  std::vector<int16_t> height;
  std::vector<uint32_t> color;

  Tile() = default;
  Tile(TileKey k, uint16_t n)
      : key(k), size(n), height(size_t(n) * n, 0), color(size_t(n) * n, 0) {}

  [[nodiscard]] size_t bytes() const {
    return sizeof(Tile) + height.size() * sizeof(int16_t) +
           color.size() * sizeof(uint32_t);
  }
};

// Halves resolution: each output sample averages a 2x2 block. The tile at
// level+1 covers the same area as the 2x2 neighbours merged by the caller; here
// we only reduce one tile's own resolution.
inline Tile downsample(const Tile &src) {
  const uint16_t n = static_cast<uint16_t>(std::max<int>(1, src.size / 2));
  Tile out(TileKey{src.key.x, src.key.z,
                   static_cast<uint8_t>(src.key.level + 1)},
           n);
  for (uint16_t z = 0; z < n; ++z) {
    for (uint16_t x = 0; x < n; ++x) {
      int hs = 0;
      int a = 0, r = 0, g = 0, b = 0;
      int count = 0;
      for (int dz = 0; dz < 2; ++dz) {
        for (int dx = 0; dx < 2; ++dx) {
          const size_t sx = std::min<size_t>(size_t(x) * 2 + dx, src.size - 1);
          const size_t sz = std::min<size_t>(size_t(z) * 2 + dz, src.size - 1);
          const size_t i = sz * src.size + sx;
          const uint32_t c = src.color[i];
          hs += src.height[i];
          a += (c >> 24) & 0xFF;
          r += (c >> 16) & 0xFF;
          g += (c >> 8) & 0xFF;
          b += c & 0xFF;
          ++count;
        }
      }
      const size_t o = size_t(z) * n + x;
      out.height[o] = static_cast<int16_t>(hs / count);
      out.color[o] = (uint32_t(a / count) << 24) | (uint32_t(r / count) << 16) |
                     (uint32_t(g / count) << 8) | uint32_t(b / count);
    }
  }
  return out;
}

// Thread-safe LRU cache bounded by bytes, never by tile count.
class LodCache {
public:
  explicit LodCache(size_t budgetBytes) : mBudget(budgetBytes) {}

  void setBudget(size_t budgetBytes) {
    std::lock_guard lock(mMutex);
    mBudget = budgetBytes;
    evictLocked();
  }

  void put(Tile tile) {
    auto ptr = std::make_shared<const Tile>(std::move(tile));
    std::lock_guard lock(mMutex);
    if (auto it = mMap.find(ptr->key); it != mMap.end()) {
      mBytes -= it->second.tile->bytes();
      mLru.erase(it->second.pos);
      mMap.erase(it);
    }
    mLru.push_front(ptr->key);
    mBytes += ptr->bytes();
    mMap.emplace(ptr->key, Entry{ptr, mLru.begin()});
    evictLocked();
  }

  [[nodiscard]] std::shared_ptr<const Tile> get(const TileKey &key) {
    std::lock_guard lock(mMutex);
    auto it = mMap.find(key);
    if (it == mMap.end()) return nullptr;
    mLru.splice(mLru.begin(), mLru, it->second.pos);
    return it->second.tile;
  }

  void clear() {
    std::lock_guard lock(mMutex);
    mMap.clear();
    mLru.clear();
    mBytes = 0;
  }

  [[nodiscard]] size_t bytes() const {
    std::lock_guard lock(mMutex);
    return mBytes;
  }
  [[nodiscard]] size_t size() const {
    std::lock_guard lock(mMutex);
    return mMap.size();
  }
  [[nodiscard]] uint64_t evictions() const {
    std::lock_guard lock(mMutex);
    return mEvictions;
  }

private:
  struct Entry {
    std::shared_ptr<const Tile> tile;
    std::list<TileKey>::iterator pos;
  };

  void evictLocked() {
    while (mBytes > mBudget && !mLru.empty()) {
      const TileKey victim = mLru.back();
      auto it = mMap.find(victim);
      mBytes -= it->second.tile->bytes();
      mMap.erase(it);
      mLru.pop_back();
      ++mEvictions;
    }
  }

  mutable std::mutex mMutex;
  size_t mBudget;
  size_t mBytes = 0;
  uint64_t mEvictions = 0;
  std::list<TileKey> mLru;
  std::unordered_map<TileKey, Entry, TileKeyHash> mMap;
};

// One low-priority background thread. After each job it sleeps long enough that
// the thread's duty cycle stays at or below `cpuPercent`, so LOD work can never
// starve the game's own threads. The queue is bounded: when full, the oldest
// job is dropped (stale terrain is worth less than fresh terrain).
class ThrottledWorker {
public:
  ThrottledWorker(int cpuPercent, size_t maxQueue)
      : mCpuPercent(std::clamp(cpuPercent, 1, 100)), mMaxQueue(maxQueue) {
    mThread = std::thread([this] { run(); });
  }
  ~ThrottledWorker() { stop(); }
  ThrottledWorker(const ThrottledWorker &) = delete;
  ThrottledWorker &operator=(const ThrottledWorker &) = delete;

  void setCpuPercent(int p) { mCpuPercent = std::clamp(p, 1, 100); }

  void submit(std::function<void()> job) {
    {
      std::lock_guard lock(mMutex);
      if (mStop) return;
      if (mQueue.size() >= mMaxQueue) {
        mQueue.pop_front();
        ++mDropped;
      }
      mQueue.push_back(std::move(job));
    }
    mCv.notify_one();
  }

  void stop() {
    {
      std::lock_guard lock(mMutex);
      if (mStop) return;
      mStop = true;
      mQueue.clear();
    }
    mCv.notify_all();
    if (mThread.joinable()) mThread.join();
  }

  [[nodiscard]] uint64_t dropped() const { return mDropped.load(); }
  [[nodiscard]] uint64_t completed() const { return mCompleted.load(); }

private:
  void run() {
#if defined(__linux__)
    setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 19);
#endif
    for (;;) {
      std::function<void()> job;
      {
        std::unique_lock lock(mMutex);
        mCv.wait(lock, [this] { return mStop || !mQueue.empty(); });
        if (mStop) return;
        job = std::move(mQueue.front());
        mQueue.pop_front();
      }
      const auto t0 = std::chrono::steady_clock::now();
      try {
        job();
      } catch (...) {
      }
      ++mCompleted;
      const auto busy = std::chrono::steady_clock::now() - t0;
      const int p = mCpuPercent.load();
      if (p < 100) {
        const auto rest = busy * (100 - p) / p;
        std::unique_lock lock(mMutex);
        mCv.wait_for(lock, rest, [this] { return mStop; });
      }
    }
  }

  std::atomic<int> mCpuPercent;
  size_t mMaxQueue;
  std::mutex mMutex;
  std::condition_variable mCv;
  std::deque<std::function<void()>> mQueue;
  bool mStop = false;
  std::atomic<uint64_t> mDropped{0};
  std::atomic<uint64_t> mCompleted{0};
  std::thread mThread;
};

} // namespace dh
