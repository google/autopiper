set(SRCS main.cc cmdline-driver.cc
         ir.cc ir-parser.cc ir-crosslinker.cc ir-typechecker.cc
         pipe.cc lower.cc pipe-timing.cc
         gen-verilog.cc gen-printer.cc
         compiler.cc)

# Optionally build a statically-linked executable. This comes first so that
# static versions of third-party libraries are found below.
if (STATIC_BUILD)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    set(BUILD_SHARED_LIBRARIES OFF)
    set(CMAKE_EXE_LINKER_FLAGS "-static")
endif(STATIC_BUILD)

# Require GMP and boost's wrapper around GMP for arbitrary-size bignums.
find_package(Boost 1.36.0 REQUIRED)
find_path(GMP_INCLUDE_DIR NAMES gmp.h)
find_library(GMP_LIBRARIES NAMES gmp libgmp)
set(AUTOPIPER_LIBS ${Boost_LIBRARIES} ${GMP_LIBRARIES})
include_directories(${GMP_INCLUDE_DIR} ${Boost_INCLUDE_DIRS})

# Generate a config header with the current release number.
configure_file("build-config.h.in" "${PROJECT_BINARY_DIR}/build-config.h")

# Set the incldue path so that project-local includes and config header
# includes work.
include_directories(.)
include_directories("${PROJECT_BINARY_DIR}")

# Build the main compiler with the above sources.
add_executable(autopiper ${SRCS})
target_link_libraries(autopiper ${AUTOPIPER_LIBS})
