# embed_file(INPUT OUTPUT VAR_NAME)
# Convert a file into a C++ header containing an inline constexpr const char*.
function(embed_file INPUT OUTPUT VAR_NAME)
  file(READ "${INPUT}" content)
  string(REPLACE "\\" "\\\\" content "${content}")
  string(REPLACE "\"" "\\\"" content "${content}")
  string(REPLACE "\n" "\\n\"\n\"" content "${content}")
  file(WRITE "${OUTPUT}"
    "// Auto-generated from ${INPUT} - do not edit\n"
    "#pragma once\n"
    "namespace xtils {\n"
    "inline constexpr const char* ${VAR_NAME} =\n"
    "\"${content}\";\n"
    "}  // namespace xtils\n"
  )
endfunction()
