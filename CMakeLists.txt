cmake_minimum_required(VERSION 3.22)
project(foresighter LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

#3rd party libraries
find_package(SQLite3)
find_package(GTest)
find_package(Boost)
find_package(spdlog)
find_package(cereal)
find_package(libpqxx)


# Collect all the hpp, cpp and test.cpp files under the src directory
file(GLOB_RECURSE SOURCE_CPP_FILES CONFIGURE_DEPENDS src/*.cpp)
file(GLOB_RECURSE SOURCE_HPP_FILES CONFIGURE_DEPENDS src/*.hpp)
file(GLOB_RECURSE SOURCE_C_FILES CONFIGURE_DEPENDS src/*.c)
file(GLOB_RECURSE SOURCE_H_FILES CONFIGURE_DEPENDS src/*.h)
file(GLOB_RECURSE SOURCE_HH_FILES CONFIGURE_DEPENDS src/*.hh)
file(GLOB_RECURSE UNIT_TEST_FILES CONFIGURE_DEPENDS src/*.test.cpp)
file(GLOB_RECURSE ENTRIES_UTILS CONFIGURE_DEPENDS entries/utils/*.cpp)
include_directories(src)
include_directories(entries/utils)

# Exclude test files from the main source files list
list(REMOVE_ITEM SOURCE_CPP_FILES ${UNIT_TEST_FILES})

# Create a library from the source files and header files
add_library(foresighter_lib ${SOURCE_CPP_FILES} ${SOURCE_HPP_FILES} ${SOURCE_C_FILES} ${SOURCE_H_FILES} ${SOURCE_HH_FILES})
set_target_properties(foresighter_lib PROPERTIES LINKER_LANGUAGE CXX)
# Create an executable for the unit utils
add_executable(foresighter_unit_tests ${UNIT_TEST_FILES})


add_executable(ablation_run entries/ablation_run.cpp ${ENTRIES_UTILS})
add_executable(fta_construct entries/fta_construct.cpp ${ENTRIES_UTILS})
add_executable(slicing_oracle_construct entries/slicing_oracle_construct.cpp ${ENTRIES_UTILS})
add_executable(online_search_with_slicing entries/online_search_with_slicing.cpp ${ENTRIES_UTILS})
add_executable(slicing_server_building entries/slicing_server_building.cpp ${ENTRIES_UTILS})
add_executable(gather_slicing_size_info entries/gather_slicing_size_info.cpp ${ENTRIES_UTILS})
add_executable(slicing_server_building_ablation entries/slicing_server_building_ablation.cpp ${ENTRIES_UTILS})
add_executable(build_state_map_for_slicing entries/build_state_map_for_slicing.cpp ${ENTRIES_UTILS})
add_executable(dump_slice_fta entries/dump_slice_fta.cpp ${ENTRIES_UTILS})
# add_executable(test_sqlite_speed entries/test_sqlite_speed.cpp)


# Link the libraries

target_link_libraries(foresighter_lib
        SQLite::SQLite3
        boost::boost
        spdlog::spdlog
        cereal::cereal
)

target_link_libraries(foresighter_unit_tests
        SQLite::SQLite3
        gtest::gtest
        boost::boost
        spdlog::spdlog
        cereal::cereal
        foresighter_lib)


target_link_libraries(ablation_run
        SQLite::SQLite3
        boost::boost
        spdlog::spdlog
        libpqxx::pqxx
        foresighter_lib)


target_link_libraries(fta_construct
        SQLite::SQLite3
        boost::boost
        spdlog::spdlog
        foresighter_lib)


target_link_libraries(slicing_oracle_construct
        SQLite::SQLite3
        boost::boost
        spdlog::spdlog
        foresighter_lib)


target_link_libraries(slicing_server_building
        SQLite::SQLite3
        boost::boost
        spdlog::spdlog
        foresighter_lib)

target_link_libraries(online_search_with_slicing
        SQLite::SQLite3
        boost::boost
        spdlog::spdlog
        libpqxx::pqxx
        foresighter_lib)

target_link_libraries(gather_slicing_size_info
        SQLite::SQLite3
        boost::boost
        spdlog::spdlog
        libpqxx::pqxx
        foresighter_lib)

target_link_libraries(slicing_server_building_ablation
        SQLite::SQLite3
        boost::boost
        spdlog::spdlog
        foresighter_lib)

target_link_libraries(dump_slice_fta
        SQLite::SQLite3
        boost::boost
        spdlog::spdlog
        foresighter_lib)

target_link_libraries(build_state_map_for_slicing
        SQLite::SQLite3
        boost::boost
        spdlog::spdlog
        foresighter_lib)


include(GoogleTest)

gtest_discover_tests(foresighter_unit_tests)
enable_testing()



