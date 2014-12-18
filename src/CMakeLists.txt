set(BACKEND_SRCS
    backend/ir.cc
    backend/ir-parser.cc
    backend/ir-crosslinker.cc
    backend/ir-typechecker.cc
    backend/pipe.cc
    backend/lower.cc
    backend/pipe-timing.cc
    backend/gen-verilog.cc
    backend/gen-printer.cc
    backend/compiler.cc
    backend/cmdline-driver.cc)

set(BACKEND_DRIVER
    backend/main.cc)

set(FRONTEND_SRCS
    frontend/parser.cc
    frontend/ast.cc
    frontend/visitor.cc
    frontend/func-inline.cc
    frontend/var-scope.cc
    frontend/type.cc
    frontend/agg-types.cc
    frontend/type-infer.cc
    frontend/type-lower.cc
    frontend/codegen.cc
    frontend/compiler.cc
    frontend/cmdline-driver.cc)

set(FRONTEND_DRIVER
    frontend/main.cc)

set(COMMON_SRCS
    common/parse-args.cc)

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
include_directories(backend)
include_directories(frontend)
include_directories("${PROJECT_BINARY_DIR}")

# Build the backend sources into a library to share between backend and
# frontend command-line binaries.
add_library(backend STATIC ${BACKEND_SRCS})

# Build the main compiler backend with the above sources.
add_executable(autopiper-backend ${BACKEND_DRIVER} ${COMMON_SRCS})
target_link_libraries(autopiper-backend backend)
target_link_libraries(autopiper-backend ${AUTOPIPER_LIBS})

add_executable(autopiper ${FRONTEND_SRCS} ${FRONTEND_DRIVER} ${COMMON_SRCS})
target_link_libraries(autopiper backend)
target_link_libraries(autopiper ${AUTOPIPER_LIBS})
