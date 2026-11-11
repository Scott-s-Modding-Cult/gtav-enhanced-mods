"""Builds a self-contained three.js preview page from CodeWalker-exported ped XML.

Usage:  python ped_preview.py <out.html> <ped_xml_dir> [<ped_xml_dir> ...]
Each <ped_xml_dir> is E:\\GTAV-Modding\\peds\\<ped>\\xml (contains <ped>.ydd.xml and a <ped>\\ folder of .dds).
For each body slot (head, hair, uppr, lowr, feet, accs) the first variant (_000) is used, with texture variant 'a'.
"""
import base64
import io
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image

SLOTS = ["head", "hair", "uppr", "lowr", "feet", "hand", "teef", "accs", "task", "decl", "jbib"]
COMPONENT_SIZES = {"Position": 3, "Normal": 3, "TexCoord0": 2}


def diffuse_names(drawable):
    names = []
    for shader in drawable.findall("ShaderGroup/Shaders/Item"):
        tex = None
        for p in shader.findall("Parameters/Item"):
            if p.get("name") == "DiffuseSampler":
                tex = p.findtext("Name")
        names.append(tex)
    return names


def slot_and_variant(drawable):
    """Identify slot from drawable name, or from its diffuse texture name when the name is a hash."""
    name = (drawable.findtext("Name") or "").lower()
    m = re.match(r"([a-z]{4})_(\d{3})", name)
    if m and m.group(1) in SLOTS:
        return m.group(1), int(m.group(2))
    for tex in diffuse_names(drawable):
        m = re.match(r"([a-z]{4})_diff_(\d{3})", (tex or "").lower())
        if m and m.group(1) in SLOTS:
            return m.group(1), int(m.group(2))
    return None, None


def parse_geometry(geom):
    vb = geom.find("VertexBuffer")
    layout = [c.tag for c in vb.find("Layout")]
    data = vb.find("Data")
    if data is None:
        data = vb.find("Data2")
    pos, nrm, uv = [], [], []
    for line in data.text.strip().splitlines():
        groups = line.strip().split("   ")
        comp = dict(zip(layout, groups))
        p = [float(v) for v in comp["Position"].split()]
        pos += p[:3]
        n = comp.get("Normal")
        nrm += [float(v) for v in n.split()][:3] if n else [0.0, 0.0, 1.0]
        t = comp.get("TexCoord0")
        uv += [float(v) for v in t.split()][:2] if t else [0.0, 0.0]
    idx = [int(v) for v in geom.find("IndexBuffer/Data").text.split()]
    return pos, nrm, uv, idx


def texture_data_uri(tex_dir, name, cache):
    if not name:
        return None, False
    if name in cache:
        return cache[name]
    path = tex_dir / f"{name}.dds"
    if not path.exists():
        cache[name] = (None, False)
        return cache[name]
    img = Image.open(path)
    img.load()
    has_alpha = img.mode in ("RGBA", "LA") and img.getextrema()[-1][0] < 250
    buf = io.BytesIO()
    if has_alpha:
        img.convert("RGBA").save(buf, "PNG", optimize=True)
        mime = "image/png"
    else:
        img.convert("RGB").save(buf, "JPEG", quality=88)
        mime = "image/jpeg"
    uri = f"data:{mime};base64," + base64.b64encode(buf.getvalue()).decode()
    cache[name] = (uri, has_alpha)
    return cache[name]


def build_ped(xml_dir):
    xml_dir = Path(xml_dir)
    ydd = next(xml_dir.glob("*.ydd.xml"))
    ped = ydd.name[:-len(".ydd.xml")]
    root = ET.parse(ydd).getroot()
    tex_dir = xml_dir / ped
    label = xml_dir.parent.name
    chosen = {}
    for d in root.findall("Item"):
        slot, var = slot_and_variant(d)
        if slot and (slot not in chosen or var < chosen[slot][0]):
            chosen[slot] = (var, d)
    tex_cache, parts = {}, []
    for slot in SLOTS:
        if slot not in chosen:
            continue
        var, d = chosen[slot]
        diffuse = diffuse_names(d)
        model = d.find("DrawableModelsHigh/Item")
        for geom in model.findall("Geometries/Item"):
            si = int(geom.find("ShaderIndex").get("value"))
            pos, nrm, uv, idx = parse_geometry(geom)
            uri, alpha = texture_data_uri(tex_dir, diffuse[si] if si < len(diffuse) else None, tex_cache)
            parts.append({"slot": f"{slot}_{var:03d}", "pos": pos, "nrm": nrm, "uv": uv,
                          "idx": idx, "tex": uri, "alpha": alpha})
    print(f"{label}: " + ", ".join(sorted({p['slot'] for p in parts})) +
          f"  ({sum(len(p['pos']) // 3 for p in parts)} verts)")
    return {"name": label, "parts": parts}


PAGE = """<!doctype html>
<html><head><meta charset="utf-8"><title>Ped Preview</title>
<style>
 html,body{margin:0;height:100%;background:#1b1d22;color:#ddd;font:13px system-ui,sans-serif;overflow:hidden}
 #hud{position:absolute;left:12px;top:10px;line-height:1.5;pointer-events:none}
 #labels{position:absolute;left:0;right:0;bottom:14px;display:flex;justify-content:space-around;pointer-events:none;font-size:15px}
</style>
<script type="importmap">{"imports":{"three":"https://cdn.jsdelivr.net/npm/three@0.160.0/build/three.module.js",
"three/addons/":"https://cdn.jsdelivr.net/npm/three@0.160.0/examples/jsm/"}}</script>
</head><body>
<div id="hud">Drag to rotate &middot; scroll to zoom &middot; right-drag to pan</div>
<div id="labels"></div>
<script type="module">
import * as THREE from 'three';
import {OrbitControls} from 'three/addons/controls/OrbitControls.js';
const PEDS = __DATA__;
const renderer = new THREE.WebGLRenderer({antialias:true, preserveDrawingBuffer:true});
renderer.setPixelRatio(devicePixelRatio); renderer.setSize(innerWidth, innerHeight);
renderer.outputColorSpace = THREE.SRGBColorSpace;
document.body.appendChild(renderer.domElement);
const scene = new THREE.Scene(); scene.background = new THREE.Color(0x1b1d22);
const camera = new THREE.PerspectiveCamera(30, innerWidth/innerHeight, 0.01, 100);
camera.up.set(0,0,1);
scene.add(new THREE.HemisphereLight(0xffffff, 0x444450, 1.6));
const key = new THREE.DirectionalLight(0xffffff, 2.2); key.position.set(1.5, 3, 2.5); scene.add(key);
const rim = new THREE.DirectionalLight(0xaaccff, 0.8); rim.position.set(-2, -3, 1.5); scene.add(rim);
const loader = new THREE.TextureLoader();
const box = new THREE.Box3();
const spacing = 1.35;
PEDS.forEach((ped, i) => {
  const g = new THREE.Group();
  for (const p of ped.parts) {
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.Float32BufferAttribute(p.pos, 3));
    geo.setAttribute('normal', new THREE.Float32BufferAttribute(p.nrm, 3));
    geo.setAttribute('uv', new THREE.Float32BufferAttribute(p.uv, 2));
    geo.setIndex(p.idx);
    const mat = new THREE.MeshStandardMaterial({color:0xffffff, roughness:0.75, metalness:0, side:THREE.DoubleSide});
    if (p.tex) { const t = loader.load(p.tex); t.flipY = false; t.colorSpace = THREE.SRGBColorSpace; t.wrapS = t.wrapT = THREE.RepeatWrapping; mat.map = t; }
    if (p.alpha) { mat.alphaTest = 0.45; }
    g.add(new THREE.Mesh(geo, mat));
  }
  g.position.x = (i - (PEDS.length - 1) / 2) * spacing;
  scene.add(g); box.expandByObject(g);
  const l = document.createElement('div'); l.textContent = ped.name; document.getElementById('labels').appendChild(l);
});
const c = box.getCenter(new THREE.Vector3()), s = box.getSize(new THREE.Vector3());
const controls = new OrbitControls(camera, renderer.domElement);
controls.target.copy(c);
const dist = Math.max(s.z, s.x * innerHeight / innerWidth) / (2 * Math.tan(THREE.MathUtils.degToRad(15))) * 1.15;
const view = new URLSearchParams(location.search).get('view') || 'front';
const dirs = {front:[0,-1,0.05], back:[0,1,0.05], side:[-1,0,0.05], three:[-0.6,-0.8,0.1]};
const d = new THREE.Vector3(...(dirs[view] || dirs.front)).normalize().multiplyScalar(dist);
camera.position.copy(c).add(d); controls.update();
addEventListener('resize', () => { camera.aspect = innerWidth/innerHeight; camera.updateProjectionMatrix(); renderer.setSize(innerWidth, innerHeight); });
renderer.setAnimationLoop(() => { controls.update(); renderer.render(scene, camera); });
window.viewer = {THREE, scene, camera, controls};  // for inspection from the console
</script></body></html>
"""


def main():
    out = Path(sys.argv[1])
    peds = [build_ped(d) for d in sys.argv[2:]]
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(PAGE.replace("__DATA__", json.dumps(peds, separators=(",", ":"))), encoding="utf-8")
    print(f"wrote {out} ({out.stat().st_size / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
