# Leela 0.11 (Gian-Carlo Pascutto, MIT), rebuilt for Gifu: no Boost, no BLAS library,
# no OpenCL SDK. Two programs from the same sources, as the original shipped:
#   leela         CPU, the 128-filter policy net and built-in matrix routines (Gemm.h)
#   leela-opencl  GPU, the 192-filter policy net; OpenCL is loaded when the program
#                 starts (OpenCLLoader.cpp), so it builds without an SDK and fails with
#                 a message on machines without an OpenCL driver
# Both read the network from leela-0.11.weights, copied next to them.
enable_language(CXX)

file(GLOB LEELA_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
list(REMOVE_ITEM LEELA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/OpenCL.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/OpenCLLoader.cpp)
set(LEELA_OUT ${CMAKE_BINARY_DIR}/leela)

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
