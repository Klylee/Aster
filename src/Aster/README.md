# Aster 框架

Aster 是 3dgs-opengl 的跨平台应用框架（C++20），以静态库 `aster` 形式构建，
由可执行目标（如 `aster_demo`）链接。框架负责应用生命周期、场景图、事件、
资源管理与双渲染后端（OpenGL / Vulkan）抽象。

## 目录结构

| 目录 | 内容 |
| --- | --- |
| `Core/` | 应用框架：`App`（主循环 / `FixedUpdate`）、`GlobalTime`（时间）、`Input`（输入）、`Path`（路径工具） |
| `Scene/` | 场景图：`Scene`、`SceneObject`、`Transform`、`Camera`、`SceneManager`、`Model` |
| `Event/` | 事件系统：`Event`（事件类型）、`EventDispatcher`（分发器，Meyers 单例） |
| `Render/` | 渲染抽象：`RenderAPI`、`RenderAPIFactory`、`Renderer` |
| `Render/OpenGL/` | OpenGL 后端 `OpenGLRenderAPI`（macOS 不编译） |
| `Render/Vulkan/` | Vulkan 后端 `VulkanRenderAPI` / `VulkanPipeline` / `VulkanMeshBuffer` / `VulkanSceneRenderer` / `VulkanUtil` |
| `Resource/` | 资源：`Mesh`、`MeshManager`、`Material`、`Shader`、`Texture`、`EnvironmentMap`（HDR 环境贴图：cubemap / irradiance / 预过滤 / BRDF LUT） |
| `Lighting/` | 灯光：`Light`、`LightData`、`LightManager` |

## 代码约定

### 命名空间
- 框架类型统一放在 `namespace aster` 中（如 `aster::App`、`aster::SceneManager`）。
- `Event` 与 `VulkanUtil` 是自包含的顶层命名空间，保持在全局（不嵌套进 `aster`）。
- `Transform.h` 中的 `Vec2/Vec3/Vec4/Mat4/Quat` 等 glm 别名保留在全局作用域（便捷层）。
- demo 目录（`aster` 之外）通过 `using namespace aster;`（.cpp）或 `using aster::X;`（.h）引用框架类型。

### 全局单例与静态状态
- **持有全局状态的管理器** → Meyers 单例 `Instance()`：`SceneManager`、`MeshManager`、`EventDispatcher`。
- **无状态工具类** → 静态方法：`Input`、`GlobalTime`。
- **当前活动渲染后端** → `RenderAPI::Current()` / `SetCurrent()`（运行期注册表）。

### 纯头文件 vs .cpp
- 模板与极简转发 → 留在头文件（如 `Scene::GetObject<T>`、`SceneManager`、`EventDispatcher` 的模板注册）。
- 非模板实质逻辑 → 下沉到 `.cpp`（如 `Scene`、`SceneObject`、`LightManager`），
  减小头文件耦合、加速增量编译。
- 头文件守卫统一使用 `#pragma once`。

### GL 依赖（依赖方向）
- 框架头文件**不**包含 `<GL/glew.h>`（`LightManager.h` / `Material.h` 已剥离，
  用 `unsigned int` 代替 `GLuint` / `GLenum`）。
- GL 调用集中在各 `.cpp`，且 `.cpp` 中 `glew.h` 必须先于任何 OpenGL 头包含。
- 因此 `App.cpp` 无需再关心 GLEW 包含顺序。

### include 风格
- 框架内部使用扁平 `#include "X.h"`（include 路径由 CMake 提供，见 `ASTER_INCLUDE_DIRS`）。
- demo 目录使用 `#include "Aster/Sub/X.h"` 前缀风格（依赖 `src` 在 include 路径中）。

## 更新循环（App）
`App::Run()` 采用“固定步长 + 累计器”：
- `FixedUpdate()`：固定频率（默认 120Hz），适合物理 / 确定性逻辑。
- `Update()`：每帧一次，适合帧相关逻辑。
- `GetFixedAlpha()`：返回 0~1 插值系数，供渲染侧在两个固定状态间平滑插值。
