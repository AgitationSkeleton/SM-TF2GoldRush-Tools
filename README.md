# TF2 Gold Rush Tools

SourceMod support for **Team Fortress 2: Gold Rush** (Steam app 3826520, dedicated
server 4007540, game folder `tf_goldrush`, x64). As far as I know, nobody had made
SourceMod tools for this game before.

This repository contains:

| Part | What it does |
|---|---|
| `gamedata/` | SDKTools and SDKHooks offsets for Gold Rush. Without them `AcceptEntityInput`, `OnPlayerRunCmd`, `GetPlayerWeaponSlot`, `RemovePlayerItem`, `TeleportEntity`, `SetEntityModel`, `ForcePlayerSuicide` and every SDKHooks hook fail silently for **all** plugins on a Gold Rush server, or crash it if TF2's offsets are forced. |
| `tf2goldrush.smx` (library `tf2goldrush`) | SourceMod's **TF2 Tools** natives and forwards (`TF2_*`). SourceMod's own `tf2.ext` does not run on Gold Rush. |
| `tf2attributes_goldrush.smx` (library `tf2attributes`) | The **TF2Attributes** natives (`TF2Attrib_*`). The upstream plugin fails on Gold Rush. |
| Gold Rush controls (in `tf2goldrush.smx`) | Hats, weapons and class 10 (the Civilian): natives, admin commands and cvars. |

The plugins and the gamedata are pure SourcePawn and text: no extensions, and no
DHooks. Everything was verified on a LAN copy of a Gold Rush server (build 2001) with
bots. The self-test passes 33 of 33 checks, and each SDKTools and SDKHooks function
listed below was exercised.

## Install

1. **Gamedata.** Copy `gamedata/sdktools.games/custom/common.games.txt` and
   `gamedata/sdkhooks.games/custom/common.games.txt` into
   `addons/sourcemod/gamedata/` (keep the same sub-paths). Then **restart the server**:
   gamedata is only read when the server starts.
2. **Plugins.** Copy `plugins/tf2goldrush.smx` and `plugins/tf2attributes_goldrush.smx`
   to `addons/sourcemod/plugins/`. Remove any upstream `tf2attributes.smx` (it fails on
   Gold Rush).
3. **Plugins that use `<tf2>`.** Recompile them against `scripting/include/` from this
   repository, placed before SourceMod's include folder:
   `spcomp64 -i path/to/this/scripting/include -i path/to/sourcemod/scripting/include plugin.sp`.
   A plugin compiled with stock `tf2.inc` requires `tf2.ext` and will not load.
   * Plugins compiled against upstream TF2Attributes load as they are.

`sm_tf2goldrush_status` shows the backend state. If the gamedata is missing,
`tf2goldrush.smx` logs an error that says so.

## Why the gamedata is needed, and how it was made

SourceMod picks its offsets by game folder, and it has none for `tf_goldrush`. Gold
Rush's `server.dll` is built from TF2's x64 code. When TF2's offsets were forced onto
it, the first `AcceptEntityInput` crashed the server, because Gold Rush's vtables differ.

The offsets in `gamedata/` were derived from the binary itself, using the tools in
`tools/`. No symbols are needed.

* **`vtable_find.py`** finds the slot of a single function:
  * it locates the function by a string only that function references;
  * it gets the function's exact bounds from the x64 `.pdata` unwind table,
    following MSVC's chained cold-code fragments back to the real function start;
  * it finds the class vtable through MSVC RTTI.
  * Checked on live TF2: `AcceptInput` came out as slot 38, which matches SourceMod's
    gamedata. On Gold Rush it is slot 37.
* **`map_offsets.py`** maps whole vtables:
  * it aligns the `CBaseEntity`, `CTFPlayer` and `CTFWeaponBase` vtables of TF2 and
    Gold Rush by function fingerprints (code bytes with relocations masked);
  * result: Gold Rush has one virtual fewer at TF2 slot 13, so every offset from slot
    14 up is exactly one lower, in all three classes. This agrees with the
    independently found `AcceptInput`.
* **`gen_gamedata.py`** writes the custom gamedata from SourceMod's own TF2 files using
  that rule:
  * offsets that are not vtable slots (SDKHooks `EntityListeners`) are copied unchanged;
  * signatures are omitted, because TF2's patterns do not match this binary. This only
    affects `HookEntityOutput` (FireOutput), which still does not work.

The files go in `custom/` folders, so SourceMod updates do not overwrite them.

**Verified on Gold Rush:**

* SDKTools: `AcceptEntityInput`, `TeleportEntity`, `GetClientEyeAngles`,
  `GetPlayerWeaponSlot`, `SetEntityModel`, `RemovePlayerItem`, `IgniteEntity`,
  `ExtinguishEntity`, `ForcePlayerSuicide` and `EquipPlayerWeapon`.
* SDKHooks callbacks: `OnPlayerRunCmd`, `OnTakeDamage`, `PreThink`, `PostThink`,
  `StartTouch`, `WeaponSwitch` and `Spawn`. They are all called, with no crashes
  (`scripting/tests/tf2gr_gamedata_test.sp`).

## TF2 Tools and TF2Attributes

These work the same way as
[SM-TF2Classified-Tools](https://github.com/AgitationSkeleton/SM-TF2Classified-Tools).
Every native calls Gold Rush's own VScript API (`AddCondEx`, `StunPlayer`,
`ForceRespawn`, `AddAttribute`, `AddCustomAttribute` and so on) through the
`RunScriptCode` input.

Values come back through a private entity's targetname. Gold Rush's
`Convars.SetValue` only accepts convars listed in `cfg/vscript_convar_allowlist.txt`,
so a convar return channel would need server config edits.

| | Status |
|---|---|
| `TF2_AddCondition` / `RemoveCondition` / `IgnitePlayer` / `MakeBleed` / `StunPlayer` / `RespawnPlayer` / `RegeneratePlayer` / `SetPlayerPowerPlay` / `RemovePlayerDisguise` / `GetClass` / `GetResourceEntity` / `IsHolidayActive` / `RemoveWearable` | ✔ |
| `TF2_DisguisePlayer` | ✔ for a living Spy (issues his `disguise` command) |
| `TF2_OnConditionAdded` / `Removed`, `TF2_OnWaitingForPlayersStart` / `End` | ✔ (conditions by polling the networked bits) |
| `TF2_CalcIsAttackCritical`, `TF2_OnPlayerTeleport`, `TF2_OnGetHoliday`, `TF2_OnIsHolidayActive` | ✘ never fire |
| TF2Attributes: set / get / remove by name or index, values, tokens, static (schema) attributes, custom player attributes | ✔ |
| `HookValue*`, `UnsafeGetStringValue`, refundable currency, `GetSOCAttribs` | not supported |

As with TF2 Classified, the `Address` values these natives return are tokens: they work
with the `TF2Attrib_*` getters and setters but cannot be dereferenced.

## Gold Rush controls: hats, weapons, class 10

**Class 10** is old TF2's leftover Civilian:

* It uses the Scout model and the class script gives 200 health and 250 speed.
* No item lists the class, so it has no weapons.
* It cannot be picked in the class menu ("invalid class name").
* The plugin sets it the way TF2's respawn code applies a class: it writes the desired
  class and respawns the player.
* A class set this way is pinned. It is re-applied on spawn, because bots otherwise
  re-pick their class, until the player chooses a class from the menu.

**Natives** (`scripting/include/tf2.inc`):

* `TF2GR_SetPlayerClass(client, TFClassType, respawn)`, including `TFClass_Civilian`
* `TF2GR_GenerateAndWearItem(client, name)` and `TF2GR_GiveItemByDef(client, defindex)`
* `TF2GR_RemoveWearables(client)`
* `TF2GR_GetItemName(defindex, buffer, maxlen)`
* `TF2GR_RemoveAllItems`, `TF2GR_RunScript`, `TF2GR_Get/SetCondDuration`, `TF2GR_IsReady`

**Commands:**

| Command | Access | What it does |
|---|---|---|
| `sm_gr_class <target> <class\|civilian>` | slay | set a class; Civilian allowed |
| `sm_gr_give <target> <item name\|defindex>` | cheats | give a weapon or hat |
| `sm_gr_strip <target> <hats\|weapons\|all>` | cheats | remove hats and/or weapons |
| `sm_gr_items [filter]` | generic | list the schema items (defindex and name) |
| `sm_civilian` / `joinclass civilian` | everyone, when enabled | become the Civilian |
| `sm_givemeall`, `sm_unlocks` | everyone, when enabled | unlock and pick Gold Rush's unlock weapons (server-side) |
| `sm_fov <value>` | everyone | unrestricted FOV that persists |
| `sm_autoreload`, `cl_autoreload <0\|1>` | everyone, when allowed | live TF2's autoreload, per player |

Weapons for a Civilian are built and equipped directly (`EquipPlayerWeapon`), because
the schema's item system refuses items for a class none of them list.

**Cvars** (written to `cfg/sourcemod/tf2goldrush.cfg`):

| Cvar | Default | Meaning |
|---|---|---|
| `tf2goldrush_civilian_enable` | 0 | let players become the Civilian with `!civilian` or `joinclass civilian` |
| `tf2goldrush_civilian_launch_stats` | 1 | launch TF2's Civilian stats, 50 health and 240 speed (from launch TF2's `civilian.ctx`), instead of the class script's 200 / 250 |
| `tf2goldrush_civilian_health` / `_speed` | 0 | explicit overrides (0 = launch or class script value) |
| `tf2goldrush_civilian_bot_chance` | 0 | chance (0-1) that a bot plays the Civilian, rolled once per connection |
| `tf2goldrush_civilian_model` | "" | Civilian player model (empty = the Scout model) |
| `tf2goldrush_civilian_loadout` | "" | comma list of item names or defindices given on spawn (empty = none, as in old TF2) |
| `tf2goldrush_whitelist` / `_whitelist_file` | 0 / `configs/tf2goldrush_whitelist.txt` | only whitelisted weapons are allowed (one defindex or name per line; stock weapons always allowed); others become the slot's stock weapon |
| `tf2goldrush_force_stock` | 0 | every weapon becomes its slot's stock weapon |
| `tf2goldrush_givemeall` | 0 | players may use `!givemeall` (see below) |
| `tf2goldrush_hats` | 1 | 0 = remove everyone's cosmetics (head and misc items) on spawn; wearable weapons such as the Gunboats are kept |
| `tf2goldrush_headgear` | 1 | 0 = remove everyone's headgear (head-slot items) on spawn |
| `tf2goldrush_bot_hats` | 0 | chance that a bot gets a random hat its class may wear |
| `tf2goldrush_fov_enable` / `_fov_min` / `_fov_max` | 1 / 75 / 130 | `!fov` limits |
| `tf2goldrush_autoreload` | 1 | players may opt in to autoreload |

The rules (whitelist, force stock, unlock picks, cosmetics, Civilian stats and loadout) run
on spawn, on inventory updates, and after touching a resupply locker. Gold Rush's
`Regenerate` does not always fire the inventory event, so the plugin does not rely on it.

### Unlocks: `!givemeall`

Gold Rush ties some weapons to achievements. `!givemeall` does **not** touch achievements:
awarding Steam achievements from a server would be achievement manipulation. Instead it
unlocks every Gold Rush weapon for that player on this server.

* `!unlocks` opens a menu per slot for the player's current class.
* Picks are saved in client cookies and equipped on every spawn or resupply.
* The weapon whitelist and force-stock rules still apply to picks.
* Nothing changes on the player's Steam account, and no client file is modified.

### Autoreload: `!autoreload` / `cl_autoreload 1`

This recreates live TF2's per-player autoreload server-side. Live TF2
(`CTFWeaponBase::ItemPostFrame`) presses the reload key for the player when all of these hold:

* the active weapon uses clips;
* the clip is not full;
* there is reserve ammo;
* attack and alt-attack are not held.

The plugin does the same in `OnPlayerRunCmd`. The game's own reload code therefore gives the
same timing, and firing cancels a reload the same way, shell by shell for single-reload weapons.

Players opt in with `!autoreload`, or by typing `cl_autoreload 1`: Gold Rush's client forwards
the unknown command to the server. A client `cl_autoreload` userinfo value is honoured if the
client sends one. The choice is saved in a cookie.

`tf2goldrush_autoreload` (default 1) allows the feature; players are off until they opt in.
`sm_gr_autoreload_stats` counts the reload presses made for players (diagnostics).

### Unrestricted FOV: `!fov <value>`

This replaces `unrestricted_fov.smx`. `!fov <75-130>` (0 = back to `fov_desired`) sets
`m_iDefaultFOV`/`m_iFOV`, and the value is saved in a cookie.

The game resets the FOV on unscoping, teleporting, respawning, resupply and taunts, where
older plugins lose it. Here a 0.2 s check re-applies it whenever it has drifted and the
player is not zoomed in, so the scope's own zoom is never overridden.

Admin and test helpers: `sm_gr_setpick <target> <slot> <defindex|0>` (sets a pick and
implies `!givemeall`), `sm_gr_setfov <target> <value>`, `sm_gr_whitelist_reload`.

## Requirements

* TF2: Gold Rush dedicated server (x64, tested on build 2001)
* SourceMod 1.12+ (tested on 1.13.0.7279), Metamod:Source 2.0

## License

GPL-3.0. The include files derive from SourceMod (GPL-3.0) and TF2Attributes. The
gamedata derives from SourceMod's TF2 gamedata.
