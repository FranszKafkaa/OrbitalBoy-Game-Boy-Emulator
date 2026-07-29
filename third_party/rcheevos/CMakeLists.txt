set(RCHEEVOS_SOURCES
    src/rc_compat.c
    src/rc_client.c
    src/rc_util.c
    src/rc_version.c
    src/rapi/rc_api_common.c
    src/rapi/rc_api_editor.c
    src/rapi/rc_api_info.c
    src/rapi/rc_api_runtime.c
    src/rapi/rc_api_user.c
    src/rcheevos/alloc.c
    src/rcheevos/condition.c
    src/rcheevos/condset.c
    src/rcheevos/consoleinfo.c
    src/rcheevos/format.c
    src/rcheevos/lboard.c
    src/rcheevos/memref.c
    src/rcheevos/operand.c
    src/rcheevos/rc_validate.c
    src/rcheevos/richpresence.c
    src/rcheevos/runtime.c
    src/rcheevos/runtime_progress.c
    src/rcheevos/trigger.c
    src/rcheevos/value.c
    src/rhash/hash.c
    src/rhash/hash_rom.c
    src/rhash/md5.c
)

add_library(rcheevos STATIC ${RCHEEVOS_SOURCES})
target_include_directories(rcheevos PUBLIC include PRIVATE src/rcheevos)
target_compile_definitions(rcheevos PRIVATE
    RC_DISABLE_LUA=1
    RC_CLIENT_SUPPORTS_HASH=1
    RC_HASH_NO_DISC=1
    RC_HASH_NO_ZIP=1
    RC_HASH_NO_ENCRYPTED=1
)
set_target_properties(rcheevos PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
