#version 450 core

// 地面网格线片元着色器：在大平面网格上“程序化”绘制网格线。
// 原理：用世界 XZ 坐标对格子边长取余（fract）判断是否落在网格线上，
//       格内片段直接 discard，只保留线框；按到相机距离做淡出（远减淡）。
// 抗虚线：用 fwidth() 做屏幕空间自适应线宽——掠射角（线与视线平行）时
//       世界空间线宽被压缩到亚像素，线会断成虚线；保底 1px 后任何角度连续。
//
// 访问共享描述集的绑定：
//   binding 10 = 环境 UBO（相机位置在 params2.yzw，用于距离淡出）
//   binding 12 = 自定义材质参数（M4：OpenGL 风格 SetUniform(key,type,value) 打包）
//
// 自定义参数（与 CPU 端 SetUniform 的注册顺序一一对应，每个 uniform 占一个 vec4 槽位）：
//   params[0].rgb = gridColor（线框颜色，vec3f）
//   params[1].x   = opacity（透明度，0~1，float）
//   params[2].x   = cellSize（格子边长，世界单位，float）
//   params[3].x   = fadeStart（距离淡出起点，float）
//   params[4].x   = fadeEnd（距离淡出终点，超过后完全消失，float）
//   params[5].x   = lineWidth（线宽，相对格子的比例，如 0.03 = 3% 格子宽，float）

layout(location = 0) in vec3 vWorldPos;

layout(std140, set = 0, binding = 10) uniform EnvUBO {
    vec4 params0; // x=强度, y=模式, z=mip级数, w=AO
    vec4 params1; // x=粗糙度, y=金属度, z=方位角, w=曝光
    vec4 params2; // x=tonemap, yzw=相机位置
} uEnv;

layout(std140, set = 0, binding = 12) uniform MaterialParams {
    vec4 params[8];
} uMatParams;

layout(location = 0) out vec4 FragColor;

void main()
{
    // 每个 uniform 占一个 vec4 槽位（与 CPU 端 SetUniform 注册顺序一致）
    vec3  gridColor = uMatParams.params[0].rgb;
    float opacity   = uMatParams.params[1].x;
    float cellSize  = max(uMatParams.params[2].x, 0.01);
    float fadeStart = uMatParams.params[3].x;
    float fadeEnd   = max(uMatParams.params[4].x, fadeStart + 1.0);
    float lineW     = clamp(uMatParams.params[5].x, 0.0001, 1.0); // 相对格子的线宽
    if (opacity <= 0.001)
        opacity = 1.0; // 未设置透明度时默认不透明，避免整张网格被 discard

    // 网格单元坐标（世界 XZ / cellSize）+ 屏幕空间导数（每像素跨越多少个格子单元）。
    // fwidth 是相邻像素间 p 的变化量，用于把“世界空间线宽”换算成“像素宽度”。
    vec2 p = vWorldPos.xz / cellSize;
    vec2 d = max(fwidth(p), vec2(1e-6));

    // 到最近网格线的距离（格子单元，0~0.5，0=线中心）
    vec2 f = abs(fract(p) - 0.5);

    // 线半宽（格子单元）：用户线宽 vs 至少 1 像素，取大者。
    // 关键：掠射角（线与视线接近平行）时，世界空间线宽在屏幕上被压缩到亚像素，
    // 像素中心有时落在线上、有时落在线外，看起来就成了“断断续续的虚线”。
    // 用 d 保底后，线在屏幕上恒 ≥1px 宽，任何角度都连续。
    vec2 halfW = max(vec2(lineW * 0.5), d);

    // 线掩码：线中心=1，边缘在 1 像素（d）内平滑过渡到 0（屏幕空间抗锯齿）
    vec2 line = 1.0 - smoothstep(halfW - d, halfW, f);
    float mask = max(line.x, line.y);

    // 距离淡出：远处颜色减淡、透明度下降
    vec3 camPos = uEnv.params2.yzw;
    float dist = length(vWorldPos.xz - camPos.xz);
    float fade = 1.0 - smoothstep(fadeStart, fadeEnd, dist);

    float alpha = opacity * fade * mask;
    if (alpha < 0.003)
        discard; // 格子内部不绘制（只留线框）

    FragColor = vec4(gridColor * fade, alpha);
}
