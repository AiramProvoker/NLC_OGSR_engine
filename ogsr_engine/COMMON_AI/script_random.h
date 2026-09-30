#pragma once

#include "script_export_space.h"

typedef class_exporter<CRandom> CScriptRandom;
add_to_type_list(CScriptRandom)
#undef script_type_list
#define script_type_list save_type_list(CScriptRandom)
