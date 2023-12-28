cmake_minimum_required(VERSION 3.9)

project(ray-tracer)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/")

find_package(Vulkan REQUIRED)
find_package(SDL2 REQUIRED)
find_package(glm REQUIRED)
find_package(assimp REQUIRED)

if (DEFINED VULKAN_SDK_PATH)
  set(Vulkan_INCLUDE_DIRS "${VULKAN_SDK_PATH}/Include")
  set(Vulkan_LIBRARIES "${VULKAN_SDK_PATH}/Lib")
  set(Vulkan_FOUND "True")
else()
  find_package(Vulkan REQUIRED)
  message(STATUS "Found Vulkan: $ENV{VULKAN_SDK}")
endif()
if (NOT Vulkan_FOUND)
    message(FATAL_ERROR "Could not find Vulkan library!")
else()
    message(STATUS "Using vulkan lib at: ${Vulkan_LIBRARIES}")
endif()

add_definitions(
  -DGLM_FORCE_LEFT_HANDED
)

include_directories(
  ${Vulkan_INCLUDE_DIRS}
  ${SDL2_INCLUDE_DIRS}
  ${ASSIMP_INCLUDE_DIR}
  vendor/
  vendor/glm
  vendor/VulkanMemoryAllocator/include
)

add_executable(${PROJECT_NAME} 
  src/main.cpp 
  src/logger.cpp 
  src/vulkan_swapchain.cpp 
  src/vulkan_device.cpp 
  src/vulkan_texture.cpp 
  src/vulkan_buffer.cpp 
  src/vulkan_resources.cpp 
  src/vulkan_pipeline.cpp
  src/vulkan_descriptor_allocator.cpp
  src/vulkan_descriptor_layout_cache.cpp
  src/vulkan_descriptor_builder.cpp
)

target_link_directories(
  ${PROJECT_NAME} PRIVATE
  ${ASSIMP_LIBRARY_DIRS}
)

target_link_libraries(
  ${PROJECT_NAME}
  ${Vulkan_LIBRARIES}
  ${SDL2_LIBRARIES}
  ${ASSIMP_LIBRARIES}
)

file(GLOB_RECURSE ASSETS
  "assets/textures/*.jpg"
  "assets/textures/*.png"
  "assets/models/*.obj"
  "assets/models/*.mtl"
)
foreach(ASSET ${ASSETS})
  get_filename_component(FILE_NAME ${ASSET} NAME)
  get_filename_component(FILE_PATH ${ASSET} PATH)
  get_filename_component(LAST_DIR ${FILE_PATH} NAME)
  file(COPY ${ASSET} DESTINATION "${PROJECT_BINARY_DIR}/bin/assets/${LAST_DIR}/")
endforeach()

file(GLOB_RECURSE VK_GLSL_SOURCE_FILES
  "assets/shaders/*.vert"
  "assets/shaders/*.tesc"
  "assets/shaders/*.tese"
  "assets/shaders/*.geom"
  "assets/shaders/*.frag"
  "assets/shaders/*.comp"
)
set(GLSLANG "glslangValidator")
foreach(GLSL ${VK_GLSL_SOURCE_FILES})
  get_filename_component(FILE_NAME ${GLSL} NAME)
  set(SPIRV "${PROJECT_BINARY_DIR}/bin/assets/shaders/${FILE_NAME}.spv")
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/bin/assets/shaders/"
    COMMAND ${GLSLANG} --target-env vulkan1.2 ${GLSL} -o ${SPIRV}
    DEPENDS ${GLSL})
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

add_custom_target(
  shaders 
  DEPENDS ${SPIRV_BINARY_FILES}
)
add_dependencies(${PROJECT_NAME} shaders)