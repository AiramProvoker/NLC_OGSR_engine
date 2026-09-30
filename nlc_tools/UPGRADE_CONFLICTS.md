# Upgrade conflict report: `nlc` + upstream OGSR `main`

Merge: branch `nlc-upgrade` (from `nlc` `96bc451`) + upstream `main`
`2021123275064886770edda017a8e904af0c84b8` (2026-09-27, "Доработка луж"),
103 commits after tag `3.525`. Written before resolution (2026-09-30).

Overlap: 42 files under `ogsr_engine/` are changed on both sides
(`3.525..upstream/main` and `3.525..nlc`). Git auto-merged 36 of them; 6 have
textual conflicts. The auto-merged ones were reviewed hunk by hunk where the
two sides' hunks lie within 15 lines of each other.

## Textual conflicts

| File | Upstream intent | NLC intent | Resolution |
|---|---|---|---|
| `COMMON_AI/script_ini_file_script.cpp` | `078693a`: new `ini:iterate_lines`; `reload_system_ini` made `static` | `ini:release()` export; `reload_system_ini` non-static | Keep both exports (`release`, `iterate_lines`); take `static` only if nothing else links `reload_system_ini` |
| `xrCore/xr_ini.h` | `9c33d09`: `CInifile` is `final`, non-virtual members | adds `virtual void release()` (used by gamedata `ini:release()`) | Upstream declarations plus non-virtual `void release();` |
| `xrCore/Xr_ini.cpp` | `insert_item` made `static` with the new `Sect::Ordered_Data` item type | adds `CInifile::release()` | Keep `release()` (body mirrors the upstream destructor minus save), take upstream `insert_item` |
| `xrGame/Actor_Flags.h` | `513095b`: `AF_THROW_DEBUG` moved to bit 23, new `AF_WEAPON_BOBBING` bit 24 | `AF_SHOW_DYN_DESC_WND` bit 23, `AF_HIDE_UNTRADABLE_ITEMS` bit 24, `AF_THROW_DEBUG` bit 25 | Keep NLC bits 23-25, `AF_WEAPON_BOBBING` = bit 26 (flags persist by command name, not by bit) |
| `xr_3da/Device_Initialize.cpp` | `75ab8d5` (Sync with CoP): `CreateWindowEx(WS_EX_TOPMOST, ...)` | window title "NLC Improved" | Upstream call with the NLC title |
| `xr_3da/defines.cpp` | `1b811fd`: `rsExclusiveMode` no longer on by default | adds `int ps_wm_rotfix = 1;` | Upstream default flags plus `ps_wm_rotfix` |

## Behaviour clashes in auto-merged code (user decision)

Decisions (user, 2026-09-30): 1 = take upstream `user_ogsr.ltx`; 2 = take
upstream default (bobbing on); 3 = keep NLC: `GiveGameTaskToActor` now also
honours `dont_switch_active_task_by_prio`, and NLC gamedata
`config/external.ltx` sets it to `true`.

1. **`user.ltx` renamed** (`94a2cca`, `xrCore/LocatorAPI_defs.h`
   `fsgame::user_ltx = "user_ogsr.ltx"`). The NLC install keeps its settings in
   `appdata/user.ltx`; after the upgrade every player would start with default
   settings and a new `user_ogsr.ltx`. Proposal: keep `"user.ltx"` (one-line NLC
   edit of the constant).
2. **Weapon bobbing.** Upstream replaced the core feature `wpn_bobbing`
   (NLC `gamedata/config/external.ltx`: `wpn_bobbing = false`) with the console
   flag `g_weapon_bobbing` (`AF_WEAPON_BOBBING`), which is on by default in
   `psActorFlags`. After the upgrade NLC would bob unless the player turns it
   off. Proposal: drop `AF_WEAPON_BOBBING` from the NLC default flags so the
   default stays off; the console command and any menu row still work.
3. **Active task selection** (`8632435`, `GametaskManager.cpp`). Upstream
   again makes the highest-priority task active: `GiveGameTaskToActor` activates
   a new task whose priority beats the active one (not gated), and
   `UpdateTasks` picks the highest-priority task unless the new feature
   `dont_switch_active_task_by_prio` is set. NLC does not set that feature. The
   NLC edits in this file (no `eTaskStateSkiped` handling) merged cleanly.
   Options: take upstream behaviour, or keep the 3.525/NLC behaviour by gating
   the `GiveGameTaskToActor` branch with the same feature and setting
   `dont_switch_active_task_by_prio = true` in gamedata.

## Auto-merged overlaps checked, no action

- `Weapon.cpp`, `WeaponMagazined.cpp`, `WeaponKnife.cpp` (`4cbbd55`): NPC
  weapons skip `UpdateCL` while strapped or hidden; actor path unchanged, NLC
  fire/reload/alt-aim/drum-anim code intact. Addon bone visibility now guards
  `BI_NONE`. `hit_probability()` returns by value and honours
  `features.fixed_hit_probability` (default off = old behaviour).
- `player_hud.cpp`: `Render->hud_loading` became `shader_option_hud_loading()`;
  no NLC code uses the old member.
- `GametaskManager.cpp`: see clash 3.
- `console_commands.cpp`: upstream language switching rework (`a767bb6`),
  `give_info_portion`/`disable_info_portion`, `g_weapon_bobbing`,
  `time_factor` no longer saved, `g_console_show_always` moved to `xr_3da`. NLC
  commands (`g_loadstring`, `g_show_dyn_desc_wnd`, `g_hide_untradable_items`,
  `g_exo_snd_vol`, `hud_show_status_icons`, `fl_hook` registration) intact.
- `xrCore/arc_sqfs.cpp`: upstream loop cleanup in `index_dir_sqfs`; NLC
  `sqfs_path()` and chunked `read_sqfs` intact.
- `xrCore/xrCore.h`: `wpn_bobbing` feature bit freed, new
  `dont_switch_active_task_by_prio` (bit 32); NLC
  `use_all_available_slots_on_take` (bit 44) intact.
- `xrCore/xrCore.cpp`: upstream static-only cleanup; NLC version string to be
  replaced in step 6 of the handoff.
- `xr_3da/x_ray.cpp`, `xr_ioc_cmd.cpp`, `defines.h`: `rs_fullscreen`
  registered again (borderless), `load_draw_internal` sets up the display
  backbuffer, feature list updated.
- Render (`r__dsgraph_build.cpp`, `dx10ResourceManager_Scripting.cpp`,
  `r4_rendertarget_phase_combine.cpp`, `ResourceManager.*`,
  `xrRender_console.cpp`): NLC `scope_pass`, `CreateTexture`, ambient
  `min_lumscale_amb` intact beside the upstream upscaler/render-domain work.
- `trade2.cpp`/`trade.h`, `UICarBodyWnd.h`, `script_game_object_script3.cpp`,
  `level_script.cpp`, `InventoryOwner.*`, `script_game_object_inventory_owner.cpp`:
  upstream const/initializer fixes and new exports next to NLC exports.

Remaining semantic breakage (API changes in NLC-only files) is left to the
compiler.
