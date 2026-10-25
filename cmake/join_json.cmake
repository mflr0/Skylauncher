# Writes OUTPUT, a JSON array of the JSON files in INPUTS ('|'-separated), in that order: the per-game art manifests
# (games/<id>/art.json) built into the executables with embed_file.cmake. Run as a script:
#   cmake -DINPUTS=<a.json|b.json> -DOUTPUT=<file> -P join_json.cmake
string(REPLACE "|" ";" inputs "${INPUTS}")
set(text "[\n")
set(first TRUE)
foreach(input IN LISTS inputs)
    file(READ "${input}" content)
    if(NOT first)
        string(APPEND text ",\n")
    endif()
    string(APPEND text "${content}")
    set(first FALSE)
endforeach()
string(APPEND text "]\n")
file(WRITE "${OUTPUT}" "${text}")
