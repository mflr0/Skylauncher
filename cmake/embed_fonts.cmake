# Writes OUTPUT, a C++ file defining the font byte arrays declared in src/embedded_fonts.h.
# Run as a script: cmake -DOUTFIT=<ttf> -DBUNGEE=<ttf> -DOUTPUT=<cpp> -P embed_fonts.cmake
function(embed_array name path)
    file(READ "${path}" bytes HEX)
    string(LENGTH "${bytes}" length)
    math(EXPR size "${length} / 2")
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${bytes}")
    string(REGEX REPLACE "(0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,0x..,)" "\\1\n" bytes "${bytes}")
    file(APPEND "${OUTPUT}" "extern const unsigned char ${name}[] = {\n${bytes}\n};\nextern const size_t ${name}Size = ${size};\n")
endfunction()

file(WRITE "${OUTPUT}" "// Generated from resources/fonts by cmake/embed_fonts.cmake. Do not edit.\n#include \"embedded_fonts.h\"\n")
embed_array(g_embeddedOutfit "${OUTFIT}")
embed_array(g_embeddedBungee "${BUNGEE}")
