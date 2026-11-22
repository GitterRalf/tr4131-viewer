cmake_minimum_required(VERSION 3.21)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/version.h")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.h" tr4131_version_line
    REGEX "^#define[ \t]+TR4131_VIEWER_VERSION[ \t]+")
if(NOT tr4131_version_line MATCHES "\"([0-9.]+)\"")
    message(FATAL_ERROR "version.h must define TR4131_VIEWER_VERSION as a numeric version string.")
endif()

project(TR4131Viewer VERSION "${CMAKE_MATCH_1}" LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "TR4131-Viewer requires a Windows kit.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
enable_language(RC)

set(TR4131_QT_MAJOR 6 CACHE STRING "Qt major version (5 or 6)")
set_property(CACHE TR4131_QT_MAJOR PROPERTY STRINGS 5 6)
set(qt_components Widgets Concurrent Network SerialPort LinguistTools)
if(TR4131_QT_MAJOR STREQUAL "5")
    find_package(Qt5 5.15 REQUIRED COMPONENTS ${qt_components})
    set(qwt_default_dir qwt-qt5-x86)
elseif(TR4131_QT_MAJOR STREQUAL "6")
    find_package(Qt6 6.5 REQUIRED COMPONENTS ${qt_components} OpenGLWidgets)
    set(qwt_default_dir qwt)
    set(qt_opengl_library Qt6::OpenGLWidgets)
else()
    message(FATAL_ERROR "TR4131_QT_MAJOR must be 5 or 6.")
endif()
set(qt "Qt${TR4131_QT_MAJOR}")
option(TR4131_QT_COMMUNITY "Show the About Qt dialog for the community build" ON)
set(QT_I18N_SOURCE_LANGUAGE de)

set(QWT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.deps/${qwt_default_dir}" CACHE PATH "Qwt built for the selected Qt kit")
find_path(QWT_INCLUDE_DIR NAMES qwt_plot.h HINTS "${QWT_ROOT}" PATH_SUFFIXES include include/qwt REQUIRED)
find_library(QWT_IMPORT_LIBRARY NAMES qwt qwt-qt${TR4131_QT_MAJOR} HINTS "${QWT_ROOT}" PATH_SUFFIXES lib REQUIRED)
find_file(QWT_RUNTIME_DLL NAMES qwt.dll qwt-qt${TR4131_QT_MAJOR}.dll HINTS "${QWT_ROOT}" PATH_SUFFIXES bin lib REQUIRED)
add_library(Qwt::Qwt SHARED IMPORTED)
set_target_properties(Qwt::Qwt PROPERTIES
    IMPORTED_IMPLIB "${QWT_IMPORT_LIBRARY}"
    IMPORTED_LOCATION "${QWT_RUNTIME_DLL}"
    INTERFACE_INCLUDE_DIRECTORIES "${QWT_INCLUDE_DIR}"
    INTERFACE_COMPILE_DEFINITIONS QWT_DLL
)

if(TR4131_QT_MAJOR STREQUAL "5")
    add_executable(TR4131-Viewer WIN32)
else()
    qt_add_executable(TR4131-Viewer WIN32)
endif()
target_sources(TR4131-Viewer PRIVATE
    main.cpp version.h
    mainwindow.cpp mainwindow.h mouseposition.h updates.cpp
    spectrogram.cpp spectrogram.h
    spectrogram3d.cpp spectrogram3d.h
    tr4131.cpp tr4131.h
    elektor.cpp gpibconnection.h
    assets/viewer.rc assets/viewer.qrc
)
target_include_directories(TR4131-Viewer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/assets")
if(TR4131_QT_COMMUNITY)
    target_compile_definitions(TR4131-Viewer PRIVATE TR4131_QT_COMMUNITY)
endif()

set(ts_files translations/tr4131_en.ts translations/tr4131_fr.ts)
if(TR4131_QT_MAJOR STREQUAL "5")
    qt5_add_translation(qm_files ${ts_files})
    set(translation_resources "<RCC><qresource prefix=\"/i18n\">\n")
    foreach(qm IN LISTS qm_files)
        get_filename_component(filename "${qm}" NAME)
        string(APPEND translation_resources "  <file alias=\"${filename}\">${qm}</file>\n")
    endforeach()
    string(APPEND translation_resources "</qresource><qresource prefix=\"/qt-i18n\">\n")
    foreach(locale IN ITEMS de fr)
        get_filename_component(qt_catalog "${Qt5_DIR}/../../../translations/qtbase_${locale}.qm" ABSOLUTE)
        if(EXISTS "${qt_catalog}")
            string(APPEND translation_resources "  <file alias=\"qtbase_${locale}.qm\">${qt_catalog}</file>\n")
        endif()
    endforeach()
    string(APPEND translation_resources "</qresource></RCC>\n")
    set(translations_qrc "${CMAKE_CURRENT_BINARY_DIR}/translations.qrc")
    file(CONFIGURE OUTPUT "${translations_qrc}" CONTENT "${translation_resources}" @ONLY)
    qt5_add_resources(translation_sources "${translations_qrc}")
    target_sources(TR4131-Viewer PRIVATE ${translation_sources})
else()
    qt_add_translations(TR4131-Viewer TS_FILES ${ts_files})
    foreach(locale IN ITEMS de fr)
        set(qt_catalog "${Qt6_DIR}/../../../translations/qtbase_${locale}.qm")
        if(EXISTS "${qt_catalog}")
            get_filename_component(qt_catalog "${qt_catalog}" ABSOLUTE)
            set_source_files_properties("${qt_catalog}" PROPERTIES QT_RESOURCE_ALIAS "qtbase_${locale}.qm")
            qt_add_resources(TR4131-Viewer "qt_i18n_${locale}"
                PREFIX "/qt-i18n"
                FILES "${qt_catalog}"
            )
        endif()
    endforeach()
endif()
target_link_libraries(TR4131-Viewer PRIVATE
    ${qt}::Widgets ${qt}::Concurrent ${qt}::Network ${qt}::SerialPort ${qt_opengl_library} Qwt::Qwt)
target_compile_definitions(TR4131-Viewer PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(TR4131-Viewer PRIVATE /utf-8)
endif()

add_custom_command(TARGET TR4131-Viewer POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:Qwt::Qwt>" "$<TARGET_FILE_DIR:TR4131-Viewer>"
    VERBATIM
)
