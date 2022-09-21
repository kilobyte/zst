cmake_minimum_required(VERSION 2.8.12)
project(zst C)
include(CheckIncludeFiles)
include(CheckLibraryExists)
include(CheckFunctionExists)
include(GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "RelWithDebInfo")
endif (NOT CMAKE_BUILD_TYPE)

function(find_lib define so func)
	CHECK_LIBRARY_EXISTS(${so} ${func} "" HAVE_LIB${define})
	if(${HAVE_LIB${define}})
		set(libs ${libs} ${so} PARENT_SCOPE)
	endif()
endfunction()
find_lib(Z z gzread)
find_lib(BZ2 bz2 BZ2_bzBuffToBuffCompress)
find_lib(LZMA lzma lzma_code)
find_lib(ZSTD zstd ZSTD_compress)

function(add_flag flag)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${flag}" PARENT_SCOPE)
endfunction()
add_flag(-Wall)
add_flag(-Werror=format-security)
add_flag(-Wmissing-declarations)
add_flag(-Wredundant-decls)
add_flag(-Wno-parentheses)

configure_file(config.h.cmake ${CMAKE_BINARY_DIR}/config.h)

set(zst_sources
	compress.c
	prefix.c
	zst.c
)

add_executable(zst ${zst_sources})
target_link_libraries(zst ${libs})

install(TARGETS zst DESTINATION bin)
#install(FILES zst.1 zst.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
#install(FILES ${CMAKE_BINARY_DIR}/zst DESTINATION bin)

#enable_testing()
#add_subdirectory(tests)
