cmake_minimum_required(VERSION 3.22)
project(TwinCommander VERSION 0.9.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Concurrent PrintSupport Network)
find_package(LibArchive QUIET)
if(NOT LibArchive_FOUND)
    find_library(ARCHIVE_RUNTIME NAMES archive libarchive.so.13 REQUIRED)
    add_library(LibArchive::LibArchive UNKNOWN IMPORTED)
    set_target_properties(LibArchive::LibArchive PROPERTIES IMPORTED_LOCATION "${ARCHIVE_RUNTIME}"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libarchive")
endif()

add_library(commander_ui
    resources/commander.qrc

    # app
    src/app/commandregistry.h
    src/app/extensioncommands.cpp
    src/app/filecommands.cpp
    src/app/mainwindow.cpp
    src/app/mainwindow.h
    src/app/networkcommands.cpp

    # archives
    src/archives/archives.cpp
    src/archives/archives.h
    src/archives/externalarchivebackend.cpp
    src/archives/externalarchivebackend.h

    # filesystem
    src/filesystem/directorysizes.cpp
    src/filesystem/directorysizes.h
    src/filesystem/fileanalysis.cpp
    src/filesystem/fileanalysis.h
    src/filesystem/filecomments.cpp
    src/filesystem/filecomments.h
    src/filesystem/fileexports.cpp
    src/filesystem/fileexports.h
    src/filesystem/filenamepresentation.cpp
    src/filesystem/filenamepresentation.h
    src/filesystem/filetransforms.cpp
    src/filesystem/filetransforms.h
    src/filesystem/nativefs.h
    src/filesystem/pathencoding.cpp
    src/filesystem/pathencoding.h

    # network
    src/network/ftptransferoptions.cpp
    src/network/ftptransferoptions.h
    src/network/remoteservices.cpp
    src/network/remoteservices.h

    # operations
    src/operations/operationprogress.cpp
    src/operations/batchrename.cpp
    src/operations/batchrename.h
    src/operations/fileoperations.cpp
    src/operations/fileoperations.h
    src/operations/iothreadpool.h
    src/operations/jobqueue.cpp
    src/operations/jobqueue.h
    src/operations/operationlog.cpp
    src/operations/operationlog.h

    # platform
    src/platform/externalprograms.cpp
    src/platform/externalprograms.h
    src/platform/nativeextensions.cpp
    src/platform/nativeextensions.h

    # search
    src/search/filesearch.cpp
    src/search/filesearch.h

    # settings
    src/settings/applicationpreferences.cpp
    src/settings/applicationpreferences.h
    src/settings/interfacepreferences.cpp
    src/settings/interfacepreferences.h
    src/settings/panelpreferences.cpp
    src/settings/panelpreferences.h

    # thumbnails
    src/thumbnails/thumbnailproviders.cpp
    src/thumbnails/thumbnailproviders.h

    # ui/dialogs
    src/ui/dialogs/operationprogressdialog.cpp
    src/ui/dialogs/operationprogressdialog.h
    src/ui/dialogs/filecontentandsearchdialogs.cpp
    src/ui/dialogs/filecontentandsearchdialogs.h
    src/ui/dialogs/fileoperationdialogs.cpp
    src/ui/dialogs/fileoperationdialogs.h

    # ui
    src/ui/mainwindowui.cpp

    # ui/icons
    src/ui/icons/commandicons.cpp
    src/ui/icons/commandicons.h

    # ui/panels
    src/ui/panels/directorymodel.cpp
    src/ui/panels/directorymodel.h
    src/ui/panels/filepanel.cpp
    src/ui/panels/filepanel.h
    src/ui/panels/filepanellayout.cpp
    src/ui/panels/filepanellayout.h
    src/ui/panels/foldertabpreferences.cpp
    src/ui/panels/foldertabpreferences.h
    src/ui/panels/nativepathmime.cpp
    src/ui/panels/nativepathmime.h

    # ui/settings
    src/ui/settings/appearanceandtoolbareditors.cpp
    src/ui/settings/configurationeditors.cpp
    src/ui/settings/panelpreferencebindings.cpp
    src/ui/settings/referenceformrenderer.cpp
    src/ui/settings/referenceformrenderer.h
)

target_include_directories(commander_ui PUBLIC src)
target_link_libraries(commander_ui PUBLIC Qt6::Widgets Qt6::Concurrent Qt6::PrintSupport Qt6::Network LibArchive::LibArchive)
target_compile_options(commander_ui PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

add_executable(twin-commander src/app/main.cpp)
target_link_libraries(twin-commander PRIVATE commander_ui)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(application_preferences_tests tests/application_preferences_tests.cpp)
    target_link_libraries(application_preferences_tests PRIVATE commander_ui Qt6::Test)
    add_test(NAME application_preferences_tests COMMAND application_preferences_tests)
    set_tests_properties(application_preferences_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180)
    add_executable(interface_preferences_tests tests/interface_preferences_tests.cpp)
    target_link_libraries(interface_preferences_tests PRIVATE commander_ui Qt6::Test)
    add_test(NAME interface_preferences_tests COMMAND interface_preferences_tests)
    set_tests_properties(interface_preferences_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
    add_executable(preferences_tests tests/preferences_tests.cpp)
    target_link_libraries(preferences_tests PRIVATE commander_ui Qt6::Test)
    add_test(NAME preferences_tests COMMAND preferences_tests)
    set_tests_properties(preferences_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
    add_executable(command_dialog_tests tests/command_dialog_tests.cpp)
    target_link_libraries(command_dialog_tests PRIVATE commander_ui Qt6::Test)
    add_test(NAME command_dialog_tests COMMAND command_dialog_tests)
    set_tests_properties(command_dialog_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
    add_executable(native_services_tests tests/native_services_tests.cpp)
    target_link_libraries(native_services_tests PRIVATE commander_ui Qt6::Test)
    add_test(NAME native_services_tests COMMAND native_services_tests)
    set_tests_properties(native_services_tests PROPERTIES TIMEOUT 120)
    add_executable(commander_tests tests/commander_tests.cpp)
    target_link_libraries(commander_tests PRIVATE commander_ui Qt6::Test)
    add_executable(operation_tests tests/operation_tests.cpp)
    target_link_libraries(operation_tests PRIVATE commander_ui Qt6::Test)
    add_executable(advanced_tests tests/advanced_tests.cpp)
    target_link_libraries(advanced_tests PRIVATE commander_ui Qt6::Test)
    add_executable(queue_tests tests/queue_tests.cpp)
    target_link_libraries(queue_tests PRIVATE commander_ui Qt6::Test)
    add_test(NAME advanced_tests COMMAND advanced_tests)
    add_test(NAME queue_tests COMMAND queue_tests)
    set_tests_properties(advanced_tests queue_tests PROPERTIES TIMEOUT 60)
    add_test(NAME operation_tests COMMAND operation_tests)
    add_test(NAME commander_tests COMMAND commander_tests)
    set_tests_properties(commander_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
    set_tests_properties(operation_tests PROPERTIES TIMEOUT 60)
endif()

include(GNUInstallDirs)
install(TARGETS twin-commander RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/twin-commander.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

install(FILES third_party/libarchive/COPYRIGHT DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/twin-commander RENAME LIBARCHIVE-COPYRIGHT)

foreach(icon_size 16 32 48 64 128 256 512)
    install(FILES resources/icons/twin-commander-${icon_size}.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${icon_size}x${icon_size}/apps
        RENAME twin-commander.png)
endforeach()
