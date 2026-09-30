#pragma once

#include "alife_space.h"

// Features that used to live in the dinput8.dll proxy (fl_hook) of the NLC build.
// Console commands, Lua globals and engine behaviour keep the names and semantics
// the gamedata already relies on.

class CSE_Abstract;
class CHudItem;
class CWeapon;
class CUIDialogWnd;
class CUIPdaWnd;
class CScriptGameObject;
class CCoverPoint;
class CControlRunAttack;
struct lua_State;

namespace fl_hook
{
// backpack inventory UI: hide the first-person body and HUD hands (console: bpui)
extern bool bp_ui;
// "st_ballon_remove" line in the worn outfit's inventory menu (console: blr)
extern bool blr_on;
// engine kTORCH / kTORCH_MODE handling with empty hands blocked, Lua owns it (console: tlua)
extern bool torch_lua;

// NPC PDA hack on the 3D PDA screen
extern bool pda3d_hack;
extern bool pda3d_nosign;
extern bool pda3d_freeze;
extern bool pda3d_trace;
// true only while CLevel::OnRender draws the PDA into $user$viewport2
extern bool pda3d_vp2_pass;

extern u32 skip_body_n;
extern u32 skip_hands_n;
extern u32 torch_blocked_n;
extern u32 blr_added;

void register_console_commands();
void script_register(lua_State* L);

// escape_bridge_zone exception for the two bridge NPCs
bool brz_net_spawn_begin(CSE_Abstract* data);
void brz_net_spawn_end(bool tracked);
shared_str brz_default_in(ALife::_OBJECT_ID id, const shared_str& default_in);

// relaxed CControlRunAttack::check_start_conditions diagnostics (console: boar)
enum EBoarReason
{
    eBoarOk = 0,
    eBoarActive,
    eBoarNoObject,
    eBoarNoEnemy,
    eBoarDistance,
    eBoarStanding,
    eBoarCooldown,
    eBoarNotFacing,
};
extern float boar_last_dist;
extern float boar_last_vel;
void boar_result(EBoarReason reason);

// xrs_battle_ai: close / far cover and killer id multiplexed onto ambush_cover
const CCoverPoint* ambush_cover_ex(CScriptGameObject* self, const Fvector& position, const Fvector& enemy_position, float packed_radius, float min_distance,
                                   const luabind::functor<bool>& callback);

// actor weapon before-fire callback: fl_on_actor_weapon_before_fire(is_gl)
void weapon_before_fire(CWeapon* weapon, bool is_gl);

// wpn_knife_m1 attack combo and draw / holster sounds
bool knife_combo_applicable(CHudItem* item);
u32 knife_play_motion(CHudItem* item, const char* M, bool bMixIn, u32 state, bool randomAnim, float speed);

// transient HUD recoil of the weapon model
void hudrc_apply();
void hudrc_restore();

// set_cost for game_object (NLC 3.0)
void set_cost(CScriptGameObject* self, u32 cost);
void set_cost_registered();

// PDA hack helpers
CUIDialogWnd* pda3d_top_receiver();
bool pda3d_draw_hack(CUIPdaWnd* pda, u32& last_frame);
bool pda3d_block_input_receiver(CUIDialogWnd* ir);
void pda3d_trace_action(s32 cmd, u32 flags, u32 slot_before, u32 slot_after, bool result, bool is_actor);

class CHudRenderUIGuard
{
    CUIDialogWnd* m_wnd{};
    bool m_saved{};

public:
    CHudRenderUIGuard();
    ~CHudRenderUIGuard();
};
} // namespace fl_hook
