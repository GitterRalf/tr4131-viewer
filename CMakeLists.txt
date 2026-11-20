cmake_minimum_required(VERSION 3.21)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/version.h")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.h" tr4131_version_line
    REGEX "^#define[ \t]+TR4131_VIEWER_VERSION[ \t]+")
if(NOT tr4131_version_line MATCHES "\"([0-9.]+)\"")
    message(FATAL_ERROR "version.h must define TR4131_VIEWER_VERSION as a numeric version string.")
endif()

project(TR4131Viewer VERSION "${CMAKE_MATCH_1}" LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "TR4131-Viewer benötigt ein 64-Bit-Windows-Kit.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
enable_language(RC)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Concurrent Network SerialPort LinguistTools OpenGLWidgets)
option(TR4131_QT_COMMUNITY "Show the About Qt dialog for the community build" ON)
set(QT_I18N_SOURCE_LANGUAGE de)

set(QWT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.deps/qwt" CACHE PATH "Qt-6-Qwt-Installation")
find_path(QWT_INCLUDE_DIR NAMES qwt_plot.h HINTS "${QWT_ROOT}" PATH_SUFFIXES include include/qwt REQUIRED)
find_library(QWT_IMPORT_LIBRARY NAMES qwt qwt-qt6 HINTS "${QWT_ROOT}" PATH_SUFFIXES lib REQUIRED)
find_file(QWT_RUNTIME_DLL NAMES qwt.dll qwt-qt6.dll HINTS "${QWT_ROOT}" PATH_SUFFIXES bin lib REQUIRED)
add_library(Qwt::Qwt SHARED IMPORTED)
set_target_properties(Qwt::Qwt PROPERTIES
    IMPORTED_IMPLIB "${QWT_IMPORT_LIBRARY}"
    IMPORTED_LOCATION "${QWT_RUNTIME_DLL}"
    INTERFACE_INCLUDE_DIRECTORIES "${QWT_INCLUDE_DIR}"
    INTERFACE_COMPILE_DEFINITIONS QWT_DLL
)

qt_add_executable(TR4131-Viewer WIN32
    main.cpp version.h
    mainwindow.cpp mainwindow.h updates.cpp
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

qt_add_translations(TR4131-Viewer TS_FILES translations/tr4131_en.ts translations/tr4131_fr.ts)
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
target_link_libraries(TR4131-Viewer PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Network Qt6::SerialPort Qt6::OpenGLWidgets Qwt::Qwt)
target_compile_definitions(TR4131-Viewer PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
if(MSVC)
    target_compile_options(TR4131-Viewer PRIVATE /utf-8)
endif()

add_custom_command(TARGET TR4131-Viewer POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:Qwt::Qwt>" "$<TARGET_FILE_DIR:TR4131-Viewer>"
    VERBATIM
)
