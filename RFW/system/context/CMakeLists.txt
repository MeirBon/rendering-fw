cmake_minimum_required(VERSION 3.10)
project("RenderContext")

find_package(glm CONFIG REQUIRED)
find_package(TBB CONFIG REQUIRED)

add_library(${PROJECT_NAME} STATIC rfw/context/context.cpp rfw/context/camera.cpp)
target_link_libraries(${PROJECT_NAME} PUBLIC Half glm rfwUtils TBB::tbb)
target_include_directories(${PROJECT_NAME} PUBLIC ${PROJECT_SOURCE_DIR} Half glm rfwUtils TBB::tbb)
if (UNIX)
	target_compile_options(${PROJECT_NAME} PUBLIC "-fPIC")
endif (UNIX)