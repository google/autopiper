set(SRCS fe/main.clj)

# This is a bit hackish, but it's a workable way of integrating Leiningen, the
# Clojure build tool, with CMake. We take a template project.clj (lein's config
# file) and do variable substitution, which will produce a file with an
# absolute source path. We place this file in the out-of-source build tree.
# Lein thinks the build tree is the project tree, finds the source via absolute
# paths, and places the build artifacts in the build tree.

configure_file("project.clj.in" "${PROJECT_BINARY_DIR}/project.clj")

add_custom_target(autopiper_frontend_jar ALL
    COMMAND lein uberjar
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
    COMMENT Building Autopiper frontend jar with Leiningen
    SOURCES ${SRCS})

add_custom_command(
    DEPENDS autopiper_frontend_jar
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
    COMMAND cp ${PROJECT_SOURCE_DIR}/src/frontend/autopiper src/frontend/
    COMMAND chmod a+x src/frontend/autopiper
    COMMAND cp target/frontend.jar src/frontend/autopiper-frontend.jar
    OUTPUT ${PROJECT_BINARY_DIR}/src/frontend/autopiper ${PROJECT_BINARY_DIR}/src/frontend/autopiper-frontend.jar)
add_custom_target(autopiper_frontend_wrapper_force ALL
    DEPENDS ${PROJECT_BINARY_DIR}/src/frontend/autopiper)

install(FILES ${PROJECT_BINARY_DIR}/src/frontend/autopiper-frontend.jar DESTINATION ${CMAKE_INSTALL_PREFIX}/share/autopiper)
install(FILES ${PROJECT_BINARY_DIR}/src/frontend/autopiper DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
    
