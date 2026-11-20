cmake_minimum_required(VERSION 3.16)
project(OCRCatalog VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Sql Concurrent Svg)
find_package(Qt6 COMPONENTS Multimedia QUIET)
find_package(Qt6 COMPONENTS Test QUIET)
find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
    pkg_check_modules(LIBTORRENT QUIET IMPORTED_TARGET libtorrent-rasterbar)
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
if (TARGET PkgConfig::LIBTORRENT)
    target_compile_definitions(OCRCatalog PRIVATE OCRCATALOG_HAS_TORRENT)
    target_link_libraries(OCRCatalog PRIVATE PkgConfig::LIBTORRENT)
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
