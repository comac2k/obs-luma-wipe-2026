# Luma Wipe Transition for OBS (C)

> [!WARNING]
> **UNFINISHED PROJECT:** This project is currently under active development. It may not work, may not even build, and could cause OBS to crash. The codebase and features may change unexpectedly overnight. Use at your own risk.

## Why this exists?
This plugin is designed as a modern overhaul of the built-in OBS luma-wipe transition. The original implementation:
- Dates back to the early 2010s.
- Relies on low-resolution 480p images in 4:3 aspect ratio.
- Has not received significant updates in over a decade.

**Luma Wipe 2026** aims to bring this classic transition into the modern era with high-fidelity assets and advanced rendering features.

## Key Improvements
- **16-bit Accuracy:** Full support for 16-bit grayscale PNG masks to eliminate "stepping" or "banding" during slow transitions.
- **Native Motion Blur (WIP):** High-quality temporal smoothing for more dynamic wipes.
- **Modern Aspect Ratios:** Optimized for 16:9 HD workflows.
- **Advanced Fitting Algorithms (WIP):** Multiple ways to handle mask scaling (Stretch, Cover).
- **HD Asset Library (WIP):** A new set of high-definition 16:9 transition masks.

## Features
- **Mask-based timing:** Pixel value in the mask determines when the switch from Source A to Source B occurs.
- **High bit-depth support:** Preserves 16-bit accuracy for smooth transitions.
- **Softness control:** Adjustable edge softness using smoothstep.
- **Inversion:** Option to invert the mask behavior.
- **Native Performance:** Built as a C plugin for optimal integration with OBS Studio.

## Installation

### Building from Source
This plugin uses CMake and follows the standard OBS plugin template structure.

1.  **Clone the repository:**
    ```bash
    git clone https://github.com/comac2k/obs-luma-wipe-2026.git
    cd obs-luma-wipe-2026
    ```

2.  **Generate build files:**
    ```bash
    cmake -B build -S .
    ```

3.  **Build and Install:**
    ```bash
    cmake --build build --config Release
    ```
    *Note: You may need to specify your OBS installation path or copy the resulting binaries manually to your OBS plugins folder.*

## Usage
1.  Open OBS Studio.
2.  In the **Scene Transitions** dock (or the transition dropdown), click the `+` button to add a new transition.
3.  Select **Luma Wipe 2026**.
4.  In the transition properties, select a grayscale image as your **Luma Mask Image**.
    - Black (0%) = Transitions immediately.
    - White (100%) = Transitions at the very end.
    - 16-bit PNG images are recommended for the smoothest results.
5.  Adjust **Softness** and **Invert Mask** as needed.

## Technical Details
- **Plugin ID:** `luma_wipe_2026`
- **Logic:** Pixel value `V` (0.0 to 1.0) means the switch occurs at `T = V`.
- **Shader:** Uses `luma_wipe.effect` (HLSL) for GPU-accelerated rendering.
- **Transition Formula:** `lerp(SourceA, SourceB, smoothstep(V - softness, V, progress))`.
