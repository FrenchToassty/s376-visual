# Quality tiers and GPU/CPU budgets

s376-visual must beat Raster XT4 in every way. **Accuracy is first**
(see [ACCURACY.md](ACCURACY.md)): never trade a FAA number, lighting
system, or geodetic conversion for looks or frame time.

**Disk is not a constraint.** The packed dataset may exceed Raster's
857 GB CERTS farm when quality improves — the owner will fit new SSDs
in the visual PCs. Quality and accuracy win over storage. Fetch the
highest published ortho at airports and approaches, and denser global
terrain; do not downsample to save disk.

The remaining hard constraint is the **runtime** box — an
**i7-5930K + GTX 980 Ti 6 GB** (Maxwell, NVIDIA 580, OpenGL 3.3
compatibility) and **16 GB RAM** at 60 Hz per channel. Resident
`ram_mb` / `vram_mb` still apply; a larger pack is fine, a larger
resident set is not. The hangar RTX 5060 Ti is the `rtx` preset.

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
- **Upload DXT1 through OSG 3.6.** `dxt::read_dds` keeps the block
  payload. BC7 (BPTC) is transcoded block-by-block to DXT1 because
  apt OSG 3.6 does not treat `GL_COMPRESSED_RGBA_BPTC_UNORM` as
  compressed (`glTexImage2D` → `invalid enumerant`). No 48 MB RGBA
  inflate. `setUnRefImageDataAfterApply(true)` drops the CPU copy.
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

## Imagery resolution (disk-unlimited tiers)

`s376-cache fetch` metres/px default to **0 = native for the chosen
source**. `max_export=2048` is the ImageServer cap (USGS 3DEP and NYS
return 500/504 on ~4k F32/JPG). More tiles, not blurrier pixels. A
2048 px tile at NYS 0.15 m/px is **307 m** on a side. Re-run fetch to
resume — completed `lodN/tY_X` tiles are skipped. `--plan` prints
every tile bbox and FAA coverage with no download. Do not upsample a
source finer than native unless the operator set a positive
`--near-m` (e.g. `--near-m 1` on NYS stays 1 m).

| LOD | Coverage | Imagery | DEM |
|---|---|---|---|
| 0 | **8 km** around the ARP + **12 nm** finals | native: NYS **0.15 m**, HRO **0.30 m**, NAIP **0.60 m**, S2 **10 m** | native: 3DEP **1 m**, GLO-30 **30 m** |
| 1 | **40 km** | 4× native | 2× native |
| 2 | **50 km** outer | 16× native | 8× native (never finer than native) |

| Source | Native | Default pull (near / mid / far) | Licence |
|---|---|---|---|
| USDA NAIP (USGS NAIPImagery) | ~0.6 m | **0.60 / 2.4 / 9.6 m** | US public domain |
| USGS NAIP Plus / HRO (`--imagery hro`) | ~0.3 m | **0.30 / 1.2 / 4.8 m** | US public domain |
| NYS GIS Latest ortho (`--imagery nys`, auto at KJFK) | ~0.15 m (0.5 ft) | **0.15 / 0.60 / 2.4 m** | NYS public ortho |
| Sentinel-2 cloudless | 10 m | **10 / 40 / 160 m** | CC BY 4.0 + Copernicus |
| USGS 3DEP | 1 m | **1 / 2 / 8 m** | US public domain |
| Copernicus GLO-30 | 30 m | **30 / 60 / 240 m** | Copernicus open |

`--imagery auto` uses the per-airport table in [SOURCES.md](SOURCES.md)
(NYS at KJFK, King County at KSEA, San Mateo at KSFO, NAIP at KRNO, …).
If the free source is weaker than Raster (SKRG, VNKT), fetch refuses
and `s376-cache import-raster` reads Joey's local airport folder.
No Google / Bing / Esri scrape.

The pack may be larger than Raster's 857 GB. The pager still evicts
when `ram_mb` / `vram_mb` would exceed the preset (2 GiB / 5 GiB on
`gtx980ti`). VRAM grows with how many lod0 tiles sit in the frustum,
not with how many tiles sit on disk.

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
