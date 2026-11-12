# Leela 0.11 (Gian-Carlo Pascutto, MIT), rebuilt for Gifu: no Boost, no BLAS library,
# no OpenCL SDK. Two programs from the same sources, as the original shipped:
#   leela         CPU, the 128-filter policy net and built-in matrix routines (Gemm.h)
#   leela-opencl  GPU, the 192-filter policy net; OpenCL is loaded when the program
#                 starts (OpenCLLoader.cpp), so it builds without an SDK and fails with
#                 a message on machines without an OpenCL driver
# Both read the network from leela-0.11.weights, copied next to them.
enable_language(CXX)

# GIFU_LEELA_PREBUILT: a folder holding leela[.exe] and leela-opencl[.exe] built earlier
# from these same sources (CI keeps them in its cache, keyed by a hash of leela/):
# they are copied instead of compiled. Empty or incomplete: built from source.
set(GIFU_LEELA_PREBUILT "" CACHE PATH "Prebuilt leela / leela-opencl to use instead of compiling (CI cache)")
set(LEELA_EXE "${CMAKE_EXECUTABLE_SUFFIX}")
set(LEELA_OUT ${CMAKE_BINARY_DIR}/leela)
if(GIFU_LEELA_PREBUILT AND EXISTS "${GIFU_LEELA_PREBUILT}/leela${LEELA_EXE}" AND EXISTS "${GIFU_LEELA_PREBUILT}/leela-opencl${LEELA_EXE}")
    message(STATUS "Leela: prebuilt programs from ${GIFU_LEELA_PREBUILT}")
    foreach(name leela leela-opencl)
        add_custom_target(${name} ALL
            COMMAND ${CMAKE_COMMAND} -E make_directory ${LEELA_OUT}
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${GIFU_LEELA_PREBUILT}/${name}${LEELA_EXE}" "${LEELA_OUT}/${name}${LEELA_EXE}"
            VERBATIM)
        install(PROGRAMS "${GIFU_LEELA_PREBUILT}/${name}${LEELA_EXE}" DESTINATION leela)
    endforeach()
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/weights/leela-0.11.weights ${LEELA_OUT}/leela-0.11.weights COPYONLY)
    install(FILES weights/leela-0.11.weights DESTINATION leela)
    install(FILES LICENSE DESTINATION licenses RENAME leela-LICENSE.txt)
    install(FILES opencl/LICENSE DESTINATION licenses RENAME opencl-headers-LICENSE.txt)
    return()
endif()

file(GLOB LEELA_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
list(REMOVE_ITEM LEELA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/OpenCL.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/OpenCLLoader.cpp)

find_package(Threads REQUIRED)

function(leela_program name)
    add_executable(${name} ${LEELA_SOURCES} ${ARGN})
    set_target_properties(${name} PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
        RUNTIME_OUTPUT_DIRECTORY ${LEELA_OUT})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    # An engine: always optimised (a debug build of Gifu still gets a usable opponent).
    target_compile_options(${name} PRIVATE -O3 -ffast-math -w)
    target_compile_definitions(${name} PRIVATE NDEBUG)
    target_link_libraries(${name} PRIVATE Threads::Threads)
    if(WIN32)
        target_link_options(${name} PRIVATE -static)
    elseif(NOT APPLE)
        target_link_options(${name} PRIVATE -static-libgcc -static-libstdc++)
    endif()
endfunction()

leela_program(leela)

leela_program(leela-opencl src/OpenCL.cpp src/OpenCLLoader.cpp)
target_compile_definitions(leela-opencl PRIVATE USE_OPENCL CL_TARGET_OPENCL_VERSION=120)
target_include_directories(leela-opencl SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/opencl)
if(NOT WIN32)
    target_link_libraries(leela-opencl PRIVATE ${CMAKE_DL_LIBS})
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/weights/leela-0.11.weights ${LEELA_OUT}/leela-0.11.weights COPYONLY)

# Shipped next to Gifu: <install>/leela/{leela, leela-opencl, leela-0.11.weights}.
install(TARGETS leela leela-opencl RUNTIME DESTINATION leela)
install(FILES weights/leela-0.11.weights DESTINATION leela)
install(FILES LICENSE DESTINATION licenses RENAME leela-LICENSE.txt)
install(FILES opencl/LICENSE DESTINATION licenses RENAME opencl-headers-LICENSE.txt)
