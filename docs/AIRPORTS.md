# Airport rebuild plan

The RSI airport databases (about 17 GiB of `.nxb` / `.nxt`) cannot be
converted. s376-visual rebuilds each field from open data plus published
lighting standards. The owner's RSI `.nx3` text files (runway thresholds,
headings, light-switch maps) are a **private local reference only** and
must never be committed.

## Fields we need (from the live XT4 install)

| ICAO | Name | Priority |
|---|---|---|
| KLAS | Harry Reid / McCarran, Las Vegas | 1 — typical start |
| KSFO | San Francisco Intl | 1 |
| KSEA | Seattle-Tacoma | 1 |
| KLAX | Los Angeles Intl | 1 |
| KJFK | New York Kennedy | 2 |
| KBOS | Boston Logan | 2 |
| KIAD | Washington Dulles | 2 |
| KMCO | Orlando Intl (+ KORL) | 2 |
| KMIA | Miami Intl | 2 |
| KRNO | Reno-Tahoe | 2 |
| EGLL | London Heathrow | 2 |
| EDDF | Frankfurt Main | 2 |
| LEBL | Barcelona El Prat | 3 |
| SEQM | Quito Mariscal Sucre | 3 |
| SKBO | Bogotá El Dorado | 3 |
| SKRG | Rionegro / Medellín | 3 |
| VNKT | Kathmandu Tribhuvan | 3 |

Plus two generic “anywhere” pads later, for host airport-select fallback.

## Data sources (all public)

| Layer | Source | What we take |
|---|---|---|
| Airport / runway / displaced threshold | [OurAirports](https://ourairports.com/data/) `airports.csv`, `runways.csv` | ICAO, lat/lon, elev, runway ends, width, surface, lighted flag, heading |
| US runway + NASR | [FAA 28-day NASR / CIFP](https://www.faa.gov/air_traffic/flight_info/aeronav/digital_products/) | Authoritative US thresholds, TCH, declared distances |
| European AIP | National AIS (NATS, DFS, ENAIRE, …) and [EUROCONTROL](https://www.eurocontrol.int/) where the product is open | Same, for EGLL / EDDF / LEBL |
| Taxiways, aprons, buildings, parking | [OpenStreetMap](https://www.openstreetmap.org/) via Overpass (`aeroway=*`) | Polygons and lines. ODbL attribution |
| Terminal / hangar massing | OSM `building` + a later simple extrude. Optional public CityGML | Boxy is fine for v1 |
| Imagery under the airport | NAIP (CONUS) or Sentinel-2 (everywhere) — see [SOURCES.md](SOURCES.md) | Draped, not modelled |
| Elevation under the airport | Copernicus GLO-30, then a local airport flatten | Conform runways to a plane |

## Lighting — from standards, not from RSI binaries

Light **positions** come from runway geometry plus the published
standard for that approach. Light **switch numbers** (which host
packet 112 turns up) will be assigned in our own airport TOML so they
can be lined up with the host later. The private `.nx3` switch map is
the cheat-sheet for that lining-up step; the committed files use our
names.

| System | Standard | Layout rule (short) |
|---|---|---|
| Runway edge | FAA AC 150/5340-30, ICAO Annex 14 | White (yellow last 600 m / 2 000 ft on instrument runways), 60 m / 200 ft spacing, two rows at runway width |
| Runway centreline | same | 15 m / 50 ft; red/white then red in the last 600 m |
| Threshold / end | same | Green threshold bar, red end bar |
| Touchdown zone | same | White bars 150–900 m from threshold on precision runways |
| PAPI / VASI | AC 150/5345-28, ICAO | Four (or two) boxes, 15 m left of edge, ~300 m from threshold, 3° glide typical |
| ALSF-2 | FAA approach-light spec | 2 400 ft centreline bars + 5-bar crossbar at 1 000 ft + side rows. KJFK / KLAX / KSFO precision ends |
| ALSF-1 / MALSR / SSALR / ODALS / REIL | same family | Shorter / fewer bars. Match the charted system per end |
| Taxi edge / centreline | AC 150/5340-30 | Blue edge, green centreline. OSM taxiway graph is the path |
| Hold-short / stop bar | SMGCS / ICAO | Red stop bar, yellow hold |
| Approach REIL / wig-wag | same | Paired white flashers at threshold |

Intensity steps are **0–5** (packet 112). The GPU light-point pass
scales colour and alpha by `step / 5`. Switch numbers are ours:

| Switch | Group |
|---|---|
| 1 | Runway edge |
| 2 | Runway centreline |
| 3 | Approach (ALSF-2 / MALSR bars) |
| 4 | Threshold / end |

`s376::ksea_public()` builds KSEA from OurAirports runway ends (16L
matches the synth takeoff) plus FAA-charted ALS. Edge / centreline /
approach / threshold points are placed from the published spacing
rules above. Taxi connectors are schematic offsets from those
runways — a stand-in until an Overpass `aeroway=*` pull (ODbL) lands.

`data/airports/ksea.toml` documents the same public numbers. Do not
paste RSI `.nx3` coordinates into it.

## Suggested build order

1. **KSEA 16L/34R** — we already have a synthetic takeoff from that
   threshold in `s376-synth`. Easiest eyeball test.
2. **KSFO** — complex ALS, water, hills. Proves flatten + lights.
3. **KLAS** — desert, lots of night light points.
4. **KLAX** then **KJFK** — parallel-runway ALSF-2.
5. **EGLL / EDDF** — ICAO lighting, Sentinel-2.
6. The rest in the table order.

## What stays private

On the owner's PC, `domain.nx3` and friends list RSI switch numbers,
threshold lat/lon, and light-group names. Use them to **audit** our
layouts and to pick switch numbers the host already sends. Do not copy
the files into this public repo, do not paste blocks of them into
commits, and do not check in screenshots that are just the RSI GUI.
