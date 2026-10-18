# Free elevation and imagery

RSI CERTS tiles and `.nxt` textures are proprietary and are not used.
s376-visual streams or caches standard formats.

## Imagery (draped on the globe)

| Source | How we use it | Notes |
|---|---|---|
| [Esri World Imagery](https://www.arcgis.com/home/item.html?id=10df2279f9684e4a9f6a7f08febac2a9) (default earth file) | osgEarth XYZ, spherical mercator | Attribution required. Fine for development; check Esri terms before a public show |
| OpenStreetMap standard tiles | osgEarth XYZ | [Tile usage policy](https://operations.osmfoundation.org/policies/tiles/) — do **not** point a 60 Hz IG at `tile.openstreetmap.org` in production. Host your own tile cache |
| Sentinel-2 L2A (Copernicus) | Local GeoTIFF / MBTiles / WMTS | Best free true-colour around European airports (EGLL, EDDF, LEBL). Copernicus open licence |
| [NAIP](https://naip-usdaonline.hub.arcgis.com/) | Local GeoTIFF / MBTiles | USDA 1 m imagery for CONUS airports (KSEA, KSFO, KLAX, KLAS, KJFK, …) |
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

## Caching

```bash
# synthetic KSEA (no download, CI and laptops)
./build/s376-cache synth-ksea --out cache/ksea

# real Copernicus GLO-30 + optional imagery (GDAL)
./build/s376-cache from-geotiff ~/data/glo30-ksea.tif \
    --imagery ~/data/naip-ksea.tif --out cache/ksea
```

Then set `render.cache_dir = "cache/ksea"` (or `--cache-dir`). Packed
files are `elev.s376dem`, `imagery.dds` (BC1), and `region.toml`.
KTX2/Basis is a later swap for the same drape slot; BC1 is the v0
GPU format because OSG 3.6 already understands DXT1.

osgEarth's own file cache is separate and is enough for a hangar box
that has the library. Do not commit downloaded tiles.

## What we will not ship

- RSI / CAE binaries, textures, CERTS tiles, `.nx3` extracts
- Captured host datagrams
- Paywalled imagery (most Maxar / Google / Bing tilesets)
