file(GLOB tests
	LIST_DIRECTORIES false
	CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/*.t)

function(compressor TOOL EXT)
	foreach(t ${tests})
		string(REGEX REPLACE ".*/([^/]*)\\.t$"
			"\\1" t
			${t})
		set(tn "${t}-${TOOL}")
		if(CMAKE_VERSION VERSION_LESS 3.19)
			string(REPLACE " " "_" tn "${tn}")
			string(REPLACE "#" "♯" tn "${tn}")
			string(REPLACE "(" "⦅" tn "${tn}")
			string(REPLACE ")" "⦆" tn "${tn}")
		endif()
		add_test(NAME ${tn}
			COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh "${t}")
		set_tests_properties(${tn} PROPERTIES
			SKIP_RETURN_CODE 42
			ENVIRONMENT "TOOL=${TOOL};EXT=${EXT};SRC=${CMAKE_SOURCE_DIR};BIN=${CMAKE_BINARY_DIR}")
	endforeach()
endfunction()

if(HAVE_LIBZ)
	compressor(gzip .gz)
endif()
if(HAVE_LIBBZ2)
	compressor(bzip2 .bz2)
endif()
if(HAVE_LIBLZMA)
	compressor(xz .xz)
endif()
if(HAVE_LIBZSTD)
	compressor(zstd .zst)
endif()

file(GLOB Tests
	LIST_DIRECTORIES false
	CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/*.T)

foreach(t ${Tests})
	string(REGEX REPLACE ".*/([^/]*)\\.T$"
		"\\1" t
		${t})
	set(tn ${t})
	if(CMAKE_VERSION VERSION_LESS 3.19)
		string(REPLACE " " "_" tn "${tn}")
		string(REPLACE "#" "♯" tn "${tn}")
		string(REPLACE "(" "⦅" tn "${tn}")
		string(REPLACE ")" "⦆" tn "${tn}")
	endif()
	add_test(NAME ${tn}
		COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/runtest.sh "${t}")
	set_tests_properties(${tn} PROPERTIES
		SKIP_RETURN_CODE 42
		ENVIRONMENT "SRC=${CMAKE_SOURCE_DIR};BIN=${CMAKE_BINARY_DIR}")
endforeach()
