#include "stdafx.h"
#include "CustomHUD.h"

// NLC: fresh-start defaults (no user.ltx): crosshair, dynamic crosshair and NPC info off; hard crosshair and status icons on
Flags32 psHUD_Flags{HUD_DRAW | HUD_CROSSHAIR_RT | HUD_CROSSHAIR_RT2 | HUD_DRAW_RT | HUD_CROSSHAIR_HARD | HUD_SHOW_STATUS_ICONS};

ENGINE_API CCustomHUD* g_hud = nullptr;

CCustomHUD::CCustomHUD()
{
    // g_hud = this; ???
}

CCustomHUD::~CCustomHUD()
{
    g_hud = nullptr;
}
