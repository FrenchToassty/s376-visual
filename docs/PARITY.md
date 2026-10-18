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
| 124 lightning trigger | Flash on storm / env | **partial** | Words 10–11 stored. Edge change flashes sun/sky ~4 frames. No storm-cell bolts |
| 124 runway condition | Wet / snow surface look | **gap** | Decoded, unused |
| 124 date | Sun season | **ok** | Month/day into `sun_ecef` |
| 128 fog select / RVR | Fog / RVR overlay | **partial** | RVR caps vis when select ≥ 0. Patchy and speed-cue unused |
| 128 layer height | Fog deck AGL | **partial** | Thickens EXP2 when last HAT is below height+200 ft. No visible deck slab |
| 131 display 0/1/2 | Channel off / on / standby | **gap** | Decoded, windows always on |
| Cloud layers (lower / upper / top) | 3D cloud decks, coverage, type | **gap** | Type number / body not clean-roomed in-tree. Do not invent a packet. No fake slabs |
| Storm cells (≤3 of 30) | Cells + lightning | **gap** | Same. Env lightning flash is the only bolt cue until the cell array is clean-roomed |
| Channel geometry | 3 frusta, ~50°×32.5°, ±35.5° / ±35.25° | **partial** | Shared-eye + heading offset. No off-axis / NLIM / edge blend |
| Airport lights | Edge, CL, ALS, PAPI, taxi | **partial** | Edge/CL/ALS/threshold on KSEA KSFO KLAS KLAX KJFK. PAPI/taxi **gap** |
| Terrain / imagery | CERTS + NX tiles | **n/a** | Rebuilt from GLO-30/3DEP + NAIP/S2. Different pixels, same role |
| Airport meshes | NX airport DB | **n/a** | Rebuilt from OurAirports + OSM. No RSI geometry |

## IG → host

| Packet / field | Original effect | Status | Notes |
|---|---|---|---|
| 200 HAT ft (IEEE BE) | Host radio altimeter / HAT | **partial** | 64-byte host-compatible reply. On DEM: elev + hat. Off DEM: elev=0, hat=alt (stand-in) |
| 200 HOT / terrain elev | Terrain MSL | **partial** | Same as above |
| 200 valid flag | Reply usable | **ok** | Always valid so the host is never starved |

## Image quality (allowed deltas)

These must **not** change what is in the scene, only how sharp it is.

| Lever | Status |
|---|---|
| 4× MSAA (`render.msaa`) | **ok** — GL 3.3 / Maxwell-safe |
| Anisotropic filter (`render.aniso`, default 16) | **ok** — DEM / globe drapes |
| Mipmapped drapes | **ok** |
| GPU point sprites for lights | **ok** — crisper than CPU dots; still step 0–5 |
| GL 3.3 compatibility (not Core) | **ok** — GTX 980 Ti + RTX 5060 Ti |

## Channel / desk

| Item | Status |
|---|---|
| Front 0°, left +35.515°, right −35.25°, 50×32.5 | **ok** |
| 4-monitor desk pin (`config/desk-4monitor.toml`) | **ok** — IOS on DP-4 (x=0), L/F/R on DP-3 / HDMI-0 / DP-0 |
| IOS / debug view | **partial** — same eyepoint, pitched −18°, 60° FOV. Not a second ownship |

## Safety (must never regress)

- No RSI / CAE files, ICD text, captures, or NX tiles in git
- Tests/CI never send to `10.1.95.0/24`, `192.168.173.0/24`, or UDP 3340/3341
