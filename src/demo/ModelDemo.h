#pragma once

// ============================================================================
// ModelDemo —— 演示框架 Model 对象同时支持 OpenGL 与 Vulkan 后端
// ----------------------------------------------------------------------------
// 与 RenderAPIDemo（仅驱动 RenderAPI 抽象）不同，本 demo 直接使用框架的
// Model / Mesh / Material 场景管线：
//   - 程序化生成一个 icosphere（无需 assimp，跨平台可构建）
//   - OpenGL 后端：Model::draw() -> 框架 Renderer::FlushBatches
//   - Vulkan 后端：Model::draw() -> VulkanRenderAPI::SubmitSceneMesh
//                     -> VulkanSceneRenderer（懒创建 VulkanMeshBuffer）
//
// 运行：ASTER_MODEL_DEMO=1 ./aster_demo
//       ASTER_RENDER_API=vulkan ASTER_MODEL_DEMO=1 ./aster_demo   （Vulkan 后端）
// ============================================================================

int RunModelDemo();
