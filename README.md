# Hailstone FSR 4 for Linux

Unofficial native Linux upscaler add-on for [Hailstone](https://github.com/projecthailstone/hailstone-client).
Uses the FSR 4.1.1 INT8 model from [fsr4-vk](https://github.com/Astyyyyy/fsr4-vk), without Wine.

## Install

1. Use a Hailstone build with upscaler provider support.
2. Extract the release's Linux x86-64 archive into `upscalers/` in your Hailstone data directory. Both `.so` files must be in `upscalers/fsr4/`.
3. Choose **AMD FSR 4** in **Options → Graphics → Upscaler** and restart. Look for `FSR 4.1.1 Vulkan INT8` in the log.

This preview is for manual installation, not the Hailstone add-on store.
Tested in Hailstone's headless zone viewer on a Radeon RX 7900 GRE with CachyOS and RADV 26.2.6. Other distributions, older system libraries and live gameplay have not been verified.

## Build from the release source archive

Extract the `-source.tar.gz` release asset and run the following from its top-level directory. Follow the runtime's README for compiler and tool requirements.

```sh
cd fsr4-vk
python3 tools/setup.py --dll ../inputs/amd_fidelityfx_upscaler_dx12.dll
bash tools/build/linux.sh
cd ..
cmake -S adapter -B adapter/build \
  -DHAILSTONE_CLIENT_DIR="$PWD/inputs/hailstone" \
  -DFFX_DIR="$PWD/inputs/ffx" \
  -DFETCHCONTENT_SOURCE_DIR_VULKANHEADERS="$PWD/inputs/Vulkan-Headers" \
  -DFSR4_VK_RUNTIME="$PWD/fsr4-vk/build/linux/amd_fidelityfx_upscaler_vk.so"
cmake --build adapter/build
```

The adapter outputs two libraries in `adapter/build/dist/`. Keep the notices from the binary release alongside them when redistributing.
The runtime is pinned to `b0125ea9798633750f83ebabbc00279bcb49d8dd`.

## Licensing

The adapter derives from [projecthailstone/hailstone-fsr4](https://github.com/projecthailstone/hailstone-fsr4) and retains its MIT licence. The separately built `fsr4-vk` runtime is GPL-3.0-only. Its AMD shaders and weights come from the MIT-listed DirectX 12 upscaler DLL in FidelityFX SDK 2.3.0; see the runtime's third-party notices and licence files in both archives.
