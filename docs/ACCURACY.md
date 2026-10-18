# Accuracy (top project rule)

The new IG must beat Raster in every way. **Accuracy is first.** Never
trade a surveyed number, a lighting system, or a geodetic conversion
for looks, frame time, or disk. Disk is not a constraint (the pack
may exceed Raster's 857 GB). The remaining hard constraint is the
GTX 980 Ti **runtime** RAM/VRAM / 60 Hz channel budget.

**One exception:** the hidden Classic (pre-2001) NYC skyline easter
egg is **art-directed**, not surveyed. Footprints are only rough
Lower Manhattan placement so the towers read as downtown. Hero
lighting, facades, night windows, and reflections may be stylized.
It is session-only. Classic **draws** only inside the NYC geofence
(`data/regions/ny.toml`); the rest of the world stays the accurate
Current skyline. Repositioning or leaving NYC does **not** clear the
egg — coming back to NYC shows Classic again. It clears only when
the channels reset (host close/reopen, or a future Refresh channels
control). Everywhere else, accuracy-first still holds.

CI (`s376-tests` → `test_accuracy`) fails if any of these slip:

| Check | Limit |
|---|---|
| Runway-end position vs FAA | 3 m |
| Runway heading vs WGS84 geodesic of the two FAA ends | 0.1° |
| Displaced threshold vs FAA | 3 m |
| Approach-light type (NONE / ALSF-2 / MALSR / RLLS) | exact |
| PAPI side (P4L / P4R / P4L+P4L) and angle | exact / 0.05° |
| Airport imagery geotransform vs runway geometry | 1 m |
| Terrain elevation at each runway end vs FAA orthometric | 1 m |

`s376-doctor` runs the same FAA checks on KJFK / KEWR. With
`--cache-dir cache/KJFK` it also fails if lod0 tiles miss the ARP or
any runway end (`faa-coverage`), and if the north-up imagery
geotransform or DEM sample is off.

## Sources

| Field | Authority | File |
|---|---|---|
| US runway ends, elevations, displaced thresholds, widths, ALS, PAPI, TCH, EMAS | FAA AIP AD 2 / NFDC (NASR 28-day family) | `data/faa/*.toml`, `src/faa.cpp` |
| Cross-check | OurAirports `runways.csv` (public domain), AirNav | comments only |
| Taxi / apron / buildings | OSM Overpass (ODbL) when fetched | `cache/<ICAO>/osm.bin` |
| Imagery | NYS Latest ortho (NYC/LI) or NAIP | `s376-cache fetch` |
| Elevation | USGS 3DEP (CONUS), Copernicus GLO-30 elsewhere | same |

Every stored value carries `source`, `effective` (NASR / AIP cycle),
and `retrieved`. Current KJFK / KEWR cycle: **2026-10-02**, retrieved
**2026-10-18**.

Runways are built from **both physical FAA ends**. Heading and length
come from Vincenty inverse on WGS84. Do not compute the far end from
LE + published heading + length — that is what put 04R / 13L / 13R
0.5–2.5 km off.

## Datum, geoid, projection

| Item | What we do |
|---|---|
| Horizontal datum | WGS84 ellipsoid (`geo::kWgsA` / `kWgsB`) |
| Vertical | FAA elevations are orthometric (MSL). Ellipsoid = orthometric + N |
| Geoid | EGM96 and EGM2008 sample interpolation (`geo::geoid_undulation_m`) |
| Local projection | UTM (`wgs84_to_utm` / `utm_to_wgs84`), round-trip < 1 m at JFK (zone 18N) |
| Imagery | North-up geographic (EPSG:4326) export. Pixel↔geodetic round-trip must stay inside 1 m |

Tests live in `tests/test_geo.cpp` and `tests/test_accuracy.cpp`.

## Known differences from Raster (and why ours is more correct)

Raster photos and the prior committed KJFK layout are **reference
only**. Numbers below are public FAA versus that prior/wrong layout
and versus typical Raster-era JFK (LE + chart heading + length, ALS
copied across a pair). We do not copy RSI / CAE coordinates.

| Item | Raster / prior IG | Ours (FAA) | Why ours is more correct |
|---|---|---|---|
| 22R displaced threshold | Missing or a short/old figure | **3424 ft** (AIP AD 2.14) | FAA published declared distance. Landing threshold is 3424 ft down-runway from the 22R physical end |
| 04L displaced | Often omitted | **460 ft** | Same AIP table |
| 13R / 31L / 13L / 31R displaced | Often omitted | **2044 / 3264 / 907 / 1027 ft** | Same AIP table |
| 04R far end | LE + 31° + 8400 ft → lon ~0.006° west (~0.5 km) | Both FAA ends (40-37-31.5418N / 73-46-13.2441W and 40-38-42.8531N / 73-45-17.5027W) | Chart heading is a rounded true bearing; the surveyed ends define the runway |
| 13L / 31R ends | LE + 121° + 10000 ft, 1–2 km off | Both FAA ends | Same |
| 13R / 31L ends | LE was near the 04R/22L pair (~2.5 km east of the real 13R end) | Both FAA ends (13R at 73-49-00.173W) | 13R is the long Bay Runway; the west end sits in Jamaica Bay, not next to 13L |
| 13L / 31R width | 150 ft | **200 ft** | FAA AD 2.12 |
| 04L / 22R ALS | ALSF-2 both ends | **NONE** both ends | FAA AD 2.14: no approach lighting system on 04L or 22R |
| 04R / 22L ALS | MALSR / ALSF-2 | **ALSF-2 both**, with SFL | Precision pair; standard 2400 ft ALSF-2 (5-light bars / 100 ft, 1000 ft crossbar, side rows) |
| 13L ALS | ALSF-2 | ALSF-2 | Same, now from the real threshold |
| 31R ALS | MALSR | **MALSR** (bars 200–1400 ft, RAIL 1600–2400) | Not a single row + bar |
| 13R ALS | ALSF-2 | **RLLS** (lead-in groups) | Charted runway lead-in lighting, not ALSF-2 |
| 31L ALS | ALSF-2 | **NONE** | FAA: no ALS on 31L |
| PAPI | Missing or one generic VASI | P4L on seven ends, **P4R on 31R**, **two P4L on 13R**, all **3.00°** | AIP AD 2.14 |
| TDZ lights | Missing | 04R, 22L, 13L | Charted touchdown-zone |
| EMAS | Missing | DER 4R **393×226 ft**, DER 22L **405×226 ft** | AIP general remarks |
| KEWR | Not in the public Raster-replacement set | Full FAA both-end layout | 4L/22R 11000×150 MALSR; 4R/22L 9999×150 ALSF-2; 11/29 6725×150 REIL+PAPI+EMAS |
| KEWR 04L / 22R displaced | Owner memory 2543 / 1423 ft | **NFDC 2540.6 / 1440.9 ft** | NFDC is the current digital NASR figure. Owner numbers are documented here so a later NASR cycle can replace them if they win |
| KJFK cache location | Hangar pack: region.toml ~40.34–40.94 N, 74.07–73.48 W, ~20 km SW; no lod0 north of 40.614 (field is 40.62–40.66) | ARP-centered region; lod0 = ARP 5 km + 10 NM finals from **both FAA ends**; fetch **fails** if ARP or any end is outside lod0 | `tile_raster` iy=0 is south. Packing `dems.front()` recentered the pyramid on the SW tile. That is a bug, not a datum |
| Geodesy | Implicit local / ellipsoid mix | WGS84 + EGM96/EGM2008 + UTM | Orthometric FAA elevations become ellipsoid heights with published N; UTM is tested |
| Imagery vs pavement | Draped without a 1 m gate | North-up geotransform must round-trip runway ends inside 1 m | Accuracy over looks: do not warp the ortho to hide a geometry error |

## ALS geometry (not a single row + bar)

| System | Layout we build |
|---|---|
| ALSF-2 | 5-light bars every 100 ft to 2400 ft, 1000 ft crossbar, red side-row bars 100–1000 ft, sequenced flashers on the outer bars |
| MALSR | 5-light bars 200–1400 ft, RAIL / flashers 1600–2400 ft |
| RLLS | Lead-in groups out to ~6000 ft |
| NONE | No approach bar (04L, 22R, 31L at JFK; 11/29 at EWR) |
| PAPI | Four boxes ~350 ft past the **landing** threshold (after displaced), left or right of edge per P4L / P4R |
| REIL | Paired white strobes at the landing threshold |
| TDZ | White bar pairs for the first 3000 ft on charted ends |
| End / threshold | Green landing bar at the displaced threshold; red end bar at the physical DER (bidirectional) |

## Re-fetch (KJFK cache was SW-shifted)

Delete the old pack. `s376-cache fetch` now unions every lod0 tile,
writes `region.toml` from an **ARP-centered** box, writes
`coverage.toml`, and **throws** if the ARP or any runway end is
outside lod0.

```bash
rm -rf cache/KJFK
./build/s376-cache fetch KJFK --out cache/KJFK --dem 3dep --imagery nys
./build/s376-cache fetch KEWR --out cache/KEWR --dem 3dep --imagery nys
# defaults: native NYS 0.15 m on 8 km + 12 nm finals, 50 km outer, 3DEP 1 m
./build/s376-doctor --cache-dir cache/KJFK --no-bench
# expect [pass] faa-kjfk, [pass] faa-coverage, [pass] imagery-align
```

Shared NY outer box (denser mid/far terrain) is planned in `data/regions/ny.toml`
(~40.40–40.98 N, 74.45–73.45 W). High-res stays per airport. The
pager should load KEWR only when nearby.

## 980 Ti

Accuracy numbers are not optional to save milliseconds. The only
budgeted skip is the display-correction GPU pass when the warp / LUT
are identity (`needs_gpu_pass`).
