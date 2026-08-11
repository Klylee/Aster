cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(3dgs-opengl LANGUAGES CXX)

# ============================================================================
# 可选功能开关
# ============================================================================
option(ASTER_ENABLE_VULKAN "Enable the Vulkan render backend (official Vulkan SDK)" ON)
option(ASTER_ENABLE_DEMO   "Build the standalone cross-platform 'aster_demo' executable" ON)

# ============================================================================
# Vulkan（官方 Vulkan SDK）
# ============================================================================
set(ASTER_VULKAN_AVAILABLE OFF)

if(APPLE)
    # 优先使用用户安装的Vulkan SDK
    set(VULKAN_SDK "$ENV{VULKAN_SDK}")
    if(NOT VULKAN_SDK AND EXISTS "/Users/duskidew/VulkanSDK/1.4.350.0/macOS")
        set(VULKAN_SDK "/Users/duskidew/VulkanSDK/1.4.350.0/macOS")
        set(ENV{VULKAN_SDK} ${VULKAN_SDK})
    endif()
    
    if(VULKAN_SDK)
        set(Vulkan_INCLUDE_DIR "${VULKAN_SDK}/include")
        set(Vulkan_LIBRARY "${VULKAN_SDK}/lib/libvulkan.1.dylib")
        set(Vulkan_FOUND TRUE)
        message(STATUS "Using Vulkan SDK at: ${VULKAN_SDK}")
    endif()
endif()

if(ASTER_ENABLE_VULKAN)
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        set(ASTER_VULKAN_AVAILABLE ON)
        message(STATUS "Vulkan SDK found: ${Vulkan_INCLUDE_DIRS}")
    else()
        message(WARNING "Vulkan SDK not found - Vulkan backend disabled. "
                        "Install the LunarG Vulkan SDK and set VULKAN_SDK.")
    endif()
endif()

# glslc：将 assets/shader/vulkan/*.glsl 编译为 SPIR-V
set(VULKAN_SPV_DIR ${CMAKE_BINARY_DIR}/assets/shaders)
set(ASTER_HAS_SHADER_TARGET OFF)
if(ASTER_VULKAN_AVAILABLE)
    find_program(VULKAN_GLSLC glslc HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "$ENV{VULKAN_SDK}/bin"
        "${Vulkan_INCLUDE_DIRS}/../bin"
        "${Vulkan_INCLUDE_DIRS}/../../bin"
    )
    if(VULKAN_GLSLC)
        file(MAKE_DIRECTORY ${VULKAN_SPV_DIR})
        set(VULKAN_SHADERS
            triangle.vert
            triangle.frag
            mesh.vert
            mesh.frag
            depth.frag
            depth_point.frag
            skybox.vert
            skybox.frag
            toon.vert
            toon.frag
            grid.vert
            grid.frag
        )
        set(VULKAN_SPV_OUTPUTS "")
        foreach(SHADER ${VULKAN_SHADERS})
            set(SPV ${VULKAN_SPV_DIR}/${SHADER}.spv)
            list(APPEND VULKAN_SPV_OUTPUTS ${SPV})
            add_custom_command(
                OUTPUT ${SPV}
                COMMAND ${VULKAN_GLSLC} ${CMAKE_SOURCE_DIR}/assets/shader/vulkan/${SHADER}
                        -o ${SPV}
                DEPENDS ${CMAKE_SOURCE_DIR}/assets/shader/vulkan/${SHADER}
                COMMENT "Compiling Vulkan shader ${SHADER} (SPIR-V)")
        endforeach()
        add_custom_target(aster_shaders ALL DEPENDS ${VULKAN_SPV_OUTPUTS})
        set(ASTER_HAS_SHADER_TARGET ON)
    else()
        message(WARNING "glslc not found - Vulkan triangle shaders won't be compiled "
                        "(Vulkan backend still works for clear + ImGui).")
    endif()
endif()

# ============================================================================
# 源文件说明：src/Aster 按关注点分子目录，可执行目标显式列出各自源文件
#   Core/         应用框架（App / GlobalTime / Input / Path）
#   Scene/        场景图（Scene / SceneObject / Transform / Camera / SceneManager / Model）
#   Event/        事件系统（Event / EventDispatcher）
#   Render/       渲染抽象（RenderAPI / Renderer）+ OpenGL、Vulkan 后端
#   Resource/     资源（Mesh / Material / Shader / Texture / MeshManager）
#   Lighting/     灯光（Light / LightData / LightManager）
# ============================================================================
configure_file(env_config.h.in ${CMAKE_BINARY_DIR}/env_config.h @ONLY)

# ============================================================================
# imgui（核心 + GLFW / OpenGL3 / Vulkan 后端）
# ============================================================================
set(IMGUI_BACKENDS
    ${CMAKE_SOURCE_DIR}/include/imgui/backends/imgui_impl_glfw.cpp
    ${CMAKE_SOURCE_DIR}/include/imgui/backends/imgui_impl_opengl3.cpp
)
if(ASTER_VULKAN_AVAILABLE)
    list(APPEND IMGUI_BACKENDS ${CMAKE_SOURCE_DIR}/include/imgui/backends/imgui_impl_vulkan.cpp)
endif()

# macOS：构建一个剔除 GL 子目录的 include 镜像。
# 仓库内置的 include/GL/glew.h 无法在 macOS 编译（与系统 OpenGL 框架冲突），
# 而 Apple Clang 会把 /opt/homebrew/include 自动转成 -isystem（最后搜索），
# 无法覆盖内置 -I.../include。因此 GL/glew.h、GLFW 由 Homebrew 提供，
# assimp/glm/stb/imgui 等仍从该镜像（剔除 GL 后）取得。
if(APPLE)
    set(ASTER_INCLUDE_NO_GL ${CMAKE_BINARY_DIR}/include_no_gl)
    file(REMOVE_RECURSE ${ASTER_INCLUDE_NO_GL})
    file(MAKE_DIRECTORY ${ASTER_INCLUDE_NO_GL})
    file(GLOB _aster_inc_entries ${CMAKE_SOURCE_DIR}/include/*)
    foreach(_aster_inc_e ${_aster_inc_entries})
        get_filename_component(_aster_inc_n "${_aster_inc_e}" NAME)
        if(NOT _aster_inc_n STREQUAL "GL")
            file(COPY ${_aster_inc_e} DESTINATION ${ASTER_INCLUDE_NO_GL})
        endif()
    endforeach()
    set(ASTER_INCLUDE_ROOT ${ASTER_INCLUDE_NO_GL})
else()
    set(ASTER_INCLUDE_ROOT ${CMAKE_SOURCE_DIR}/include)
endif()

add_library(imgui
    ${CMAKE_SOURCE_DIR}/include/imgui/imgui.cpp
    ${CMAKE_SOURCE_DIR}/include/imgui/imgui_demo.cpp
    ${CMAKE_SOURCE_DIR}/include/imgui/imgui_draw.cpp
    ${CMAKE_SOURCE_DIR}/include/imgui/imgui_tables.cpp
    ${CMAKE_SOURCE_DIR}/include/imgui/imgui_widgets.cpp
    ${IMGUI_BACKENDS}
)
target_include_directories(imgui PUBLIC
    ${ASTER_INCLUDE_ROOT}
    ${ASTER_INCLUDE_ROOT}/imgui
    ${ASTER_INCLUDE_ROOT}/imgui/backends
)
target_compile_definitions(imgui PRIVATE
    IMGUI_IMPL_VULKAN_NO_DRAW_CALLBACKS
    IMGUI_DEFINE_MATH_OPERATORS
)
if(ASTER_VULKAN_AVAILABLE)
    target_include_directories(imgui PUBLIC ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(imgui PUBLIC Vulkan::Vulkan)
endif()

# ============================================================================
# 通用头文件目录 / 编译定义 / 着色器依赖
# ============================================================================
set(ASTER_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/Aster
    ${CMAKE_SOURCE_DIR}/src/Aster/Core
    ${CMAKE_SOURCE_DIR}/src/Aster/Scene
    ${CMAKE_SOURCE_DIR}/src/Aster/Event
    ${CMAKE_SOURCE_DIR}/src/Aster/Render
    ${CMAKE_SOURCE_DIR}/src/Aster/Render/OpenGL
    ${CMAKE_SOURCE_DIR}/src/Aster/Render/Vulkan
    ${CMAKE_SOURCE_DIR}/src/Aster/Resource
    ${CMAKE_SOURCE_DIR}/src/Aster/Lighting
    ${CMAKE_SOURCE_DIR}/
    ${CMAKE_BINARY_DIR}
)
if(APPLE)
    # macOS：优先使用剔除 GL 的 include 镜像；GL/glew.h 等由 Homebrew 提供
    list(INSERT ASTER_INCLUDE_DIRS 0 "${ASTER_INCLUDE_NO_GL}")
    list(INSERT ASTER_INCLUDE_DIRS 0 "/opt/homebrew/include")
else()
    list(INSERT ASTER_INCLUDE_DIRS 0 ${CMAKE_SOURCE_DIR}/include)
endif()
if(ASTER_VULKAN_AVAILABLE)
    list(APPEND ASTER_INCLUDE_DIRS ${Vulkan_INCLUDE_DIRS})
endif()

function(aster_apply_common TARGET)
    target_include_directories(${TARGET} PUBLIC ${ASTER_INCLUDE_DIRS})
    target_compile_definitions(${TARGET} PUBLIC
        ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
        IMGUI_ENABLE_DOCKING # 启用Docking功能
    )
    if(APPLE)
        target_compile_definitions(${TARGET} PUBLIC
            ASTER_FORCE_RENDER_API_VULKAN
        )
    endif()
    if(ASTER_VULKAN_AVAILABLE)
        target_compile_definitions(${TARGET} PUBLIC
            ASTER_ENABLE_VULKAN
            VULKAN_SHADER_DIR="${VULKAN_SPV_DIR}")
        if(ASTER_HAS_SHADER_TARGET)
            add_dependencies(${TARGET} aster_shaders)
        endif()
    endif()
endfunction()

# ============================================================================
# aster —— Aster 框架静态库（应用框架 + 场景 + 事件 + 资源 + 渲染后端）
# 由 aster_demo 链接；include 目录与编译定义通过 PUBLIC 传播给使用方。
# ============================================================================
add_library(aster STATIC
    ${CMAKE_SOURCE_DIR}/src/Aster/Core/App.cpp
    ${CMAKE_SOURCE_DIR}/src/Aster/Core/App.h
    ${CMAKE_SOURCE_DIR}/src/Aster/Core/GlobalTime.h
    ${CMAKE_SOURCE_DIR}/src/Aster/Core/Input.h
    ${CMAKE_SOURCE_DIR}/src/Aster/Core/Path.cpp
    ${CMAKE_SOURCE_DIR}/src/Aster/Core/Path.h
    ${CMAKE_SOURCE_DIR}/src/Aster/Event/Event.h
    ${CMAKE_SOURCE_DIR}/src/Aster/Event/EventDispatcher.h
    ${CMAKE_SOURCE_DIR}/src/Aster/Lighting/Light.h
    ${CMAKE_SOURCE_DIR}/src/Aster/Lighting/LightData.h
    ${CMAKE_SOURCE_DIR}/src/Aster/Lighting/LightManager.cpp
    ${CMAKE_SOURCE_DIR}/src/Aster/Lighting/LightManager.h
    ${CMAKE_SOURCE_DIR}/src/Aster/Resource/Material.cpp
    ${CMAKE_SOURCE_DIR}/src/Aster/Resource/Material.h
    ${CMAKE_SOURCE_DIR}/src/Aster/Resource/MaterialManager.cpp
    ${CMAKE_SOURCE_DIR}/src/Aster/Resource/MaterialManager.h
    ${CMAKE_SOURCE_DIR}/src/Aster/Resource/Mesh.cpp
    ${CMAKE_SOURCE_DIR}/src/Aster/Resource/Mesh.h
    ${CMAKE_SOURCE_DIR}/src/Aster/Resource/MeshManager.cpp
    ${CMAKE_SOURCE_DIR}/src/Aster/Resource/MeshManager.h
    ${CMAKE_SOURCE_DIR}/src/Aster/Resource/Shader.cpp
    ${CMAKE_SOURCE_DIR}/src/Aster/Resource/Shader.h
    ${CMAKE_SOURCE_DIR}/src/Aster/Resource/Texture.cpp
    ${CMAKE_SOURCE_DIR}/src/Aster/Resource/Texture.h
    ${CMAKE_SOURCE_DIR}/src/Aster/Resource/EnvironmentMap.cpp
    ${CMAKE_SOURCE_DIR}/src/Aster/Resource/EnvironmentMap.h
    ${CMAKE_SOURCE_DIR}/src/Aster/Resource/stb_image_write_impl.cpp
    ${CMAKE_SOURCE_DIR}/src/Aster/Scene/Camera.cpp
    ${CMAKE_SOURCE_DIR}/src/Aster/Scene/Camera.h
    ${CMAKE_SOURCE_DIR}/src/Aster/Scene/Model.cpp
    ${CMAKE_SOURCE_DIR}/src/Aster/Scene/Model.h
    ${CMAKE_SOURCE_DIR}/src/Aster/Scene/Scene.cpp
    ${CMAKE_SOURCE_DIR}/src/Aster/Scene/Scene.h
    ${CMAKE_SOURCE_DIR}/src/Aster/Scene/SceneManager.h
    ${CMAKE_SOURCE_DIR}/src/Aster/Scene/SceneObject.cpp
    ${CMAKE_SOURCE_DIR}/src/Aster/Scene/SceneObject.h
    ${CMAKE_SOURCE_DIR}/src/Aster/Scene/Transform.cpp
    ${CMAKE_SOURCE_DIR}/src/Aster/Scene/Transform.h
    ${CMAKE_SOURCE_DIR}/src/Aster/Render/RenderAPI.h
    ${CMAKE_SOURCE_DIR}/src/Aster/Render/RenderAPIFactory.cpp
    ${CMAKE_SOURCE_DIR}/src/Aster/Render/Renderer.cpp
    ${CMAKE_SOURCE_DIR}/src/Aster/Render/Renderer.h
)
if(NOT APPLE) # macOS 上的 OpenGL 已被弃用，无法使用
    target_sources(aster PRIVATE
        ${CMAKE_SOURCE_DIR}/src/Aster/Render/OpenGL/OpenGLRenderAPI.cpp
        ${CMAKE_SOURCE_DIR}/src/Aster/Render/OpenGL/OpenGLRenderAPI.h
        ${CMAKE_SOURCE_DIR}/src/Aster/Render/OpenGL/OpenGLEnvironment.cpp
        ${CMAKE_SOURCE_DIR}/src/Aster/Render/OpenGL/OpenGLEnvironment.h
    )
endif()
if(ASTER_VULKAN_AVAILABLE)
    target_sources(aster PRIVATE
        ${CMAKE_SOURCE_DIR}/src/Aster/Render/Vulkan/VulkanMeshBuffer.cpp
        ${CMAKE_SOURCE_DIR}/src/Aster/Render/Vulkan/VulkanMeshBuffer.h
        ${CMAKE_SOURCE_DIR}/src/Aster/Render/Vulkan/VulkanPipeline.cpp
        ${CMAKE_SOURCE_DIR}/src/Aster/Render/Vulkan/VulkanPipeline.h
        ${CMAKE_SOURCE_DIR}/src/Aster/Render/Vulkan/VulkanRenderAPI.cpp
        ${CMAKE_SOURCE_DIR}/src/Aster/Render/Vulkan/VulkanRenderAPI.h
        ${CMAKE_SOURCE_DIR}/src/Aster/Render/Vulkan/VulkanSceneRenderer.cpp
        ${CMAKE_SOURCE_DIR}/src/Aster/Render/Vulkan/VulkanSceneRenderer.h
        ${CMAKE_SOURCE_DIR}/src/Aster/Render/Vulkan/VulkanUtil.h
    )
endif()

aster_apply_common(aster)
target_link_libraries(aster PUBLIC imgui)
if(ASTER_VULKAN_AVAILABLE)
    target_link_libraries(aster PUBLIC Vulkan::Vulkan)
endif()

# ============================================================================
# 独立演示可执行文件：aster_demo（跨平台，无 CUDA / assimp 依赖）
# 运行：./aster_demo（默认 OpenGL）或 ASTER_RENDER_API=vulkan ./aster_demo
# ============================================================================
if(ASTER_ENABLE_DEMO)
    add_executable(aster_demo
        ${CMAKE_SOURCE_DIR}/src/demo/main_demo.cpp
        ${CMAKE_SOURCE_DIR}/src/demo/RenderAPIDemo.cpp
        ${CMAKE_SOURCE_DIR}/src/demo/RenderAPIDemo.h
        ${CMAKE_SOURCE_DIR}/src/demo/ModelDemo.cpp
        ${CMAKE_SOURCE_DIR}/src/demo/ModelDemo.h
        ${CMAKE_SOURCE_DIR}/src/demo/ModelDemoApp.h
    )
    aster_apply_common(aster_demo)
    target_link_libraries(aster_demo PRIVATE aster imgui)


    if(WIN32)
        target_link_directories(aster_demo PRIVATE ${CMAKE_SOURCE_DIR}/lib/msvc-2022)
        target_link_libraries(aster_demo PRIVATE glfw3dll glew32.dll)
        if(ASTER_VULKAN_AVAILABLE)
            target_link_libraries(aster_demo PRIVATE Vulkan::Vulkan)
        endif()

        file(GLOB DLL_FILES "${CMAKE_SOURCE_DIR}/lib/*.dll")
        foreach(DLL ${DLL_FILES})
            add_custom_command(TARGET aster_demo POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${DLL}" "$<TARGET_FILE_DIR:aster_demo>")
        endforeach()

    elseif(APPLE)
        # macOS：需要 Homebrew 的 glfw 与 glew；Vulkan 由 MoltenVK（Vulkan SDK）提供
        set(CMAKE_PREFIX_PATH "/opt/homebrew" ${CMAKE_PREFIX_PATH})
        find_package(glfw3 QUIET)
        find_package(GLFW QUIET)
        find_package(GLEW QUIET)

        if(NOT GLEW_FOUND)
            set(GLEW_INCLUDE_DIRS "/opt/homebrew/include")
            set(GLEW_LIBRARY "/opt/homebrew/lib/libGLEW.dylib")
            set(GLEW_FOUND TRUE)
            message(STATUS "GLEW manually set to: ${GLEW_LIBRARY}")
        endif()
        
        if(NOT GLFW_FOUND)
            set(GLFW_INCLUDE_DIRS "/opt/homebrew/include")
            set(GLFW_LIBRARY "/opt/homebrew/lib/libglfw.dylib")
            set(GLFW_FOUND TRUE)
            message(STATUS "GLFW manually set to: ${GLFW_LIBRARY}")
        endif()

        if(TARGET glfw)
            target_link_libraries(aster_demo PRIVATE glfw)
        elseif(TARGET glfw3)
            target_link_libraries(aster_demo PRIVATE glfw3)
        elseif(GLFW_FOUND)
            target_link_libraries(aster_demo PRIVATE ${GLFW_LIBRARY})
            target_include_directories(aster_demo PRIVATE ${GLFW_INCLUDE_DIRS})
        else()
            message(WARNING "GLFW not found - aster_demo needs 'brew install glfw'")
        endif()

        if(GLEW_FOUND)
            # find_package 提供 GLEW_LIBRARIES（复数）；手动回退用 GLEW_LIBRARY（单数）
            if(TARGET GLEW::GLEW)
                target_link_libraries(aster_demo PRIVATE GLEW::GLEW)
            elseif(GLEW_LIBRARIES)
                target_link_libraries(aster_demo PRIVATE ${GLEW_LIBRARIES})
            elseif(GLEW_LIBRARY)
                target_link_libraries(aster_demo PRIVATE ${GLEW_LIBRARY})
            endif()
            # macOS 的 GL 函数（glEnable 等）位于 OpenGL.framework
            target_link_libraries(aster_demo PRIVATE "-framework OpenGL")
            target_include_directories(aster_demo PRIVATE ${GLEW_INCLUDE_DIRS})
        else()
            message(WARNING "GLEW not found - aster_demo OpenGL path needs 'brew install glew'")
        endif()

        target_compile_definitions(aster_demo PRIVATE
            IMGUI_ENABLE_DOCKING  # 启用Docking功能
        )

        if(ASTER_VULKAN_AVAILABLE)
            target_link_libraries(aster_demo PRIVATE Vulkan::Vulkan)
        endif()
    endif()
endif()