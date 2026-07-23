"""
通用 3D 模型 → ARGB8565 精灵帧渲染器

用法：
    blender --background --python scripts/render_frames.py

配置（修改下方 CONFIG 区域）：
    MODEL_PATH   — 你的 3D 模型路径 (.glb / .obj / .fbx)
    OUTPUT_DIR   — 输出目录
    SPRITE_SIZE  — 帧分辨率（像素，默认正方形）
    ANGLE_STEPS  — 角度映射网格档数（10 = 10×10 = 100 帧）
    SEQ_FRAMES   — 时序动画每段帧数

依赖：Blender（bpy），无需其他 Python 包

模型适配逻辑：
    - 自动计算包围盒，调整相机正交缩放使模型占画面 70%
    - 自动翻转垂直方向（检测并修正上下颠倒）
    - 支持 GLB/OBJ/FBX 格式
"""

import bpy
import os
import math

# ============================================================
# 配置区域 —— 换模型改这里
# ============================================================

MODEL_PATH      = r"C:/Users/djh666/Downloads/kenney_cube-pets_1.0/Models/GLB format/animal-chick.glb"
OUTPUT_DIR      = r"C:/Users/djh666/Desktop/demo2/pet_frames"

SPRITE_SIZE     = 128       # 输出像素
ANGLE_STEPS     = 10        # 角度档数（10 = 10×10 网格）
ANGLE_MAX       = 45.0      # 最大角度（度）
SEQ_FRAMES      = 16        # 时序动画帧数

ENGINE          = 'BLENDER_EEVEE'  # EEVEE（快）或 CYCLES（光影好但慢）

# ============================================================
# 初始化
# ============================================================

TMP_PNG = None

def find_model_file():
    """在 MODEL_PATH 目录中找 GLB/OBJ/FBX 文件"""
    if os.path.isfile(MODEL_PATH):
        return MODEL_PATH
    # MODEL_PATH 是目录，找第一个支持的格式
    for ext in ['.glb', '.gltf', '.obj', '.fbx']:
        for f in os.listdir(MODEL_PATH):
            if f.lower().endswith(ext):
                return os.path.join(MODEL_PATH, f)
    raise FileNotFoundError(f"No 3D model found in {MODEL_PATH}")

def import_model(path):
    """导入模型，返回所有 mesh 对象的父级"""
    ext = os.path.splitext(path)[1].lower()
    if ext in ('.glb', '.gltf'):
        bpy.ops.import_scene.gltf(filepath=path)
    elif ext == '.obj':
        bpy.ops.import_scene.obj(filepath=path)
    elif ext == '.fbx':
        bpy.ops.import_scene.fbx(filepath=path)
    else:
        raise RuntimeError(f"Unsupported format: {ext}")

    # 收集所有 mesh 对象
    meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    if not meshes:
        raise RuntimeError("No mesh objects found in model")

    # 如果只有一个顶层对象就用它，否则找共同的父级
    roots = [o for o in bpy.context.scene.objects if o.parent is None]
    if len(roots) == 1 and roots[0].type == 'MESH':
        return roots[0]

    # 多个对象：全选并设 origin
    bpy.ops.object.select_all(action='DESELECT')
    for m in meshes:
        m.select_set(True)
    if meshes:
        bpy.context.view_layer.objects.active = meshes[0]

    print(f"  Found {len(meshes)} mesh objects, {len(roots)} root(s)")
    return meshes[0]  # 返回第一个 mesh 用于操作

def get_model_bbox(objs):
    """计算所有 mesh 对象的世界空间包围盒"""
    if not isinstance(objs, list):
        objs = [objs]
    xs, ys, zs = [], [], []
    for obj in objs:
        if obj.type != 'MESH':
            continue
        for v in obj.data.vertices:
            w = obj.matrix_world @ v.co
            xs.append(w.x); ys.append(w.y); zs.append(w.z)
    if not xs:
        return 1.0, 1.0, 1.0
    return max(xs)-min(xs), max(ys)-min(ys), max(zs)-min(zs)

def setup_scene():
    """清空场景、导入模型、布置相机灯光"""
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)

    model_path = find_model_file()
    print(f"  Loading: {model_path}")
    main_obj = import_model(model_path)

    all_meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    sx, sy, sz = get_model_bbox(all_meshes)
    model_size = max(sx, sy, sz)
    print(f"  BBox: {sx:.2f}×{sy:.2f}×{sz:.2f}, max={model_size:.2f}")

    # 正交相机
    bpy.ops.object.camera_add(location=(0, -5, 0))
    camera = bpy.context.object
    camera.name = "RenderCamera"
    camera.data.type = 'ORTHO'
    camera.data.ortho_scale = model_size / 0.70

    # 三灯布光
    for loc, e in [((3,-3,5), 4.0), ((-2,-1,1), 2.0), ((0,2,3), 1.5)]:
        bpy.ops.object.light_add(type='SUN', location=loc)
        bpy.context.object.data.energy = e

    # 渲染设置
    scene = bpy.context.scene
    scene.render.engine = ENGINE
    scene.render.resolution_x = SPRITE_SIZE
    scene.render.resolution_y = SPRITE_SIZE
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True
    scene.camera = camera

    if ENGINE == 'BLENDER_EEVEE':
        scene.eevee.taa_render_samples = 16

    return all_meshes, camera, model_size


# ============================================================
# 渲染 → ARGB8565（内置垂直翻转修正）
# ============================================================

BYTES_PER_FRAME = SPRITE_SIZE * SPRITE_SIZE * 3

def render_to_argb8565():
    """渲染当前场景 → 临时 PNG → 读像素 → ARGB8565 字节"""
    bpy.context.scene.render.filepath = TMP_PNG
    bpy.ops.render.render(write_still=True)

    img = bpy.data.images.load(TMP_PNG)
    pixels = img.pixels

    data = bytearray(BYTES_PER_FRAME)

    for i in range(SPRITE_SIZE * SPRITE_SIZE):
        r = int(pixels[i * 4]     * 255.0 + 0.5)
        g = int(pixels[i * 4 + 1] * 255.0 + 0.5)
        b = int(pixels[i * 4 + 2] * 255.0 + 0.5)
        a = int(pixels[i * 4 + 3] * 255.0 + 0.5)

        r5, g6, b5 = (r>>3)&0x1F, (g>>2)&0x3F, (b>>3)&0x1F
        rgb565 = (r5 << 11) | (g6 << 5) | b5

        # 垂直翻转：第 y 行映射到第 (H-1-y) 行
        y = i // SPRITE_SIZE
        x = i % SPRITE_SIZE
        fy = SPRITE_SIZE - 1 - y
        offset = (fy * SPRITE_SIZE + x) * 3

        data[offset]     = rgb565 & 0xFF
        data[offset + 1] = (rgb565 >> 8) & 0xFF
        data[offset + 2] = a

    bpy.data.images.remove(img)
    os.remove(TMP_PNG)
    return bytes(data)


# ============================================================
# 相机控制
# ============================================================

def set_camera_angle(camera, pitch_deg, roll_deg, dist=5.0):
    """pitch: +俯视/-仰视, roll: +右/-左"""
    p, r = math.radians(pitch_deg), math.radians(roll_deg)
    x = dist * math.sin(r) * math.cos(p)
    y = -dist * math.cos(r) * math.cos(p)
    z = dist * math.sin(p)
    camera.location = (x, y, z)
    direction = -camera.location
    rot = direction.to_track_quat('-Z', 'Z')
    camera.rotation_euler = rot.to_euler()


# ============================================================
# 渲染 IDLE 角度网格
# ============================================================

def render_idle(camera, meshes):
    """10×10 角度网格"""
    out = os.path.join(OUTPUT_DIR, "idle")
    os.makedirs(out, exist_ok=True)

    pitch_vals = [ANGLE_MAX - i * (2*ANGLE_MAX)/(ANGLE_STEPS-1) for i in range(ANGLE_STEPS)]
    roll_vals  = [-ANGLE_MAX + i * (2*ANGLE_MAX)/(ANGLE_STEPS-1) for i in range(ANGLE_STEPS)]

    idx = 0
    for row, pitch in enumerate(pitch_vals):
        for col, roll in enumerate(roll_vals):
            set_camera_angle(camera, pitch, roll)
            data = render_to_argb8565()
            with open(os.path.join(out, f"{idx:03d}.bin"), 'wb') as f:
                f.write(data)
            print(f"  idle [{idx:03d}/{ANGLE_STEPS*ANGLE_STEPS-1}] "
                  f"p={pitch:+.0f} r={roll:+.0f}")
            idx += 1

    with open(os.path.join(out, "meta.txt"), 'w') as f:
        f.write(f"{SPRITE_SIZE} {SPRITE_SIZE} {ANGLE_STEPS} {ANGLE_STEPS}\n")
    print(f"  idle: {idx} frames ✓")


# ============================================================
# 时序动画
# ============================================================

def render_seq(name, camera, meshes, frame_count, anim_func):
    out = os.path.join(OUTPUT_DIR, name)
    os.makedirs(out, exist_ok=True)

    # 保存所有 mesh 的原始状态
    saved = []
    for m in meshes:
        saved.append((m, m.location.copy(), m.scale.copy()))

    for i in range(frame_count):
        t = i / max(frame_count-1, 1)
        pitch, roll, dz, scale = anim_func(t)

        set_camera_angle(camera, pitch, roll)
        for m, _, _ in saved:
            m.location.z += dz
            m.scale = (scale, scale, scale)

        data = render_to_argb8565()
        with open(os.path.join(out, f"{i:03d}.bin"), 'wb') as f:
            f.write(data)
        print(f"  {name} [{i:03d}/{frame_count-1:03d}]")

    # 恢复
    for m, loc, scl in saved:
        m.location = loc
        m.scale = scl

    with open(os.path.join(out, "meta.txt"), 'w') as f:
        f.write(f"{SPRITE_SIZE} {SPRITE_SIZE} {frame_count}\n")
    print(f"  {name}: {frame_count} frames ✓")


# ============================================================
# 动画函数（可根据模型调整振幅）
# ============================================================

def ani_eat(t):
    return (5*math.sin(t*math.pi*2), 0, 0.06*math.sin(t*math.pi*3), 1.0)

def ani_play(t):
    return (3*math.sin(t*math.pi*3), 18*math.sin(t*math.pi*2),
            0.08*abs(math.sin(t*math.pi*2)), 1.0)

def ani_shocked(t):
    return (5*math.sin(t*math.pi*2), 0, 0.15*math.sin(t*math.pi),
            1.0 + 0.2*math.sin(t*math.pi))

def ani_walk(t):
    return (2*math.sin(t*math.pi*4), 12*math.sin(t*math.pi*2),
            0.04*math.sin(t*math.pi*4), 1.0)


# ============================================================
# 主入口
# ============================================================

def main():
    global TMP_PNG
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    TMP_PNG = os.path.join(OUTPUT_DIR, "_tmp.png")

    print("=" * 50)
    print("3D Model → Sprite Frame Renderer")
    print(f"  Output: {OUTPUT_DIR}")
    print(f"  Size:   {SPRITE_SIZE}×{SPRITE_SIZE}")
    print(f"  Engine: {ENGINE}")
    print("=" * 50)

    meshes, camera, _ = setup_scene()

    print("\n[1/5] IDLE — 3D angle grid")
    render_idle(camera, meshes)

    print("\n[2/5] EATING")
    render_seq("eating", camera, meshes, SEQ_FRAMES, ani_eat)

    print("\n[3/5] PLAYING")
    render_seq("playing", camera, meshes, SEQ_FRAMES, ani_play)

    print("\n[4/5] SHOCKED")
    render_seq("shocked", camera, meshes, 8, ani_shocked)

    print("\n[5/5] WALKING")
    render_seq("walking", camera, meshes, SEQ_FRAMES, ani_walk)

    print(f"\n{'=' * 50}")
    print("Done! Next: python scripts/preview.py")
    print("Then copy pet_frames/ to SD card.")
    print(f"{'=' * 50}")


if __name__ == "__main__":
    main()
