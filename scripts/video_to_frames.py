#!/usr/bin/env python3
"""
视频/GIF → RGB565 帧序列转换器

支持格式: mp4, gif, webm, avi, mov 等（所有 FFmpeg 支持的格式）
自动缩放至 240×240 并居中，保持原始比例（加黑边填充）

用法:
    python video_to_frames.py input.mp4 -o output_dir -f 10
    python video_to_frames.py input.gif -o output_dir -f 8

输出:
    output_dir/
        meta.txt     → "240 240 帧数 fps"
        000.bin      → RGB565 原始帧 (240×240×2 = 115200 字节)
        001.bin
        ...
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


def find_ffmpeg():
    """查找 ffmpeg，返回可执行路径或 None"""
    import shutil

    # 1. 先尝试 PATH
    path = shutil.which("ffmpeg")
    if path:
        return path

    # 2. 常见安装位置
    candidates = [
        r"C:\softer\ffmpeg\bin\ffmpeg.exe",
        r"C:\softer\ffmpeg\ffmpeg.exe",
        r"C:\ffmpeg\bin\ffmpeg.exe",
        r"C:\Program Files\ffmpeg\bin\ffmpeg.exe",
    ]
    for c in candidates:
        if os.path.isfile(c):
            return c

    return None


MAX_FRAMES = 70  # PSRAM 8MB 约可存 70 帧 240×240 RGB565


def rmdir(path):
    """安全删除目录"""
    try:
        shutil.rmtree(str(path))
    except FileNotFoundError:
        pass


def get_duration(input_path: str) -> float:
    """用 ffprobe 获取视频时长（秒）"""
    cmd = [
        "ffprobe", "-v", "error",
        "-show_entries", "format=duration",
        "-of", "csv=p=0",
        input_path
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode == 0:
        try:
            return float(result.stdout.strip())
        except ValueError:
            pass
    return 0.0


def get_video_info(input_path: str):
    """用 ffprobe 获取视频元信息"""
    cmd = [
        "ffprobe", "-v", "error",
        "-select_streams", "v:0",
        "-show_entries", "stream=width,height,r_frame_rate,nb_frames",
        "-of", "csv=p=0",
        input_path
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        return None

    parts = result.stdout.strip().split(",")
    if len(parts) < 3:
        return None

    info = {"width": 0, "height": 0, "fps": 0}
    try:
        info["width"] = int(parts[0])
        info["height"] = int(parts[1])
        # r_frame_rate 格式如 "10/1" 或 "30000/1001"
        fps_str = parts[2]
        if "/" in fps_str:
            num, den = fps_str.split("/")
            info["fps"] = round(float(num) / float(den))
        else:
            info["fps"] = round(float(fps_str))
    except (ValueError, IndexError, ZeroDivisionError):
        pass

    return info


def video_to_frames(input_path: str, output_dir: str, fps: int,
                    width: int = 240, height: int = 240,
                    pad_color: str = "black"):
    """用 FFmpeg 提取帧 → PNG → 转 RGB565 .bin"""
    input_path = os.path.abspath(input_path)
    out = Path(output_dir)
    out.mkdir(parents=True, exist_ok=True)

    # 检测源视频信息
    info = get_video_info(input_path)
    is_gif = input_path.lower().endswith(".gif")

    if info:
        src_fps = info["fps"] if info["fps"] > 0 else fps
        print(f"[INFO] Source: {info['width']}x{info['height']}, "
              f"{'GIF' if is_gif else 'Video'} ~{src_fps}fps")
    else:
        src_fps = fps
        print(f"[INFO] Source: {'GIF' if is_gif else 'Video'} (no probe data)")

    target_fps = fps if fps > 0 else max(src_fps, 1)
    print(f"[INFO] Target: {width}x{height} @ {target_fps}fps, "
          f"pad={pad_color}, keep aspect ratio")

    # 预估帧数 = 时长 × 帧率
    duration = get_duration(input_path)
    if duration > 0:
        est_frames = int(duration * target_fps) + 1
        if est_frames > MAX_FRAMES:
            print(f"[SKIP] Estimated {est_frames} frames > {MAX_FRAMES} limit "
                  f"({duration:.1f}s @ {target_fps}fps)")
            print(f"  Solutions: use lower fps (-f 6) or trim the video")
            rmdir(str(out))
            return False
        print(f"[INFO] Estimated: ~{est_frames} frames ({duration:.1f}s)")

    # FFmpeg 滤镜链:
    #   1. fps=target_fps     → 统一帧率
    #   2. scale=w:h:force_original_aspect_ratio=decrease → 缩放到适应框内
    #   3. pad=w:h:(ow-iw)/2:(oh-ih)/2:color → 居中黑边填充
    vf = (f"fps={target_fps},"
          f"scale={width}:{height}:force_original_aspect_ratio=decrease,"
          f"pad={width}:{height}:(ow-iw)/2:(oh-ih)/2:{pad_color}")

    tmp_png = str(out / "frame_%04d.png")
    cmd = ["ffmpeg", "-y", "-i", input_path, "-vf", vf, tmp_png]

    print(f"\n[FFmpeg] {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)

    if result.returncode != 0:
        print(f"[ERROR] FFmpeg failed:\n{result.stderr}")
        rmdir(str(out))
        return False

    # 收集生成的 PNG 文件
    png_files = sorted(out.glob("frame_*.png"))
    if not png_files:
        print("[ERROR] No frames extracted")
        rmdir(str(out))
        return False

    print(f"[OK] Extracted {len(png_files)} frames")

    # 实际帧数超限检查
    if len(png_files) > MAX_FRAMES:
        print(f"[ERROR] {len(png_files)} frames exceeds {MAX_FRAMES} limit")
        print(f"  Removing output directory...")
        for pf in png_files:
            try: pf.unlink()
            except OSError: pass
        rmdir(str(out))
        return False

    # 逐帧转 RGB565
    from PIL import Image

    for i, png_path in enumerate(png_files):
        bin_path = out / f"{i:03d}.bin"
        png_to_rgb565(str(png_path), str(bin_path))
        png_path.unlink()  # 删除临时 PNG

        if (i + 1) % 50 == 0 or i == len(png_files) - 1:
            pct = (i + 1) * 100 // len(png_files)
            print(f"  [{pct:3d}%] {i + 1}/{len(png_files)} frames")

    # 写 meta.txt
    meta_path = out / "meta.txt"
    meta_path.write_text(f"{width} {height} {len(png_files)} {target_fps}\n")

    print(f"\n[DONE] {len(png_files)} frames → {out.absolute()}")
    print(f"  Resolution: {width}x{height} (padded)")
    print(f"  Frame rate: {target_fps} fps")
    print(f"  Duration:   {len(png_files) / target_fps:.1f}s")
    total_mb = sum(f.stat().st_size for f in out.glob("*.bin")) / (1024 * 1024)
    print(f"  SD space:   {total_mb:.1f} MB")

    return True


def png_to_rgb565(png_path: str, bin_path: str):
    """PNG → raw RGB565 bytes (little-endian, LVGL 兼容)"""
    from PIL import Image

    img = Image.open(png_path).convert("RGB")
    w, h = img.size
    pixels = list(img.getdata())
    buf = bytearray(w * h * 2)

    for idx, (r, g, b) in enumerate(pixels):
        r5 = (r >> 3) & 0x1F
        g6 = (g >> 2) & 0x3F
        b5 = (b >> 3) & 0x1F
        val = (r5 << 11) | (g6 << 5) | b5
        # LE (LVGL draw buffer 格式)
        buf[idx * 2] = val & 0xFF
        buf[idx * 2 + 1] = (val >> 8) & 0xFF

    with open(bin_path, "wb") as f:
        f.write(buf)


SUPPORTED_EXT = {".mp4", ".gif", ".webm", ".avi", ".mov", ".mkv",
                  ".wmv", ".flv", ".m4v", ".ogv", ".mpg", ".mpeg"}


def batch_convert(input_dir: str, output_base: str, fps: int,
                  width: int, height: int, pad_color: str):
    """批量转换文件夹内所有媒体文件"""
    in_path = Path(input_dir)
    if not in_path.is_dir():
        print(f"[ERROR] Not a directory: {input_dir}")
        return False

    files = sorted(in_path.iterdir())
    media_files = [f for f in files
                   if f.is_file() and f.suffix.lower() in SUPPORTED_EXT]

    if not media_files:
        print(f"[ERROR] No supported media files in {input_dir}")
        print(f"  Supported: {', '.join(sorted(SUPPORTED_EXT))}")
        return False

    print(f"[BATCH] Found {len(media_files)} file(s) in {input_dir}\n")

    ok = 0
    for f in media_files:
        name = f.stem  # 无扩展名的文件名
        out_dir = os.path.join(output_base, name)
        print(f"{'='*50}")
        print(f"[{ok + 1}/{len(media_files)}] {f.name}")
        print(f"  Output: {out_dir}")

        if video_to_frames(str(f), out_dir, fps, width, height, pad_color):
            ok += 1
            # 清理 FFmpeg 可能生成的额外帧文件
            for leftover in Path(out_dir).glob("frame_*.png"):
                leftover.unlink()
        else:
            print(f"  [FAILED]")

        print()

    print(f"{'='*50}")
    print(f"[BATCH DONE] {ok}/{len(media_files)} succeeded")
    return ok > 0


def main():
    parser = argparse.ArgumentParser(
        description="Video/GIF to RGB565 frame sequence (240x240 centered)")
    parser.add_argument("input", help="Input file or directory (batch mode)")
    parser.add_argument("-o", "--output", default="./frames", help="Output directory")
    parser.add_argument("-f", "--fps", type=int, default=10,
                        help="Target FPS (default: 10)")
    parser.add_argument("-W", "--width", type=int, default=240,
                        help="Frame width (default: 240)")
    parser.add_argument("-H", "--height", type=int, default=240,
                        help="Frame height (default: 240)")
    parser.add_argument("-p", "--pad-color", default="black",
                        help="Background color for padding (default: black)")
    args = parser.parse_args()

    if not os.path.exists(args.input):
        print(f"[ERROR] Input not found: {args.input}")
        sys.exit(1)

    ffmpeg_path = find_ffmpeg()
    if not ffmpeg_path:
        print("[ERROR] FFmpeg not found. Install from https://ffmpeg.org/download.html")
        sys.exit(1)

    # 把 ffmpeg 所在目录加入 PATH，确保 ffprobe 也能找到
    ffmpeg_dir = os.path.dirname(ffmpeg_path)
    os.environ["PATH"] = ffmpeg_dir + os.pathsep + os.environ.get("PATH", "")
    print(f"[INFO] FFmpeg found: {ffmpeg_path}")

    try:
        from PIL import Image  # noqa: F401
    except ImportError:
        print("[ERROR] Pillow required: pip install Pillow")
        sys.exit(1)

    if os.path.isdir(args.input):
        success = batch_convert(
            args.input, args.output, args.fps,
            args.width, args.height, args.pad_color
        )
    else:
        success = video_to_frames(
            args.input, args.output, args.fps,
            args.width, args.height, args.pad_color
        )

    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
