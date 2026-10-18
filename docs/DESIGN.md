# s376-visual design

s376-visual is a Linux-first, Windows-buildable image generator that sits
where RSI Raster XT4 used to sit: it listens for host visual datagrams and
draws the out-the-window scene. The xT4 reader lives **inside** the IG.
There is no separate bridge process.

## Engine: osgEarth on OpenSceneGraph 3.6

**Choice.** osgEarth (current 3.x line) on top of OpenSceneGraph 3.6.

The IG has to do all of the following from day one or shortly after:

| Need | Why osgEarth |
|---|---|
| Streamed elevation and imagery | This is osgEarth's job: TMS/XYZ/WMTS/MBTiles/GDAL pages, not a 857 GB unpacked tile farm |
| Light points | OSG point sprites plus a dedicated light-point pass; osgEarth annotations for later |
| Fog / visibility | `osg::Fog` plus osgEarth horizon haze |
| Time of day | Our own sun/moon from host clock; osgEarth ephemeris later |
| Multi-channel off-axis frusta | One `osgViewer` (or CompositeViewer) per channel, shared map node |
| Small footprint | No CERTS-style store. First frame is a WGS84 globe; tiles fill in |
| Fast cold start | Earth file + HTTP/local cache, not a 17 GiB airport preload |

**Why not Godot 4.** Geocentric WGS84, streamed DEM/imagery, and three
off-axis simulator channels are not first-class Godot features. We would
still need GDAL and a tile stack, plus a heavier editor/runtime for a
machine that only ever shows the OTW.

**Why not a custom Vulkan renderer.** A from-scratch tile pager, geodesy,
ellipsoid, and atmosphere is months of work before the first useful frame.
The host is already sending packets. We need a camera on a globe now.

**Packaging caveat.** Ubuntu 24.04 / Linux Mint 22 ship OSG 3.6.5
(`libopenscenegraph-dev`) but **do not** ship osgEarth. osgEarth is built
from source (see the README). v0.1 therefore:

- always builds a headless-safe core (decoder, host socket, config, tests);
- builds an OSG window + procedural WGS84 globe when OSG is present;
- drapes a local DEM (portable `.s376dem` or GDAL GeoTIFF) with BC1/DXT1
  imagery from `s376-cache` / `render.cache_dir`;
- loads `data/earth/s376.earth` and uses streamed tiles when osgEarth is
  found at configure time (`S376_HAS_OSGEARTH`).

The camera, fog, and sun path is the same in both renderers.

## Process shape

```
SIMH MicroVAX 3900 (VMS)
        │  UDP  (host → IG :2300, IG → host :2301)
        ▼
 ┌─────────────────────────────────────────────┐
 │  s376-visual                                │
 │   xt4::Decoder  →  HostState (mutex)        │
 │   HostSocket    →  optional HAT reply       │
 │   Channel views ←  HostState + config       │
 │   osgEarth / OSG globe, fog, sun            │
 └─────────────────────────────────────────────┘
```

One process. Default bind `0.0.0.0:2300`. Reply port `2301`. A tiny
`s376-synth` tool emits fake host traffic so the IG can be developed
without SIMH.

## Protocol

The on-the-wire layout is documented in [PROTOCOL.md](PROTOCOL.md) in our
own words. The decoder is a clean-room C++ port of the field order,
sizes, and encoding rules that the earlier s376bridge Python decoder
already tested. Synthetic packets only — no captures, no RSI headers.

## Safety

The real simulator LANs (`10.1.95.0/24`, `192.168.173.0/24`) are refused
as destinations unless `host.allow_sim_lan = true` or
`S376_ALLOW_SIM_LAN=1`. Tests and CI never set that flag and never send
to those nets. Binding `0.0.0.0` is listen-only and is allowed.

## Config

One TOML file (`config/s376-visual.toml`). Three channels match the 1987
CAE 737-300 RSI setup:

| Channel | Heading offset | FOV |
|---|---|---|
| Front | 0° | 50° × 32.5° |
| Left | +35.515° | 50° × 32.5° |
| Right | −35.25° | 50° × 32.5° |

The IG opens every enabled `[[channels]]` entry (CompositeViewer). One
display tiles left|front|right; three monitors use `monitor` 0/1/2.
`--channel front` is the single-window laptop fallback.

## Terrain and airports

RSI `.nxb` / `.nxt` / CERTS tiles are proprietary and are **not**
converted. Terrain comes from open streamed sources (see
[SOURCES.md](SOURCES.md)). Airports are rebuilt from OSM + FAA /
OurAirports + published lighting standards ([AIRPORTS.md](AIRPORTS.md)).
Private RSI `.nx3` text on the owner's PC may be used as a local
reference and is never committed.

## Non-goals for v0.1

- Distortion meshes / edge blend (NLIM)
- Landing-light volumetric beams
- Weather cells, ocean FFT, traffic models
- Three genlocked channel PCs (one box, one GPU, one or more windows)

Those are follow-on work. The sockets, decoder, config, and camera path
are the foundation they sit on.
