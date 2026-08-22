// aster_demo 可执行文件入口：
//   cmake -DASTER_ENABLE_DEMO=ON   （默认开启）
// 运行：./aster_demo（默认 OpenGL）或 ASTER_RENDER_API=vulkan ./aster_demo
//   Model 双后端演示：ASTER_MODEL_DEMO=1 ./aster_demo
//   物理模拟演示：ASTER_PHYSICS_DEMO=1 ./aster_demo
#include <cstdlib>
#include "RenderAPIDemo.h"
#include "ModelDemo.h"
#include "PhysicsDemoApp.h"

int main()
{
    // ASTER_PHYSICS_DEMO=1 时运行物理模拟演示（运动学 → 碰撞 → 射线 → BVH）
    if (std::getenv("ASTER_PHYSICS_DEMO"))
        return RunPhysicsDemo();
    // ASTER_MODEL_DEMO=1 时运行 Model 双后端演示（程序化模型，无需 assimp）
    // if (std::getenv("ASTER_MODEL_DEMO"))
    return RunModelDemo();
    // return RunRenderAPIDemo();
}
