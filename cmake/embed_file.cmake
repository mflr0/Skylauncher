# Writes OUTPUT, a C++ file defining `extern const unsigned char NAME[]` (INPUT's bytes) and `extern const size_t
# NAMESize`. Run as a script: cmake -DNAME=<symbol> -DINPUT=<file> -DOUTPUT=<cpp> -P embed_file.cmake
file(READ "${INPUT}" bytes HEX)
string(LENGTH "${bytes}" length)
math(EXPR size "${length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${bytes}")
string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" bytes "${bytes}")
get_filename_component(source "${INPUT}" NAME)
file(WRITE "${OUTPUT}" "// Generated from ${source} by cmake/embed_file.cmake. Do not edit.\n#include <cstddef>\n"
    "extern const unsigned char ${NAME}[] = {\n${bytes}\n};\nextern const size_t ${NAME}Size = ${size};\n")
