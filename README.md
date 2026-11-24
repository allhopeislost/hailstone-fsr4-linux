# AMD FSR 4 for Hailstone on Linux

This add-on lets [Hailstone](https://github.com/projecthailstone/hailstone-client) use **AMD FSR 4** on Linux.
FSR 4 draws the 3D world at a lower resolution and rebuilds it at full size, with sharper edges and less flickering
than older upscalers.

It is an **unofficial preview**. It is not made by the Hailstone team and not made by AMD.

## What you need

- Hailstone on Linux, 64-bit, with upscaler add-on support: 0.7.2-beta or newer, or a build from `main`.
- An AMD graphics card that FSR 4 can run on: Radeon RX 7000 or RX 9000, or a supported Ryzen APU.
- A recent Mesa driver.

On another vendor's GPU Hailstone keeps using its own upscaler.

## RX 7000 and RX 9000

| Card | Model | Why |
|---|---|---|
| Radeon RX 9000 (RDNA 4) | FP8 | The card has 8-bit floating point in hardware. |
| Radeon RX 7000 (RDNA 3) | INT8 | The card has no FP8 hardware, so it uses the integer model. |

The adapter prefers FP8 when the device reports the required features. If that model cannot be created, it tries
INT8. Both use the same FSR 4.1.2 assets and the same Hailstone frame inputs.

## Frame generation

This add-on contains the **upscaler only**. It does not contain AMD's separate FSR 4.0 frame-generation provider.

Hailstone's own frame generation still works with this upscaler. That is the frame generation you can enable in
Hailstone today. AMD FSR 4.0 frame generation is a different, newer neural feature and requires an RX 9000/RDNA 4
card; the pinned `ebuwiz/fsr4-vk` fork does not yet provide a complete runtime for it.

## Install

1. Start Hailstone and open **Add-ons** on the login screen.
2. Paste this link and press **Look**:

   ```
   https://github.com/allhopeislost/hailstone-fsr4-linux
   ```

3. Press **Install**.
4. In the game, open **Options → Graphics** and set **Upscaler** to **AMD FSR 4**.
5. Restart Hailstone. The upscaler is chosen when the game starts.

## If it will not install

If Hailstone says **"the kind is script, plugin or tool"**, your Hailstone is older than 0.7.2-beta and does not know
upscaler add-ons yet. Update Hailstone, or use a build from the `main` branch.

## Remove it again

Open **Add-ons → My add-ons**, pick this add-on and press **Remove**. Then set **Upscaler** back to **AMD FSR 3.1**
or another choice, and restart.

## Install by hand

Download the Linux x86-64 archive from the [releases page](https://github.com/allhopeislost/hailstone-fsr4-linux/releases)
and unpack it into a new folder named `fsr4` inside the `upscalers` folder of your Hailstone data folder. Afterwards
`upscalers/fsr4/fsr4.so` and `upscalers/fsr4/amd_fidelityfx_upscaler_vk.so` must sit together.

## Build it yourself

Clone with the pinned runtime and its build tools:

```sh
git clone --recurse-submodules https://github.com/allhopeislost/hailstone-fsr4-linux.git
cd hailstone-fsr4-linux
```

The asset preparation needs `amd_fidelityfx_upscaler_dx12.dll` from AMD's FidelityFX SDK 2.4.0. The Frame Generation
DLL is not needed. The tools verify the DLL's SHA-256 before extracting anything.

```sh
python3 tools/prepare_assets.py --dll /path/to/amd_fidelityfx_upscaler_dx12.dll
HAILSTONE_CLIENT_DIR=/path/to/hailstone-client bash tools/build.sh
```

`HAILSTONE_CLIENT_DIR` must contain `sdk/hailstone_upscaler.h`. Use Hailstone's `main` branch. The result is in
`build/dist/`; the build also runs the runtime's shader, embedded-asset and upscaler regression checks.

The runtime is pinned in the `runtime` Git submodule. Frame Generation targets and assets are deliberately excluded
from the default build and package.

## Licences

The Hailstone adapter is MIT and derives from
[projecthailstone/hailstone-fsr4](https://github.com/projecthailstone/hailstone-fsr4). The separately built runtime
comes from [ebuwiz/fsr4-vk](https://github.com/ebuwiz/fsr4-vk) and is GPL-3.0-only. Its AMD shaders and model weights
come from the DirectX 12 upscaler in FidelityFX SDK 2.4.0, which AMD lists under the MIT licence. All licence texts
and third-party notices are copied into the package's `licenses/` folder.
