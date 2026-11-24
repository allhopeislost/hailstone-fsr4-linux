#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Prepare only the FSR 4.1.2 upscaler assets from the pinned AMD DX12 DLL."""

import argparse
import importlib.util
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SETUP = ROOT / "runtime" / "tools" / "setup.py"
UPSCALER_DLL = "amd_fidelityfx_upscaler_dx12.dll"


def load_runtime_setup():
    spec = importlib.util.spec_from_file_location("fsr4_runtime_setup", SETUP)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {SETUP}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dll", type=Path, help=f"path to {UPSCALER_DLL} or its directory")
    parser.add_argument("--assets", type=Path, default=ROOT / "runtime" / "assets")
    parser.add_argument("--translator", type=Path)
    parser.add_argument("--library", type=Path)
    parser.add_argument("--spirv-tools", type=Path)
    args = parser.parse_args()

    setup = load_runtime_setup()

    def upscaler_only_provider_dlls(directory):
        if directory is None:
            return {}
        path = Path(directory)
        if path.is_dir():
            path = path / UPSCALER_DLL
        if not path.is_file():
            raise ValueError(f"missing {UPSCALER_DLL}: {path}")
        sdk = setup.workflow.load("setup_sdk", setup.HERE / "extract" / "sdk.py")
        sdk.SDK(path)
        return {"upscaler": path}

    # The pinned runtime's setup.py always prepares Frame Generation as well.
    # This add-on does not ship it, so keep the upstream upscaler workflow and
    # replace only the provider-input and Frame Generation steps.
    setup.provider_dlls = upscaler_only_provider_dlls
    setup.prepare_framegeneration = lambda _args, _dlls, _pack: None
    setup.setup(args)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, RuntimeError, OSError, KeyError) as error:
        parser = argparse.ArgumentParser(description=__doc__)
        parser.error(str(error))
