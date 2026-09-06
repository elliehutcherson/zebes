# Wraps one text file in a C++ raw string literal so a tool can serve it without
# depending on the file's path at run time.
#
# Run with -P from a custom command:
#   cmake -DINPUT=<file> -DOUTPUT=<header> -DSYMBOL=<name> -P embed_text_file.cmake

foreach(required INPUT OUTPUT SYMBOL)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "embed_text_file.cmake requires -D${required}")
  endif()
endforeach()

file(READ "${INPUT}" CONTENT)

# The delimiter has to be absent from the content or the literal ends early and
# the generated header fails to compile somewhere confusing.
if(CONTENT MATCHES "\\)zebesembed\"")
  message(FATAL_ERROR "${INPUT} contains the raw string delimiter )zebesembed\"")
endif()

get_filename_component(SOURCE_NAME "${INPUT}" NAME)

file(WRITE "${OUTPUT}"
"// Generated from ${SOURCE_NAME} by cmake/embed_text_file.cmake. Do not edit.\n"
"#pragma once\n"
"\n"
"namespace zebes {\n"
"\n"
"inline constexpr char ${SYMBOL}[] = R\"zebesembed(${CONTENT})zebesembed\";\n"
"\n"
"}  // namespace zebes\n")
