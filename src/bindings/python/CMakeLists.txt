##############################################################################
# python using pybind11
##############################################################################

include(FetchContent)

FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG        v2.5.0
)
# FetchContent_MakeAvailable(pybind11)
FetchContent_GetProperties(pybind11)
if(NOT pybind11_POPULATED)
    FetchContent_Populate(pybind11)
    add_subdirectory(${pybind11_SOURCE_DIR} ${pybind11_BINARY_DIR})
endif()

pybind11_add_module(disc PyDisc.cpp)
target_link_libraries(disc PRIVATE DISC pybind11)
target_include_directories(disc PRIVATE ${CMAKE_SOURCE_DIR}/src)
install(TARGETS disc LIBRARY DESTINATION ".")
