# Writes one SPIR-V file as a C++ header of 32-bit words --
# docs/specs/UTA-0014-vulkan-draw-path.md SS 4.2.
#
#   cmake -DINPUT=<file.spv> -DOUTPUT=<file.h> -DNAME=<identifier> -P EmbedSpirv.cmake
#
# A CMake script rather than `xxd -i`: the Visual Studio generator runs a custom
# command through cmd.exe, where no xxd exists, and this runs wherever CMake
# does. Words rather than bytes, because VkShaderModuleCreateInfo::pCode is a
# pointer to uint32_t and a byte array carries no guarantee of that alignment.
# SPIR-V is little-endian on disk, so each word's four bytes are reversed.

file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" digits)
math(EXPR remainder "${digits} % 8")
if(digits EQUAL 0 OR NOT remainder EQUAL 0)
    message(FATAL_ERROR "${INPUT} is ${digits} hex digits long, which is not a whole number of SPIR-V words")
endif()

string(REGEX REPLACE "([0-9a-f][0-9a-f])([0-9a-f][0-9a-f])([0-9a-f][0-9a-f])([0-9a-f][0-9a-f])"
                     "0x\\4\\3\\2\\1," words "${hex}")
string(REGEX REPLACE "((0x[0-9a-f]+,){12})" "\\1\n" words "${words}")

file(WRITE "${OUTPUT}"
    "// Generated from ${INPUT} by EmbedSpirv.cmake. Do not edit.\n"
    "#pragma once\n"
    "#include <cstdint>\n"
    "inline constexpr std::uint32_t ${NAME}[] = {\n${words}\n};\n")
