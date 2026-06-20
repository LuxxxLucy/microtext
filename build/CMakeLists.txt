cmake_minimum_required(VERSION 3.15)
project(microtext LANGUAGES C)

# Header-only INTERFACE target. microtext.h sits one level up, at the repo root.
# Consume with add_subdirectory(microtext/build) or find_package(microtext).
add_library(microtext INTERFACE)
add_library(microtext::microtext ALIAS microtext)

target_include_directories(microtext INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:include>)
target_compile_features(microtext INTERFACE c_std_99)

if(APPLE)
    target_link_libraries(microtext INTERFACE
        "-framework CoreText"
        "-framework CoreGraphics"
        "-framework CoreFoundation")
endif()

include(GNUInstallDirs)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../microtext.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS microtext EXPORT microtextTargets)
install(EXPORT microtextTargets
    NAMESPACE microtext::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/microtext
    FILE microtextConfig.cmake)
