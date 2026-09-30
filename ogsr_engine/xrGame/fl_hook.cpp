#include "stdafx.h"
#include "fl_hook.h"

#include "../xr_3da/XR_IOConsole.h"
#include "../xr_3da/xr_ioc_cmd.h"
#include "../xr_3da/GameMtlLib.h"
#include "../Include/xrRender/Kinematics.h"
#include "../Include/xrRender/KinematicsAnimated.h"
#include "../Include/xrRender/RenderVisual.h"
#include "../Include/xrRender/WallMarkArray.h"
#include "../Include/xrRender/RenderFactory.h"
#include "../Include/xrRender/RenderDeviceRender.h"
#include "ai_space.h"
#include "script_engine.h"
#include "script_game_object.h"
#include "Actor.h"
#include "Actor_Flags.h"
#include "Inventory.h"
#include "inventory_item.h"
#include "player_hud.h"
#include "HudItem.h"
#include "Weapon.h"
#include "Level.h"
#include "HUDManager.h"
#include "UI.h"
#include "UIGameSP.h"
#include "ui/UIPdaWnd.h"
#include "ui/UIInventoryWnd.h"
#include "PDA.h"
#include "entity_alive.h"
#include "material_manager.h"
#include "ai/stalker/ai_stalker.h"
#include "cover_evaluators.h"
#include "cover_manager.h"
#include "cover_point.h"

extern CAI_Space* g_ai_space;
extern CInifile* reload_system_ini();

namespace fl_hook
{
bool bp_ui{};
bool blr_on{};
bool torch_lua{true};
bool pda3d_hack{};
bool pda3d_nosign{};
bool pda3d_freeze{};
bool pda3d_trace{};
bool pda3d_vp2_pass{};

u32 skip_body_n{};
u32 skip_hands_n{};
u32 torch_blocked_n{};
u32 blr_added{};

float boar_last_dist{-1.f};
float boar_last_vel{-1.f};

static lua_State* vm()
{
    if (!g_ai_space)
        return nullptr;
    return ai().script_engine().lua();
}

static bool has_level() { return g_pGameLevel != nullptr; }

// ---------------------------------------------------------------------------
// fl: run Lua from the console
// ---------------------------------------------------------------------------

static const char BOOT[] = "local a=... local i=a:match(\"^%s*([%w_.]+)%s*$\") "
                           "if i then local b=i:gsub(\"%.script$\",\"\") "
                           "local m,f=b:match(\"^([%w_]+)%.([%w_]+)$\") "
                           "if f then _G[m][f]() else return _G[b] end "
                           "else assert(loadstring(a))() end";

static void run_fl(LPCSTR args)
{
    lua_State* L = vm();
    if (!L)
        return;
    const int top = lua_gettop(L);
    int rc = luaL_loadbuffer(L, BOOT, sizeof(BOOT) - 1, "@fl");
    if (!rc)
    {
        lua_pushstring(L, args);
        rc = lua_pcall(L, 1, 0, 0);
    }
    if (rc)
        CScriptEngine::print_output(L, "@fl", rc);
    lua_settop(L, top);
}

// ---------------------------------------------------------------------------
// rl: reload named script modules / system ini / ui
// ---------------------------------------------------------------------------

static const char WRAP_PRE[] = "local function script_name() return '";
static const char WRAP_MID[] = "' end; local this; module('";
static const char WRAP_POST[] = "', package.seeall, function(m) this = m end); ";

static constexpr int RL_MAX_MODS = 48;

struct WantMods
{
    string64 name[RL_MAX_MODS];
    bool found[RL_MAX_MODS];
    int n;
};

static bool basename_mod(LPCSTR path_or_name, char* out, int outcap)
{
    if (!path_or_name || !*path_or_name || outcap < 2)
        return false;
    LPCSTR last = path_or_name;
    for (LPCSTR p = path_or_name; *p; ++p)
        if (*p == '/' || *p == '\\')
            last = p + 1;
    int n = 0;
    for (LPCSTR p = last; *p && *p != '.' && n + 1 < outcap; ++p)
    {
        char c = *p;
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            break;
        out[n++] = c;
    }
    out[n] = 0;
    return n > 0;
}

static bool has_script_ext(LPCSTR name)
{
    const size_t n = xr_strlen(name);
    return n >= 7 && !_stricmp(name + n - 7, ".script");
}

static void ensure_slash(char* path, size_t cap)
{
    const size_t n = xr_strlen(path);
    if (!n || n + 1 >= cap)
        return;
    if (path[n - 1] == '\\' || path[n - 1] == '/')
        return;
    path[n] = '\\';
    path[n + 1] = 0;
}

static int want_index(WantMods& w, LPCSTR mod)
{
    for (int i = 0; i < w.n; ++i)
        if (!_stricmp(w.name[i], mod))
            return i;
    return -1;
}

static bool reload_script_file_safe(lua_State* L, LPCSTR full_path, LPCSTR mod)
{
    IReader* r = FS.r_open(full_path);
    if (!r)
    {
        Msg("!! [rl] cannot open [%s]", full_path);
        return false;
    }

    LPCSTR body = (LPCSTR)r->pointer();
    size_t body_sz = r->elapsed();
    if (body_sz >= 3 && (u8)body[0] == 0xEF && (u8)body[1] == 0xBB && (u8)body[2] == 0xBF)
    {
        body += 3;
        body_sz -= 3;
    }

    xr_string buf;
    buf.reserve(body_sz + 256);
    buf.append(WRAP_PRE).append(mod).append(WRAP_MID).append(mod).append(WRAP_POST).append(body, body_sz);
    FS.r_close(r);

    string_path cname;
    xr_strconcat(cname, "@", full_path);

    const int top = lua_gettop(L);
    int rc = luaL_loadbuffer(L, buf.c_str(), buf.size(), cname);
    if (!rc)
        rc = lua_pcall(L, 0, 0, 0);
    if (rc)
        CScriptEngine::print_output(L, cname, rc);
    lua_settop(L, top);
    return rc == 0;
}

static void walk_reload_wanted(lua_State* L, LPCSTR path, WantMods& want, int& ok_n, int& fail_n)
{
    if (auto files = FS.file_list_open(path, FS_ListFiles))
    {
        for (LPCSTR name : *files)
        {
            if (!name || !has_script_ext(name))
                continue;
            string_path mod;
            if (!basename_mod(name, mod, sizeof(mod)))
                continue;
            const int wi = want_index(want, mod);
            if (wi < 0)
                continue;

            string_path full;
            xr_strconcat(full, path, name);
            Msg("~ [rl] reload [%s]", full);
            if (reload_script_file_safe(L, full, mod))
            {
                want.found[wi] = true;
                ++ok_n;
            }
            else
                ++fail_n;
        }
        FS.file_list_close(files);
    }

    if (auto dirs = FS.file_list_open(path, FS_ListFolders))
    {
        for (LPCSTR name : *dirs)
        {
            if (!name || !xr_strcmp(name, ".") || !xr_strcmp(name, ".."))
                continue;
            string_path sub;
            xr_strconcat(sub, path, name);
            ensure_slash(sub, sizeof(sub));
            walk_reload_wanted(L, sub, want, ok_n, fail_n);
        }
        FS.file_list_close(dirs);
    }
}

static void cmd_rl_scripts(LPCSTR args)
{
    while (*args == ' ' || *args == '\t')
        ++args;
    if (!*args)
    {
        Msg("~ [rl] usage: rl scripts <mod> [mod2 ...]");
        Msg("~ [rl] example: rl scripts thirst");
        Msg("~ [rl] example: rl scripts thirst nlc_corpse_decay kotovod");
        Msg("~ [rl] NOTE: full dump of all scripts is unsafe mid-game (breaks db/schemes)");
        return;
    }

    WantMods want{};
    while (*args && want.n < RL_MAX_MODS)
    {
        while (*args == ' ' || *args == '\t')
            ++args;
        if (!*args)
            break;
        string128 raw;
        int i = 0;
        while (*args && *args != ' ' && *args != '\t' && i + 1 < (int)sizeof(raw))
            raw[i++] = *args++;
        raw[i] = 0;
        if (!basename_mod(raw, want.name[want.n], sizeof(want.name[0])))
            continue;
        want.found[want.n] = false;
        ++want.n;
    }
    if (!want.n)
    {
        Msg("!! [rl] no valid module names");
        return;
    }

    lua_State* L = vm();
    if (!L)
    {
        Msg("!! [rl] no lua / FS (need loaded game)");
        return;
    }
    string_path root;
    FS.update_path(root, "$game_scripts$", "");
    ensure_slash(root, sizeof(root));

    Msg("~ [rl] reloading %d module(s)", want.n);
    int ok_n = 0, fail_n = 0;
    walk_reload_wanted(L, root, want, ok_n, fail_n);

    for (int i = 0; i < want.n; ++i)
    {
        if (!want.found[i])
        {
            Msg("!! [rl] not found: [%s].script", want.name[i]);
            ++fail_n;
        }
    }
    Msg("~ [rl] scripts done: ok=%d fail=%d", ok_n, fail_n);
}

static void cmd_rl_ui()
{
    Console->ExecuteCommand("ui_reload", false, true);
    Msg("~ [rl] ui_reload");
}

static void cmd_rl_configs()
{
    if (reload_system_ini())
        Msg("~ [rl] reload_system_ini OK (pSettings rebuilt)");
    else
        Msg("!! [rl] reload_system_ini failed");
    Msg("~ [rl] tip: UI XML -> rl ui ; close trade before reload if open");
}

static void cmd_rl_help()
{
    Msg("~ [rl] usage:");
    Msg("~ [rl]   rl scripts <mod> [mod2 ...]  - re-exec named .script modules");
    Msg("~ [rl]   rl configs                   - reload_system_ini only");
    Msg("~ [rl]   rl ui                        - ui_reload (HUD/XML)");
    Msg("~ [rl]   rl all                       - same as rl configs");
    Msg("~ [rl] example: rl scripts thirst");
}

static void run_rl(LPCSTR args)
{
    while (*args == ' ' || *args == '\t')
        ++args;
    if (!*args)
    {
        cmd_rl_help();
        return;
    }
    string64 tok;
    int i = 0;
    while (*args && *args != ' ' && *args != '\t' && i + 1 < (int)sizeof(tok))
        tok[i++] = *args++;
    tok[i] = 0;

    if (!_stricmp(tok, "all"))
    {
        Msg("~ [rl] all = configs only (no scripts bulk, no ui)");
        cmd_rl_configs();
    }
    else if (!_stricmp(tok, "scripts") || !_stricmp(tok, "script") || !_stricmp(tok, "s"))
        cmd_rl_scripts(args);
    else if (!_stricmp(tok, "configs") || !_stricmp(tok, "config") || !_stricmp(tok, "cfg") || !_stricmp(tok, "c"))
        cmd_rl_configs();
    else if (!_stricmp(tok, "ui") || !_stricmp(tok, "hud"))
        cmd_rl_ui();
    else if (!_stricmp(tok, "help") || !_stricmp(tok, "?"))
        cmd_rl_help();
    else
    {
        xr_string buf{tok};
        buf += args;
        cmd_rl_scripts(buf.c_str());
    }
}

// ---------------------------------------------------------------------------
// escape_bridge_zone: engine-level exception for the two bridge NPCs
// ---------------------------------------------------------------------------

static constexpr LPCSTR BRZ_NPC[] = {
    "esc_bridge_soldier5", // Kuznetsov
    "esc_soldier_commander_sub", // Kovalchuk
};
static constexpr LPCSTR BRZ_ZONE = "escape_bridge_zone";
static constexpr int BRZ_IDS_MAX = 8;

static ALife::_OBJECT_ID brz_ids[BRZ_IDS_MAX];
static bool brz_logged[BRZ_IDS_MAX];
static int brz_n{};
static int brz_pending{};
static bool brz_reached{};
static shared_str brz_last_src, brz_last_dst;

static bool brz_name_match(LPCSTR name)
{
    if (!name)
        return false;
    for (LPCSTR npc : BRZ_NPC)
        if (!_stricmp(name, npc))
            return true;
    return false;
}

static CObject* level_object(ALife::_OBJECT_ID id)
{
    if (id == ALife::_OBJECT_ID(-1) || !has_level())
        return nullptr;
    return Level().Objects.net_Find(id);
}

static int brz_slot(ALife::_OBJECT_ID id)
{
    for (int i = 0; i < brz_n; ++i)
        if (brz_ids[i] == id)
            return i;
    return -1;
}

static void brz_remember(ALife::_OBJECT_ID id)
{
    if (brz_slot(id) >= 0 || brz_n >= BRZ_IDS_MAX)
        return;
    brz_logged[brz_n] = false;
    brz_ids[brz_n++] = id;
}

static bool brz_is_exempt(ALife::_OBJECT_ID id)
{
    if (brz_pending)
        return true;
    if (CObject* obj = level_object(id))
        return brz_name_match(obj->cName().c_str());
    return brz_slot(id) >= 0;
}

bool brz_net_spawn_begin(CSE_Abstract* data)
{
    if (!data)
        return false;
    LPCSTR nm = data->name_replace();
    if (!nm || !*nm)
        nm = data->s_name.c_str();
    if (!brz_name_match(nm))
        return false;
    ++brz_pending;
    Msg("~ [brz] tracking [%s]", nm);
    return true;
}

void brz_net_spawn_end(bool tracked)
{
    if (tracked && brz_pending > 0)
        --brz_pending;
}

shared_str brz_default_in(ALife::_OBJECT_ID id, const shared_str& default_in)
{
    if (!brz_is_exempt(id))
        return default_in;

    string4096 buf;
    int o = 0;
    bool removed = false;
    LPCSTR s = default_in.c_str();
    for (LPCSTR p = s ? s : ""; *p;)
    {
        while (*p == ',' || *p == ' ' || *p == '\t')
            ++p;
        if (!*p)
            break;
        LPCSTR b = p;
        while (*p && *p != ',')
            ++p;
        LPCSTR e = p;
        while (e > b && (e[-1] == ' ' || e[-1] == '\t'))
            --e;
        const int n = int(e - b);
        if (n <= 0)
            continue;
        if (n == (int)xr_strlen(BRZ_ZONE) && !_strnicmp(b, BRZ_ZONE, n))
        {
            removed = true;
            continue;
        }
        if (o && o + 1 < (int)sizeof(buf))
            buf[o++] = ',';
        for (int i = 0; i < n && o + 1 < (int)sizeof(buf); ++i)
            buf[o++] = b[i];
    }
    buf[o] = 0;

    brz_reached = true;
    brz_last_src = default_in;
    if (!removed)
    {
        brz_last_dst = default_in;
        return default_in;
    }

    const shared_str result = o ? shared_str(buf) : shared_str();
    brz_last_dst = result;

    if (brz_pending)
        brz_remember(id);
    const int i = brz_slot(id);
    if (i >= 0 && !brz_logged[i])
    {
        brz_logged[i] = true;
        CObject* obj = level_object(id);
        Msg("~ [brz] escape_bridge_zone dropped for [%s]", obj ? obj->cName().c_str() : "?");
    }
    return result;
}

static void cmd_brz()
{
    Msg("~ [brz] zone [%s]", BRZ_ZONE);
    for (LPCSTR npc : BRZ_NPC)
        Msg("~ [brz] npc  [%s]", npc);
    if (!brz_n)
        Msg("~ [brz] no tracked object online yet");
    else
    {
        for (int i = 0; i < brz_n; ++i)
        {
            CObject* obj = level_object(brz_ids[i]);
            Msg("~ [brz] id=%d applied=%d", (int)brz_ids[i], (int)brz_logged[i]);
            Msg("~ [brz]   name [%s]", obj ? obj->cName().c_str() : "(offline)");
        }
    }
    if (brz_reached)
    {
        Msg("~ [brz] default_in      [%s]", brz_last_src.c_str() ? brz_last_src.c_str() : "");
        Msg("~ [brz] default_in(brz) [%s]", brz_last_dst.c_str() ? brz_last_dst.c_str() : "");
    }
    else
        Msg("~ [brz] restrict() not reached for a tracked object yet");
}

// ---------------------------------------------------------------------------
// boar run-attack diagnostics
// ---------------------------------------------------------------------------

static int boar_ok{};
static int boar_fail{};
static EBoarReason boar_last_reason{eBoarOk};

void boar_result(EBoarReason reason)
{
    boar_last_reason = reason;
    if (reason == eBoarOk)
        ++boar_ok;
    else
        ++boar_fail;
}

static void cmd_boar()
{
    Msg("~ [boar] ok=%d fail=%d", boar_ok, boar_fail);
    LPCSTR why = "ok";
    switch (boar_last_reason)
    {
    case eBoarActive: why = "active"; break;
    case eBoarNoObject: why = "no_object"; break;
    case eBoarNoEnemy: why = "no_enemy"; break;
    case eBoarDistance: why = "distance"; break;
    case eBoarStanding: why = "standing"; break;
    case eBoarCooldown: why = "cooldown"; break;
    case eBoarNotFacing: why = "not_facing"; break;
    default: break;
    }
    Msg("~ [boar] last_reason [%s]", why);
    Msg("~ [boar] last_dist_x100=%d last_vel_x100=%d", (int)(boar_last_dist * 100.f), (int)(boar_last_vel * 100.f));
}

// ---------------------------------------------------------------------------
// xrs_battle_ai: close / far cover and killer id on top of ambush_cover.
// The mode rides in a negative radius: -(1000 * mode + radius).
// ---------------------------------------------------------------------------

static constexpr int COVER_MODE_CLOSE = 1;
static constexpr int COVER_MODE_FAR = 2;
static constexpr int COVER_MODE_KILLER_ID = 3;

static void bai_set_ret(lua_Integer v)
{
    lua_State* L = vm();
    if (!L)
        return;
    lua_pushinteger(L, v);
    lua_setfield(L, LUA_GLOBALSINDEX, "bai_ret");
}

const CCoverPoint* ambush_cover_ex(CScriptGameObject* self, const Fvector& position, const Fvector& enemy_position, float radius, float min_distance,
                                   const luabind::functor<bool>& callback)
{
    const float packed = -radius;
    const int mode = int(packed / 1000.0f);
    const float r = packed - float(mode) * 1000.0f;
    if (r <= 0.0f)
        return nullptr;

    if (mode == COVER_MODE_KILLER_ID)
    {
        const CEntityAlive* alive = smart_cast<const CEntityAlive*>(&self->object());
        bai_set_ret(alive ? lua_Integer(alive->killer_id()) : -1);
        return nullptr;
    }

    if (mode != COVER_MODE_CLOSE && mode != COVER_MODE_FAR)
        return nullptr;
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(&self->object());
    if (!stalker)
        return nullptr;

    xr_vector<const CCoverPoint*> covers;
    const auto collect = [&](const CCoverPoint* point) -> bool {
        covers.push_back(point);
        return true;
    };

    if (mode == COVER_MODE_CLOSE)
    {
        stalker->m_ce_close->setup(enemy_position, min_distance, 300.f, 10.f, collect);
        ai().cover_manager().best_cover(position, r, *stalker->m_ce_close);
    }
    else
    {
        stalker->m_ce_far->setup(enemy_position, min_distance, 300.f, 10.f, collect);
        ai().cover_manager().best_cover(position, r, *stalker->m_ce_far);
    }

    for (int i = int(covers.size()) - 1; i >= 0; --i)
    {
        const CCoverPoint* p = covers[i];
        if (callback(p))
            return p;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// actor weapon before-fire callback
// ---------------------------------------------------------------------------

static constexpr LPCSTR WBF_NAME = "fl_on_actor_weapon_before_fire";

static int wbf_calls{}, wbf_no_vm{}, wbf_no_cb{}, wbf_err{}, wbf_skip_np{}, wbf_skip_mf{}, wbf_last_gl{-1};
static bool wbf_busy{};

static void wbf_call(int is_gl)
{
    if (wbf_busy)
        return;
    lua_State* L = vm();
    if (!L)
    {
        ++wbf_no_vm;
        return;
    }

    wbf_busy = true;
    const int top = lua_gettop(L);
    lua_pushstring(L, WBF_NAME);
    lua_rawget(L, LUA_GLOBALSINDEX);
    if (lua_type(L, -1) != LUA_TFUNCTION)
        ++wbf_no_cb;
    else
    {
        lua_pushinteger(L, is_gl);
        if (lua_pcall(L, 1, 0, 0))
        {
            if (++wbf_err <= 3)
            {
                LPCSTR e = lua_tostring(L, -1);
                Msg("!! [wbf] %s: %s", WBF_NAME, e ? e : "?");
            }
        }
        else
            ++wbf_calls;
    }
    lua_settop(L, top);
    wbf_last_gl = is_gl;
    wbf_busy = false;
}

void weapon_before_fire(CWeapon* weapon, bool is_gl)
{
    if (!weapon || !g_actor || weapon->H_Parent() != static_cast<CObject*>(g_actor))
    {
        if (!is_gl)
            ++wbf_skip_np;
        return;
    }
    if (!is_gl && weapon->IsMisfire())
    {
        ++wbf_skip_mf;
        return;
    }
    wbf_call(is_gl ? 1 : 0);
}

static void cmd_wbf()
{
    Msg("~ [wbf] name=%s hooks: native", WBF_NAME);
    Msg("~ [wbf] calls=%d no_vm=%d no_callback=%d lua_errors=%d", wbf_calls, wbf_no_vm, wbf_no_cb, wbf_err);
    Msg("~ [wbf] skipped_not_actor=%d skipped_misfire=%d last_gl=%d", wbf_skip_np, wbf_skip_mf, wbf_last_gl);
}

// ---------------------------------------------------------------------------
// transient HUD recoil of the weapon model
// ---------------------------------------------------------------------------

static constexpr float HUDRC_MAX_POS = 0.05f; // metres
static constexpr float HUDRC_MAX_ROT = 0.25f; // radians
static constexpr u32 HUDRC_WATCHDOG = 250; // ms of game time before the contribution expires

static bool hudrc_on{};
static float hudrc_pos[3]{};
static float hudrc_rot[3]{};
static CHudItem* hudrc_owner{};
static u32 hudrc_stamp{};
static int hudrc_frames{}, hudrc_clears{}, hudrc_expired{}, hudrc_rej_nohud{}, hudrc_rej_item{}, hudrc_rej_bad{}, hudrc_rej_lock{};
static bool hudrc_lock{};
static float hudrc_saved[16];
static float* hudrc_saved_at{};

static bool hudrc_finite(float v) { return v == v && (v - v) == 0.f; }

static float hudrc_clamp(float v, float lim) { return v > lim ? lim : (v < -lim ? -lim : v); }

// slot 0 of the player hud is the item in hands; an item without a parent CHudItem is half-attached
static attachable_hud_item* hudrc_item()
{
    if (!g_player_hud)
        return nullptr;
    attachable_hud_item* it = g_player_hud->attached_item(0);
    if (!it || !it->m_parent_hud_item)
        return nullptr;
    return it;
}

static void hudrc_reset()
{
    if (hudrc_on)
        ++hudrc_clears;
    hudrc_on = false;
    for (int i = 0; i < 3; ++i)
    {
        hudrc_pos[i] = 0.f;
        hudrc_rot[i] = 0.f;
    }
    hudrc_owner = nullptr;
}

static bool hudrc_set6(float px, float py, float pz, float pitch, float yaw, float roll)
{
    const float in[6] = {px, py, pz, pitch, yaw, roll};
    for (float v : in)
    {
        if (!hudrc_finite(v))
        {
            ++hudrc_rej_bad;
            hudrc_reset();
            return false;
        }
    }
    attachable_hud_item* it = hudrc_item();
    if (!it)
    {
        ++hudrc_rej_nohud;
        hudrc_reset();
        return false;
    }
    for (int i = 0; i < 3; ++i)
    {
        hudrc_pos[i] = hudrc_clamp(in[i], HUDRC_MAX_POS);
        hudrc_rot[i] = hudrc_clamp(in[3 + i], HUDRC_MAX_ROT);
    }
    hudrc_owner = it->m_parent_hud_item;
    hudrc_stamp = Device.dwTimeGlobal;
    hudrc_on = true;
    return true;
}

static bool hudrc_set(float pos_y, float pitch, float yaw) { return hudrc_set6(0.f, pos_y, 0.f, pitch, yaw, 0.f); }

// o = a * b, row-vector order; o must not alias a or b
static void hudrc_mul(float* o, const float* a, const float* b)
{
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
        {
            float s = 0.f;
            for (int k = 0; k < 4; ++k)
                s += a[i * 4 + k] * b[k * 4 + j];
            o[i * 4 + j] = s;
        }
}

// Rx(pitch) * Ry(yaw) * Rz(roll) in the engine's row-vector convention, translation in row 4
static void hudrc_build(float* r)
{
    const float cp = _cos(hudrc_rot[0]), sp = _sin(hudrc_rot[0]);
    const float cy = _cos(hudrc_rot[1]), sy = _sin(hudrc_rot[1]);
    const float cr = _cos(hudrc_rot[2]), sr = _sin(hudrc_rot[2]);
    const float rx[16] = {1.f, 0.f, 0.f, 0.f, 0.f, cp, sp, 0.f, 0.f, -sp, cp, 0.f, 0.f, 0.f, 0.f, 1.f};
    const float ry[16] = {cy, 0.f, -sy, 0.f, 0.f, 1.f, 0.f, 0.f, sy, 0.f, cy, 0.f, 0.f, 0.f, 0.f, 1.f};
    const float rz[16] = {cr, sr, 0.f, 0.f, -sr, cr, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f};
    float t[16];
    hudrc_mul(t, rx, ry);
    hudrc_mul(r, t, rz);
    r[12] = hudrc_pos[0];
    r[13] = hudrc_pos[1];
    r[14] = hudrc_pos[2];
    r[15] = 1.f;
}

void hudrc_apply()
{
    hudrc_saved_at = nullptr;
    if (!hudrc_on)
        return;
    if (!hudrc_lock && Device.dwTimeGlobal - hudrc_stamp > HUDRC_WATCHDOG)
    {
        ++hudrc_expired;
        hudrc_reset();
        return;
    }
    attachable_hud_item* it = hudrc_item();
    if (!it)
    {
        ++hudrc_rej_nohud;
        hudrc_reset();
        return;
    }
    if (it->m_parent_hud_item != hudrc_owner)
    {
        ++hudrc_rej_item;
        hudrc_reset();
        return;
    }
    float* m = &it->m_item_transform._11;
    for (int i = 0; i < 16; ++i)
        hudrc_saved[i] = m[i];

    float r[16], o[16];
    hudrc_build(r);
    hudrc_mul(o, r, hudrc_saved);
    for (int i = 0; i < 16; ++i)
        m[i] = o[i];

    hudrc_saved_at = m;
    ++hudrc_frames;
}

void hudrc_restore()
{
    if (!hudrc_saved_at)
        return;
    for (int i = 0; i < 16; ++i)
        hudrc_saved_at[i] = hudrc_saved[i];
    hudrc_saved_at = nullptr;
}

// minimal fixed-notation float parser (Lua string.format("%f") output)
static LPCSTR parse_float(LPCSTR s, float* out)
{
    while (*s == ' ' || *s == '\t')
        ++s;
    bool neg = false;
    if (*s == '-')
    {
        neg = true;
        ++s;
    }
    else if (*s == '+')
        ++s;
    if ((*s < '0' || *s > '9') && *s != '.')
        return nullptr;
    double v = 0.0;
    bool any = false;
    while (*s >= '0' && *s <= '9')
    {
        v = v * 10.0 + (*s - '0');
        ++s;
        any = true;
    }
    if (*s == '.')
    {
        ++s;
        double f = 0.1;
        while (*s >= '0' && *s <= '9')
        {
            v += (*s - '0') * f;
            f *= 0.1;
            ++s;
            any = true;
        }
    }
    if (!any)
        return nullptr;
    *out = float(neg ? -v : v);
    return s;
}

static void cmd_hudrc(LPCSTR a)
{
    while (*a == ' ' || *a == '\t')
        ++a;

    if (a[0] == 'c' && a[1] == 'l')
    {
        hudrc_reset();
        Msg("~ [hudrc] cleared");
        return;
    }

    if (a[0] == 'l' && a[1] == 'o')
    {
        LPCSTR s = a + 4;
        while (*s == ' ' || *s == '\t')
            ++s;
        if (*s == '1')
            hudrc_lock = true;
        else if (*s == '0')
        {
            hudrc_lock = false;
            hudrc_reset();
        }
        Msg("~ [hudrc] lock=%d (Lua writes ignored, watchdog frozen)", hudrc_lock ? 1 : 0);
        return;
    }

    if (a[0] == 't' && a[1] == 'e')
    {
        float v[6]{};
        LPCSTR s = a + 4;
        int n = 0;
        while (n < 6 && s)
        {
            LPCSTR nxt = parse_float(s, &v[n]);
            if (!nxt)
                break;
            s = nxt;
            ++n;
        }
        bool ok;
        if (n == 3)
            ok = hudrc_set(v[0], v[1], v[2]);
        else if (n == 6)
            ok = hudrc_set6(v[0], v[1], v[2], v[3], v[4], v[5]);
        else
        {
            Msg("!! [hudrc] usage: hudrc test <py> <pitch> <yaw>  |  hudrc test <px> <py> <pz> <pitch> <yaw> <roll>");
            return;
        }
        Msg("~ [hudrc] test set=%d pos=%f %f %f rot=%f %f %f", ok ? 1 : 0, hudrc_pos[0], hudrc_pos[1], hudrc_pos[2], hudrc_rot[0], hudrc_rot[1], hudrc_rot[2]);
        return;
    }

    attachable_hud_item* it = hudrc_item();
    Msg("~ [hudrc] bound=1 player_hud=%p item=%p hud_item=%p owner=%p", g_player_hud, it, it ? it->m_parent_hud_item : nullptr, hudrc_owner);
    Msg("~ [hudrc] on=%d pos=%f %f %f rot=%f %f %f age_ms=%u frames=%d", hudrc_on ? 1 : 0, hudrc_pos[0], hudrc_pos[1], hudrc_pos[2], hudrc_rot[0], hudrc_rot[1],
        hudrc_rot[2], hudrc_on ? Device.dwTimeGlobal - hudrc_stamp : 0u, hudrc_frames);
    Msg("~ [hudrc] lock=%d clears=%d expired=%d rej_nohud=%d rej_item=%d rej_invalid=%d rej_locked=%d", hudrc_lock ? 1 : 0, hudrc_clears, hudrc_expired, hudrc_rej_nohud,
        hudrc_rej_item, hudrc_rej_bad, hudrc_rej_lock);
}

// ---------------------------------------------------------------------------
// wpn_knife_m1: attack combo keys and draw / holster sounds
// ---------------------------------------------------------------------------

static CHudItem* knife_combo_owner{};
static u32 knife_combo_last_ms{};
static u32 knife_combo_index{};
static u32 knife_combo_logged{};
static CHudItem* knife_return_owner{};
static u32 knife_return_start_ms{};
static u32 knife_return_duration{};
static u32 knife_return_index{};
static u32 knife_return_logged{};
static u32 knife_sound_errors{};
static u32 knife_sound_missing{};
static bool knife_sound_busy{};

bool knife_combo_applicable(CHudItem* item)
{
    attachable_hud_item* attached = hudrc_item();
    if (!attached || attached->m_parent_hud_item != item)
        return false;
    return !xr_strcmp(item->object().cNameSect().c_str(), "wpn_knife_m1");
}

static bool knife_combo_has_key(LPCSTR key) { return pSettings->line_exist("wpn_knife_m1_hud", key); }

static void knife_notify_motion(int event)
{
    if (knife_sound_busy)
        return;
    lua_State* L = vm();
    if (!L)
        return;
    knife_sound_busy = true;
    const int top = lua_gettop(L);
    lua_pushstring(L, "fl_on_knife_motion");
    lua_rawget(L, LUA_GLOBALSINDEX);
    if (lua_type(L, -1) == LUA_TFUNCTION)
    {
        lua_pushinteger(L, event);
        if (lua_pcall(L, 1, 0, 0) && knife_sound_errors++ < 3u)
        {
            LPCSTR error = lua_tostring(L, -1);
            Msg("!! [knife_sound] %s", error ? error : "?");
        }
    }
    else if (knife_sound_missing++ == 0u)
        Msg("! [knife_sound] Lua callback unavailable");
    lua_settop(L, top);
    knife_sound_busy = false;
}

u32 knife_play_motion(CHudItem* self, LPCSTR key, bool mix_in, u32 state, bool random, float speed)
{
    const auto play = [&](LPCSTR k) { return self->PlayHUDMotion_base(k, mix_in, state, random, speed); };

    if (!xr_strcmp(key, "anm_hide") || !xr_strcmp(key, "anm_hide_fast"))
    {
        knife_combo_owner = nullptr;
        knife_return_owner = nullptr;
        const u32 duration = play(key);
        if (duration && !xr_strcmp(key, "anm_hide"))
            knife_notify_motion(2);
        return duration;
    }
    if (!xr_strcmp(key, "anm_show") || !xr_strcmp(key, "anm_show_empty") || !xr_strcmp(key, "anm_show_fast"))
    {
        knife_return_owner = nullptr;
        const u32 duration = play(key);
        if (duration && xr_strcmp(key, "anm_show_fast"))
            knife_notify_motion(1);
        return duration;
    }

    const bool primary = !xr_strcmp(key, "anm_attack");
    const bool secondary = !xr_strcmp(key, "anm_attack2");
    if (!primary && !secondary)
    {
        if ((!xr_strcmp(key, "anm_idle") || !xr_strcmp(key, "anm_idle_aim")) && knife_return_owner == self)
        {
            static constexpr LPCSTR return_keys[3] = {"anm_hit12idle", "anm_hit22idle", "anm_hit32idle"};
            const u32 elapsed = Device.dwTimeGlobal - knife_return_start_ms;
            const u32 threshold = knife_return_duration > 80u ? knife_return_duration - 80u : 0u;
            const u32 index = knife_return_index;
            knife_return_owner = nullptr;
            if (elapsed >= threshold && elapsed <= knife_return_duration + 400u && index < 3u && knife_combo_has_key(return_keys[index]))
            {
                const u32 duration = play(return_keys[index]);
                if (duration)
                {
                    if (knife_return_logged++ < 12u)
                        Msg("~ [knife_combo] %s -> %s (%u ms)", key, return_keys[index], duration);
                    return duration;
                }
            }
        }
        else
            knife_return_owner = nullptr;
        return play(key);
    }

    static constexpr LPCSTR primary_keys[4] = {"anm_attack", "anm_attack_svariant1", "anm_attack_svariant2", "anm_attack_svariant3"};
    static constexpr LPCSTR secondary_keys[4] = {"anm_attack2", "anm_attack2_svariant1", "anm_attack2_svariant2", "anm_attack2_svariant3"};
    const u32 now = Device.dwTimeGlobal;
    const u32 next = (knife_combo_owner == self && now - knife_combo_last_ms <= 1000u) ? (knife_combo_index + 1u) % 4u : 0u;
    LPCSTR chosen = (primary ? primary_keys : secondary_keys)[next];
    if (next != 0u && !knife_combo_has_key(chosen))
        chosen = key;

    knife_return_owner = nullptr;
    const u32 duration = play(chosen);
    if (duration)
    {
        knife_combo_owner = self;
        knife_combo_last_ms = now;
        knife_combo_index = chosen == key ? 0u : next;
        knife_return_owner = primary && knife_combo_index < 3u ? self : nullptr;
        knife_return_start_ms = now;
        knife_return_duration = duration;
        knife_return_index = knife_combo_index;
        if (knife_combo_logged++ < 12u)
            Msg("~ [knife_combo] %s -> %s (%u ms)", key, chosen, duration);
    }
    return duration;
}

// ---------------------------------------------------------------------------
// texture surface swap (sleeves follow the outfit)
// ---------------------------------------------------------------------------

// A missing file must never reach CTexture::Load: the engine treats it as fatal.
static bool texture_exists(LPCSTR name)
{
    if (!name || !name[0])
        return false;
    string_path rel;
    int i = 0;
    for (; name[i] && i < (int)sizeof(rel) - 6; ++i)
        rel[i] = name[i] == '/' ? '\\' : name[i];
    rel[i] = 0;
    if (i < 4 || rel[i - 4] != '.')
        xr_strcat(rel, ".dds");
    return !!FS.exist("$game_textures$", rel);
}

static bool tex_swap(LPCSTR reg, LPCSTR file)
{
    if (!reg || !reg[0] || !file || !file[0])
        return false;
    if (!texture_exists(reg) || !texture_exists(file))
        return false;
    ITexture* tex = Device.m_pRender->GetResourceManager()->CreateTexture(reg);
    if (!tex)
        return false;
    tex->Unload();
    tex->Load(file);
    return true;
}

static void cmd_tex(LPCSTR args)
{
    string_path reg, file;
    int n = 0, i = 0;
    while (*args == ' ' || *args == '\t')
        ++args;
    while (args[i] && args[i] != ' ' && args[i] != '\t' && n < (int)sizeof(reg) - 1)
        reg[n++] = args[i++];
    reg[n] = 0;
    while (args[i] == ' ' || args[i] == '\t')
        ++i;
    n = 0;
    while (args[i] && args[i] != ' ' && args[i] != '\t' && n < (int)sizeof(file) - 1)
        file[n++] = args[i++];
    file[n] = 0;
    if (!reg[0])
    {
        Msg("~ [tex] usage: tex <texture> [file]   (file omitted = restore)");
        return;
    }
    LPCSTR target = file[0] ? file : reg;
    if (tex_swap(reg, target))
        Msg("~ [tex] [%s] <- [%s]", reg, target);
    else
        Msg("!! [tex] failed: [%s] <- [%s]", reg, target);
}

// ---------------------------------------------------------------------------
// surface material under the actor (bullet shell sounds)
// ---------------------------------------------------------------------------

static LPCSTR gmtl_name(u16 idx)
{
    if (idx >= GMLib.CountMaterial())
        return nullptr;
    SGameMtl* mtl = GMLib.GetMaterialByIdx(idx);
    return mtl ? mtl->m_Name.c_str() : nullptr;
}

static bool step_material_read(LPCSTR& ground, LPCSTR& self, int& gid)
{
    if (!g_actor)
        return false;
    const CMaterialManager& mm = g_actor->material();
    const u16 g = mm.last_material_idx();
    LPCSTR gn = gmtl_name(g);
    if (!gn)
        return false;
    ground = gn;
    self = gmtl_name(mm.self_material_idx());
    gid = g;
    return true;
}

// ---------------------------------------------------------------------------
// blood pool wallmarks (sivol_blood_pools.script: bpm x y z dist size [tex])
// ---------------------------------------------------------------------------

static constexpr LPCSTR BP_TEX[] = {
    "wm\\wm_blood_pool_1",  "wm\\wm_blood_pool_2",  "wm\\wm_blood_pool_3",  "wm\\wm_blood_pool_4",  "wm\\wm_blood_pool_5",  "wm\\wm_blood_pool_6",
    "wm\\wm_blood_pool_7",  "wm\\wm_blood_pool_8",  "wm\\wm_blood_pool_9",  "wm\\wm_blood_pool_10", "wm\\wm_blood_pool_11", "wm\\wm_blood_pool_12",
    "wm\\wm_blood_pool_13", "wm\\wm_blood_pool_14", "wm\\wm_blood_pool_15", "wm\\wm_blood_pool_16", "wm\\wm_blood_pool_17", "wm\\wm_blood_pool_18",
    "wm\\wm_blood_pool_19", "wm\\wm_blood_pool_20", "wm\\wm_blood_pool_21", "wm\\wm_blood_pool_22", "wm\\wm_blood_pool_23", "wm\\wm_blood_pool_24",
};
static constexpr int BP_TEX_N = int(std::size(BP_TEX));

// all 24 textures (random per stamp) and one single-texture array per pool texture,
// kept for the process lifetime like the engine's own combat blood array
static IWallMarkArray* bp_array{};
static IWallMarkArray* bp_one[BP_TEX_N]{};
static int bp_ok{}, bp_fail{}, bp_reason{};

static void bp_build_array()
{
    if (bp_array)
        return;
    IWallMarkArray* arr = RenderFactory->CreateWallMarkArray();
    if (!arr)
        return;
    for (LPCSTR t : BP_TEX)
        arr->AppendMark(t);
    for (int i = 0; i < BP_TEX_N; ++i)
    {
        IWallMarkArray* one = RenderFactory->CreateWallMarkArray();
        if (!one)
            break;
        one->AppendMark(BP_TEX[i]);
        bp_one[i] = one;
    }
    bp_array = arr;
}

static bool bp_fail_with(int reason)
{
    bp_reason = reason;
    ++bp_fail;
    return false;
}

static bool bp_place(float x, float y, float z, float dist, float size, int tex)
{
    if (!has_level())
        return bp_fail_with(1);

    bp_build_array();
    if (!bp_array)
        return bp_fail_with(2);
    IWallMarkArray* arr = bp_array;
    if (tex >= 1 && tex <= BP_TEX_N && bp_one[tex - 1])
        arr = bp_one[tex - 1];

    const Fvector start{x, y, z};
    const Fvector dir{0.f, -1.f, 0.f};
    collide::rq_result R;
    if (!Level().ObjectSpace.RayPick(start, dir, dist, collide::rqtStatic, R, nullptr))
        return bp_fail_with(3);
    if (R.O)
        return bp_fail_with(4);
    if (R.element < 0)
        return bp_fail_with(3);

    Fvector end;
    end.mad(start, dir, R.range);

    CDB::TRI* tris = Level().ObjectSpace.GetStaticTris();
    Fvector* verts = Level().ObjectSpace.GetStaticVerts();
    if (!tris || !verts)
        return bp_fail_with(5);

    CDB::TRI* tri = tris + R.element;
    const u16 matid = u16(tri->material);
    if (matid >= GMLib.CountMaterial())
        return bp_fail_with(6);
    SGameMtl* mtl = GMLib.GetMaterialByIdx(matid);
    if (!mtl || !mtl->Flags.test(SGameMtl::flBloodmark))
        return bp_fail_with(7);
    if (!::Render)
        return bp_fail_with(8);

    ::Render->add_StaticWallmark(arr, end, size, tri, verts);
    bp_reason = 0;
    ++bp_ok;
    return true;
}

static void cmd_bpm(LPCSTR a)
{
    while (*a == ' ' || *a == '\t')
        ++a;
    if (!*a)
    {
        Msg("~ [bpm] ok=%d fail=%d", bp_ok, bp_fail);
        Msg("~ [bpm] last_reason=%d (0ok 3no_hit 7not_bloodmark)", bp_reason);
        Msg("~ [bpm] rotfix=%d (decal rotation jitter off)", ps_wm_rotfix);
        return;
    }
    // decal rotation jitter: affects all static wallmarks, not just pools
    if (a[0] == 'r' && a[1] == 'o' && a[2] == 't')
    {
        LPCSTR b = a + 3;
        while (*b == ' ' || *b == '\t')
            ++b;
        if (*b == '0' || *b == '1')
            ps_wm_rotfix = *b - '0';
        Msg("~ [bpm] rotfix=%d", ps_wm_rotfix);
        return;
    }
    float x, y, z, dist, size, tex = 0.f;
    LPCSTR p = parse_float(a, &x);
    if (p)
        p = parse_float(p, &y);
    if (p)
        p = parse_float(p, &z);
    if (p)
        p = parse_float(p, &dist);
    if (p)
        p = parse_float(p, &size);
    if (!p)
    {
        Msg("!! [bpm] usage: bpm x y z dist size [tex]");
        return;
    }
    parse_float(p, &tex);
    bp_place(x, y, z, dist, size, int(tex));
}

// ---------------------------------------------------------------------------
// HUD hands child submesh visibility (wrist watch gate: wm)
// ---------------------------------------------------------------------------

static constexpr unsigned WM_MAX_IDX = 64;
static int wm_calls{}, wm_applied{}, wm_skipped{}, wm_last_state{-1}, wm_nomodel{}, wm_mismatch{}, wm_skipped_model{}, wm_skipped_name{};

static IKinematicsAnimated* wm_hands_model(bool second)
{
    if (!g_player_hud)
        return nullptr;
    return second ? g_player_hud->Model2() : g_player_hud->Model();
}

static IKinematics* wm_hands_kin(bool second)
{
    IKinematicsAnimated* model = wm_hands_model(second);
    return model ? model->dcast_PKinematics() : nullptr;
}

static shared_str wm_visual_name(bool second)
{
    IKinematicsAnimated* model = wm_hands_model(second);
    if (!model)
        return "<none>";
    IRenderVisual* vis = model->dcast_RenderVisual();
    if (!vis)
        return "<no visual>";
    const shared_str name = vis->getDebugName();
    return name.size() ? name : shared_str("<empty>");
}

// several hand visuals share a child count, so the index list is also tied to a file name
static bool wm_name_eq(LPCSTR path, LPCSTR want)
{
    if (!path || !want || !*want)
        return false;
    LPCSTR base = path;
    for (LPCSTR p = path; *p; ++p)
        if (*p == '\\' || *p == '/')
            base = p + 1;
    const size_t n = xr_strlen(want);
    if (_strnicmp(base, want, n))
        return false;
    LPCSTR rest = base + n;
    return !*rest || !_stricmp(rest, ".ogf");
}

static u32 wm_child_count(IKinematics* kin) { return kin ? kin->RChildCount() : 0; }

static void wm_set(IKinematics* kin, u32 idx, bool state)
{
    if (!kin)
        return;
    if (idx >= wm_child_count(kin))
    {
        ++wm_skipped;
        return;
    }
    kin->SetRFlag(idx, state);
    ++wm_applied;
}

static void cmd_wm(LPCSTR a)
{
    while (*a == ' ' || *a == '\t')
        ++a;

    IKinematics* k1 = wm_hands_kin(false);
    IKinematics* k2 = wm_hands_kin(true);

    if (!*a)
    {
        Msg("~ [wm] m_model=%p children=%u visual=%s", k1, wm_child_count(k1), wm_visual_name(false).c_str());
        Msg("~ [wm] m_model_2=%p children=%u visual=%s", k2, wm_child_count(k2), wm_visual_name(true).c_str());
        Msg("~ [wm] calls=%d applied=%d skipped_idx=%d skipped_model=%d skipped_name=%d nomatch=%d last_state=%d nomodel=%d", wm_calls, wm_applied, wm_skipped,
            wm_skipped_model, wm_skipped_name, wm_mismatch, wm_last_state, wm_nomodel);
        return;
    }

    if (*a != '0' && *a != '1')
    {
        Msg("!! [wm] usage: wm <0|1> <nchildren> [visual] <idx> [idx ...]  |  wm");
        return;
    }
    const bool state = *a == '1';
    ++a;

    while (*a == ' ' || *a == '\t')
        ++a;
    if (*a < '0' || *a > '9')
    {
        Msg("!! [wm] usage: wm <0|1> <nchildren> [visual] <idx> [idx ...]  |  wm");
        return;
    }
    u32 want = 0;
    while (*a >= '0' && *a <= '9')
    {
        want = want * 10u + u32(*a - '0');
        ++a;
    }

    string64 vname{};
    while (*a == ' ' || *a == '\t')
        ++a;
    if (*a && (*a < '0' || *a > '9'))
    {
        u32 j = 0;
        while (*a && *a != ' ' && *a != '\t' && j + 1 < sizeof(vname))
            vname[j++] = *a++;
        vname[j] = 0;
    }

    if (!k1 && !k2)
    {
        if (++wm_nomodel <= 3)
            Msg("!! [wm] no hands model (g_player_hud/m_model empty), n=%d", wm_nomodel);
        return;
    }

    // an index list belongs to one model file; another child count means another file
    const u32 n1 = wm_child_count(k1), n2 = wm_child_count(k2);
    IKinematics* t1 = (want && n1 != want) ? nullptr : k1;
    IKinematics* t2 = (want && n2 != want) ? nullptr : k2;
    if ((k1 && !t1) || (k2 && !t2))
        ++wm_skipped_model;
    if (vname[0])
    {
        if (t1 && !wm_name_eq(wm_visual_name(false).c_str(), vname))
        {
            t1 = nullptr;
            ++wm_skipped_name;
        }
        if (t2 && !wm_name_eq(wm_visual_name(true).c_str(), vname))
        {
            t2 = nullptr;
            ++wm_skipped_name;
        }
    }
    if (!t1 && !t2)
    {
        if (++wm_mismatch <= 3)
            Msg("!! [wm] no model %s with %u children: m_model=%u (%s) m_model_2=%u (%s)", vname[0] ? vname : "<any>", want, n1, wm_visual_name(false).c_str(), n2,
                wm_visual_name(true).c_str());
        return;
    }

    int n = 0;
    while (*a)
    {
        while (*a == ' ' || *a == '\t' || *a == ',')
            ++a;
        if (*a < '0' || *a > '9')
            break;
        u32 idx = 0;
        while (*a >= '0' && *a <= '9')
        {
            idx = idx * 10u + u32(*a - '0');
            ++a;
        }
        if (idx > WM_MAX_IDX)
            continue;
        wm_set(t1, idx, state);
        wm_set(t2, idx, state);
        ++n;
    }

    ++wm_calls;
    wm_last_state = state ? 1 : 0;
    if (!n)
        Msg("!! [wm] no valid indices in command");
}

// ---------------------------------------------------------------------------
// game_object:set_cost(n) (NLC 3.0)
// ---------------------------------------------------------------------------

static u32 scost_bound{}, scost_calls{}, scost_misses{}, scost_last{};

void set_cost(CScriptGameObject* self, u32 cost)
{
    CInventoryItem* item = smart_cast<CInventoryItem*>(&self->object());
    if (!item)
    {
        ++scost_misses;
        return;
    }
    item->SetCost(cost);
    scost_last = cost;
    ++scost_calls;
}

void set_cost_registered() { ++scost_bound; }

static void cmd_scost()
{
    Msg("~ [scost] bound=%u calls=%u", scost_bound, scost_calls);
    Msg("~ [scost] not_an_item=%u last_cost=%u", scost_misses, scost_last);
}

// ---------------------------------------------------------------------------
// NPC PDA on the 3D PDA screen
// ---------------------------------------------------------------------------

// phase 1/2 channel state; the RT painter of phase 2 was superseded by the
// viewport2 hack below, only the Lua / console surface is kept
static bool pda3d_rt_arm{};
static u32 pda3d_pda_id{};
static int pda3d_opens{}, pda3d_clears{};
static string128 pda3d_lines[14];
static int pda3d_nlines{};

static bool pda3d_hide_2d{true};
static CUIDialogWnd* pda3d_hack_wnd{};
static u32 pda3d_hack_frame{};
static int pda3d_hack_draws{}, pda3d_hack_hides{}, pda3d_hack_miss{}, pda3d_nosign_draws{}, pda3d_ir_blocks{};

static CUIPdaWnd* pda3d_pda_menu()
{
    if (!g_hud || !HUD().GetUI())
        return nullptr;
    CUIGameSP* sp = smart_cast<CUIGameSP*>(HUD().GetUI()->UIGame());
    return sp ? sp->PdaMenu : nullptr;
}

CUIDialogWnd* pda3d_top_receiver()
{
    if (!g_hud || !HUD().GetUI())
        return nullptr;
    return HUD().GetUI()->MainInputReceiver();
}

bool pda3d_draw_hack(CUIPdaWnd* pda, u32& last_frame)
{
    if (!pda3d_vp2_pass)
        return false;
    if (pda3d_nosign)
    {
        last_frame = Device.dwFrame;
        ++pda3d_nosign_draws;
        pda->CUIWindow::Draw();
        return true;
    }
    if (pda3d_hack)
    {
        CUIDialogWnd* wnd = pda3d_top_receiver();
        if (wnd && wnd != pda)
        {
            last_frame = Device.dwFrame;
            pda3d_hack_wnd = wnd;
            pda3d_hack_frame = Device.dwFrame;
            ++pda3d_hack_draws;
            wnd->Draw();
            return true;
        }
        ++pda3d_hack_miss;
    }
    return false;
}

bool pda3d_block_input_receiver(CUIDialogWnd* ir)
{
    if (!pda3d_hack || !ir || ir != pda3d_pda_menu())
        return false;
    ++pda3d_ir_blocks;
    return true;
}

void pda3d_trace_action(s32 cmd, u32 flags, u32 slot_before, u32 slot_after, bool result, bool is_actor)
{
    Msg("~ [pda3d trace] Action cmd=%d flags=0x%X slot %u -> %u ret=%d%s", cmd, flags, slot_before, slot_after, result ? 1 : 0, is_actor ? " (actor)" : "");
}

CHudRenderUIGuard::CHudRenderUIGuard()
{
    if (pda3d_hack && pda3d_hide_2d && pda3d_hack_wnd && pda3d_hack_frame == Device.dwFrame && pda3d_top_receiver() == pda3d_hack_wnd)
    {
        m_wnd = pda3d_hack_wnd;
        m_saved = m_wnd->GetVisible();
        m_wnd->SetVisible(false);
        ++pda3d_hack_hides;
    }
}

CHudRenderUIGuard::~CHudRenderUIGuard()
{
    if (m_wnd)
        m_wnd->SetVisible(m_saved);
}

static void pda3d_hack_set(bool on)
{
    pda3d_hack = on;
    if (!on)
    {
        pda3d_hack_wnd = nullptr;
        pda3d_hack_frame = 0;
    }
}

static void pda3d_set_text(LPCSTR s)
{
    pda3d_nlines = 0;
    for (LPCSTR p = s ? s : ""; *p && pda3d_nlines < (int)std::size(pda3d_lines);)
    {
        int k = 0;
        while (*p && *p != '\n' && k < 79)
            pda3d_lines[pda3d_nlines][k++] = *p++;
        pda3d_lines[pda3d_nlines][k] = 0;
        ++pda3d_nlines;
        if (*p == '\n')
            ++p;
    }
}

static void cmd_pda3d_why()
{
    CPda* pda = g_actor ? g_actor->GetPDA() : nullptr;
    Msg("~ [pda3d why] g_3d_pda=%d actor=%p", psActorFlags.test(AF_3D_PDA) ? 1 : 0, g_actor);
    if (!g_actor)
        return;
    Msg("~ [pda3d why] active_slot=%u pda=%p this_is_3d_pda=%d", g_actor->inventory().GetActiveSlot(), pda, pda ? (pda->Is3DPDA() ? 1 : 0) : -1);
    CUIPdaWnd* pda_wnd = pda3d_pda_menu();
    CUIGameSP* sp = g_hud && HUD().GetUI() ? smart_cast<CUIGameSP*>(HUD().GetUI()->UIGame()) : nullptr;
    CUIDialogWnd* inv_wnd = sp ? static_cast<CUIDialogWnd*>(sp->InventoryMenu) : nullptr;
    Msg("~ [pda3d why] PdaMenu=%p show=%d  InventoryMenu=%p show=%d  top_recv=%p", pda_wnd, pda_wnd ? (pda_wnd->GetVisible() ? 1 : 0) : -1, inv_wnd,
        inv_wnd ? (inv_wnd->GetVisible() ? 1 : 0) : -1, pda3d_top_receiver());
    const bool gate = pda && pda->Is3DPDA() && psActorFlags.test(AF_3D_PDA);
    Msg("~ [pda3d why] 3d_gate=%d -> %s", gate ? 1 : 0, gate ? "CInventory::Action should Activate(PDA slot)" : "engine takes the flat 2D path (CUIGameSP opens PdaMenu)");
}

static void cmd_pda3d_pull()
{
    if (!g_actor)
    {
        Msg("!! [pda3d pull] no inventory");
        return;
    }
    CInventory& inv = g_actor->inventory();
    const u32 before = inv.GetActiveSlot();
    const bool r = inv.Activate(PDA_SLOT, eKeyAction);
    Msg("~ [pda3d pull] Activate(%u, eKeyAction) ret=%d slot %u -> %u", u32(PDA_SLOT), r ? 1 : 0, before, inv.GetActiveSlot());
}

static LPCSTR skip_ws(LPCSTR s)
{
    while (*s == ' ' || *s == '\t')
        ++s;
    return s;
}

static void cmd_pda3d(LPCSTR a)
{
    a = skip_ws(a);
    if (a[0] == 'r' && a[1] == 't')
    {
        LPCSTR s = skip_ws(a + 2);
        if (*s == '1')
            pda3d_rt_arm = true;
        else if (*s == '0')
        {
            pda3d_rt_arm = false;
            pda3d_pda_id = 0;
        }
        Msg("~ [pda3d] rt_arm=%d pda_id=%u", pda3d_rt_arm ? 1 : 0, pda3d_pda_id);
        return;
    }
    if ((a[0] == 'm' && (a[1] == '1' || a[1] == '2')) || !strncmp(a, "fill", 4))
    {
        Msg("~ [pda3d] phase 2 RT tests are not part of the native engine (superseded by `pda3d hack`)");
        return;
    }
    if (!strncmp(a, "text", 4))
    {
        pda3d_set_text(skip_ws(a + 4));
        Msg("~ [pda3d] text: %d line(s)", pda3d_nlines);
        return;
    }
    if (!strncmp(a, "trac", 4))
    {
        LPCSTR s = skip_ws(a + 5);
        if (*s == '1')
            pda3d_trace = true;
        else if (*s == '0')
            pda3d_trace = false;
        Msg("~ [pda3d] trace=%d hooked=1", pda3d_trace ? 1 : 0);
        return;
    }
    if (!strncmp(a, "pull", 4))
    {
        cmd_pda3d_pull();
        return;
    }
    if (!strncmp(a, "hide", 4))
    {
        LPCSTR s = skip_ws(a + 4);
        if (*s == '1')
            pda3d_hide_2d = true;
        else if (*s == '0')
            pda3d_hide_2d = false;
        Msg("~ [pda3d] hide_2d=%d", pda3d_hide_2d ? 1 : 0);
        return;
    }
    if (!strncmp(a, "free", 4))
    {
        LPCSTR s = skip_ws(a + 6);
        if (*s == '1')
            pda3d_freeze = true;
        else if (*s == '0')
            pda3d_freeze = false;
        Msg("~ [pda3d] freeze=%d", pda3d_freeze ? 1 : 0);
        return;
    }
    if (!strncmp(a, "sign", 4))
    {
        LPCSTR s = skip_ws(a + 4);
        if (*s == '1')
            pda3d_nosign = true;
        else if (*s == '0')
            pda3d_nosign = false;
        Msg("~ [pda3d] nosign=%d draws=%d", pda3d_nosign ? 1 : 0, pda3d_nosign_draws);
        return;
    }
    if (!strncmp(a, "why", 3))
    {
        cmd_pda3d_why();
        return;
    }
    if (!strncmp(a, "hack", 4))
    {
        LPCSTR s = skip_ws(a + 4);
        if (*s == '1')
            pda3d_hack_set(true);
        else if (*s == '0')
            pda3d_hack_set(false);
        Msg("~ [pda3d] hack=%d bound=1 draws=%d hides=%d miss=%d", pda3d_hack ? 1 : 0, pda3d_hack_draws, pda3d_hack_hides, pda3d_hack_miss);
        return;
    }
    if (!strncmp(a, "off", 3))
    {
        pda3d_rt_arm = false;
        Msg("~ [pda3d] off");
        return;
    }
    if (!strncmp(a, "on", 2))
    {
        pda3d_rt_arm = true;
        Msg("~ [pda3d] on");
        return;
    }
    Msg("~ [pda3d] rt_arm=%d pda_id=%u opens=%d clears=%d bound=1", pda3d_rt_arm ? 1 : 0, pda3d_pda_id, pda3d_opens, pda3d_clears);
    Msg("~ [pda3d] lines=%d", pda3d_nlines);
    Msg("~ [pda3d] hack=%d bound=1 wnd=%p hides=%d", pda3d_hack ? 1 : 0, pda3d_hack_wnd, pda3d_hack_hides);
    Msg("~ [pda3d] ir_guard=1 ir_blocks=%d", pda3d_ir_blocks);
}

// ---------------------------------------------------------------------------
// Lua globals
// ---------------------------------------------------------------------------

static int lua_fl_step_material(lua_State* L)
{
    LPCSTR ground{}, self{};
    int gid{};
    if (!step_material_read(ground, self, gid))
        return 0;
    lua_pushstring(L, ground);
    lua_pushinteger(L, gid);
    lua_pushstring(L, self ? self : "");
    return 3;
}

static int lua_fl_hud_recoil_available(lua_State* L)
{
    lua_pushboolean(L, hudrc_item() ? 1 : 0);
    return 1;
}

static int lua_fl_hud_recoil_set(lua_State* L)
{
    if (hudrc_lock)
    {
        ++hudrc_rej_lock;
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, hudrc_set(float(lua_tonumber(L, 1)), float(lua_tonumber(L, 2)), float(lua_tonumber(L, 3))) ? 1 : 0);
    return 1;
}

static int lua_fl_hud_recoil_set6(lua_State* L)
{
    if (hudrc_lock)
    {
        ++hudrc_rej_lock;
        lua_pushboolean(L, 0);
        return 1;
    }
    float v[6];
    for (int i = 0; i < 6; ++i)
        v[i] = float(lua_tonumber(L, i + 1));
    lua_pushboolean(L, hudrc_set6(v[0], v[1], v[2], v[3], v[4], v[5]) ? 1 : 0);
    return 1;
}

static int lua_fl_hud_recoil_clear(lua_State* L)
{
    if (!hudrc_lock)
        hudrc_reset();
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_fl_tex_swap(lua_State* L)
{
    lua_pushboolean(L, tex_swap(lua_tostring(L, 1), lua_tostring(L, 2)) ? 1 : 0);
    return 1;
}

static int lua_fl_pda3d_available(lua_State* L)
{
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_fl_pda3d_set_active(lua_State* L)
{
    const u32 id = u32(lua_tonumber(L, 1));
    if (!id)
    {
        lua_pushboolean(L, 0);
        return 1;
    }
    pda3d_pda_id = id;
    pda3d_rt_arm = true;
    ++pda3d_opens;
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_fl_pda3d_clear(lua_State* L)
{
    pda3d_rt_arm = false;
    pda3d_pda_id = 0;
    ++pda3d_clears;
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_fl_pda3d_text(lua_State* L)
{
    pda3d_set_text(lua_tostring(L, 1));
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_fl_pda3d_hack(lua_State* L)
{
    pda3d_hack_set(int(lua_tonumber(L, 1)) != 0);
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_fl_pda3d_nosign(lua_State* L)
{
    pda3d_nosign = int(lua_tonumber(L, 1)) != 0;
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_fl_pda3d_freeze(lua_State* L)
{
    pda3d_freeze = int(lua_tonumber(L, 1)) != 0;
    lua_pushboolean(L, pda3d_freeze ? 1 : 0);
    return 1;
}

void script_register(lua_State* L)
{
    lua_register(L, "fl_step_material", lua_fl_step_material);
    lua_register(L, "fl_hud_recoil_available", lua_fl_hud_recoil_available);
    lua_register(L, "fl_hud_recoil_set", lua_fl_hud_recoil_set);
    lua_register(L, "fl_hud_recoil_set6", lua_fl_hud_recoil_set6);
    lua_register(L, "fl_hud_recoil_clear", lua_fl_hud_recoil_clear);
    lua_register(L, "fl_tex_swap", lua_fl_tex_swap);
    lua_register(L, "fl_pda3d_available", lua_fl_pda3d_available);
    lua_register(L, "fl_pda3d_set_active", lua_fl_pda3d_set_active);
    lua_register(L, "fl_pda3d_clear", lua_fl_pda3d_clear);
    lua_register(L, "fl_pda3d_text", lua_fl_pda3d_text);
    lua_register(L, "fl_pda3d_hack", lua_fl_pda3d_hack);
    lua_register(L, "fl_pda3d_nosign", lua_fl_pda3d_nosign);
    lua_register(L, "fl_pda3d_freeze", lua_fl_pda3d_freeze);
    hudrc_reset();
}

// ---------------------------------------------------------------------------
// console commands
// ---------------------------------------------------------------------------

class CCC_FlHook : public IConsole_Command
{
    void (*m_fn)(LPCSTR);

public:
    CCC_FlHook(LPCSTR N, void (*fn)(LPCSTR)) : IConsole_Command(N), m_fn(fn)
    {
        bLowerCaseArgs = FALSE;
        bEmptyArgsHandled = TRUE;
    }
    void Execute(LPCSTR args) override { m_fn(args); }
};

static bool arg_01(LPCSTR a, bool& value)
{
    a = skip_ws(a);
    if (*a == '1')
        value = true;
    else if (*a == '0')
        value = false;
    else
        return false;
    return true;
}

static void cmd_bpui(LPCSTR a)
{
    arg_01(a, bp_ui);
    Msg("! [fl] bpui=%d body_skips=%u hands_skips=%u", bp_ui ? 1 : 0, skip_body_n, skip_hands_n);
}

static void cmd_blr(LPCSTR a)
{
    if (!arg_01(a, blr_on))
        Msg("! [fl] blr=%d added=%u hook=native", blr_on ? 1 : 0, blr_added);
}

static void cmd_tlua(LPCSTR a)
{
    arg_01(a, torch_lua);
    Msg("! [fl] tlua=%d blocked=%u hook=native active_item=%p", torch_lua ? 1 : 0, torch_blocked_n, g_actor ? g_actor->inventory().ActiveItem() : nullptr);
}

static void cmd_stepmtl(LPCSTR)
{
    LPCSTR ground{}, self{};
    int gid{};
    if (step_material_read(ground, self, gid))
        Msg("! [fl] stepmtl: ground=[%s] id=%d self=[%s] bound=1", ground, gid, self ? self : "");
    else
        Msg("! [fl] stepmtl: unavailable (no actor / no material lib)");
}

static void cmd_bpbody(LPCSTR)
{
    Msg("! [fl] bpbody: bpui=%d body_skips=%u hands_skips=%u g_player_hud=%p part=%u item=%p body_hook=native hud_hook=native", bp_ui ? 1 : 0, skip_body_n,
        skip_hands_n, g_player_hud, g_player_hud ? u32(g_player_hud->script_anim_part) : 0xFFu, g_player_hud ? g_player_hud->script_anim_item_model : nullptr);
}

static void cmd_apath(LPCSTR) { Msg("! [fl] apath: patched=1 (native)"); }

void register_console_commands()
{
    CMD2(CCC_FlHook, "fl", &run_fl);
    CMD2(CCC_FlHook, "rl", &run_rl);
    CMD2(CCC_FlHook, "brz", [](LPCSTR) { cmd_brz(); });
    CMD2(CCC_FlHook, "boar", [](LPCSTR) { cmd_boar(); });
    CMD2(CCC_FlHook, "bpm", &cmd_bpm);
    CMD2(CCC_FlHook, "tex", &cmd_tex);
    CMD2(CCC_FlHook, "wm", &cmd_wm);
    CMD2(CCC_FlHook, "hudrc", &cmd_hudrc);
    CMD2(CCC_FlHook, "scost", [](LPCSTR) { cmd_scost(); });
    CMD2(CCC_FlHook, "wbf", [](LPCSTR) { cmd_wbf(); });
    CMD2(CCC_FlHook, "bpui", &cmd_bpui);
    CMD2(CCC_FlHook, "blr", &cmd_blr);
    CMD2(CCC_FlHook, "tlua", &cmd_tlua);
    CMD2(CCC_FlHook, "stepmtl", &cmd_stepmtl);
    CMD2(CCC_FlHook, "bpbody", &cmd_bpbody);
    CMD2(CCC_FlHook, "pda3d", &cmd_pda3d);
    CMD2(CCC_FlHook, "apath", &cmd_apath);
}
} // namespace fl_hook
