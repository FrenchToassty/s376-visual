# RSI Raster XT4 parity checklist

The new IG must **look and behave like the original Raster XT4** on this
737-300: same host packet fields honored, same visual features, same
HAT/query replies. Allowed differences are lighter hardware, a sharper
image (MSAA, anisotropic filtering, crisp light points), and cleaner
code. Do **not** invent scenery, weather, or lighting the host does not
send.

This table is the source of truth for gaps. Status:

| Status | Meaning |
|---|---|
| **ok** | Decoded and rendered / replied like the original |
| **partial** | Decoded; effect is incomplete or uses our own mapping |
| **gap** | Host sends it (or XT4 showed it); we ignore or stub it |
| **n/a** | Not on the S376 wire / not a Raster feature we must match |

Packet layouts are in [PROTOCOL.md](PROTOCOL.md) (clean-room). Switch
numbers in committed airport TOML are **ours**, not RSI's.

## Host → IG

| Packet / field | Original effect | Status | Notes |
|---|---|---|---|
| 100 eyepoint CSN 1 / minor −1 | Pilot-eye camera, WGS84, heading negated | **ok** | Shared eyepoint; each channel rotates by ViewAngles |
| 100 heading / pitch / roll | Body attitude | **ok** | |
| 100 lat / lon / alt ft | Position | **ok** | |
| 100 other CSN (traffic, marshaller) | Other movers in the scene | **gap** | Counted, body ignored |
| 109 beacon / strobe | Ownship anti-collision flashes | **gap** | Decoded, not drawn |
| 109 landing-light mask + bright | Landing / taxi lobes in the scene | **gap** | Decoded, no beams (NLIM later) |
| 112 switch + step 0–5 | Airport light group intensity | **partial** | GPU points scale by step/5. Switches 1–4 = edge / CL / ALS / threshold (ours). No PAPI, taxi, hold-short yet |
| 113 query type 200 | Ask for HAT/HOT of CSN 1 | **ok** | |
| 124 tod_select −1 / 0…3 | Continuous hour or day/dusk/dawn/night | **ok** | −1 uses `tod_hours`; 0–3 snap to 14.5 / 19 / 6 / 0.5 |
| 124 tod_hours | Sun / sky from local solar time | **ok** | |
| 124 visible_range_ft | Global vis, cap 600 000 ft | **ok** | EXP2 fog density |
| 124 precip type + intensity | Rain / snow / hail in the volume | **partial** | GPU points around the eyepoint (type 1 rain / 2 snow / 3 hail). Density from intensity 0–10. No streaks, no wet-runway look |
| 124 wind speed / dir | Drift, streak, wave cues | **partial** | Precip drifts with wind FROM dir. No waves / smoke |
| 124 lightning trigger | Flash on storm / env | **partial** | Words 10–11 stored. Edge change flashes sun/sky ~4 frames. Storm cells add their own bolts |
| 124 runway condition | Wet / snow surface look | **gap** | Decoded, unused |
| 124 date | Sun season | **ok** | Month/day into `sun_ecef` |
| 105 storm select / active / CSN | Up to 3 of 30 cells on | **ok** | Type 105. CSN 28–30 |
| 105 storm precip / lightning | Rain shaft + bolts in the cell | **partial** | Intensity scales stand-in radius and precip near the cell. lightning 1=random, >1=edge |
| 100 CSN 28–30 | Storm position | **ok** | Lat/lon/alt from type 100 |
| 105 / catalog cell size | Cell diameter | **partial** | Not on the wire (original used IG catalog). 1.5 nm × intensity stand-in |
| 125 / 126 ceiling + top | Lower / upper decks, MSL | **ok** | Cheap stacked discs + 8 cards, GL 3.3. Wind crawls the deck |
| 125 / 126 select | Type / coverage catalog | **partial** | −1 off, ≥0 on. No RSI type table; a layer that is on draws as a solid deck |
| 125 / 126 visAbove | Vis above the deck | **ok** | Caps vis when the eye is above the top |
| 125 / 126 scud / trans | Ragged edges, transition | **partial** | Scud bit 0 widens the disc. Transition heights unused |
| 127 top layer | Highest deck | **partial** | On/off + catalog only. Heights 25–28 kft stand-in (original: IG catalog) |
| 128 fog select / RVR | Fog / RVR overlay | **partial** | RVR caps vis when select ≥ 0. Patchy and speed-cue unused |
| 128 layer height | Fog deck AGL | **partial** | Thickens EXP2 when last HAT is below height+200 ft. No visible deck slab |
| In-cloud fog | Thick vis inside 125/126/127 | **ok** | EXP2 × 2.4 when eye MSL is between ceiling and top |
| 131 display 0/1/2 | Channel off / on / standby | **gap** | Decoded, windows always on |
| Channel geometry | 3 Raster frusta on 4 projector PCs | **ok** | far_left=left +35.515, mid_left=front 0, mid_right=front 0 (duplicate), far_right=right −35.25, 50×32.5. Optional FO `eye_right_m`. No off-axis / NLIM / edge blend |
| Airport lights | Edge, CL, ALS, PAPI, taxi | **partial** | Edge/CL/ALS/threshold on KSEA KSFO KLAS KLAX KJFK KBOS. PAPI/taxi **gap** |
| Terrain / imagery | CERTS + NX tiles | **n/a** | Rebuilt from GLO-30/3DEP + NAIP/S2. Different pixels, same role |
| Airport meshes | NX airport DB | **n/a** | Rebuilt from OurAirports + OSM. No RSI geometry |

## IG → host

| Packet / field | Original effect | Status | Notes |
|---|---|---|---|
| 200 HAT ft (IEEE BE) | Host radio altimeter / HAT | **partial** | 64-byte host-compatible reply. On DEM: elev + hat. Off DEM: elev=0, hat=alt (stand-in) |
| 200 HOT / terrain elev | Terrain MSL | **partial** | Same as above |
| 200 valid flag | Reply usable | **ok** | Always valid so the host is never starved |

## Image quality (allowed deltas)

These must **not** change what the host put in the scene — only how sharp
or deep it is. Expensive levers are TOML toggles; `render.quality =
"gtx980ti"` (default) or `"rtx"`.

| Lever | Status |
|---|---|
| 4× MSAA (`render.msaa`) | **ok** — GL 3.3 / Maxwell-safe |
| Anisotropic filter (`render.aniso`, default 16) | **ok** — DEM / globe drapes |
| Mipmapped drapes | **ok** |
| FAA AC 150/5340-1 markings | **ok** — geometry (threshold, 5×7 numbers/LRC, aiming, TDZ, CL, edge, hold-short). Stays sharp at grazing angles; no committed textures |
| Asphalt / rubber / grass detail | **partial** — hash+land-class noise. `scripts/fetch-textures.sh` pulls CC0 Poly Haven 1K JPGs into `cache/textures/` (never committed); shader fallback if absent |
| OSM buildings | **partial** — extruded footprints + schematic terminals. LOD cap 280. Instancing later |
| Analytic sky + aerial perspective | **partial** — Preetham-like dome, sun disc, moon+stars at night, EXP2 + altitude haze (`uHaze`). Not a full Bruneton LUT |
| Water / coastlines | **partial** — soft land mask (public geography), Fresnel + stable waves + sun glint. OSM water/coastline queried, not meshed yet |
| Oblique mip / aniso | **ok** — 2048×1024 globe, trilinear, aniso 16, LOD bias −0.35 |
| Cloud soft edge / lighting | **partial** — hash edge + sun wrap on the weather-round decks |
| HDR + ACES | **ok** — filmic in every forward shader; `render.exposure` |
| SSAO | **partial** — cheap wall darkening; on for both `gtx980ti` (one channel) and `rtx`. No depth post-pass |
| Sun shadows | **gap** — `render.shadows` toggle only; CSM not drawn (980 Ti budget) |
| GPU point sprites for lights | **ok** — GLSL 120 points (no PointSprite / POINT_SMOOTH); still step 0–5 |
| GL compatibility (not Core) | **ok** — Linux leaves the context version unset (NVIDIA 4.6 compat). Elsewhere 3.3 + `COMPATIBILITY_PROFILE_BIT`. Scene lighting/fog/points are shaders so a CORE context does not emit `invalid enumerant` |
| `--screenshot PATH --at ICAO[:RWY[:nm]]` | **ok** — `--alt` / `--bank` / `--hdg` / `--pitch`. Defaults to `mid_left`. `--channel front` aliases that |

## Channel / desk

| Item | Status |
|---|---|
| 4 projector PCs, Raster 3 channels | **ok** — captain: far_left+mid_left; FO: mid_right+far_right; both mids = front. `$S376_CHANNEL` |
| Host-stream frame sync | **ok** — `render.sync_to_host` draws only on a new type-100 eyepoint |
| HAT reply master | **ok** — `host.reply_master` / `--reply-master` (default mid_left); other PCs listen only |
| Per-seat eyepoint | **partial** — `eye_right_m` / `eye_fwd_m` / `eye_up_m` optional on FO channels; default 0 (shared host eye) |
| Laptop 3-window | **ok** — `config/channels-3.toml` (front/left/right names) |
| 4-monitor desk pin (`config/desk-4monitor.toml`) | **ok** — one-PC hangar desk: IOS on DP-4, L/F/R on DP-3 / HDMI-0 / DP-0 |
| IOS / debug view | **partial** — same eyepoint, pitched −18°, 60° FOV. Not a second ownship |

## Safety (must never regress)

- No RSI / CAE files, ICD text, captures, or NX tiles in git
- Tests/CI never send to `10.1.95.0/24`, `192.168.173.0/24`, or UDP 3340/3341
