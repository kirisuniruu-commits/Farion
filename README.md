# Farion

A distant-terrain (LOD) mod for [LeviLauncher](https://github.com/LiteLDev/LeviLaunchroid) on Android (arm64-v8a),
built on the [preloader-android](https://github.com/LiteLDev/preloader-android) SDK.

## Status

| Part | State |
| --- | --- |
| Mod loads, Mod Menu module with live settings, persisted config | Working |
| LOD core: tiles, downsampling, byte-budgeted LRU cache, CPU-throttled worker | Working, unit tested |
| Game render hooks (chunk load/unload, render distance) | **Off until you supply signatures** |

The renderer hooks live in `libminecraftpe.so`, and their byte signatures change with every Minecraft version.
They are read from `config/signatures.json`. While any signature is empty or does not match, the mod stays
completely inert: no hooks, no patches, no crash risk.

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

Tag `vX.Y.Z` to publish a release with the `.levipack` attached.
