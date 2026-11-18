# AMD FSR 4 for Hailstone on Linux

This add-on lets [Hailstone](https://github.com/projecthailstone/hailstone-client) use **AMD FSR 4** on Linux.
FSR 4 is AMD's newest upscaler: it draws the 3D world at a lower resolution and rebuilds it at full size, with
sharper edges and less flickering than older upscalers.

It is an **unofficial preview**. It is not made by the Hailstone team and not made by AMD. It has been tested in the
game on a Radeon RX 7900 GRE with CachyOS.

## What you need

- Hailstone on Linux, 64-bit, with upscaler add-on support. That is the 0.7.2-beta release or newer; a build from
  the `main` branch works too.
- An AMD graphics card that FSR 4 can run on: a Radeon RX 7000 or RX 9000 series, or a supported Ryzen APU.
  On RX 9000 it uses the faster FP8 model; on RX 7000 the INT8 model.
- A recent Mesa driver. Mesa 26.2 or newer is known to work.

On any other graphics card Hailstone simply keeps using its own upscaler.

## Install

1. Start Hailstone and open **Add-ons** on the login screen.
2. Paste this link and press **Look**:

   ```
   https://github.com/allhopeislost/hailstone-fsr4-linux
   ```

3. Press **Install**.
4. In the game, open **Options → Graphics** and set **Upscaler** to **AMD FSR 4**.
5. Restart Hailstone. The upscaler is chosen when the game starts.

## Remove it again

Open **Add-ons → My add-ons**, pick this add-on and press **Remove**. Then set **Upscaler** back to **AMD FSR 3.1**
or another choice, and restart Hailstone.

## Install by hand

If you prefer not to use the add-on manager, download the `fsr4-0.1.1-linux-x64.zip` file from the
[releases page](https://github.com/allhopeislost/hailstone-fsr4-linux/releases) and unpack it into a new folder
named `fsr4` inside the `upscalers` folder of your Hailstone data folder. Afterwards `upscalers/fsr4/fsr4.so` and
`upscalers/fsr4/amd_fidelityfx_upscaler_vk.so` must sit together.

## Build it yourself

The release page also has a source archive with everything needed to rebuild the add-on. Follow the instructions in
the runtime's own README for compilers and tools, then:

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

The result is in `adapter/build/dist/`. Keep the licence files from the release beside it if you pass it on.

## Licences

The small Hailstone part of this add-on is MIT, and comes from
[projecthailstone/hailstone-fsr4](https://github.com/projecthailstone/hailstone-fsr4). The FSR 4 runtime is built
from [fsr4-vk](https://github.com/Astyyyyy/fsr4-vk), which is GPL-3.0-only. Its AMD shaders and model weights come
from the DirectX 12 upscaler in AMD's FidelityFX SDK 2.3.0, which AMD lists under the MIT licence. All licence texts
are in the `licenses` folder of the download.
