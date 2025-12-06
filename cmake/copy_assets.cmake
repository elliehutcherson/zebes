# Expected Variables:
# ACTION_SRC: Path to the source assets directory (e.g. .../assets)
# ACTION_DEST: Parent directory of the destination assets directory (e.g. .../build/src)
# COPY_SQL: Boolean
# COPY_TEXTURES: Boolean

message(STATUS "running copy_assets.cmake with SRC=${ACTION_SRC} DEST=${ACTION_DEST}")

# 1. Copy everything EXCEPT 'sql' and 'textures' directories to ensure other assets are synced.
# file(COPY) copies the directory itself if it is passed.
# This will copy ${ACTION_SRC} (which is 'assets') into ${ACTION_DEST}, creating ${ACTION_DEST}/assets.
file(COPY "${ACTION_SRC}" 
     DESTINATION "${ACTION_DEST}"
     PATTERN "sql" EXCLUDE
     PATTERN "textures" EXCLUDE
)

# Define paths for the specific subdirectories
# Note: We assume the structure inside 'assets' is 'zebes/sql' and 'zebes/textures' 
# OR just 'sql' and 'textures' directly?
# Based on ls output: assets/zebes/sql and assets/zebes/textures.
# AND the exclude above works on partial path matching?
# "The PATTERN option will match the full path of the file or directory" - unlikely, usually simple glob.
# CMake docs: "PATTERN matches the filename only."
# So excluding "sql" matches ".../sql".

set(SRC_SQL "${ACTION_SRC}/zebes/sql")
set(DEST_SQL "${ACTION_DEST}/assets/zebes/sql")
set(SRC_TEX "${ACTION_SRC}/zebes/textures")
set(DEST_TEX "${ACTION_DEST}/assets/zebes/textures")

# 2. Check and copy SQL
if(EXISTS "${SRC_SQL}")
    if(COPY_SQL OR NOT EXISTS "${DEST_SQL}")
        message(STATUS "Copying SQL assets from ${SRC_SQL} to ${DEST_SQL} (Force: ${COPY_SQL})")
        # We want to copy the CONTENTS of src/sql to dest/sql
        # or copy directory 'sql' to 'dest/assets/zebes'
        file(COPY "${SRC_SQL}" DESTINATION "${ACTION_DEST}/assets/zebes")
    else()
        message(STATUS "Skipping SQL assets copy (Destination exists and COPY_SQL is OFF)")
    endif()
endif()

# 3. Check and copy Textures
if(EXISTS "${SRC_TEX}")
    if(COPY_TEXTURES OR NOT EXISTS "${DEST_TEX}")
        message(STATUS "Copying Texture assets from ${SRC_TEX} to ${DEST_TEX} (Force: ${COPY_TEXTURES})")
        file(COPY "${SRC_TEX}" DESTINATION "${ACTION_DEST}/assets/zebes")
    else()
        message(STATUS "Skipping Texture assets copy (Destination exists and COPY_TEXTURES is OFF)")
    endif()
endif()
