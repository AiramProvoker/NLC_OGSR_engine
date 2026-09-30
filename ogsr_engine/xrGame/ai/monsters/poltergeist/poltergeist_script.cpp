#include "stdafx.h"
#include "poltergeist.h"

using namespace luabind;


void CPoltergeist::script_register(lua_State* L)
{
    module(L)[class_<CPoltergeist, CGameObject>("CPoltergeist")
                  .def(constructor<>())
                  .def("enable_ability", &CPoltergeist::enable_ability)
                  .def("is_ability_enabled", &CPoltergeist::is_ability_enabled)];
}
