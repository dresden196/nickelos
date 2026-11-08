# NickelOS: Mozc's session handler in WebAssembly (mozc_wasm.cc), on Mozc
# as fcitx-contrib/fcitx5-mozc builds it with CMake.
#   emcmake cmake -B build -DFCITX5_MOZC=/path/to/fcitx5-mozc
# The dictionary, data_manager/oss/mozc_data.inc, goes in
# build/fcitx5-mozc first (fcitx5-mozc's release has it).
cmake_minimum_required(VERSION 3.21)
project(nickel-mozc CXX)
set(CMAKE_CXX_STANDARD 20)
set(BUILD_MOZC_ADDON OFF CACHE BOOL "" FORCE)
add_subdirectory(${FCITX5_MOZC} fcitx5-mozc)

add_executable(nickel-mozc mozc_wasm.cc)
target_include_directories(nickel-mozc PRIVATE
    ${FCITX5_MOZC}/mozc/src
    ${FCITX5_MOZC}/abseil-cpp
    ${FCITX5_MOZC}/protobuf/src
    ${FCITX5_MOZC}/protobuf/third_party/utf8_range
    ${CMAKE_BINARY_DIR}/fcitx5-mozc)
target_compile_options(nickel-mozc PRIVATE -funsigned-char)
target_link_libraries(nickel-mozc libprotobuf mozc-static absl::flags_parse
                      absl::log_flags)
set_target_properties(nickel-mozc PROPERTIES SUFFIX ".js")
target_link_options(nickel-mozc PRIVATE
    -sMODULARIZE=1
    -sEXPORT_NAME=createNickelMozc
    "-sEXPORTED_FUNCTIONS=['_mozc_init','_mozc_eval','_mozc_versions','_mozc_is_valid_reading','_mozc_pos_list']"
    "-sEXPORTED_RUNTIME_METHODS=['cwrap','FS']"
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=134217728
    -sSTACK_SIZE=1048576
    -sENVIRONMENT=web
    -sFORCE_FILESYSTEM=1
    -lidbfs.js)
