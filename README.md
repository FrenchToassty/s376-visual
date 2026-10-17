# s376-visual

Open-source image generator for the CAE Boeing 737-300 full-flight simulator
(UAL ship S376). It replaces RSI Raster XT4: the host (an emulated VAX) sends
visual datagrams to UDP **2300**, and this process draws the out-the-window
scene. The xT4 reader lives **inside** the IG. There is no separate bridge.

v0.1 is the foundation: decoder, host socket, HAT reply, TOML channel config,
logging, diagnostics, a synth sender, and a single-window globe (OSG now,
osgEarth when you build it).

This repository is public. It does **not** contain RSI or CAE files, ICD text,
textures, databases, binaries, or packet captures.

## Requirements

| | Linux Mint 22 / Ubuntu 24.04 | Windows 10/11 |
|---|---|---|
| Compiler | g++ 13+ or clang++ 16+ | MSVC 2019+ or clang-cl |
| Build | CMake 3.16+ | CMake 3.16+ |
| Renderer | `libopenscenegraph-dev` (3.6.5) | OSG via vcpkg (optional) |
| Streamed tiles | osgEarth built from source (optional) | osgEarth via vcpkg (optional) |
| GPU | NVIDIA recommended; any GL 3.3 card runs the procedural globe | same |

## Linux Mint 22 install

```bash
sudo apt update
sudo apt install -y git cmake g++ libopenscenegraph-dev libgl-dev libglew-dev

git clone https://github.com/joeylawell/s376-visual.git
cd s376-visual
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Binaries land in `build/`: `s376-visual`, `s376-synth`, `s376-doctor`, `s376-tests`.

### Run without a VAX (demo)

```bash
./build/s376-visual --demo --channel front
```

A synthetic 737 takes off from KSEA 16L and the camera follows. `--headless`
runs the same path with no window (useful on SSH).

### Run against SIMH (emulated host)

On the IG PC, with the visual LAN isolated:

```bash
# production only — allows replies to 192.168.173.x
export S376_ALLOW_SIM_LAN=1
./build/s376-visual --config config/local.toml --channel front
```

`config/local.toml` (gitignored) should set `host.allow_sim_lan = true` and
keep `bind = "0.0.0.0:2300"`. The IG replies on UDP **2301**.

Default bind is `0.0.0.0:2300`. Tests and CI bind loopback and **never**
contact `10.1.95.0/24` or `192.168.173.0/24`.

### Two-process bench (loopback)

```bash
./build/s376-visual --bind 127.0.0.1:2300 --channel front
# other terminal
./build/s376-synth --dest 127.0.0.1:2300 --rate 20 --encoding le/vax
```

### Flags

| Flag | Meaning |
|---|---|
| `-c / --config PATH` | TOML file (see `config/s376-visual.toml`) |
| `--bind HOST:PORT` | override listen address |
| `--channel front\|left\|right` | which frustum to open |
| `--headless` | decoder + socket, no GL |
| `--demo` | in-process synth to loopback |
| `-v` / `--debug` | verbose / per-packet hex |
| `--diagnostics` | dump config and build options |
| `--doctor` | environment checks (local only) |

`s376-doctor` never sends a packet off the machine.

## Windows

1. Install [CMake](https://cmake.org/download/) and Visual Studio 2022 (C++).
2. Optional renderer: `vcpkg install openscenegraph:x64-windows` and pass
   `-DCMAKE_TOOLCHAIN_FILE=.../vcpkg.cmake`.
3. From a VS developer prompt:

```bat
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Without OSG the core, tests, synth, and `--headless` IG still build.

## osgEarth (streamed DEM + imagery)

Ubuntu 24.04 does not package osgEarth. After the OSG packages above:

```bash
sudo apt install -y libgdal-dev libgeos-dev libprotobuf-dev protobuf-compiler libtinyxml-dev
git clone --depth 1 https://github.com/gwaldron/osgearth.git
cmake -S osgearth -B osgearth/build -DCMAKE_BUILD_TYPE=Release
cmake --build osgearth/build -j"$(nproc)"
sudo cmake --install osgearth/build
# then re-configure s376-visual so it picks osgEarth up
```

`data/earth/s376.earth` points at Esri World Imagery for a first look.
Replace it with local NAIP / Sentinel-2 / Copernicus GLO-30 — see
[docs/SOURCES.md](docs/SOURCES.md).

## What is in the box

- Clean-room xT4 decoder (eyepoint, TOD, visibility, fog, light steps, lights)
- HAT / status reply the S376 host already knows how to read
- Channel TOML (Front 0°, Left +35.515°, Right −35.25°, 50° × 32.5°)
- Logging, `--debug`, `--doctor`, `--diagnostics`
- `s376-synth` kinematic takeoff from KSEA 16L
- Single-window globe, camera from the host, fog, sun from time of day

Not yet: airport meshes, light points, three windows, NLIM, landing-light
beams. The plan is in [docs/AIRPORTS.md](docs/AIRPORTS.md) and
[docs/DESIGN.md](docs/DESIGN.md). Protocol field layouts:
[docs/PROTOCOL.md](docs/PROTOCOL.md).

## License

MIT. See [LICENSE](LICENSE).
