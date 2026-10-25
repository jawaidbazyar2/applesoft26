cmake_minimum_required(VERSION 3.20)
project(applesoft26 LANGUAGES C OBJC)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")

add_library(applesoft_core STATIC
    src/common.c
    src/token.c
    src/tokenize.c
    src/program.c
    src/font.c
    src/vars.c
    src/screen.c
    src/expr.c
    src/exec.c
    src/peekpoke.c
)
target_include_directories(applesoft_core PUBLIC src)
target_link_libraries(applesoft_core PUBLIC m)
target_compile_options(applesoft_core PRIVATE -Wall -Wextra -Wno-unused-parameter)

if(APPLE)
    add_executable(applesoft MACOSX_BUNDLE
        src/main.m
        src/EditorController.m
        src/RunWindow.m
    )
    target_link_libraries(applesoft PRIVATE applesoft_core "-framework Cocoa" "-framework UniformTypeIdentifiers")
    target_compile_options(applesoft PRIVATE -fobjc-arc -Wall -Wextra)
    set_target_properties(applesoft PROPERTIES
        OUTPUT_NAME "AppleSoft"
        MACOSX_BUNDLE_BUNDLE_NAME "AppleSoft"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.applesoft26.AppleSoft"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/Info.plist"
    )
endif()

enable_testing()

function(as_test name)
    add_executable(${name} tests/${name}.c)
    target_link_libraries(${name} PRIVATE applesoft_core)
    target_compile_options(${name} PRIVATE -Wall -Wextra)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

as_test(test_tokenize)
as_test(test_program)
as_test(test_exec)
