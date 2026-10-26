# Writes OUTPUT, a C++ file with the language files as bytes: `g_embeddedLanguages` (code, data, size) and its count.
# Run as a script: cmake "-DINPUTS=<file>|<file>..." -DOUTPUT=<cpp> -P embed_languages.cmake
string(REPLACE "|" ";" files "${INPUTS}")
set(tables "")
set(entries "")
set(index 0)
foreach(file ${files})
    get_filename_component(code "${file}" NAME_WE)
    file(READ "${file}" bytes HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${bytes}")
    string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" bytes "${bytes}")
    string(APPEND tables "static const unsigned char lang${index}[] = {\n${bytes}\n};\n")
    string(APPEND entries "    { \"${code}\", lang${index}, sizeof(lang${index}) },\n")
    math(EXPR index "${index} + 1")
endforeach()
file(WRITE "${OUTPUT}" "// Generated from resources/lang by cmake/embed_languages.cmake. Do not edit.\n#include <cstddef>\n"
    "struct EmbeddedLanguage { const char* code; const unsigned char* data; size_t size; };\n${tables}"
    "extern const EmbeddedLanguage g_embeddedLanguages[] = {\n${entries}    { \"\", nullptr, 0 }\n};\n"
    "extern const size_t g_embeddedLanguageCount = ${index};\n")
