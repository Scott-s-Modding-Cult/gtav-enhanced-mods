"""Preview Source/SFM body models in the three.js viewer page used by ped_preview.py.

Usage: python sfm_preview.py <out.html> <pack_root> <ModelName> [<ModelName> ...] [--skin N] [--tex 1024]
  pack_root  folder containing models\\ and materials\\ (the SFM addon root)
"""
import argparse
import base64
import io
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))
import ped_preview  # noqa: E402  (reuses the viewer page)
import source_mdl as sm  # noqa: E402

INCH = 0.0254


def material_texture(root, cddirs, material, size, cache):
    if material in cache:
        return cache[material]
    uri = None
    for d in cddirs:
        vmt = root / "materials" / d.replace("\\", "/") / f"{material}.vmt"
        if vmt.exists():
            base = None
            for line in vmt.read_text(errors="ignore").splitlines():
                parts = line.replace('"', " ").split()
                if len(parts) >= 2 and parts[0].lower() == "$basetexture":
                    base = " ".join(parts[1:])
            if base:
                vtf = root / "materials" / (base.replace("\\", "/") + ".vtf")
                if vtf.exists():
                    img = sm.load_vtf(vtf).convert("RGB").resize((size, size))
                    buf = io.BytesIO()
                    img.save(buf, "JPEG", quality=85)
                    uri = "data:image/jpeg;base64," + base64.b64encode(buf.getvalue()).decode()
            break
    cache[material] = uri
    return uri


def build(root, name, skin, size, cache):
    m = sm.load_model(root / "models" / "Alan13" / "SFM" / "Bases" / f"{name}.mdl")
    parts = []
    for mesh in m["meshes"]:
        mat = mesh["material"]
        if skin > 1 and mat in ("torso", "arms", "legs"):
            mat = f"{mat}{skin}"
        pos = mesh["positions"] * INCH
        uv = mesh["uvs"].copy()
        parts.append({"slot": mesh["material"], "pos": pos.ravel().round(5).tolist(),
                      "nrm": mesh["normals"].ravel().round(4).tolist(), "uv": uv.ravel().round(5).tolist(),
                      "idx": mesh["tris"].ravel().tolist(),
                      "tex": material_texture(root, m["cddirs"], mat, size, cache), "alpha": False})
    print(f"{name}: {sum(len(p['idx']) // 3 for p in parts)} tris, materials {[p['slot'] for p in parts]}")
    return {"name": name, "parts": parts}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out", type=Path)
    ap.add_argument("root", type=Path)
    ap.add_argument("models", nargs="+")
    ap.add_argument("--skin", type=int, default=1)
    ap.add_argument("--tex", type=int, default=1024)
    a = ap.parse_args()
    cache = {}
    peds = [build(a.root, n, a.skin, a.tex, cache) for n in a.models]
    a.out.write_text(ped_preview.PAGE.replace("__DATA__", json.dumps(peds, separators=(",", ":"))), encoding="utf-8")
    print(f"wrote {a.out} ({a.out.stat().st_size / 1e6:.1f} MB)")


if __name__ == "__main__":
    main()
