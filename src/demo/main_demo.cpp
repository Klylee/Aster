// aster_demo 可执行文件入口：
//   cmake -DASTER_ENABLE_DEMO=ON   （默认开启）
// 运行：./aster_demo（默认 OpenGL）或 ASTER_RENDER_API=vulkan ./aster_demo
//   Model 双后端演示：ASTER_MODEL_DEMO=1 ./aster_demo
#include <cstdlib>
#include "RenderAPIDemo.h"
#include "ModelDemo.h"

int main()
{
    // ASTER_MODEL_DEMO=1 时运行 Model 双后端演示（程序化模型，无需 assimp）
    // if (std::getenv("ASTER_MODEL_DEMO"))
    return RunModelDemo();
    // return RunRenderAPIDemo();
}
