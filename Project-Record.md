# 双图形后端（OpenGL + Vulkan）

App 不再直接调用 OpenGL，而是通过 `RenderAPI` 抽象驱动渲染后端。
OpenGL 与 Vulkan 两套实现共存，通过开关切换。

## 架构

```
src/Aster/                # Aster 框架（静态库 aster）
  Core/                   # 应用框架（App/GlobalTime/Input/Path）
  Scene/                  # 场景图（Scene/SceneObject/Transform/Camera/SceneManager/Model）
  Event/                  # 事件系统（Event/EventDispatcher）
  Render/                 # 渲染抽象（RenderAPI/Renderer）
  Render/OpenGL/          # OpenGL 后端（默认，行为与旧版一致；macOS 不编译）
  Render/Vulkan/          # Vulkan 后端（实例/设备/交换链/渲染流程/ImGui）
  Resource/               # 资源（Mesh/MeshManager/Material/Shader/Texture）
  Lighting/               # 灯光（Light/LightData/LightManager）
  README.md               # 框架结构与代码约定（命名空间/单例/GL 依赖等）
src/demo/                 # aster_demo 可执行文件（链接 aster 库）
  RenderAPIDemo.*         # 独立跨平台演示（不依赖 CUDA/assimp）
  ModelDemo.*             # Model 双后端演示（基于 App 框架）
  main_demo.cpp           # aster_demo 可执行文件入口
include/imgui/backends/   # 官方 imgui GLFW/OpenGL3/Vulkan 后端
assets/shader/vulkan/     # 演示三角形 GLSL（由 glslc 编译为 SPIR-V）
```

> 详见 `src/Aster/README.md`（2026-08 整理的框架结构与代码约定）。

## 如何选择后端

优先级：编译期宏 > 环境变量 `ASTER_RENDER_API` > 构造参数。

- 环境变量：`ASTER_RENDER_API=opengl|vulkan`
- 编译期强制：`-DASTER_FORCE_RENDER_API_VULKAN` / `-DASTER_FORCE_RENDER_API_OPENGL`
- App 构造：`App(w, h, title, RenderAPIType::Vulkan)`

## 构建

```bash
cmake -B build -DASTER_ENABLE_VULKAN=ON -DASTER_ENABLE_DEMO=ON
cmake --build build
```

- Windows + CUDA：默认生成 `3dgs-opengl`（GsEditor）。`ASTER_RENDER_API=vulkan 3dgs-opengl` 会运行独立演示。
- 任意平台（macOS + MoltenVK 也可）：`aster_demo` 不依赖 CUDA/assimp，
  可用 `ASTER_RENDER_API=opengl|vulkan ./aster_demo` 切换后端演示。
- Vulkan 后端需要官方 Vulkan SDK（`find_package(Vulkan)`）；未找到时自动回退 OpenGL。
- 三角形着色器由 `glslc` 编译到 `${build}/assets/shaders/`，找不到时自动禁用三角形（仅清屏 + ImGui）。

## 当前边界（后续里程碑）

- `Mesh` / `Shader` / `Material` / `Texture` / `Renderer` / `MeshManager` 仍为 OpenGL 实现。
- Vulkan 后端当前提供框架级渲染流程：清屏 + 演示三角形（push constants）+ ImGui。
- `GsEditor`（含 CUDA-OpenGL 互操作、帧缓冲读回）仅支持 OpenGL 后端；
  运行 `ASTER_RENDER_API=vulkan` 时自动切换到独立的 RenderAPI 演示程序。
- 下一步：把网格/材质/着色器渲染管线迁移到 Vulkan（描述符集、顶点缓冲、UBO 等）。

---

# Vulkan Shader 端口（里程碑 2）

新增可复用的 Vulkan 着色器/管线封装，并在 Vulkan 后端内渲染真实网格验证链路：

## 新增文件

```
src/Aster/VulkanUtil.h        # 共享小工具（读文件/建着色器模块/内存类型选择）
src/Aster/VulkanPipeline.*    # 图形管线封装：SPIR-V → ShaderModule → 描述符集布局
                              #   → 管线布局(push constants) → 图形管线 + 相机 UBO
src/Aster/VulkanMeshBuffer.*  # 顶点/索引缓冲封装（host-visible，原型阶段）
assets/shader/vulkan/mesh.vert/.frag  # GLSL 450 网格着色器（UBO + push constants）
```

## 约定

- `VulkanPipeline`：`set 0 binding 0` = 相机 UBO（`view/projection`，128 字节）；
  push constants = `mat4 model + vec4 color`（80 字节，顶点/片元一致声明）。
- 顶点输入：`binding 0 / location 0` = `vec3` 位置（`R32G32B32_SFLOAT`）。
- 着色器由 CMake 的 `glslc` 编译为 SPIR-V（`${build}/assets/shaders/`），
  找不到时自动跳过对应渲染（仅保留清屏 + ImGui）。

## 演示

`VulkanRenderAPI` 内新增一个**旋转立方体**（8 顶点 / 36 索引），完整走通：
GLSL(450) → SPIR-V(glslc) → ShaderModule → 图形管线 → 顶点/索引缓冲
→ 相机 UBO（描述符集）→ push constants → 深度测试绘制。

## 说明

- OpenGL 侧 `Shader` 类（`glGetUniformLocation` 逐 uniform 设置）未改动，
  继续服务于 `GsEditor` 的 OpenGL 路径。
- `.shader` 文件（GLSL 330 + 独立 uniform）仍仅供 OpenGL 使用；
  Vulkan 使用 `assets/shader/vulkan/` 下的 GLSL 450 版本。
- 下一步：把框架的 `Mesh` / `Material` / `Renderer` 迁移到
  `VulkanPipeline` / `VulkanMeshBuffer`（含描述符集按材质绑定、多 UBO）。

---

# Vulkan Renderer 迁移（里程碑 3）

把框架 `Renderer` 的语义迁移到 Vulkan：新增 `VulkanSceneRenderer`，
替换 `VulkanRenderAPI` 内硬编码的演示网格。

## 新增文件

```
src/Aster/VulkanSceneRenderer.*  # Vulkan 场景渲染器（对应框架 Renderer）
```

## 对应关系

| 框架（OpenGL）        | Vulkan 版                          |
|-----------------------|------------------------------------|
| `Mesh`                | `VulkanMeshBuffer`（顶点/索引缓冲）|
| `Material`（颜色 uniform）| `glm::vec4 color`               |
| `Renderer::FlushBatches` | `BeginFrame() -> Submit() -> Record(cmd, view, proj)` |
| `Camera`（view/proj） | 相机 UBO（`VulkanPipeline` 内）    |

每帧流程：

```
BeginFrame()                     // 清空绘制列表
Submit(mesh, model, color)       // 记录绘制
Record(cmd, view, proj)          // 更新相机 UBO、绑定管线、逐条录制
```

顶点布局与框架 `Mesh` 完全一致：`pos3 + nor3 + uv2`（stride 32B），
`mesh.vert/.frag` 升级为支持法线/UV，并加入简单的 N·L 漫反射光照。

## 演示

`VulkanRenderAPI` 内的旋转立方体改为 24 顶点（pos/nor/uv）+ 36 索引，
通过 `VulkanSceneRenderer` 提交渲染，完整验证
"网格(顶点/索引缓冲) + 材质(颜色) + 渲染器(相机 UBO/逐条绘制)" 的迁移路径。

## 说明

- `VulkanSceneRenderer` 目前使用单共享管线（单一材质 uniform 布局），
  与框架最基础材质（`transparent.shader` 的 `uniform vec4 color`）对应。
- 框架 `Mesh` / `Material` 对象直接喂给渲染器的桥接（读取 `Mesh` 的
  原始 `vertices/indices` 与 `Material` 的颜色 uniform）留作下一步，
  届时可在 `GsEditor` 的 Vulkan 端口中使用。
- 下一步：材质多 uniform / 纹理采样（描述符集扩展）、实例化渲染、透明排序。
# HDR 环境贴图（环境映射 / IBL，里程碑 9）

## 概述
从 `assets/HDRIs/*.exr`（等距柱状投影 HDR）出发，在 CPU 上确定性地生成整套
“基于图像的光照（IBL）”贴图，提供**多种环境贴图实现方式**（可切换对比）：
1. **天空盒**（skybox）：HDR 背景（cubemap 采样，位于无穷远）。
2. **反射**（reflection）：用反射向量 R=reflect(-V,N) 采样环境 cubemap（近似镜面）。
3. **漫反射 IBL**（irradiance）：irradiance map（半球余弦卷积）作环境漫反射光照。
4. **高光 IBL**（specular IBL / split-sum）：预过滤 cubemap（按粗糙度分层 mip）+
   BRDF LUT 2D，PBR 高光环境反射。

## 新增/修改文件
- `src/Aster/Resource/EnvironmentMap.h/.cpp`：HDR 加载（tinyexr）+ CPU 转换
  （等距→cubemap、irradiance、GGX 预过滤 mip 链、BRDF LUT）。数据 RGBA32F。
- `src/Aster/Resource/stb_image_write_impl.cpp`：为 tinyexr 的 STB zlib 路径提供
  `stbi_zlib_compress`（解码的 `stbi_zlib_decode_buffer` 由 Texture.cpp 提供）。
- `assets/shader/vulkan/skybox.vert/.frag`：Vulkan 天空盒（全屏三角形 + 逆 VP）。
- `assets/shader/vulkan/mesh.frag`：新增 binding 6-10（环境 cubemap / irradiance /
  预过滤 / BRDF LUT / EnvUBO）与 4 种 IBL 模式 + Reinhard tone map。
- `src/Aster/Render/Vulkan/VulkanPipeline.h/.cpp`：描述符布局扩到 11 个 binding；
  EnvUBO；环境图像创建/上传（staging + barrier）；天空盒管线（无顶点输入，
  深度测试 LESS_OR_EQUAL、深度写关）；占位 1x1 资源保证 binding 6-10 始终有效。
- `src/Aster/Render/Vulkan/VulkanSceneRenderer.h/.cpp`：Init 增加 queue/cmdPool；
  UploadEnvironmentMap / SetEnvMode / SetEnvParams；Record 写 EnvUBO + 录天空盒。
- `src/Aster/Render/Vulkan/VulkanRenderAPI.h/.cpp`：RenderAPI 环境方法转发。
- `src/Aster/Render/RenderAPI.h`：新增 SetEnvironmentMap / SetEnvMode / SetEnvParams /
  RenderEnvironment（默认空实现，OpenGL 后端覆盖）。
- `src/demo/ModelDemoApp.h` + `ModelDemo.cpp`：加载 HDR、ImGui 控制面板
  （模式 0-3 / 强度 / 粗糙度 / 金属度 / AO / 方位角 / 曝光 / tone map）。
- **OpenGL 后端**（Windows 专用，`if(NOT APPLE)` 编译）：
  - `src/Aster/Render/OpenGL/OpenGLEnvironment.h/.cpp`：GL cubemap/BRDF LUT 上传、
    天空盒（内嵌 GLSL 330 全屏三角形）、纹理绑定到单元 6-9。
  - `src/Aster/Render/OpenGL/OpenGLRenderAPI.*`：环境方法 + RenderEnvironment。
  - `assets/shader/env_ibl.shader`：GLSL 330 版 IBL（pos+normal、纹理单元 6-9）。
  - `assets/shader/skybox.shader`：GLSL 330 天空盒（参考文件）。
  - `src/Aster/Core/App.cpp`：OpenGL 分支在 FlushBatches 前调用 RenderEnvironment。
  - demo 的 GL 材质改用 env_ibl.shader，每帧更新环境 uniform。

## 数据流
```
assets/HDRIs/*.exr ──tinyexr──▶ 等距 RGBA32F ──CPU──▶ 环境 cubemap(256)
                                                     ├─ irradiance cubemap(32)
                                                     ├─ 预过滤 cubemap(128, 6 mips)
                                                     └─ BRDF LUT(256×256)
EnvironmentMap ──SetEnvironmentMap──▶ 后端上传（Vulkan：staging+barrier+描述符）
                                     └─▶ 天空盒 + mesh.frag 4 种 IBL 模式
```

## 约定
- **Vulkan 描述符**：set 0 共 11 个 binding：0 相机 / 1 灯光 / 2 2D 阴影数组 /
  3 阴影 UBO / 4 点光源 cubemap / 5 点阴影 UBO / 6 环境 cubemap / 7 irradiance /
  8 预过滤 / 9 BRDF LUT / 10 EnvUBO。主集与阴影集都写入 6-10（共享资源）。
- **EnvUBO**（std140，3×vec4）：params0=(强度, 模式, mip数, AO)，params1=(粗糙度,
  金属度, 方位角弧度, 曝光)，params2=(tonemap开关, 相机位置xyz)。
- **模式**：0=关（保持旧固定环境光 0.12），1=反射，2=漫反射 IBL，3=漫反射+高光 IBL。
- **占位资源**：pipeline 创建时建 1x1 黑色 cubemap + 1x1 LUT 并写入 binding 6-9，
  保证 mesh.frag 静态引用的采样器始终有效（真实数据上传后再替换）。
- **天空盒**：Vulkan 用全屏三角形（gl_VertexIndex）+ 逆 VP；view 只用旋转部分
  （位于无穷远）；深度测试 LESS_OR_EQUAL、深度写关、深度=1.0。
  OpenGL 用同样逻辑（gl_VertexID）。
- **方位角**：环境绕 Y 旋转由 uEnvYaw 统一控制（天空盒与物体 IBL 用同一值）。

## 验证
- macOS（Vulkan）实机运行：环境贴图加载 ~1.4s，无校验错误；截图确认天空盒、
  棱角球、地面均渲染；CPU 端验证等距顶部=天空（蓝色）、底部=沙地（暖色）。
- 独立测试：`/tmp/envmap_test` 可离线验证 CPU 转换（需 tinyexr + stb 实现）。

## 说明 / 坑
- **tinyexr 新版需要配套头文件**：`exr_reader.hh`、`streamreader.hh`（用户提供），
  且默认用 miniz → 本项目改用 `TINYEXR_USE_STB_ZLIB=1` 复用 stb 的 zlib
  （`stbi_zlib_decode_buffer` 来自 Texture.cpp，`stbi_zlib_compress` 来自
  stb_image_write_impl.cpp），避免下载 miniz。
- **Vulkan 描述符“静态使用”**：mesh.frag 只要在分支里引用 sampler，绑定就必须有效，
  因此必须用占位资源兜底，不能依赖运行时 envMode==0 跳过来跳过采样。
- **OpenGL 路径仅 Windows 编译**：macOS 无法编译/运行 GL，OpenGL 环境功能需在
  Windows 上验证（代码已用 g++ 语法检查通过）。
- **ImGui 遮挡 3D 视图**：截图验证时 ImGui 面板会盖住画面中心，需注意采样区域。

---

# 每对象材质 / 纹理 / 自定义 Shader（Vulkan，里程碑 10）

在环境贴图（IBL）基础上，让 Vulkan 后端支持**每个场景对象**单独指定材质参数、
材质纹理，乃至**自定义着色器**。分三步落地：

## M1：每对象材质参数（roughness / metallic / AO）

- `Material.h`：新增 `roughness / metallic / ao / textureIndex / vulkanPipeline` 成员。
- `RenderAPI.h`：新增 `MaterialParams`（color + 上述参数 + pipelineIndex），
  `VulkanRenderAPI::SubmitSceneMesh` 签名改为接收 `MaterialParams`。
- push constant 扩到 **112 字节**：`mat4 model + vec4 color + vec4 shadow + vec4 material`，
  `material = (粗糙度, 金属度, AO, 纹理索引)`（mesh.vert/frag 一致）。
- `mesh.frag` 的 `ComputeEnvAmbient()` 改用 `pc.material.x/y/z`（每对象）而非全局 EnvUBO。
- `Model::draw()` 从 `Material` 组装 `MaterialParams` 提交。
- demo：球体材质用滑块控制（每对象），地面设为哑光（roughness=1）。

## M2：每对象材质纹理

- `VulkanPipeline`：描述符布局扩到 **12 个 binding**，binding 11 =
  `sampler2D` 数组（`MAX_MATERIAL_TEXTURES = 16`）；描述符池 CIS 计数相应扩大。
- `RegisterMaterialTexture(queue, pool, rgba8, w, h)`：staging 上传 RGBA8 到
  device-local 并更新 binding 11 数组元素（主集 + 阴影集），返回索引；
  **索引 0 恒为 1x1 白色纹理**（无纹理对象采样它 = 纯色）。
- `mesh.frag`：`pc.material.w >= 0` 时 `texture(uMaterialTextures[idx], uv) * color`。
  片段阶段对 sampler 数组的动态均匀索引（来自 push constant）在 Vulkan 1.0 即合法，
  无需 descriptor indexing 扩展（MoltenVK 实测通过）。
- demo：`BuildIcoSphere` 生成球面 UV（等距柱状），`BuildPlane` 生成平面 UV；
  生成 256×256 棋盘纹理注册后赋给球体（地面保持纯色）。

## M3：自定义 shader 管线

- `VulkanPipeline::CreateMaterialPipeline(shaderDir, vertName, fragName)`：
  用**共享**描述集布局 / push constants / 顶点布局（pos3+nor3+uv2）创建自定义
  图形管线，返回索引（0 = 默认 mesh，自定义从 1 起）；`GetMaterialPipeline(idx)` 取用。
- `VulkanSceneRenderer`：`DrawCall` 增加 `pipelineIndex`；`Submit` 增参；
  `Record()` 按 `pipelineIndex` 分组绘制（所有管线共用同一描述符集，UBO 只更新一次）；
  `RecordShadow()` / 平面阴影回退**只处理 pipelineIndex==0**（自定义 shader 无深度/投影逻辑）。
- `VulkanRenderAPI::CreateMaterialPipeline` / `Model::draw()` 透传 `Material::vulkanPipeline`。
- 新增 `assets/shader/vulkan/toon.vert/.frag`：卡通（cel shading）示例着色器，
  量化漫反射 + 硬高光 + 边缘暗化；访问共享描述集的灯光 / irradiance / 材质纹理绑定。
- `CMakeLists.txt` VULKAN_SHADERS 加入 toon.vert/toon.frag。
- demo：创建 toon 管线赋给**地面**材质（球体仍用默认 PBR mesh shader），
  一眼可见“地面卡通、球体 PBR”的每对象 shader 差异。

## 验证
- macOS（Vulkan / MoltenVK）实机运行无校验错误。
- 截图：M1 地面哑光 + 球体滑块材质；M2 球体出现棋盘纹理（tint 橙色）；M3 地面出现
  卡通分档明暗、球体保持 PBR 反射。

## 说明 / 坑
- **push constant 块必须完全一致**（mesh.vert / mesh.frag / toon.vert / toon.frag），
  否则管线 layout 校验失败。
- **自定义管线共享描述集**：相机/灯光/环境/材质纹理 UBO 只需更新一次；自定义 shader
  能访问全部 binding（这是“自定义 shader”能力的来源）。
- **阴影 pass 排除自定义管线**：自定义 shader 没有 mesh.vert 的平面投影 / depth.frag
  深度逻辑，强投会出错，故只对默认 mesh 管线录阴影。
- **纹理索引 0 = 白色占位**：无纹理对象采样 binding 11 数组时命中白色 → 退化为纯色，
  避免“无纹理”与“索引越界”的未定义行为。

---

# 全局材质管理器（MaterialManager，里程碑 11）

让材质**可复用**：多个物体共享同一 shader，或共享同一材质对象；并支持“同一 shader、
不同参数”的**材质实例**。

## 设计

- `src/Aster/Resource/MaterialManager.h/.cpp`：全局材质管理器（Meyer 单例，与
  `SceneManager` / `MeshManager` 一致）。
  1. **Shader 复用**：`RegisterShader(name, path)` 按名缓存 1 份 `shared_ptr<Shader>`，
     同一路径只编译/持有一次（OpenGL 后端避免每材质重复加载）。
  2. **基础材质复用**：`RegisterMaterial(name, shaderName, color)` / `RegisterMaterial(name, color)`
     按名缓存；`GetMaterial(name)` 返回同一实例 → 多个物体引用同一材质对象。
  3. **材质实例**：`CreateMaterialInstance(instanceName, baseName)` 以基础材质为模板
     复制创建实例——`shader` 与 base **共享**（同一 shared_ptr），参数（颜色/粗糙度/
     金属度/AO/纹理/管线/OpenGL uniforms）复制后**独立可改**。这是“物体 A/B 用同一
     shader、只是颜色/粗糙度不同”的用法。
- `Material.h`：新增 `name` / `baseName` 元数据（实例记录基础材质名）。
- `CMakeLists.txt`：加入 `MaterialManager.cpp/h`。
- demo：`ModelDemo.cpp` 的材质创建全部改走 `MaterialManager`（注册 `ground` / `sphere`
  基础材质）；新增第二颗球 `sphereB`——`CreateMaterialInstance("sphere_blue", "sphere")`
  实例：蓝色、粗糙度 0.85、无纹理，复用同一 icosphere 网格；ImGui 面板展示管理器
  注册表（shader 数 / 材质数 / 每个材质的参数与实例关系）。

## 验证
- macOS（Vulkan / MoltenVK）实机运行无错误：主球橙色 PBR 棋盘、第二颗球蓝色哑光
  （同一 shader、不同参数）、地面 toon 卡通；ImGui 显示 `sphere_blue <- sphere`。

## 说明
- 实例是“快照”：创建后改 base 不影响已创建实例，改实例不影响 base。
- Vulkan 后端 shader 为 null（程序化材质），复用体现在“同材质对象共享”与“实例参数
  独立”；OpenGL 后端实例共享 `shared_ptr<Shader>`，避免重复编译。
