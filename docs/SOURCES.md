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

v0.1 without osgEarth uses a smooth WGS84 ellipsoid (no DEM). With
osgEarth, add an elevation layer to `s376.earth` and the same camera
path will ride the mesh.

## Caching

Put a cache directory in the config (`render.cache_dir`). osgEarth's
default file cache is enough for a hangar machine. Do not commit
downloaded tiles.

## What we will not ship

- RSI / CAE binaries, textures, CERTS tiles, `.nx3` extracts
- Captured host datagrams
- Paywalled imagery (most Maxar / Google / Bing tilesets)
