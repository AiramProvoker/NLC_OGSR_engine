#include "stdafx.h"
#include "script_random.h"

using namespace luabind;

void CScriptRandom::script_register(lua_State* L)
{
    module(L)[class_<CRandom>("CRandom")
                  .def(constructor<>())
                  .def("randI", (s32(CRandom::*)())(&CRandom::randI))
                  .def("randI", (s32(CRandom::*)(const s32))(&CRandom::randI))
                  .def("randI", (s32(CRandom::*)(const s32, const s32))(&CRandom::randI))
                  .def("randIs", (s32(CRandom::*)(const s32))(&CRandom::randIs))
                  .def("randIs", (s32(CRandom::*)(const s32, const s32))(&CRandom::randIs))
                  .def("randF", (float(CRandom::*)())(&CRandom::randF))
                  .def("randF", (float(CRandom::*)(const float))(&CRandom::randF))
                  .def("randF", (float(CRandom::*)(const float, const float))(&CRandom::randF))
                  .def("randFs", (float(CRandom::*)(const float))(&CRandom::randFs))
                  .def("randFs", (float(CRandom::*)(const float, const float))(&CRandom::randFs))];
}
