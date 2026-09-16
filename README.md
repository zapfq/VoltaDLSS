# VoltaDLSS

### Volta Tensor Upscaler

**VoltaDLSS** is an experimental Windows-based real-time upscaling utility designed around the **NVIDIA Volta architecture**, with primary development and testing performed on the **Titan V 12 GB**.

The project explores whether Volta's Tensor Cores can be used for a practical, standalone game-agnostic upscaling pipeline outside of NVIDIA's official DLSS implementation.

> **Status:** Experimental / Active Development

---

## Overview

VoltaDLSS captures a selected application's rendered output, processes the image through a CUDA-based upscaling pipeline, and presents the resulting image through a separate fullscreen/borderless output window.

The project is primarily an exploration of:

* NVIDIA Volta Tensor Cores
* CUDA WMMA
* Direct3D 11
* CUDA / D3D11 interoperability
* Real-time image upscaling
* GPU scheduling and resource contention
* Virtual keyboard and mouse input
* Low-latency presentation

The goal is to create a lightweight utility that can sit between a game/application and the display without requiring modifications to the target application's renderer.

---

## Current Development

The current implementation includes a working end-to-end pipeline consisting of:

```text
Target Application
       │
       ▼
   D3D11 Capture
       │
       ▼
 CUDA / Tensor Core Upscaling
       │
       ▼
 D3D11 Output Texture
       │
       ▼
 VoltaDLSS Presentation Window
       │
       ▼
      Display
```

The project currently uses two CUDA upscaling paths:

* Reference EASU implementation
* Tensor Core accelerated EASU implementation

The Tensor Core path is implemented through CUDA's **WMMA API** and is intended specifically for Volta-class Tensor Cores.

---

## Tensor Core Implementation

The main Tensor Core implementation is located in:

```text
src/tensor_scaler.cu
src/easu_tensor.cu
```

The implementation uses:

```cpp
#include <cuda_fp16.h>
#include <mma.h>
using namespace nvcuda;
```

and performs matrix operations through:

```cpp
wmma::fragment
wmma::mma_sync
```

The upscaling workload is built around FP16 matrix operations suitable for NVIDIA Volta Tensor Cores.

### Verification

The generated CUDA binary has been inspected at the SASS level and confirmed to contain Volta Tensor Core instructions such as:

```text
HMMA.884.F16.F16.STEP0
HMMA.884.F16.F16.STEP1
```

This confirms that the Tensor Core implementation is not simply using conventional CUDA ALUs; the compiled kernel is issuing Volta HMMA instructions.

---

## EASU Tensor Pipeline

The Tensor Core EASU implementation divides the output image into blocks and processes the interpolation workload using Tensor Core matrix operations.

The current implementation:

* Processes the image in 16-pixel output blocks
* Uses FP16 intermediate data
* Uses WMMA 16×16×16 operations
* Processes RGB channels independently
* Uses CUDA shared memory for intermediate values
* Performs CUDA ↔ D3D11 texture interoperation

The current implementation is intentionally experimental and is primarily focused on proving that a practical image-scaling workload can be mapped onto Volta Tensor Cores.

---

## Quality Modes

VoltaDLSS currently exposes several internal rendering resolutions:

| Mode              | Approx. Internal Scale |
| ----------------- | ---------------------: |
| Ultra Performance |                  33.3% |
| Performance       |                  44.4% |
| Balanced          |                    50% |
| Quality           |                  66.7% |
| Native            |                   100% |

The final image is reconstructed to the selected output resolution.

---

## Launcher

The project includes a lightweight native Win32 launcher.

The launcher is intentionally simple and provides:

* Target window selection
* Manual executable selection
* Upscaling quality selection
* Keyboard/mouse remapper toggle
* Runtime launching
* Basic hardware status information

The current interface is designed around a high-contrast, minimal UI with a restrained Titan V / NVIDIA-inspired visual identity.

The launcher and runtime are separate executables:

```text
VoltaDLSS.exe
VoltaDLSSRuntime.exe
```

---

## Target Window Handling

VoltaDLSS currently uses a separate presentation window rather than injecting itself directly into the target game's renderer.

This allows the project to remain largely **game-agnostic** and avoids requiring per-game rendering integrations.

The target application is treated as the capture source while VoltaDLSS acts as the presentation layer.

---

## Input Architecture

The intended input architecture separates physical input from the presentation window.

Current design:

```text
Keyboard ──────► Virtual Input Layer ──────► Target Application
Mouse ─────────► Virtual Input Layer ──────► Target Application

Physical Controller ───────────────────────► Target Application
```

Keyboard and mouse are intended to be virtualized and forwarded to the target application.

Physical gamepad/controller input is intentionally left untouched because controller input already works correctly without virtualization.

The project currently uses **UCR** as part of the experimental keyboard/mouse remapping pipeline.

---

## Telemetry

VoltaDLSS includes an on-screen telemetry overlay showing:

* FPS
* Frame time
* Capture time
* Upscaling time
* Presentation time
* GPU utilization
* Estimated Tensor utilization
* GPU temperature
* VRAM usage

Example:

```text
FPS 60 | 16.7 ms | CAP 2.1 ms | UPS 3.2 ms | PRE 0.4 ms |
GPU 72% | TENSOR ~4% | 67 C | VRAM 5.1 / 12.0 GB
```

### Tensor Utilization

Direct CUPTI event collection was tested but was not available in the current runtime environment.

The overlay therefore uses a **workload-based estimate** rather than a direct hardware Tensor Core occupancy counter.

The current estimate is derived from:

* Number of Tensor Core matrix operations
* Output resolution
* Estimated workload
* Measured execution time
* Titan V theoretical Tensor throughput

Therefore:

```text
TENSOR ~4%
```

means approximately:

> Estimated Tensor compute throughput relative to theoretical Titan V Tensor Core peak.

It does **not** mean that the Tensor pipeline was physically occupied for exactly 4% of the frame.

The `~` symbol is intentionally used to make this distinction visible.

---

## Performance Measurement

The runtime currently separates major parts of the frame pipeline:

```text
Capture
   ↓
Upscale
   ↓
Present
```

This allows development testing to identify where frame time is being spent.

Hardware telemetry is sampled periodically while the overlay itself continues to redraw every frame. This prevents the displayed hardware metrics from visually flickering while still allowing the frame statistics to update continuously.

---

# Known Limitations

VoltaDLSS is currently experimental and several architectural limitations remain.

## 1. GPU Scheduling and Resource Contention

The game and VoltaDLSS are sharing the same physical GPU.

The GPU may therefore be handling several workloads simultaneously:

```text
Target Application
      +
D3D11 Capture
      +
CUDA Upscaling
      +
D3D11 Presentation
      +
Driver / Desktop Work
```

Because all of these workloads compete for GPU resources, Windows and the NVIDIA driver may schedule work differently depending on the application, workload and frame timing.

This can produce:

* Frame-time spikes
* Reduced game performance
* Uneven frame pacing
* Increased GPU utilization
* Performance changes when the upscaler workload changes

The issue is not necessarily caused by Tensor Cores themselves. It is primarily a consequence of having the game and an external capture/upscaling/presentation pipeline operating concurrently on the same GPU.

This is currently one of the main architectural limitations of the project.

---

## 2. FPS Drop When the Target Window Loses Focus

Some applications significantly change their rendering behaviour when they are no longer the foreground window.

Depending on the game, losing focus may cause:

* Reduced rendering frequency
* Lower FPS
* Reduced GPU utilization
* Background throttling
* Different frame pacing

Because VoltaDLSS uses a separate presentation window, this can become particularly noticeable.

The current architecture attempts to keep the VoltaDLSS presentation window active while the target application remains the capture source.

However, this does not completely eliminate application-specific background throttling.

As a result, performance measured while the target application is focused may differ significantly from performance when it is running in the background.

---

## 3. Keyboard and Mouse Input

Keyboard and mouse forwarding is still experimental.

The current architecture introduces an additional layer between physical input and the target application:

```text
Physical Input
      ↓
VoltaDLSS
      ↓
Virtual Input
      ↓
Target Application
```

This can create compatibility problems depending on how the target application receives input.

Known or possible issues include:

* Input not reaching the target application
* Focus-related input problems
* Mouse capture inconsistencies
* Different behaviour between fullscreen and borderless modes
* Virtual input being handled differently from physical input

Some games and applications may also restrict or behave differently with virtual input devices.

Input handling remains an active development area.

---

## 4. Audio When the Target Window Is Out of Focus

VoltaDLSS currently focuses on video capture, processing and presentation.

Audio is not currently routed through the VoltaDLSS presentation pipeline.

Some applications modify their audio behaviour when their window loses focus.

Depending on the target application, this can result in:

* Audio being reduced or muted
* Audio pausing while the window is backgrounded
* Different audio behaviour compared with normal fullscreen play

This is currently outside the main VoltaDLSS processing path and remains an unresolved limitation.

---

## 5. Capture and Presentation Overhead

The current architecture requires the image to pass through an external capture and presentation pipeline.

This can introduce additional overhead compared with rendering directly into the game's swapchain.

The project is therefore not intended to represent the absolute minimum possible latency of a native in-engine upscaler.

Future work may investigate reducing:

* Texture copies
* Synchronization
* GPU↔GPU transfers
* Presentation overhead
* Unnecessary intermediate buffers

---

## 6. Game Compatibility

VoltaDLSS is intentionally designed to be game-agnostic, but different applications expose different behaviours.

Compatibility may vary based on:

* Rendering API
* Window mode
* Background behaviour
* Input system
* Anti-cheat software
* Frame pacing implementation
* GPU workload
* Audio focus behaviour

A game that works correctly with VoltaDLSS does not necessarily guarantee compatibility with another game.

---

# Development Status

Current implementation status:

| Component                          | Status                 |
| ---------------------------------- | ---------------------- |
| D3D11 capture                      | Experimental / Working |
| CUDA upscaling                     | Working                |
| Tensor Core WMMA path              | Working                |
| Volta HMMA verification            | Verified               |
| EASU Tensor implementation         | Working                |
| D3D11 presentation                 | Working                |
| Win32 launcher                     | Working                |
| NVML telemetry                     | Working                |
| Tensor utilization telemetry       | Estimated              |
| Keyboard virtualization            | Experimental           |
| Mouse virtualization               | Experimental           |
| Controller passthrough             | Working / Untouched    |
| Audio routing                      | Not implemented        |
| Focus-independent game performance | Unresolved             |

---

# Development Philosophy

VoltaDLSS is being developed as an experimental platform rather than as a direct recreation of NVIDIA DLSS.

The project focuses on understanding what can realistically be achieved with the hardware that already exists in Volta GPUs.

The development approach is therefore:

```text
Measure
   ↓
Prototype
   ↓
Verify at GPU instruction level
   ↓
Integrate
   ↓
Benchmark
   ↓
Investigate limitations
   ↓
Iterate
```

The Tensor Core path is treated as an actual compute target rather than simply using Tensor Cores as a marketing feature.

---

# Hardware

Primary development target:

### NVIDIA Titan V

* Architecture: NVIDIA Volta
* Compute Capability: 7.0
* VRAM: 12 GB
* Tensor Cores: Volta Tensor Core architecture

The project may work on other compatible Volta GPUs, but development and testing currently focuses on the Titan V.

---

# Software Stack

Current development environment:

* Windows 10 22H2
* Visual Studio 2022 Community
* C++ / CUDA
* CUDA 12.9
* CMake
* Direct3D 11
* NVML
* UCR
* NVIDIA Volta GPU

---

# Project Structure

```text
VoltaDLSS/
│
├── CMakeLists.txt
├── Build-VoltaDLSS.ps1
├── Launch-VoltaDLSS.ps1
├── README.md
│
├── include/
│   ├── capture.h
│   ├── easu_reference.h
│   ├── easu_tensor.h
│   ├── overlay.h
│   ├── presenter.h
│   ├── quality.h
│   ├── runtime.h
│   ├── scaler.h
│   ├── telemetry.h
│   └── tensor_scaler.h
│
├── launcher/
│
├── src/
│   ├── adaptive_scaler.cu
│   ├── capture.cpp
│   ├── easu_reference.cu
│   ├── easu_tensor.cu
│   ├── main.cpp
│   ├── overlay.cpp
│   ├── presenter.cpp
│   ├── quality.cpp
│   ├── runtime.cpp
│   ├── runtime_main.cpp
│   ├── scaler.cu
│   ├── telemetry.cpp
│   ├── tensor_benchmark.cu
│   ├── tensor_scaler.cu
│   └── throughput_benchmark.cpp
│
└── third_party/
```

---

# Roadmap

Planned development areas include:

* Reduce GPU scheduling overhead
* Improve capture efficiency
* Improve frame pacing
* Reduce presentation latency
* Improve keyboard/mouse virtualization
* Improve focus handling
* Investigate audio handling
* Improve Tensor Core workload efficiency
* Improve Tensor Core telemetry
* Benchmark different Volta Tensor Core workloads
* Expand compatibility testing
* Improve packaging and release workflow

---

# Disclaimer

VoltaDLSS is an independent experimental project and is **not affiliated with, endorsed by, or supported by NVIDIA**.

This project is intended for experimentation and research into GPU image processing and Volta Tensor Core capabilities.

Performance, compatibility and behaviour may vary significantly between applications and system configurations.

---
