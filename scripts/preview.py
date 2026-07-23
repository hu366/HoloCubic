"""
精灵帧 PC 预览器

用法：
    python scripts/preview.py [dir]

    1. 把 pet_frames/ 下所有 .bin 转 PNG
    2. 每个动画生成独立预览页（带底部导航）
    3. 打开 idle 预览页
"""
import os, sys
from pathlib import Path

W, H = 128, 128
BG = "#1a1a2e"

# ── 扫描 ──

def scan(dir):
    states = []
    for d in sorted(Path(dir).iterdir()):
        meta = d / "meta.txt"
        if not meta.exists(): continue
        parts = meta.read_text().strip().split()
        w, h = int(parts[0]), int(parts[1])
        if len(parts) == 4:
            mode, total = "angle", int(parts[2]) * int(parts[3])
            rows, cols = int(parts[2]), int(parts[3])
        else:
            mode, total = "seq", int(parts[2])
            rows, cols = 0, 0
        bins = sorted(f.name for f in d.glob("*.bin"))
        states.append(dict(name=d.name, mode=mode, total=total,
                           rows=rows, cols=cols, bins=bins))
        print(f"  {d.name}: {mode} {total}f")
    return states

# ── 转换 ──

def convert_all(root, states):
    from PIL import Image
    for s in states:
        d = Path(root) / s["name"]
        png_dir = d / "png"
        png_dir.mkdir(exist_ok=True)
        for f in s["bins"]:
            dst = png_dir / f.replace(".bin", ".png")
            if dst.exists(): continue
            data = (d / f).read_bytes()
            img = Image.new("RGBA", (W, H))
            px = img.load()
            for y in range(H):
                for x in range(W):
                    o = (y * W + x) * 3
                    lo, hi, a = data[o], data[o+1], data[o+2]
                    rgb565 = lo | (hi << 8)
                    r5 = (rgb565 >> 11) & 0x1F
                    g6 = (rgb565 >> 5) & 0x3F
                    b5 = rgb565 & 0x1F
                    px[x, y] = ((r5<<3)|(r5>>2), (g6<<2)|(g6>>4), (b5<<3)|(b5>>2), a)
            img.save(dst)
        print(f"  {s['name']}: {len(s['bins'])} PNGs")

# ── HTML ──

NAV_LINKS = "\n".join(
    f'<a href="../../{n}/png/preview.html">{n}</a>' for n in ["idle","eating","playing","shocked","walking"]
)

def gen_html(state, root_dir):
    name, mode, total = state["name"], state["mode"], state["total"]
    png_dir = Path(root_dir) / name / "png"
    out = png_dir / "preview.html"

    if mode == "angle":
        rows, cols = state["rows"], state["cols"]
        html = f"""<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>{name}</title>
<style>
body{{margin:0;background:{BG};display:flex;justify-content:center;align-items:center;height:100vh;flex-direction:column;font-family:monospace;user-select:none}}
canvas{{border-radius:12px;cursor:grab;image-rendering:pixelated}}
canvas:active{{cursor:grabbing}}
#i{{color:#aaa;margin-top:10px;font-size:14px}}
.h{{color:#666;margin-bottom:14px;font-size:13px}}
.n{{color:#666;margin-top:18px;font-size:12px}}
.n a{{color:#88a;margin:0 5px;text-decoration:none}}
.n a:hover{{color:#aaf}}
</style></head><body>
<div class="h">🖱 拖拽旋转视角  |  滚轮缩放</div>
<canvas id="c" width="256" height="256"></canvas>
<div id="i">pitch:0° roll:0° frame:055</div>
<div class="n">{NAV_LINKS}</div>
<script>
const G={rows},C={cols},M=45,S=128,f=[];
for(let i=0;i<{total};i++){{const im=new Image();im.src=String(i).padStart(3,'0')+'.png';f.push(im)}}
let sc=2,p=0,r=0,dr=false,mx=0,my=0;
function idx(pt,rl){{
 pt=Math.max(-M,Math.min(M,pt));rl=Math.max(-M,Math.min(M,rl));
 const rn=(M-pt)/(2*M),cn=(rl+M)/(2*M);
 return Math.round(rn*(G-1))*C+Math.round(cn*(C-1));
}}
function draw(){{
 const k=idx(p,r),w=S*sc,h=S*sc;
 c.width=w;c.height=h;ctx.imageSmoothingEnabled=false;
 ctx.fillStyle='{BG}';ctx.fillRect(0,0,w,h);
 if(f[k].complete)ctx.drawImage(f[k],0,0,w,h);
 i.textContent=`pitch:${{p.toFixed(0)}}° roll:${{r.toFixed(0)}}° frame:${{String(k).padStart(3,'0')}}`;
}}
const c=document.getElementById('c'),ctx=c.getContext('2d'),i=document.getElementById('i');
c.onmousedown=e=>{{dr=true;mx=e.clientX;my=e.clientY}};
window.onmouseup=()=>dr=false;
window.onmousemove=e=>{{if(!dr)return;r+=(e.clientX-mx)*0.5;p-=(e.clientY-my)*0.5;mx=e.clientX;my=e.clientY;r=Math.max(-M,Math.min(M,r));p=Math.max(-M,Math.min(M,p));draw()}};
c.onwheel=e=>{{e.preventDefault();sc=Math.max(1,Math.min(6,sc-e.deltaY*0.01));draw()}};
draw();
</script></body></html>"""
    else:
        html = f"""<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>{name}</title>
<style>
body{{margin:0;background:{BG};display:flex;justify-content:center;align-items:center;height:100vh;flex-direction:column;font-family:monospace}}
canvas{{border-radius:12px;image-rendering:pixelated}}
#i{{color:#aaa;margin-top:10px;font-size:14px}}
.btns{{margin-top:14px}}
.btns button{{margin:0 6px;padding:8px 18px;font-size:14px;cursor:pointer;background:#333;color:#fff;border:1px solid #555;border-radius:6px}}
.btns button:hover{{background:#444}}
input[type=range]{{width:180px;vertical-align:middle;margin:0 10px}}
.h{{color:#666;margin-bottom:14px;font-size:13px}}
.n{{color:#666;margin-top:18px;font-size:12px}}
.n a{{color:#88a;margin:0 5px;text-decoration:none}}
.n a:hover{{color:#aaf}}
</style></head><body>
<div class="h">空格播放  |  ← → 逐帧  |  滚轮缩放</div>
<canvas id="c" width="256" height="256"></canvas>
<div id="i">frame:0/{total-1}</div>
<div class="btns">
<button onclick="prev()">◀</button>
<button id="pb" onclick="tog()">▶</button>
<button onclick="next()">▶</button>
</div>
<div style="margin-top:10px"><input type="range" id="s" min="0" max="{total-1}" value="0" oninput="jmp(this.value)"></div>
<div class="n">{NAV_LINKS}</div>
<script>
const T={total},S=128,f=[],c=document.getElementById('c'),ctx=c.getContext('2d'),i=document.getElementById('i'),sl=document.getElementById('s'),pb=document.getElementById('pb');
let cur=0,pl=false,tm=null;
for(let k=0;k<T;k++){{const im=new Image();im.src=String(k).padStart(3,'0')+'.png';f.push(im)}}
function draw(){{
 ctx.fillStyle='{BG}';ctx.fillRect(0,0,256,256);ctx.imageSmoothingEnabled=false;
 if(f[cur]&&f[cur].complete)ctx.drawImage(f[cur],64,64,S,S);
 i.textContent=`frame:${{cur}}/${{T-1}}`;sl.value=cur;
}}
function nxt(){{cur=(cur+1)%T;draw()}}
function prev(){{cur=(cur-1+T)%T;draw()}}
function jmp(v){{cur=+v;draw()}}
function tog(){{pl=!pl;pb.textContent=pl?'⏸':'▶';if(pl)tm=setInterval(nxt,100);else clearInterval(tm)}}
document.onkeydown=e=>{{if(e.key==='ArrowRight')nxt();if(e.key==='ArrowLeft')prev();if(e.key===' '&&cur!=null){{e.preventDefault();tog()}}}};
c.onwheel=e=>{{e.preventDefault();const s2=document.getElementById('c');s2.width=s2.height=256+Math.max(-128,Math.min(384,-e.deltaY));draw()}};
draw();
</script></body></html>"""

    out.write_text(html, encoding="utf-8")
    return out

# ── main ──

def main():
    root = Path(sys.argv[1] if len(sys.argv) > 1 else "pet_frames")
    print(f"Scanning: {root.resolve()}")
    states = scan(root)

    print("\nConverting .bin -> .png ...")
    convert_all(root, states)

    print("\nGenerating preview pages ...")
    for s in states:
        p = gen_html(s, root)
        print(f"  {s['name']}: {p}")

    # 打开 idle
    idle_path = root / "idle" / "png" / "preview.html"
    if idle_path.exists():
        os.startfile(str(idle_path.resolve()))
        print(f"\nOpened: {idle_path}")
    else:
        print(f"\nPreview pages ready in {root}/<state>/png/preview.html")

if __name__ == "__main__":
    main()
