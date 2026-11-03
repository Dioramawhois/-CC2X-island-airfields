# Owned island airfield knowledge

Owned infrastructure is known before approaching an island. Both map screens
use the stock map difficulty metadata when the local airfield controller has
not been streamed. Difficulty 1 requests zero runways; levels 2 through 4
request one. Confirmed live/saved controller data takes precedence. This rule
does not reveal enemy infrastructure, indicate occupancy or negotiate remote
host support. Markers retain their normal team colour.

This is the checked generation rule of the supported client, not a discovery
mechanic or an assumption that every owned island has an airfield. The package
recipe pins the reviewed executable hash; unsupported builds require review.
`integrations/owned-island-generation.json` records the exact instruction windows
and executable identity used for this check.

## Engine evidence

The reviewed 1.5.18-r2 client calls its generation-options helper at `0x55b6d0`
while generating each island (`0x630aac`). It quantizes the island difficulty
factor against `island_difficulty_min/max`, clamps the result to 1 through 4,
and sets the minimum-runway option at output +0x10 to zero for level 1 or one
for levels 2 through 4. The maximum-runway option at +0x14 is zero; the placement
routine clamps a maximum below the minimum to the minimum. This gives a fixed
requested count rather than a random runway range. Island generation retries
layout placement before accepting its plan.

The stock map's `get_difficulty_level()` uses helper `0x64f200`, with the same
global settings, factor calculation, integer conversion and 1-through-4 clamp.
Consequently its replicated map metadata provides the same decision without
loading terrain, creating a controller or scanning the island. No SDK mutation,
new native hook or flight/body change is needed for this UI policy.

Read-only inspection of the private manual test save also agrees for its two
streamed owned islands: island 39 has one stored airfield, while the starting
island 53 (difficulty factor zero) has none. This is corroboration, not complete
coverage of every map or custom terrain modification. Actual controller counts
remain authoritative if a modified world differs from the standard rule.

## Regression and manual check

`owned_map_knowledge.lua` evaluates the actual owned helper functions from both
screen patches in real Lua. It first reproduced the old unknown-until-approach
failure, then passed for all four levels, unchanged markers after streaming,
remote own-map metadata, foreign-island exclusion, unavailable metadata and
authoritative absence. It runs in the existing actual-SDK Lua regression.

The user confirmed the follow-up manual marker check on 2026-11-04. Distant
owned infrastructure is shown immediately with normal team colour. This
user-reported check does not certify a remote
client or arbitrary custom terrain/engine modifications.
