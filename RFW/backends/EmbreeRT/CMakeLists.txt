cmake_minimum_required(VERSION 3.12)
project(EmbreeRT)

file(GLOB_RECURSE SOURCES ./src/*.cpp)
file(GLOB_RECURSE HEADERS ./src/*.h ./src/*.hpp)

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)

target_compile_definitions(${PROJECT_NAME} PRIVATE RENDER_API_EXPORT)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src RenderSystem ../RenderContext ../RenderSystem/src)

find_package(TBB CONFIG REQUIRED)
find_package(embree 3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

target_link_libraries(${PROJECT_NAME} PUBLIC RenderContext rfwMath rfwUtils GLEW::GLEW OpenGL::GL embree TBB::tbb)

target_include_directories(${PROJECT_NAME} PRIVATE RenderContext)
target_link_libraries(${PROJECT_NAME} PRIVATE ${EMBREE_LIBRARY})

set_target_properties(${PROJECT_NAME}
		PROPERTIES
		PREFIX ""
		ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
		LIBRARY_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
		RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
		CXX_STANDARD 17
		)

add_dependencies(backends ${PROJECT_NAME})