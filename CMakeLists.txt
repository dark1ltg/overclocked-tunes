cmake_minimum_required(VERSION 3.16)
project(OCRCatalog VERSION 1.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(OCR_REQUIRE_TORRENT "Fail configure if libtorrent-rasterbar is missing" OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Sql Concurrent Svg)
find_package(Qt6 COMPONENTS Multimedia QUIET)
find_package(Qt6 COMPONENTS Test QUIET)

set(OCR_HAS_TORRENT FALSE)
set(OCR_TORRENT_TARGET "")

find_package(LibtorrentRasterbar CONFIG QUIET)
if (LibtorrentRasterbar_FOUND)
    if (TARGET LibtorrentRasterbar::torrent-rasterbar)
        set(OCR_TORRENT_TARGET LibtorrentRasterbar::torrent-rasterbar)
        set(OCR_HAS_TORRENT TRUE)
    elseif (TARGET torrent-rasterbar)
        set(OCR_TORRENT_TARGET torrent-rasterbar)
        set(OCR_HAS_TORRENT TRUE)
    endif()
endif()

if (NOT OCR_HAS_TORRENT)
    find_package(unofficial-libtorrent CONFIG QUIET)
    if (TARGET unofficial::libtorrent::libtorrent)
        set(OCR_TORRENT_TARGET unofficial::libtorrent::libtorrent)
        set(OCR_HAS_TORRENT TRUE)
    endif()
endif()

if (NOT OCR_HAS_TORRENT)
    find_package(PkgConfig QUIET)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules(LIBTORRENT QUIET IMPORTED_TARGET libtorrent-rasterbar)
        if (TARGET PkgConfig::LIBTORRENT)
            set(OCR_TORRENT_TARGET PkgConfig::LIBTORRENT)
            set(OCR_HAS_TORRENT TRUE)
        endif()
    endif()
endif()

if (NOT OCR_HAS_TORRENT)
    find_path(LIBTORRENT_INCLUDE_DIR libtorrent/session.hpp)
    find_library(LIBTORRENT_LIBRARY NAMES torrent-rasterbar libtorrent-rasterbar)
    if (LIBTORRENT_INCLUDE_DIR AND LIBTORRENT_LIBRARY)
        add_library(OCR::libtorrent UNKNOWN IMPORTED)
        set_target_properties(OCR::libtorrent PROPERTIES
            IMPORTED_LOCATION "${LIBTORRENT_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LIBTORRENT_INCLUDE_DIR}")
        set(OCR_TORRENT_TARGET OCR::libtorrent)
        set(OCR_HAS_TORRENT TRUE)
    endif()
endif()

if (OCR_REQUIRE_TORRENT AND NOT OCR_HAS_TORRENT)
    message(FATAL_ERROR "libtorrent-rasterbar is required for in-app official torrents.")
endif()

set(OCR_SOURCES
    src/main.cpp
    src/HtmlParsers.cpp
    src/Settings.cpp
    src/CatalogStore.cpp
    src/HttpClient.cpp
    src/CatalogSync.cpp
    src/DownloadQueue.cpp
    src/DesktopIntegrator.cpp
    src/OfficialTorrentPolicy.cpp
    src/OfficialTorrentEngine.cpp
    src/MainWindow.cpp
    src/DocsScreenshots.cpp
    src/resources.qrc
)

add_executable(OCRCatalog WIN32 MACOSX_BUNDLE ${OCR_SOURCES})
target_include_directories(OCRCatalog PRIVATE src)
target_link_libraries(OCRCatalog PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Sql
    Qt6::Concurrent
    Qt6::Svg
)
if (TARGET Qt6::Multimedia)
    target_compile_definitions(OCRCatalog PRIVATE OCRCATALOG_HAS_MULTIMEDIA)
    target_link_libraries(OCRCatalog PRIVATE Qt6::Multimedia)
endif()
if (OCR_HAS_TORRENT)
    target_compile_definitions(OCRCatalog PRIVATE OCRCATALOG_HAS_TORRENT)
    target_link_libraries(OCRCatalog PRIVATE ${OCR_TORRENT_TARGET})
    if (WIN32)
        target_compile_definitions(OCRCatalog PRIVATE _WIN32_WINNT=0x0A00)
    endif()
    if (MSVC)
        target_compile_options(OCRCatalog PRIVATE /EHsc /bigobj)
    endif()
    message(STATUS "In-app official torrents: libtorrent-rasterbar")
else()
    message(STATUS "In-app official torrents: disabled (libtorrent-rasterbar not found)")
endif()

set_target_properties(OCRCatalog PROPERTIES
    OUTPUT_NAME OCRCatalog
    MACOSX_BUNDLE_BUNDLE_NAME "OCR Catalog"
    MACOSX_BUNDLE_GUI_IDENTIFIER org.ocrcatalog.OCRCatalog
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    WIN32_EXECUTABLE TRUE
)

install(TARGETS OCRCatalog RUNTIME DESTINATION bin BUNDLE DESTINATION .)
if (UNIX AND NOT APPLE)
    install(FILES packaging/linux/org.ocrcatalog.OCRCatalog.desktop
            DESTINATION share/applications)
    install(FILES packaging/linux/org.ocrcatalog.OCRCatalog.metainfo.xml
            DESTINATION share/metainfo)
    install(FILES src/resources/icons/org.ocrcatalog.OCRCatalog.svg
            DESTINATION share/icons/hicolor/scalable/apps)
endif()

enable_testing()
add_subdirectory(tests)
