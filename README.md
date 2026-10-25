# Farion

A distant-terrain (LOD) mod for [LeviLauncher](https://github.com/LiteLDev/LeviLaunchroid) on Android (arm64-v8a),
built on the [preloader-android](https://github.com/LiteLDev/preloader-android) SDK.

## Status

| Part | State |
| --- | --- |
| Mod loads, Mod Menu module with live settings, persisted config | Working |
| LOD core: tiles, downsampling, byte-budgeted LRU cache, CPU-throttled worker | Working, unit tested |
| Game render hooks (chunk load/unload, render distance) | **Off until you supply signatures** |

The renderer hooks live in `libminecraftpe.so`. Checked against a 1.26.x arm64 build: the game's own C++ symbols are
stripped (only third-party libs such as v8/openssl are exported), so every hook needs a hand-derived byte signature that
changes with each Minecraft version. Signatures are read from `config/signatures.json`. While any signature is empty or does not match, the mod stays
completely inert: no hooks, no patches, no crash risk.

## Render distance unlock (new)

Its own module, **Render Distance Changer** (off by default), with its own on/off switch and a **Max Render Distance** slider (default 32). Switch it off to get the game's normal slider back after a restart. It raises the top of the in-game
Render Distance slider only. It does not change your current distance, so it adds no load until you move the slider
yourself. Higher distances load more full chunks and will lag weak phones; raise it slowly.

It finds the game's slider code by an instruction pattern and hooks it. If the pattern is not found (a different
Minecraft build), nothing is hooked. If the game crashes while the hook is installing, it turns itself off and writes
`data/rd_hook.disabled`; delete that file to try again. The technique was confirmed by studying the
RenderDistanceChanger mod by FRUTYDIE; this is an independent implementation.

## Why it does not lag

* Background work runs on one thread at the lowest OS priority.
* That thread's CPU share is capped (default 10% of one core, set in the menu).
* The job queue is bounded and drops stale jobs instead of piling up.
* The cache has a hard byte budget (default 128 MB) with LRU eviction.
* Nothing is allocated, and no thread exists, while the module is disabled.

## Install

Download `farion.levipack` from the latest Actions run or Release and import it in LeviLauncher.

## Build

CI builds everything on every push (`.github/workflows/build.yml`). Locally:

```bash
cmake -S . -B build/host -G Ninja && cmake --build build/host && ctest --test-dir build/host
```

Finding the hook addresses yourself: see [docs/GHIDRA_GUIDE.md](docs/GHIDRA_GUIDE.md).

Tag `vX.Y.Z` to publish a release with the `.levipack` attached.
