# bin2header.cmake — Convert a binary file into a C header with a byte array.
#
# Usage (called by CMake at build time):
#   cmake -DINPUT=<file> -DOUTPUT=<file> -DARRAY_NAME=<name> -P bin2header.cmake

file(READ "${INPUT}" hex HEX)

# Convert each byte pair to "0xNN," in one regex pass. A per-byte
# loop is O(n^2) in CMake because every iteration re-expands the
# whole hex string, which made the build appear to hang on the font.
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")

# Wrap every 12 bytes. CMake's regex has no {n} quantifier, so the
# 12-byte pattern is built with string(REPEAT). The trailing comma
# after the last byte is valid C, so no special case is needed.
string(REPEAT "0x[0-9a-f][0-9a-f]," 12 row)
string(REGEX REPLACE "(${row})" "\\1\n    " bytes "${bytes}")

file(WRITE "${OUTPUT}"
  "// Auto-generated from ${INPUT} — do not edit.\n"
  "static const unsigned char ${ARRAY_NAME}[] = {\n    ${bytes}\n};\n")
