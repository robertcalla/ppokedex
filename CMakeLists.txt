# PC build of the save-reading core, for testing parsers without a Switch.
# The Switch build uses the Makefile (devkitPro).
cmake_minimum_required(VERSION 3.16)
project(pokedex_pc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(pkxcore STATIC
    source/core/bytes.cpp
    source/core/collection.cpp
    source/core/dexdata.cpp
    source/core/games.cpp
    source/core/pkm.cpp
    source/core/registry.cpp
    source/core/swish.cpp
)
target_compile_options(pkxcore PRIVATE -Wall -Wextra)

add_executable(pokedex_cli pc/cli.cpp)
target_link_libraries(pokedex_cli pkxcore)
