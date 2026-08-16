set(SQLITE3_SOURCE_DIR "${CMAKE_SOURCE_DIR}/include/sqlite")
set(SQLITE3_AMALGAMATION_DIR "${CMAKE_BINARY_DIR}/sqlite-amalgamation")

find_program(SQLITE3_MAKE_EXECUTABLE NAMES gmake make REQUIRED)

set(SQLITE3_HEADERS
    "${SQLITE3_AMALGAMATION_DIR}/sqlite3.h"
    "${SQLITE3_AMALGAMATION_DIR}/sqlite3ext.h"
)
set(SQLITE3_SOURCE "${SQLITE3_AMALGAMATION_DIR}/sqlite3.c")

# SQLite recommends compiling its generated amalgamation. Generate it in the
# CMake build tree so a fresh checkout never depends on ignored files inside
# the SQLite submodule.
file(MAKE_DIRECTORY "${SQLITE3_AMALGAMATION_DIR}")
add_custom_command(
    OUTPUT ${SQLITE3_SOURCE} ${SQLITE3_HEADERS}
    COMMAND "${CMAKE_COMMAND}" -E env "CC=${CMAKE_C_COMPILER}"
            "${SQLITE3_SOURCE_DIR}/configure"
    COMMAND "${SQLITE3_MAKE_EXECUTABLE}" sqlite3.c
    WORKING_DIRECTORY "${SQLITE3_AMALGAMATION_DIR}"
    DEPENDS
        "${SQLITE3_SOURCE_DIR}/VERSION"
        "${SQLITE3_SOURCE_DIR}/configure"
        "${SQLITE3_SOURCE_DIR}/manifest"
        "${SQLITE3_SOURCE_DIR}/src/sqlite.h.in"
        "${SQLITE3_SOURCE_DIR}/tool/mksqlite3c.tcl"
    COMMENT "Generating the SQLite amalgamation"
    VERBATIM
)

add_library(sqlite3 STATIC ${SQLITE3_SOURCE})
target_include_directories(sqlite3 PUBLIC "${SQLITE3_AMALGAMATION_DIR}")

set(SQLITE3_LIBRARIES sqlite3)
