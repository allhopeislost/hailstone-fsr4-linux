# Agent guidance

## Scope

- This repository is the Hailstone Linux upscaler add-on.
- `src/fsr4_upscaler.cpp` is the MIT Hailstone adapter.
- `runtime/` is a pinned Git submodule of `ebuwiz/fsr4-vk`; do not edit it in this repository.
- Package the upscaler only. Do not build or ship `amd_fidelityfx_framegeneration_vk`.
- Hailstone's own frame generation is separate and must keep working with the external upscaler.

## Workflow

1. Make a focused change in English.
2. Review only the changed code with a different, read-only reviewer before compiling.
3. Build with `tools/build.sh`; never bypass a NO GO review.
4. Report which checks ran and whether the add-on was tested in the game.

## Build inputs

- Hailstone `main`, through `HAILSTONE_CLIENT_DIR`.
- `git submodule update --init --recursive runtime`.
- AMD's pinned `amd_fidelityfx_upscaler_dx12.dll` for the first asset preparation.
- Generated assets live under `runtime/assets/` and are not committed.

## Licensing

Keep the adapter MIT, the runtime GPL-3.0-only, and copy every runtime licence and notice into `build/dist/licenses/`.
Never commit AMD DLLs, extracted model weights, shader packs or other generated assets.
