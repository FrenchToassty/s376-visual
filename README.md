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

One command on the hangar box (apt, build, tests, synth KSEA cache, desktop launchers):

```bash
./scripts/setup-mint.sh
```

Or by hand:

```bash
sudo apt update
sudo apt install -y git cmake g++ libopenscenegraph-dev libgl-dev libglew-dev libgdal-dev

git clone https://github.com/joeylawell/s376-visual.git
cd s376-visual
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

The renderer stays **GL 3.3-compatible** (apt OSG 3.6 compatibility profile) so the same binary runs on the hangar RTX 5060 Ti and a Maxwell GTX 980 Ti. Do not force a Core profile.

osgEarth (optional, not in Ubuntu 24.04 apt):

```bash
./scripts/build-osgearth.sh          # installs to /opt/osgearth and rebuilds
```

Use **g++**, not the default clang: Ubuntu 24.04 / Mint 22 clang often lacks a
matching `libstdc++` and the link fails with `cannot find -lstdc++`.

Binaries land in `build/`: `s376-visual`, `s376-synth`, `s376-doctor`,
`s376-cache`, `s376-tests`.

### Run without a VAX (demo)

```bash
./build/s376-visual --demo --channel front
```

A synthetic 737 takes off from KSEA 16L. Default is three channel windows
(Front / Left +35.515° / Right −35.25°), tiled on a single display or
placed on `monitor` 0/1/2. `--channel front` is the laptop single-window
fallback. `--headless` runs the same path with no GL.

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
| `--channel front\|left\|right\|all` | frustum(s) to open (default: all enabled, tiled on one display) |
| `--cache-dir PATH` | packed DEM/imagery from `s376-cache` |
| `--headless` | decoder + socket, no GL |
| `--demo` | in-process synth to loopback |
| `-v` / `--debug` | verbose / per-packet hex |
| `--diagnostics` | dump config and build options |
| `--doctor` | environment checks (local only) |

`s376-doctor` never sends a packet off the machine. It reports on-disk
`cache/<icao>` size for KSEA / KSFO / KLAS and a per-channel frame-time
bench (CPU view cost always; GPU ms/frame when `DISPLAY` is set).

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

## Local DEM + imagery (no osgEarth required)

The IG drapes a local height field even when osgEarth is absent. Pack a
region once, offline:

```bash
# synthetic 25 km Sea-Tac box (no download) — CI / laptops
./build/s376-cache synth-ksea --out cache/ksea --lods

# real, freely licensed data (hangar box with a network)
./build/s376-cache fetch KSEA --radius-km 25 --out cache/ksea
./build/s376-cache fetch KSFO --out cache/ksfo
# preview URLs only:
./build/s376-cache fetch KSEA --dry-run

./build/s376-visual --demo --cache-dir cache/ksea
```

Output is `elev.s376dem` (portable grid) + `imagery.dds` (BC1/DXT1 GPU
texture) + `region.toml`. A 1° GLO-30 clip is tens of MiB. RSI CERTS
for a comparable area was ~857 GB uncompressed; we do not convert those
tiles. KTX2/Basis can replace the DDS later; the drape slot is the same.

`from-geotiff` needs GDAL (`libgdal-dev`). Without GDAL the portable
`.s376dem` path and `synth-ksea` still work.

## osgEarth from source (Linux Mint 22 / Ubuntu 24.04)

Ubuntu 24.04 **does not** package osgEarth. The every-PR CI job therefore
builds the OSG globe + local DEM only. An optional
[osgEarth workflow](.github/workflows/osgearth.yml) (`workflow_dispatch`)
compiles osgEarth 3.7.2 from source — about 20–40 minutes, not practical
on every push.

On the hangar box, after the OSG + GDAL packages above:

```bash
sudo apt install -y libgeos-dev libproj-dev libprotobuf-dev protobuf-compiler \
    libtinyxml-dev libcurl4-openssl-dev libsqlite3-dev

git clone --depth 1 --branch osgearth-3.7.2 https://github.com/gwaldron/osgearth.git
cmake -S osgearth -B osgearth/build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=g++ \
    -DOSGEARTH_BUILD_TOOLS=OFF \
    -DOSGEARTH_BUILD_EXAMPLES=OFF \
    -DOSGEARTH_BUILD_IMGUI_NODEKIT=OFF \
    -DOSGEARTH_ENABLE_FASTDXT=OFF
cmake --build osgearth/build -j"$(nproc)"
sudo cmake --install osgearth/build
sudo ldconfig

# re-configure s376-visual so it defines S376_HAS_OSGEARTH
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build -j"$(nproc)"
```

`s376-doctor` WARNs when OSG is present but osgEarth is not. After a
source install it should PASS the osgearth check.

`data/earth/s376.earth` points at Esri World Imagery for a first look.
Replace the image/elevation layers with local NAIP / Sentinel-2 /
Copernicus GLO-30 — see [docs/SOURCES.md](docs/SOURCES.md).

Apt OSG 3.6.5 is enough for osgEarth 3.7.x. Building OSG from source
with `-DOPENGL_PROFILE=GL3` is only needed if you want a Core profile;
the hangar NVIDIA box runs the compatibility profile that apt OSG uses.

## What is in the box

- Clean-room xT4 decoder (eyepoint, TOD, visibility, fog, light steps, lights)
- HAT / HOT reply sampled from the local DEM (host-compatible 64-byte BE IEEE)
- Channel TOML (Front 0°, Left +35.515°, Right −35.25°, 50° × 32.5°)
- Three CompositeViewer windows, or `--channel front` on a laptop
- Logging, `--debug`, `--doctor`, `--diagnostics`
- `s376-synth` kinematic takeoff from KSEA 16L
- Globe + local DEM drape (BC1), camera from the host, fog, sun from TOD
- `s376-cache` offline tile packer (synth, fetch, or GDAL GeoTIFF)
- `s376-cache osm ICAO` Overpass taxi/apron pull (ODbL, stays in `cache/`)

KSEA, KSFO, and KLAS runways (OurAirports) plus edge / centreline /
approach light points are in the scene; intensity follows packet-112
steps 0–5. Taxi/apron graphs start schematic; fetch OSM when needed.

Not yet: NLIM, landing-light beams, remaining airports. The plan is in
[docs/AIRPORTS.md](docs/AIRPORTS.md) and [docs/DESIGN.md](docs/DESIGN.md).
Protocol field layouts: [docs/PROTOCOL.md](docs/PROTOCOL.md).

## License

MIT. See [LICENSE](LICENSE).
