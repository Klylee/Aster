#pragma once

// 运行一个独立的 RenderAPI 演示程序：
// 直接使用 RenderAPI 抽象（不依赖 CUDA / assimp / Aster 场景系统），
// 可通过环境变量 ASTER_RENDER_API=opengl|vulkan 切换后端。
// 返回进程退出码。
int RunRenderAPIDemo();
