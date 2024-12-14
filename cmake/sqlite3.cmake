# Steps to build
# 1. make a build directory: mkdir include/sqlite/build
# 2. cd include/sqlite/build
# 3. ../configure
# 4. make sqlite3
# 5. profit =)

set(SQLITE3_SOURCES_DIR ${CMAKE_SOURCE_DIR}/include/sqlite/build)

set(SQLITE3_HEADERS
    ${SQLITE3_SOURCES_DIR}/sqlite3.h
    ${SQLITE3_SOURCES_DIR}/sqlite3ext.h
)

set(SQLITE3_SOURCES
    ${SQLITE3_SOURCES_DIR}/sqlite3.c
)

add_library(sqlite3 STATIC ${SQLITE3_HEADERS} ${SQLITE3_SOURCES})

set(SQLITE3_LIBRARIES sqlite3)