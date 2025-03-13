cmake_minimum_required(VERSION 3.20)

include(CMakePrintHelpers)
find_package(PkgConfig REQUIRED)

add_executable(
	drmdraw
	main.cpp
	dumb_buffer.cpp
	ffmpeg_file.cpp
	ffmpeg_stream.cpp
	ffmpeg_scale.cpp
)

pkg_check_modules(LIBDRM REQUIRED libdrm alsa libavutil libavcodec libavformat libswscale libswresample)
cmake_print_variables(LIBDRM_CFLAGS LIBDRM_LDFLAGS LIBDRM_LINK_LIBRARIES LIBDRM_LIBRARIES)

target_compile_options(drmdraw PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${LIBDRM_CFLAGS}>)
target_link_libraries(drmdraw PRIVATE ${LIBDRM_LINK_LIBRARIES})

install(TARGETS drmdraw DESTINATION ${CMAKE_SOURCE_DIR}/)
