# Ride Like the Wind needs the core's OnPlayerTaxiFlightSpeed hook (PlayerScript). Older cores
# don't have it; the module still builds there and the perk does nothing. The probe reads the
# core header itself, so a fork that has the hook is found whatever its version.
file(READ "${CMAKE_SOURCE_DIR}/src/server/game/Scripting/ScriptDefines/PlayerScript.h" _gl_player_script_h)
string(FIND "${_gl_player_script_h}" "PLAYERHOOK_ON_TAXI_FLIGHT_SPEED" _gl_pos)
if (NOT _gl_pos EQUAL -1)
    foreach (GL_TARGET modules mod_mod-guild-levels)
        if (TARGET ${GL_TARGET})
            target_compile_definitions(${GL_TARGET} PRIVATE GL_CORE_HAS_TAXI_SPEED=1)
        endif()
    endforeach()
endif()
