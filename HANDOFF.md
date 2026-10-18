# HANDOFF

A coworker (human or agent) can pick up from here. Cursor usage on this
account may stop mid-run — treat this file as the source of truth.

**PR:** https://github.com/joeylawell/s376-visual/pull/1
**Branch:** `cursor/s376-visual-v1-6852`

## Milestone: image quality (2026-10-18)

Joey's phone photo of Raster on 4R (day, two collimated channels) is
**reference only** — gitignored under `refs/` / `raster-*.jpg`. Do not
commit it. Goal: match Raster's crisp markings / blue sky / horizon,
beat the tiled grass, repeating asphalt, painted-dome clouds, and
blocky terminals, stay at 60 Hz × 3 on a GTX 980 Ti.

- **Markings:** FAA AC 150/5340-1 from OurAirports geometry — threshold
  bars (8/12), 5×7 numbers/L/R/C, aiming points, TDZ, 36 m centerline,
  0.9 m edges, yellow hold-short pair. Geometry, not a texture, so they
  stay sharp at grazing angles. Aniso 16 on drapes. Rubber strip in the
  first 900 m.
- **Ground:** NAIP/S2 drape × land-class noise (grass/dirt) that
  tightens up close. `scripts/fetch-textures.sh` pulls CC0 Poly Haven
  1K JPGs into `cache/textures/` (never committed). Shader hash is the
  offline fallback. Allowlist: ambientcg.com, polyhaven.com/.org.
- **Buildings:** schematic terminals plus OSM `building=*` extrude
  (height / building:levels), cap 280, walls+roof mats. `s376-cache
  osm` Overpass query now includes `way["building"]`.
- **Sky:** analytic Preetham-like dome (40 km, bin −1) using local up
  (not ECEF Z), sun disc, moon + stars at night, EXP2 aerial
  perspective. Clouds keep hash-soft edges + sun wrap.
- **Lighting:** ACES in every forward shader. `render.quality`
  `gtx980ti` (default: hdr/atmosphere/markings/detail/buildings on,
  ssao+shadows off) vs `rtx` (ssao+shadows on). SSAO is cheap wall
  darkening. Shadows are a toggle only — no CSM yet (980 Ti budget).
- **Shot:** `s376-visual --screenshot PATH --at ICAO:RWY[:dist_nm]`
  (`KJFK:04R:0` lined-up looking at the numbers, `:3` = 3 nm / 3°).
  Raster FOV still 50×32.5; front 0 / left +35.515 / right −35.25.
- **KBOS** added (04R ALSF-2, 10006×150, hdg 35°) so we can tell
  whether Joey's photo is JFK or Logan. `all_public_airports()` is 6.

Hangar check (real GPU):

```bash
./build/s376-visual --screenshot /tmp/jfk-04r.ppm --at KJFK:04R:0 --channel front
./build/s376-visual --screenshot /tmp/bos-04r.ppm --at KBOS:04R:0 --channel front
./build/s376-visual --screenshot /tmp/jfk-04r-3nm.ppm --at KJFK:04R:3 --channel front
```

This agent Xvfb often fails `GLXBadFBConfig`; the hangar box is the
visual check.

## Milestone: Overpass glob + high-res fetch (2026-10-18)

Joey's first real `s376-cache osm KSEA` died with
`curl: (3) bad range in URL position 47` — curl globbed `[out:json]`.
GET URLs now percent-encode `[ ] { } "`, every curl line uses `-g`,
and `osm` POSTs the QL (`--data-urlencode data@file`).
`curl_glob_safe` / `http_get_command` are unit-tested.

Fetch no longer pulls one 384×384 DEM + 768×768 NAIP (~20 m/px).
ImageServer exports are tiled at 4000 px with resume (skip existing)
and `--retry 5`. Default pyramid:

- lod0: 5 km ARP + 10 nm finals, 1 m imagery / 2 m DEM, BC7 tiles
- lod1: 20 km, 4 m / 8 m, BC1
- lod2: outer radius, 20 m / 30 m, BC1 (GLO-30 / S2 outside CONUS)

`--dry-run` prints tile counts and a packed-size estimate. KSEA
defaults: **43 img + 17 DEM tiles, raw~2.0 GiB, packed~607 MiB**.
`--near-m` / `--near-dem-m` / `--mid-m` / `--far-m` / `--near-km` /
`--approach-nm` / `--max-export` override the targets. The renderer
drapes `lodN/tY_X` tiles. `s376-doctor` reports tile counts.
Resume + BC7 keep the 980 Ti (6 GB) path streamable; drop `--near-m`
to 2 if VRAM is tight.

## Milestone: GL compat + SIGTERM/Esc (2026-10-18)

Hangar box (Mint 22.3, RTX 5060 Ti, NVIDIA 595.99 open, apt OSG 3.6.5)
at 88992c4 flooded ~27k `detected OpenGL error` lines in 60 s and
ignored SIGTERM (`timeout 8` needed `kill -9`).

- Linux no longer calls `setGLContextVersion`. NVIDIA then gives a 4.6
  **compatibility** (legacy) context. `setGLContextVersion("3.3")` with
  no profile mask was CORE on this driver; apt OSG's FF StateSets are
  illegal there. Non-Linux still requests 3.3 + mask `0x2`
  (`COMPATIBILITY_PROFILE_BIT`).
- Scene lighting / fog / points are GLSL 120. Material, LightModel,
  LIGHTING, LIGHT0, FOG, ALPHA_TEST, POINT_SMOOTH, PointSprite, and
  TEXTURE_2D modes are stripped from our StateSets and from OSG's
  SceneView global defaults so a CORE context stays quiet.
- OSG `ONCE_PER_ATTRIBUTE` GL error checks are off unless `--gl-debug`
  / `render.gl_debug`. CI greps the headless and Xvfb logs for
  `detected OpenGL error`.
- SIGINT/SIGTERM set `g_stop`, the viewer `setDone`s, and
  `run_synth_sender` breaks on that flag (the old hang was the synth
  thread joining a 3600 s `--demo` with no stop). Esc still sets done.
  CI: `timeout -k 2 3` must not reach SIGKILL.

## Verified in this run

- `s376-tests`: **10725 passed, 0 failed** (KBOS 04R ALSF-2, shot pose
  `KJFK:04R:0` / `:3`, FAA marking vert delta, OSM `building=*` merge,
  quality presets `gtx980ti`/`rtx`, Poly Haven allowlist, plus previous
  suite).
- `--headless --at KJFK:04R:0`: pose 40.6253,-73.7762, 26 ft, hdg 31°,
  pitch 0. `--at KBOS:04R:3 --quality rtx`: 3 nm final, 987 ft, pitch −3°.
- `s376-doctor`: 0 fail, 1 warn (osgEarth). Now lists `cache-kbos`.
  mesh-build **1.58 ms / 26250 verts / 8728 lights** (six fields +
  markings + schematic terminals). This agent's Xvfb still cannot
  realize a GL window (`GLXBadFBConfig`); hangar GPU is the visual
  check (`--screenshot /tmp/jfk-04r.ppm --at KJFK:04R:0`).
- `timeout -k 2 3 --headless --demo --duration 3600`: exit **124**
  (SIGTERM honored; stopped after 178 frames). Previously hung 60+ s.
- `s376-doctor`: 0 fail, 1 warn (osgEarth). Reports `cache-ksea` 4.9 KiB,
  SKIP for unpacked KSFO/KLAS, `mesh-build` 0.17 ms / 4096 lights,
  `frame-{front,left,right}` CPU view + GPU share when DISPLAY works.
- `s376-visual --headless --demo --duration 2`: 40 datagrams / 40 eyepoints,
  KSEA 16L, env TOD 14.5 / vis 60760 ft.
- `s376-visual --headless --demo-weather --duration 2`: 40 datagrams,
  env precip=1/6 wind=18@200 lightning=1 vis=4000, fog select=0
  RVR 2400 / height 400, clouds lower 2500–6000 / upper 12000–16000 /
  top on, storm0 CSN 28 at 47.42,-122.308 precip 1/7 lightning 2.
- This agent Xvfb cannot realize a GL window (`GLXBadFBConfig`);
  cloud/storm draw path compiled and is unit-tested. Hangar box has
  the real GPU.
- `s376-visual --demo` under Xvfb: OSG window "front" 1920×1080, 50×32.5 FOV,
  procedural globe, camera on the eyepoint.
- `s376-synth --dest 10.1.95.1:2300` and `192.168.173.10:2300`: refused.
- `s376-cache synth-ksea --out /tmp/s376-ksea --size 64`: **18 608 bytes**
  (BC1 + `.s376dem`); `info` samples KSEA ARP at 433 ft; `--out 10.1.95.1:2300`
  refused.
- GDAL 3.8.4 PASSes.
- GitHub Actions `ci / linux` on `d3f6dfc` (HAT + KSEA + cache): **green**
  (ubuntu-24.04, 1984 tests, doctor, headless demo, sim-LAN refusal,
  `s376-cache` smoke). Optional osgEarth job is `workflow_dispatch` only.

## Standing rule: Raster XT4 parity

Match the original RSI Raster XT4 on this 737-300: same host fields,
same visual features, same HAT replies. Allowed deltas: lighter GPU,
sharper image (MSAA / AF / crisp points), cleaner code. Do not invent
scenery or weather the host does not send. Checklist:
[docs/PARITY.md](docs/PARITY.md). Gaps stay listed there until closed.

## Milestone: 4-monitor desk + sharpness (2026-10-18)

`config/desk-4monitor.toml` pins IOS at x=0 (DP-4), left 1920 (DP-3),
front 3840 (HDMI-0), right 5760 (DP-0). Same ViewAngles as Raster.
`render.msaa=4` and `render.aniso=16` (GL 3.3 / Maxwell). Type-124
`tod_select` 0–3 now snaps sun/sky to day/dusk/dawn/night.

## Milestone: type-124 / 128 weather (2026-10-18)

Honor the **already-decoded** Raster weather fields. Do not invent
cloud or storm-cell layouts (`nxHostPacket.h` is gitignored and is
not on this agent box).

- Type 124 `precip_type` 1/2/3 + `precip_intensity` 0–10 → 360 GPU
  points around the eyepoint (rain / snow / hail), GL 3.3.
- Type 124 wind speed/dir → precip drift (FROM convention).
- Type 124 lightning trigger (words 10–11) → 4-frame sun/sky flash
  on edge change.
- Type 128 RVR still caps vis; fog height thickens EXP2 when last
  HAT is below the deck + 200 ft.
- `--demo-weather` / `s376-synth --weather` emit those packets only.

## Milestone: cloud layers + storm cells (2026-10-18)

Clean-room types **105 / 125 / 126 / 127** from the hangar reference
header (read only, **never committed**). Field tables in
[docs/PROTOCOL.md](docs/PROTOCOL.md).

- Lower/upper: ceiling + top MSL, select −1=off, vis-above, layer wind.
  Cheap GL 3.3 stacked discs + 8 cards around the eyepoint (GTX 980 Ti
  budget: a few hundred tris × 3 channels).
- Top layer: region+select only; 25–28 kft stand-in (original heights
  lived in the IG catalog).
- Fog × 2.4 when the eye is inside a deck.
- Storm select 0–29, ≤3 active, CSN 28–30 pose from type 100.
  Lightning 1=random, >1=edge. Size is not on the wire — 1.5 nm ×
  intensity stand-in (PARITY partial).
- `--demo-weather` now sends IMC decks + one cell south of KSEA.

See [docs/PARITY.md](docs/PARITY.md).

## Milestone: KLAX / KJFK layouts (2026-10-18)

Same generator as KSEA: OurAirports runway ends, FAA-charted ALS
(ALSF-2 on KLAX 24R/24L/25R/25L and KJFK 04L/22R/13L/13R/31L), schematic
taxi/apron, GPU lights on packet-112 switches 1–4. `data/airports/{klax,kjfk}.toml`
holds the public numbers. `all_public_airports()` later grew to six
with KBOS (image-quality round).

## Milestone: KSEA earth file (2026-10-18)

`data/earth/ksea.earth` is the osgEarth path: GDAL elevation and imagery
from `s376-cache` (`cache/ksea/elev.tif`, `imagery.tif`) plus an OGR
`feature_geom` layer on `osm.geojson`. `write_cache` now emits those
GeoTIFF sidecars when GDAL is present. If osgEarth is missing or the
earth file fails to load, the IG uses the procedural globe + local DEM
drape. `data/earth/s376.earth` stays the Esri-dev globe. GL context
request is 3.3 compatibility (not Core).

## Milestone: Mint setup + osgEarth scripts (2026-10-18)

`scripts/setup-mint.sh` installs apt deps, builds with g++, runs tests,
packs a synth KSEA cache, and writes `s376-visual-demo` /
`s376-visual-3ch` desktop launchers. `scripts/build-osgearth.sh` builds
osgEarth 3.7.2 into `/opt/osgearth` (no FASTDXT) and re-cmakes the IG
so `S376_HAS_OSGEARTH` turns on. Renderer stays GL 3.3-compatible
(Maxwell GTX 980 Ti + hangar RTX 5060 Ti). `s376-cache geojson ICAO`
writes a FeatureCollection for the earth-file airport layer.

## Milestone: doctor size / perf (2026-10-18)

`s376-doctor` lists on-disk `cache/<icao>` bytes for each public
airport (and any extra packed dir) plus `frame-<channel>` times.
CPU `make_channel_view` cost is always measured. GPU CompositeViewer
ms/frame (tiny 160×90 windows, share of the composite) runs only when
`XOpenDisplay` succeeds so CI stays headless. `--no-bench` skips the
timing section. `mesh-build` is reported separately.

## Milestone: KSFO / KLAS layouts (2026-10-18)

`ksfo_public()` and `klas_public()` match the KSEA generator: OurAirports
runway ends, FAA-charted ALS (ALSF-2 on 28R / 26R / 26L), schematic
taxi/apron offsets, GPU lights on switches 1–4. The renderer loads all
three fields. `s376-cache osm ICAO` pulls Overpass `aeroway=*` (ODbL)
into `cache/<ICAO>/osm.json` (gitignored). `data/airports/{ksfo,klas}.toml`
holds the public numbers. Unit tests cover four runways each, ALS types,
OSM merge, and mesh fans.

## Milestone: real GLO-30 / 3DEP + NAIP fetch (2026-10-18)

`s376-cache fetch ICAO` downloads USGS 3DEP (CONUS) or Copernicus GLO-30
and NAIP / Sentinel-2 cloudless, then packs a LOD pyramid (BC7 + mips
near the field, BC1 farther out). Verified live: 8 km KSEA 3DEP+NAIP,
ARP sample 391 ft MSL, packed tiles ~1.4 MiB after dropping raw COGs.
`--dry-run` prints allow-listed URLs. Licences in `docs/SOURCES.md`.
Downloaded rasters stay under `cache/` (gitignored). Synth `--lods`
still works offline for CI.

## Milestone: real HAT / HOT (2026-10-18)

`HostReceiver` samples `DemHeightSource` at the eyepoint. On the synth
KSEA pad, elev ≈ 433 ft and HAT ≈ 14 ft (eye height). Off the DEM the
old stand-in (elev=0, hat=alt) is still sent so the host is never
starved. Live loopback test builds a type-113 query and reads the
64-byte type-200 reply.

## Milestone: KSEA airport + GPU lights (2026-10-18)

`s376::ksea_public()` builds three OurAirports runways (16L threshold
matches the synth takeoff) plus schematic taxi connectors. Edge,
centreline, threshold, and ALSF-2/MALSR approach points are placed from
published FAA/ICAO spacing. A GPU `GL_POINTS` pass scales colour/alpha
by packet-112 steps 0–5 on switches 1–4 (ours, not RSI's). Synth sends
those switches at step 5 so the demo is lit.

## Milestone: local DEM + cache (2026-10-18)

Portable `.s376dem` + BC1/DXT1 DDS + `s376-cache`. GDAL GeoTIFF in when
`libgdal-dev` is present (`S376_HAS_GDAL`). Renderer drapes the packed
region (or a synth 25 km KSEA box) on the globe. osgEarth-from-source
recipe is in the README (Mint 22 / Ubuntu 24.04, tag `osgearth-3.7.2`).
Every-PR CI does **not** compile osgEarth; optional
`.github/workflows/osgearth.yml` is `workflow_dispatch` only.

## Milestone: three channels (2026-10-17)

CompositeViewer opens every enabled `[[channels]]` entry. One display tiles
left|front|right (verified under Xvfb 1920×1080: three 640×360 windows at
x=0/640/1280, heading offsets 35.515 / 0 / −35.25). `--channel front|left|right`
is the single-window fallback. Shared eyepoint; each view rotates by the
TOML heading offset (RSI ViewAngles). `select_channels` / `layout_channels`
are unit-tested.

## What's done and working

- Engine choice documented: **osgEarth on OSG 3.6**, with a procedural OSG
  globe until osgEarth is built from source. See `docs/DESIGN.md`.
- Repo skeleton: CMake, MIT license, clang-format, GitHub Actions CI on
  `ubuntu-24.04`, `.gitignore` that blocks RSI/CAE/captures.
- Clean-room xT4 decoder in `include/s376/xt4.hpp` + `src/xt4.cpp`.
  Autodetects word order (le/be) and float format (VAX F/D vs IEEE).
  Decodes eyepoint, environment (TOD, vis, precip, wind, lightning),
  fog, light-switch steps 0–5, ownship lights, display control, HAT query.
- HAT reply (`xt4::build_hat_response`) matches what the S376 host reads
  (64-byte big-endian IEEE). Host-side conversion is unit-tested.
- `s376-synth` sends a KSEA 16L takeoff to a destination that must pass
  the safety check.
- Host socket binds `0.0.0.0:2300` by default (overridable). Replies on
  2301. Built into the IG (no bridge process).
- TOML config with the three RSI channels (`config/s376-visual.toml`).
- Logging (`-v`, `--debug`, rotating file under `logs/`).
- `s376-doctor` / `--diagnostics` (cache bytes per airport, frame time
  per channel).
- Minimal OSG renderer: WGS84 globe, host-driven camera, shader fog/sun,
  precip points, cheap cloud decks (125–127), storm cells
  (105 + CSN 28–30), lightning flash. Local DEM patch + BC1 drape.
  osgEarth earth file is loaded when the lib is present. Linux GL
  context is legacy-compat; `--gl-debug` re-enables OSG error checks.
- `s376-cache`: `fetch` / `osm` / `synth-ksea --lods` / `from-geotiff` /
  `info`. Tiled 3DEP+NAIP (or GLO-30+S2) LOD pyramid, BC1/BC7 mips.
  `osm` POSTs Overpass QL (`-g`, percent-encoded GET fallback).
- Airport rebuild plan: `docs/AIRPORTS.md`. Terrain sources: `docs/SOURCES.md`.
- Safety: `10.1.95.0/24` and `192.168.173.0/24` refused unless
  `host.allow_sim_lan` / `S376_ALLOW_SIM_LAN=1`. Tests never set that.

## Half-done

| Item | Path | What's left |
|---|---|---|
| osgEarth tiles | `data/earth/ksea.earth` | File is in. Build osgEarth on the hangar box (`scripts/build-osgearth.sh`) and confirm the cache GeoTIFFs drape |
| Cloud / storm packets | `docs/PARITY.md` | 105/125–127 decoded and drawn. Remaining: catalog type table, real cell sizes, top-layer heights |
| HAT off-DEM | `sample_hat` | Stand-in elev=0 when the point is outside the packed region |
| More airports | `docs/AIRPORTS.md` | KSEA / KSFO / KLAS / KLAX / KJFK are in. Next: EGLL / EDDF |
| Off-axis frustum | camera is rotated by heading offset | Shared eyepoint + asymmetric projection later |
| High-res fetch on hangar | `s376-cache fetch KSEA` | Dry-run estimate only on this agent. Need a real download + doctor bytes on the Mint box |

## Next 5 concrete steps (in order)

1. **Run `s376-cache fetch KSEA` + `osm KSEA` on the hangar box** and
   paste doctor cache bytes / tile counts back into this file.
2. **Close remaining PARITY gaps**: 109 beams, 131 display, 128
   patchy/speed-cue, 124 runway condition, PAPI/taxi, cloud type
   catalog, real storm-cell sizes.
3. **Asymmetric / off-axis projection** if ViewAngles overlap is not enough.
4. **EGLL / EDDF** layouts.

## How to build / run / test (Linux Mint 22)

```bash
sudo apt install -y git cmake g++ libopenscenegraph-dev libgl-dev libglew-dev libgdal-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build -j"$(nproc)"
unset S376_ALLOW_SIM_LAN S376_ALLOW_LAN
ctest --test-dir build --output-on-failure
./build/s376-cache synth-ksea --out cache/ksea
./build/s376-cache fetch KSEA --dry-run
./build/s376-cache osm KSEA --dry-run
./build/s376-doctor -c config/s376-visual.toml
./build/s376-visual --headless --demo --duration 3 --bind 127.0.0.1:2300
./build/s376-visual --headless --demo-weather --duration 3 --bind 127.0.0.1:2300
# three tiled windows (one display) or --channel front for a single view:
./build/s376-visual --demo --cache-dir cache/ksea
./build/s376-visual --demo-weather --channel front
./build/s376-visual --demo --channel front
# Ctrl-C / SIGTERM must exit within a frame. --gl-debug re-enables OSG GL checks.
```

Windows: see README (CMake + optional vcpkg OSG). `--headless` and the
tests do not need OSG.

## Known issues

- osgEarth is **not** in Ubuntu 24.04 apt. v0.1 is useful without it
  (procedural globe + local DEM). Source-build recipe is in the README.
- IEEE packers assume a little-endian CPU (every hangar box we have).
- HAT is sampled from the local DEM when the eyepoint is on it.
  Off-grid replies still use elev=0 / hat=alt.
- `s376-doctor` tries to bind `127.0.0.1:2300`; if the IG is already
  running that check WARNs.
- No capture/replay yet (the old bridge had it). Add a pcap writer only
  for loopback, and never commit the files.
- Do not copy `nxHostPacket.h` or any RSI path into this tree.

## Design decisions

- **C++17 / CMake**, not Python. The IG is the realtime process.
- **Clean-room decoder.** Field layouts are in `docs/PROTOCOL.md` in our
  own words. Test vectors are synthetic packets we built, including
  golden hex dumped from the s376bridge Python builders (also synthetic).
- **osgEarth is the engine of record**; OSG 3.6 from apt is the v0.1
  renderer so CI and a Mint box work the same day.
- **One process.** Decoder + socket + renderer. Synth is a tiny extra
  binary for bench work.
- **Safety by default.** Production sets `allow_sim_lan` explicitly.

## Layout

```
include/s376/     public headers
src/              core + s376-visual / s376-synth / s376-doctor / s376-cache
tests/            assert-style unit tests (no Catch2)
config/           default TOML
data/earth/       osgEarth earth file
data/airports/    public KSEA / KSFO / KLAS / KLAX / KJFK TOML
docs/             DESIGN PROTOCOL AIRPORTS SOURCES PARITY
```
