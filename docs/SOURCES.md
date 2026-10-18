# Free elevation and imagery

RSI CERTS tiles and `.nxt` textures are proprietary and are not used.
s376-visual streams or caches standard formats.

## Imagery (draped on the globe)

| Source | How we use it | Notes |
|---|---|---|
| [Esri World Imagery](https://www.arcgis.com/home/item.html?id=10df2279f9684e4a9f6a7f08febac2a9) (default earth file) | osgEarth XYZ, spherical mercator | Attribution required. Fine for development; check Esri terms before a public show |
| OpenStreetMap standard tiles | osgEarth XYZ | [Tile usage policy](https://operations.osmfoundation.org/policies/tiles/) — do **not** point a 60 Hz IG at `tile.openstreetmap.org` in production. Host your own tile cache |
| Sentinel-2 L2A (Copernicus) | Local GeoTIFF / MBTiles / WMTS | Best free true-colour around European airports (EGLL, EDDF, LEBL). Copernicus open licence |
| [NAIP](https://naip-usdaonline.hub.arcgis.com/) | Tiled ImageServer export + local BC7 | USDA ~0.6 m native; we pull ~1 m near the ARP and 10 nm finals (4000 px tiles), 4 m mid, 20 m far |
| osgEarth ReadyMap | TMS, if the public endpoint is still up | Historical osgEarth demo. Treat as optional |

The default `data/earth/s376.earth` points at Esri World Imagery so a
machine with osgEarth and a network gets a real-looking globe on the
first run. Swap the image layer for a local MBTiles tree when you go
offline or need airport-grade resolution.

## Elevation

| Source | Resolution | Notes |
|---|---|---|
| [Copernicus GLO-30](https://spacedata.copernicus.eu/collections/copernicus-digital-elevation-model) | 30 m | Preferred global DEM. Open Copernicus licence |
| [AWS Terrain Tiles](https://registry.opendata.aws/terrain-tiles/) (Mapzen / Amazon) | ~30 m, terrarium or quantized-mesh | Easy XYZ/TMS |
| SRTM 1 arc-second | 30 m | Void-filled copies via USGS / CGIAR. Weaker at high latitude |
| A local GeoTIFF | whatever you clip | Point the earth file `<elevation driver="gdal">` at it |

Without osgEarth the IG still has a local DEM: a portable `.s376dem`
grid (row-major metres MSL) plus a BC1/DXT1 DDS drape. `s376-cache
synth-ksea` writes a 25 km Sea-Tac box in a few hundred KiB. Point
`s376-cache from-geotiff` at a Copernicus GLO-30 clip (and optional
NAIP / Sentinel-2 GeoTIFF) for a real region — tens of MiB, not the
857 GB uncompressed RSI CERTS farm.

With osgEarth, add an elevation layer to `s376.earth` and the same
camera path rides the streamed mesh.

## Licences and attribution

| Product | Licence | How we cite it |
|---|---|---|
| Copernicus GLO-30 | [Copernicus DEM licence](https://spacedata.copernicus.eu) (© European Union) | `ATTRIBUTION.txt` in each packed cache |
| USGS 3DEP | US government work, public domain | same |
| USDA NAIP (via USGS National Map) | US government work, public domain | same |
| EOX Sentinel-2 cloudless | [CC BY 4.0](https://s2maps.eu/) + Copernicus Sentinel | same |
| OurAirports runways.csv | public domain | airport TOML / `airport.hpp` |
| OpenStreetMap aeroway=* / building=* | [ODbL](https://www.openstreetmap.org/copyright) | OSM taxi/apron/building ways; attribute OSM |
| Poly Haven 1K PBR (asphalt, concrete, grass, dirt) | [CC0 1.0](https://polyhaven.com/license) | `scripts/fetch-textures.sh` → `cache/textures/` (gitignored) |
| ambientCG (optional extra maps) | [CC0 1.0](https://docs.ambientcg.com/license/) | same cache dir |
| [stb_image_write](https://github.com/nothings/stb) v1.16 | public domain / MIT | `third_party/stb_image_write.h` — PNG from `--screenshot` / `--self-test-shot` |

Do **not** commit downloaded GeoTIFFs, JPEGs, or packed `cache/` trees.
`cache/` and `*.tif` are gitignored. `s376-cache fetch` writes
`ATTRIBUTION.txt` next to the tiles.

## Caching

```bash
# synthetic KSEA (no download, CI and laptops)
./build/s376-cache synth-ksea --out cache/ksea --lods

# real data: GLO-30 or USGS 3DEP + NAIP (CONUS) / Sentinel-2 cloudless
./build/s376-cache fetch KSEA --radius-km 25 --out cache/ksea
./build/s376-cache fetch KSFO --out cache/ksfo
./build/s376-cache fetch EGLL --dem glo30 --imagery s2 --out cache/egll

# preview the public URLs without downloading
./build/s376-cache fetch KSEA --dry-run

# OSM taxi / apron / building ways (ODbL) — not committed
./build/s376-cache osm KSFO --out cache/KSFO

# optional CC0 close-up detail (never committed)
./scripts/fetch-textures.sh           # cache/textures/*.jpg
```

`fetch` packs a LOD pyramid: high-res (~8 km, BC7 + mips) near the
field, mid (~25 km, BC1), optional coarse far ring. Root
`elev.s376dem` is the finest LOD so HAT still works. A 25 km box is
tens of MiB, not the 857 GB uncompressed RSI CERTS farm.

Then set `render.cache_dir = "cache/ksea"` (or `--cache-dir`) and, with
osgEarth, `render.earth_file = "data/earth/ksea.earth"`. That earth file
reads `cache/ksea/elev.tif`, `imagery.tif`, and `osm.geojson`. Without
osgEarth the IG drapes the same cache on the procedural globe.

osgEarth's own file cache is separate and is enough for a hangar box
that has the library.

## What we will not ship

- RSI / CAE binaries, textures, CERTS tiles, `.nx3` extracts
- Captured host datagrams
- Paywalled imagery (most Maxar / Google / Bing tilesets)
