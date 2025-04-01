cmake_minimum_required(VERSION 3.23)

project(
    egt-launcher
    VERSION 1.5.1
    DESCRIPTION "A graphical launcher built upon the EGT library."
    LANGUAGES CXX
)
#set(PROJECT_VERSION_RC 1)
if (DEFINED PROJECT_VERSION_RC)
    string(APPEND PROJECT_VERSION "-rc${PROJECT_VERSION_RC}")
endif()

include(CheckIncludeFileCXX)
include(GNUInstallDirs)

set(CMAKE_VERBOSE_MAKEFILE True)

set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBEGT REQUIRED libegt>=1.10)

CHECK_INCLUDE_FILE_CXX(egt/detail/screen/kmsscreen.h HAVE_EGT_DETAIL_SCREEN_KMSSCREEN_H)

add_executable(egt-launcher src/launcher.cpp)

target_compile_definitions(egt-launcher PRIVATE DATADIR="${CMAKE_INSTALL_FULL_DATADIR}")

target_include_directories(egt-launcher PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/rapidxml
    ${CMAKE_BINARY_DIR}
)

target_include_directories(egt-launcher PRIVATE ${LIBEGT_INCLUDE_DIRS})
target_compile_options(egt-launcher PRIVATE ${LIBEGT_CFLAGS_OTHER})
target_link_directories(egt-launcher PRIVATE ${LIBEGT_LIBRARY_DIRS})
target_link_libraries(egt-launcher PRIVATE ${LIBEGT_LIBRARIES})
target_link_options(egt-launcher PRIVATE ${LIBEGT_LDFLAGS_OTHER})

target_compile_definitions(egt-launcher PRIVATE HAVE_CONFIG_H)
configure_file(_config.h.in ${CMAKE_BINARY_DIR}/config.h @ONLY)

install(TARGETS egt-launcher RUNTIME)
install(FILES taglines.txt
        DESTINATION ${CMAKE_INSTALL_DATADIR}/egt/launcher
)
install(FILES launch.sh
        DESTINATION ${CMAKE_INSTALL_DATADIR}/egt/launcher
        PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ WORLD_EXECUTE WORLD_READ
)
install(DIRECTORY images/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/egt/launcher
        FILES_MATCHING
        PATTERN background_720x1280.png
        PATTERN background_800x480.png
)
