# FastInventory

FastInventory is a standalone Minecraft Bedrock native mod designed to optimize inventory UI slot lookups and reduce repeated native inventory queries.

## Features

- Optimized inventory slot lookups
- Per-frame slot lookup caching
- Cache invalidation when the selected slot or inventory screen changes
- Container open/close handling
- Runtime AArch64 signature scanning
- Designed for ARM64 Android
- Standalone project with no BedrockTools source files included

## Version

**v1.0.0**

## Requirements

- Minecraft Bedrock
- Levi LaunchDroid / compatible native mod loader
- Android ARM64 (`arm64-v8a`)

## Build

The project includes a GitHub Actions workflow for building the ARM64 native library.

The workflow uses:

- Ubuntu 24.04
- Android NDK
- CMake
- ARM64 (`arm64-v8a`)

Push the project to GitHub and run the workflow from the **Actions** tab.

## Project Structure

```text
FastInventory/
├── src/
│   └── FastInventory.cpp
├── assets/
│   └── ...
├── CMakeLists.txt
├── config.yml
├── fastinventory.levipack
├── README.md
└── .github/
    └── workflows/
        └── build.yml
```

## How It Works

FastInventory locates the required Minecraft functions at runtime using byte-pattern signatures instead of relying on fixed addresses.

The inventory cache is cleared when the relevant UI state changes, helping prevent stale slot data from being reused.

## Compatibility

This project targets the Minecraft/Android build for which the included signatures were verified. Minecraft updates can change native code and therefore may require updated signatures.

## Safety

This project does not include BedrockTools source code, libraries, or modules. It uses only the necessary standalone implementation for the mod.

## Credits

Created by **xiomi**.
