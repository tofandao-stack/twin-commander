cmake_minimum_required(VERSION 3.22)
project(TwinCommander VERSION 0.7.0 LANGUAGES CXX)

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
    src/classicui.cpp
    src/classicui.h
    src/preferences.cpp
    src/remainingoptions.cpp
    src/remotetools.cpp
    src/remotetools.h
    src/networkcommands.cpp
    src/extendedcommands.cpp
    src/nativesettings.cpp
    src/nativesettings.h
    src/extendedtools.cpp
    src/extendedtools.h
    src/productivitytools.cpp
    src/productivitytools.h
    src/customization.cpp
    src/productivity.cpp
    src/referenceui.cpp
    src/referenceforms.cpp
    src/referenceforms.h
    src/directorymodel.cpp
    src/directorymodel.h
    src/filepanel.cpp
    src/filepanel.h
    src/mainwindow.cpp
    src/mainwindow.h
    src/fileoperations.cpp
    src/fileoperations.h
    src/search.cpp
    src/search.h
    src/toolwindows.cpp
    src/toolwindows.h
    src/archives.cpp
    src/archives.h
    src/jobqueue.cpp
    src/jobqueue.h
    src/nativefs.h
    src/backgroundpool.h
    src/filetools.cpp
    src/filetools.h
    src/batchrename.cpp
    src/batchrename.h
    src/advancedwindows.cpp
    src/advancedwindows.h
)
target_include_directories(commander_ui PUBLIC src)
target_link_libraries(commander_ui PUBLIC Qt6::Widgets Qt6::Concurrent Qt6::PrintSupport Qt6::Network LibArchive::LibArchive)
target_compile_options(commander_ui PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

add_executable(twin-commander src/main.cpp)
target_link_libraries(twin-commander PRIVATE commander_ui)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(preferences_tests tests/preferences_tests.cpp)
    target_link_libraries(preferences_tests PRIVATE commander_ui Qt6::Test)
    add_test(NAME preferences_tests COMMAND preferences_tests)
    set_tests_properties(preferences_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
    add_executable(remaining_gui_tests tests/remaining_gui_tests.cpp)
    target_link_libraries(remaining_gui_tests PRIVATE commander_ui Qt6::Test)
    add_test(NAME remaining_gui_tests COMMAND remaining_gui_tests)
    set_tests_properties(remaining_gui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
    add_executable(remaining_tests tests/remaining_tests.cpp)
    target_link_libraries(remaining_tests PRIVATE commander_ui Qt6::Test)
    add_test(NAME remaining_tests COMMAND remaining_tests)
    set_tests_properties(remaining_tests PROPERTIES TIMEOUT 120)
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
