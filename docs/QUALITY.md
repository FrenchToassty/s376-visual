# Quality tiers and GPU/CPU budgets

s376-visual is built to look sharper than Raster XT4 without changing
host behavior. The default box is an **i7-5930K + GTX 980 Ti 6 GB**
(Maxwell, NVIDIA 580, OpenGL 3.3 compatibility) and **16 GB RAM**. The
hangar RTX 5060 Ti is the `rtx` preset. One projector PC draws one
channel at 1920×1080 / 60 Hz.

## Presets

| | `gtx980ti` (default) | `rtx` |
|---|---|---|
| Target GPU | GTX 980 Ti 6 GB Maxwell | RTX 5060 Ti / similar |
| Assumed system RAM | 16 GB | 16 GB+ |
| `render.ram_mb` | **2048** | 3072 |
| `render.vram_mb` | **5120** (leave ~1 GB for the desktop / HUD) | 10240 |
| `render.mesh_n` | 96 samples / tile | 128 |
| MSAA | 4 | 4+ |
| Aniso | 16 | 16 |
| HDR / ACES | on | on |
| Atmosphere / water / markings / detail / buildings / SSAO | on | on |
| Shadows | off | on (flag only; CSM not drawn yet) |
| Texture format | BC1 (DXT1) far, BC7 near | same |
| CPU decode to RGBA | **never** | never |

Override any field in TOML after `quality = ...`. `custom` skips the
preset clamp.

## Why a 1 GB KJFK pack used 14 GB RAM

A packed `cache/KJFK` on the hangar box was about **1.6 GB on disk**
(lod0 ~1.0 GB in 14 subdirs, lod1 29.6 MB, `osm.json` 436 MB,
`osm.geojson` 150 MB, root `elev.s376dem` 590 KB). The IG used to:

1. Mesh every lod0 DEM at full 4000² (~16 M verts ≈ 512 MB/tile × 14).
2. Hillshade / decode BC tiles to RGB(A) (~48 MB/tile).
3. Slurp `osm.json` into a DOM for every public airport.

That peaked at **anon-rss 13.9 GB** and the kernel OOM-killed the IG
and the host app.

Runtime rules now:

- **Page** `lodN/tY_X` by distance and a coarse heading frustum.
- **Subsample** each resident DEM to `mesh_n` (`DemGrid::load_subsampled`).
- **Upload BC/DXT compressed.** `dxt::read_dds` keeps the block payload;
  OSG `setUnRefImageDataAfterApply(true)` drops the CPU copy after the
  first apply. No RGBA inflate.
- **`osm.bin` only.** `s376-cache osm` / `bake-osm` stream-parses
  Overpass JSON (no full slurp) into a compact `S376OSM1` sidecar. The
  IG never opens `osm.json`.
- Evict farthest lod0 tiles when `ram_mb` / `vram_mb` would be exceeded.

`s376-doctor` prints `rss` (peak / current / budget) and `cache-tiles`
(naive full-res mesh vs paged estimate). Target: **peak RSS under the
preset budget** on a 16 GB / 6 GB VRAM box.

```bash
./build/s376-cache bake-osm cache/KJFK   # if you already have osm.json
./build/s376-doctor --cache-dir cache/KJFK
# expect [pass] rss  peak < 2 GiB   and cache-tiles paged mesh << naive
```

## Imagery resolution (KJFK)

`s376-cache fetch` defaults: `near_m=1.0`, `max_export=4000`, so a
4000 px tile is **4 km at 1 m/px**.

| Source | Native | What we pull | Licence |
|---|---|---|---|
| USDA NAIP (USGS NAIPImagery) | ~0.6 m | ~1 m near / 4 m mid / 20 m far | US public domain |
| USGS NAIP Plus / HRO (`--imagery hro`) | ~0.3–1 m | same tiling | US public domain |
| NYS GIS Latest ortho (`--imagery nys`, auto at KJFK) | ~0.15–0.30 m (0.5–1 ft) | same tiling; 4000 px still 1 m/px unless you lower `--near-m` | NYS public ortho |

A finished KJFK fetch at the defaults **slightly downsamples NAIP**
(0.6 m → 1 m) and does **not** preserve 0.5–1 ft. Use `--imagery nys`
and `--near-m 0.3` (and more tiles) if you want the NYS native
resolution; watch the 2 GB CPU / 5 GB VRAM caps — paging still holds
resident RAM, but VRAM grows with how many lod0 tiles sit in the
frustum.

`--imagery auto` picks NYS for airports inside NY (KJFK), NAIP for
other CONUS, Sentinel-2 elsewhere. No Google / Bing / Esri scrape.

## Ground detail

A generated 128² atlas (grass / asphalt / dirt / concrete quadrants)
is blended over the ortho with a **distance fade** (strong inside
~2.5 km, gone by ~8 km). Optional CC0 photos from Poly Haven /
ambientCG can be fetched into `cache/textures/` (gitignored) — the
runtime atlas does not require them. See [SOURCES.md](SOURCES.md).

## Screenshot viewport

`--screenshot` uses a non-MSAA FBO at the channel WxH. The slave
camera **does not inherit** the master's viewport (a 1920×1008
decorated window used to leave a ~7% black bar on 1920×1080 captures).
