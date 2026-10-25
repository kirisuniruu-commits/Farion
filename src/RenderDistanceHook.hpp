#pragma once

#include <filesystem>

#include <pl/Mod.hpp>

namespace dhmod {

// Unlocks a higher maximum on the in-game Render Distance slider.
//
// It only extends the list of values the slider offers. It does not change the
// distance you are actually using: the game keeps drawing whatever you pick in
// Video settings, so this adds no load until you move the slider yourself.
//
// How it works: the game builds the slider through a number-option constructor
// that receives the list of allowed values. We find that call site by a short
// instruction pattern, hook the constructor, and pass a longer list.
// Technique confirmed by studying the "RenderDistanceChanger" mod by FRUTYDIE;
// this is an independent implementation.
//
// Safety: if the pattern does not match exactly this game build, nothing is
// hooked. If the game crashed while the hook was being installed last time, the
// hook switches itself off until data/rd_hook.disabled is deleted.
class RenderDistanceHook {
public:
  static RenderDistanceHook &instance();

  // Sets the slider maximum; <= 0 means "off" (the hook, if installed, just
  // passes the game's own values through). The hook is installed the first
  // time a value > 0 is set. Takes effect the next time the game builds the
  // slider, which is normally at launch.
  void setMax(ll::mod::NativeMod &mod, int maxDistance);
  void stop();
};

} // namespace dhmod
