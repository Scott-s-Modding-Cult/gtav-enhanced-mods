"""Applies the nude freemode-female body (MARK2580's "Nude Freemode female", Enhanced build) to an ambient female ped.

Usage:
  python build_nude_ped.py <ped_xml_dir> <ped_yft_xml> <mod_xml_dir> <freemode_yft_xml> <out_xml_dir>

  ped_xml_dir       CodeWalker export of the ambient ped (…\\peds\\a_f_y_beach_01\\xml)
  mod_xml_dir       CodeWalker export of the mod (uppr_015_r.ydd.xml, lowr_015_r.ydd.xml, mp_fm_skin_f_*\\*.dds)
  freemode_yft_xml  mp_f_freemode_01.yft.xml (skeleton the mod meshes are weighted to)
  out_xml_dir       where the modified ped XML + textures are written (convert back with xml-import.ps1)

What it does
  * Replaces the ped's uppr/lowr drawables (all LODs) with the mod's body meshes.
  * Remaps bone weights from the 128-bone freemode skeleton to the ped's skeleton by bone name
    (bones the ped lacks fall back to their nearest shared parent).
  * Removes the hands from the mod's upper body (ambient peds carry their hands in the head mesh).
  * Keeps the ped's own feet: cut from its lowr mesh and merged into the new lowr, with their texture
    packed into an unused band of the lowr texture so every skin-tone variant gets matching feet.
  * Body diffuse textures = the mod's skin textures (whi/bla); normal/spec maps = the mod's.
"""
import shutil
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np
from PIL import Image

LODS = ["DrawableModelsHigh", "DrawableModelsMedium", "DrawableModelsLow"]
HAND_KEYS = ("Hand", "Finger")
FOOT_KEYS = ("Foot", "Toe")
SKIN_TONES = ("whi", "bla")  # mod ships skin textures per tone; other tones fall back to whi


# ---------------------------------------------------------------- skeletons
def load_skeleton(yft_xml):
    root = ET.parse(yft_xml).getroot()
    bones = []
    for b in root.find("Drawable/Skeleton").findall("Bones/Item"):
        bones.append((b.findtext("Name"), int(b.find("ParentIndex").get("value"))))
    return bones


def quat_to_mat(x, y, z, w):
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def bind_pose(yft_xml):
    """World-space rest-pose matrix of every bone."""
    root = ET.parse(yft_xml).getroot()
    world = []
    for b in root.find("Drawable/Skeleton").findall("Bones/Item"):
        t, q, s = b.find("Translation"), b.find("Rotation"), b.find("Scale")
        m = np.eye(4)
        m[:3, :3] = quat_to_mat(*(float(q.get(k)) for k in "xyzw")) * np.array([float(s.get(k)) for k in "xyz"])
        m[:3, 3] = [float(t.get(k)) for k in "xyz"]
        p = int(b.find("ParentIndex").get("value"))
        world.append(world[p] @ m if p >= 0 else m)
    return world


def retarget_matrices(src_sk, src_world, dst_sk, dst_world, remap):
    """Per source bone: matrix moving a vertex from the source rest pose to the destination rest pose."""
    src_idx = {n: i for i, (n, _) in enumerate(src_sk)}
    out = []
    for i in range(len(src_sk)):
        k = remap[i]                       # destination bone this source bone maps to
        j = src_idx[dst_sk[k][0]]          # same-named bone in the source skeleton
        out.append(dst_world[k] @ np.linalg.inv(src_world[j]))
    return out


def retarget_vertex(v, bone_ids, deltas):
    m = np.zeros((4, 4))
    tot = 0
    for w, i in zip(v["BlendWeights"], v["BlendIndices"]):
        w = int(w)
        if w:
            src = bone_ids[int(i)] if bone_ids else int(i)
            m += w * deltas[src]
            tot += w
    if not tot:
        return
    m /= tot
    p = m @ np.array([float(x) for x in v["Position"]] + [1.0])
    v["Position"] = [fmt(x) for x in p[:3]]
    for comp in ("Normal", "Tangent"):
        if comp in v:
            vals = [float(x) for x in v[comp]]
            d = m[:3, :3] @ np.array(vals[:3])
            n = np.linalg.norm(d)
            d = d / n if n else d
            v[comp] = [fmt(x) for x in d] + [fmt(x) for x in vals[3:]]


def bone_remap(src, dst):
    """src index -> dst index, by name, walking up src parents when dst lacks a bone."""
    dst_idx = {n: i for i, (n, _) in enumerate(dst)}
    remap = []
    for i in range(len(src)):
        j = i
        while j >= 0 and src[j][0] not in dst_idx:
            j = src[j][1]
        remap.append(dst_idx[src[j][0]] if j >= 0 else 0)
    return remap


# ---------------------------------------------------------------- geometry
class Geom:
    def __init__(self, layout, verts, idx):
        self.layout = layout      # list of component names
        self.verts = verts        # list of dict component -> list of str values
        self.idx = idx            # list of int

    @staticmethod
    def parse(g):
        layout = [c.tag for c in g.find("VertexBuffer/Layout")]
        data = g.find("VertexBuffer/Data")
        if data is None:
            data = g.find("VertexBuffer/Data2")   # Legacy exports use Data2
        verts = []
        for line in data.text.strip().splitlines():
            verts.append({k: v.split() for k, v in zip(layout, line.strip().split("   "))})
        idx = [int(v) for v in g.find("IndexBuffer/Data").text.split()]
        return Geom(layout, verts, idx)

    def bone_ids(self, g):
        txt = (g.findtext("BoneIDs") or "").replace(",", " ").split()
        return [int(x) for x in txt]

    def weights(self, v, bone_ids):
        out = {}
        for w, i in zip(v["BlendWeights"], v["BlendIndices"]):
            w = int(w)
            if w:
                b = bone_ids[int(i)] if bone_ids else int(i)
                out[b] = out.get(b, 0) + w
        return out

    def keep_triangles(self, keep_vertex):
        """Keep triangles that have at least one vertex where keep_vertex is True; compact."""
        tris = [self.idx[i:i + 3] for i in range(0, len(self.idx), 3)]
        tris = [t for t in tris if any(keep_vertex[k] for k in t)]
        used = sorted({k for t in tris for k in t})
        new = {old: n for n, old in enumerate(used)}
        self.verts = [self.verts[k] for k in used]
        self.idx = [new[k] for t in tris for k in t]

    def select_triangles(self, want_vertex):
        """Keep only triangles whose vertices are all wanted; compact."""
        tris = [self.idx[i:i + 3] for i in range(0, len(self.idx), 3)]
        tris = [t for t in tris if all(want_vertex[k] for k in t)]
        used = sorted({k for t in tris for k in t})
        new = {old: n for n, old in enumerate(used)}
        self.verts = [self.verts[k] for k in used]
        self.idx = [new[k] for t in tris for k in t]


def wrap01(v):
    return v + 1.0 if v < 0 else v


def fmt(x):
    s = f"{x:.7g}"
    return "0" if s == "-0" else s


def share(weights, names, keys):
    tot = sum(weights.values()) or 1
    return sum(w for b, w in weights.items() if any(k in names[b] for k in keys)) / tot


def remap_weights(v, bone_ids, remap):
    acc = {}
    for w, i in zip(v["BlendWeights"], v["BlendIndices"]):
        w = int(w)
        if not w:
            continue
        src = bone_ids[int(i)] if bone_ids else int(i)
        dst = remap[src]
        acc[dst] = acc.get(dst, 0) + w
    items = sorted(acc.items(), key=lambda x: -x[1])[:4]
    total = sum(w for _, w in items) or 1
    ws = [round(w * 255 / total) for _, w in items]
    ws[0] += 255 - sum(ws)  # keep the byte weights summing to 255
    ids = [b for b, _ in items]
    while len(ws) < 4:
        ws.append(0)
        ids.append(0)
    v["BlendWeights"] = [str(x) for x in ws]
    v["BlendIndices"] = [str(x) for x in ids]


def to_layout(v, layout):
    defaults = {"TexCoord1": ["0", "0"], "Tangent": ["1", "0", "0", "1"],
                "Colour0": ["255", "255", "255", "255"], "Colour1": ["0", "0", "0", "0"]}
    return {c: v.get(c, defaults.get(c, ["0"])) for c in layout}


def write_geom(g_el, geom, bone_count):
    vb = g_el.find("VertexBuffer")
    lay = vb.find("Layout")
    for c in list(lay):
        lay.remove(c)
    for c in geom.layout:
        ET.SubElement(lay, c)
    vb.find("Data").text = "\n" + "\n".join(
        "   ".join(" ".join(v[c]) for c in geom.layout) for v in geom.verts) + "\n"
    ib = g_el.find("IndexBuffer/Data")
    ib.text = "\n" + "\n".join(" ".join(str(i) for i in geom.idx[k:k + 24])
                               for k in range(0, len(geom.idx), 24)) + "\n"
    g_el.find("BoneIDs").text = ", ".join(str(i) for i in range(bone_count))
    pos = np.array([[float(x) for x in v["Position"]] for v in geom.verts])
    lo, hi = pos.min(0), pos.max(0)
    for tag, val in (("BoundingBoxMin", lo), ("BoundingBoxMax", hi)):
        el = g_el.find(tag)
        el.set("x", fmt(val[0])); el.set("y", fmt(val[1])); el.set("z", fmt(val[2])); el.set("w", fmt(val[0]))
    return pos


def set_bounds(drawable, positions):
    pos = np.vstack(positions)
    lo, hi = pos.min(0), pos.max(0)
    c = (lo + hi) / 2
    r = float(np.linalg.norm(pos - c, axis=1).max())
    for tag, val in (("BoundingBoxMin", lo), ("BoundingBoxMax", hi), ("BoundingSphereCenter", c)):
        el = drawable.find(tag)
        el.set("x", fmt(val[0])); el.set("y", fmt(val[1])); el.set("z", fmt(val[2]))
    drawable.find("BoundingSphereRadius").set("value", fmt(r))


# ---------------------------------------------------------------- textures
def save_dds_rgba(img, path):
    """Uncompressed A8R8G8B8 DDS with a full mip chain."""
    img = img.convert("RGBA")
    w, h = img.size
    levels = []
    while True:
        levels.append(img.resize((w, h), Image.LANCZOS) if levels else img)
        if w == 1 and h == 1:
            break
        w, h = max(1, w // 2), max(1, h // 2)
    W, H = img.size
    hdr = struct.pack("<4s7I44x", b"DDS ", 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x8 | 0x20000, H, W, W * 4, 0, len(levels))
    pf = struct.pack("<2I4s5I", 32, 0x41, b"\0\0\0\0", 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    caps = struct.pack("<5I", 0x1000 | 0x8 | 0x400000, 0, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(hdr + pf + caps)
        for lv in levels:
            a = np.asarray(lv, dtype=np.uint8)
            f.write(a[..., [2, 1, 0, 3]].tobytes())  # BGRA


def srgb_to_lin(a):
    a = a / 255.0
    return np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)


def lin_to_srgb(a):
    a = np.clip(a, 0, 1)
    return np.where(a <= 0.0031308, a * 12.92, 1.055 * a ** (1 / 2.4) - 0.055) * 255.0


def skin_mean(img):
    """Mean linear RGB of skin-like pixels (ignores near-black padding)."""
    a = np.asarray(img.convert("RGB"), dtype=np.float64)
    lum = a.mean(axis=2)
    return srgb_to_lin(a[lum > 40]).mean(axis=0)


def match_tone(img, target_lin):
    """Scale the skin texture per channel (in linear light) so its mean matches target_lin."""
    rgba = np.asarray(img.convert("RGBA"), dtype=np.float64)
    gain = target_lin / skin_mean(img)
    rgb = lin_to_srgb(srgb_to_lin(rgba[..., :3]) * gain)
    out = np.dstack([rgb, rgba[..., 3]]).round().astype(np.uint8)
    return Image.fromarray(out, "RGBA")


def empty_drawable(d):
    """Make a drawable render nothing: every geometry becomes one degenerate triangle."""
    for lod in LODS:
        for g in d.findall(f"{lod}/Item/Geometries/Item"):
            geom = Geom.parse(g)
            geom.verts = geom.verts[:1]
            geom.idx = [0, 0, 0]
            vb = g.find("VertexBuffer")
            vb.find("Data").text = "\n" + "   ".join(" ".join(geom.verts[0][c]) for c in geom.layout) + "\n"
            g.find("IndexBuffer/Data").text = "\n0 0 0\n"


def find_drawable(root, slot):
    for d in root.findall("Item"):
        name = d.findtext("Name").lower()
        if name.startswith(slot + "_"):
            return d
        for p in d.iter("Item"):
            if p.get("name") == "DiffuseSampler" and (p.findtext("Name") or "").lower().startswith(slot + "_diff_000"):
                return d
    raise SystemExit(f"no {slot} drawable found")


def parse_model(model_el):
    """All geometries of one LOD model merged into one Geom (they share the body shader).
    Returns (geom, bone_ids)."""
    merged, bone_ids = None, None
    for g_el in model_el.findall("Geometries/Item"):
        g = Geom.parse(g_el)
        ids = g.bone_ids(g_el)
        # High-LOD pieces stored entirely in v -1..0 sample the same texels as 0..1 under wrap; shift them.
        vs = [float(v["TexCoord0"][1]) for v in g.verts]
        if max(vs) <= 0.0:
            for v in g.verts:
                v["TexCoord0"] = [v["TexCoord0"][0], fmt(float(v["TexCoord0"][1]) + 1.0)]
        if merged is None:
            merged, bone_ids = g, ids
        else:
            if ids != bone_ids:
                raise SystemExit("geometries in one model use different BoneIDs lists; not supported")
            base = len(merged.verts)
            merged.verts += [to_layout(v, merged.layout) for v in g.verts]
            merged.idx += [i + base for i in g.idx]
    return merged, bone_ids


def uv_coverage(geom, size=128, pad=2):
    """Boolean grid of texels (wrapped into 0..1) covered by the geometry's triangles, dilated by pad."""
    occ = np.zeros((size, size), bool)
    uv = np.array([[float(x) for x in v["TexCoord0"]] for v in geom.verts]) * size
    for t in range(0, len(geom.idx), 3):
        a, b, c = uv[geom.idx[t]], uv[geom.idx[t + 1]], uv[geom.idx[t + 2]]
        lo, hi = np.floor(np.minimum(np.minimum(a, b), c)).astype(int), np.ceil(np.maximum(np.maximum(a, b), c)).astype(int)
        xs, ys = np.meshgrid(np.arange(lo[0], hi[0] + 1) + 0.5, np.arange(lo[1], hi[1] + 1) + 0.5)
        p = np.stack([xs.ravel(), ys.ravel()], 1)
        d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(d) < 1e-12:
            continue
        l1 = ((b[1] - c[1]) * (p[:, 0] - c[0]) + (c[0] - b[0]) * (p[:, 1] - c[1])) / d
        l2 = ((c[1] - a[1]) * (p[:, 0] - c[0]) + (a[0] - c[0]) * (p[:, 1] - c[1])) / d
        inside = (l1 >= -0.05) & (l2 >= -0.05) & (1 - l1 - l2 >= -0.05)
        q = np.floor(p[inside]).astype(int) % size
        occ[q[:, 1], q[:, 0]] = True
    for _ in range(pad):
        occ = occ | np.roll(occ, 1, 0) | np.roll(occ, -1, 0) | np.roll(occ, 1, 1) | np.roll(occ, -1, 1)
    return occ


TORSO_KEYS = ("ROOT", "Pelvis", "Spine", "Thigh")   # bones whose retarget offsets get smoothed


def smooth_retarget(geoms, originals, free_flags, iters):
    """Smooth the retarget displacement (new - original position) over the joined meshes so skeleton
    proportion differences spread over the torso instead of creasing at joint boundaries.
    Vertices that share an original position are welded (UV seams, the uppr/lowr waist seam).
    Vertices with free_flags False keep their exact retarget (neck, arms, lower legs)."""
    orig = np.vstack(originals)
    new = np.vstack([[[float(x) for x in v["Position"]] for v in g.verts] for g in geoms])
    free = np.concatenate(free_flags)
    keys = {}
    rep = np.array([keys.setdefault(tuple(np.round(p, 4)), len(keys)) for p in orig])
    R = len(keys)
    disp = np.zeros((R, 3)); cnt = np.bincount(rep, minlength=R)[:, None]
    np.add.at(disp, rep, new - orig); disp /= cnt
    rep_free = np.ones(R, bool); np.logical_and.at(rep_free, rep, free)
    edges, base = [], 0
    for g in geoms:
        t = np.array(g.idx).reshape(-1, 3) + base
        edges += [t[:, [0, 1]], t[:, [1, 2]], t[:, [2, 0]]]
        base += len(g.verts)
    e = rep[np.vstack(edges)]
    e = e[e[:, 0] != e[:, 1]]
    e = np.unique(np.vstack([e, e[:, ::-1]]), axis=0)
    deg = np.bincount(e[:, 0], minlength=R)[:, None]
    movable = rep_free & (deg[:, 0] > 0)
    for _ in range(iters):
        acc = np.zeros_like(disp); np.add.at(acc, e[:, 0], disp[e[:, 1]])
        avg = acc / np.maximum(deg, 1)
        disp[movable] = 0.5 * disp[movable] + 0.5 * avg[movable]
    final = orig + disp[rep]

    # Recompute normals of the smoothed (free) region from the new surface, welded across seams,
    # so shading follows the smoothed shape instead of the rigidly rotated per-bone normals.
    fn_acc = np.zeros((R, 3)); base = 0
    for g in geoms:
        t = np.array(g.idx).reshape(-1, 3) + base
        fnrm = np.cross(final[t[:, 1]] - final[t[:, 0]], final[t[:, 2]] - final[t[:, 0]])  # area-weighted
        for c in range(3):
            np.add.at(fn_acc, rep[t[:, c]], fnrm)
        base += len(g.verts)
    old_n = np.vstack([[[float(x) for x in v["Normal"][:3]] for v in g.verts] for g in geoms])
    vn = fn_acc[rep]
    if np.sum(np.einsum("ij,ij->i", vn, old_n)) < 0:   # match the mesh's winding convention
        vn = -vn
    vn /= np.maximum(np.linalg.norm(vn, axis=1, keepdims=True), 1e-9)

    k = 0
    for g in geoms:
        for v in g.verts:
            v["Position"] = [fmt(x) for x in final[k]]
            if free[k]:
                v["Normal"] = [fmt(x) for x in vn[k]] + v["Normal"][3:]
            k += 1
    return np.linalg.norm(final - new, axis=1).max()


def edge_pad(img, mask, iters=24):
    """Fill texels outside `mask` with the average of their already-filled neighbours, growing outward,
    so texture filtering at UV-island borders never samples unrelated colours (seams/halos)."""
    a = np.asarray(img.convert("RGBA"), dtype=np.float32).copy()
    m = mask.copy()
    dirs = [(-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1)]
    for _ in range(iters):
        acc = np.zeros_like(a)
        cnt = np.zeros(m.shape, np.float32)
        am = a * m[..., None]
        for dy, dx in dirs:
            acc += np.roll(am, (dy, dx), (0, 1))
            cnt += np.roll(m, (dy, dx), (0, 1))
        grow = ~m & (cnt > 0)
        if not grow.any():
            break
        a[grow] = acc[grow] / cnt[grow, None]
        m |= grow
    return Image.fromarray(a.round().clip(0, 255).astype(np.uint8), "RGBA")


def best_free_rect(occ, want_w, want_h):
    """Largest empty rectangle (in 0..1 UV units) for content of aspect want_w:want_h.
    Returns (u0, v0, w, h, scale) where scale = how much the content is scaled to fit."""
    n = occ.shape[0]
    heights = np.zeros(n, int)
    best = (0, 0, 0, 0, 0.0)
    for row in range(n):
        heights = np.where(occ[row], 0, heights + 1)
        for j in range(n):
            if not heights[j]:
                continue
            h = heights[j]
            for k in range(j, n):
                h = min(h, heights[k])
                if not h:
                    break
                w = k - j + 1
                s = min(w / n / want_w, h / n / want_h)
                if s > best[4]:
                    best = (j / n, (row - h + 1) / n, w / n, h / n, s)
    return best


def main():
    import argparse
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ped_xml", type=Path)
    ap.add_argument("ped_yft", type=Path)
    ap.add_argument("mod_xml", type=Path)
    ap.add_argument("free_yft", type=Path)
    ap.add_argument("out_xml", type=Path)
    ap.add_argument("--uppr", default="uppr_015_r", help="mod upper-body drawable (file stem in mod_xml)")
    ap.add_argument("--lowr", default="lowr_015_r", help="mod lower-body drawable (file stem in mod_xml)")
    ap.add_argument("--keep-accs", action="store_true", help="keep the ped's accessory drawables")
    ap.add_argument("--no-retarget", action="store_true", help="skip rest-pose retargeting (debug/compare)")
    ap.add_argument("--smooth", type=int, default=150,
                    help="smoothing passes for the torso/hip retarget offsets (0 = rigid per-bone retarget)")
    args = ap.parse_args()
    ped_xml, ped_yft, mod_xml, free_yft, out_xml = args.ped_xml, args.ped_yft, args.mod_xml, args.free_yft, args.out_xml
    ped = ped_xml.parent.name
    if out_xml.exists():
        shutil.rmtree(out_xml)
    shutil.copytree(ped_xml, out_xml)

    ped_sk, free_sk = load_skeleton(ped_yft), load_skeleton(free_yft)
    remap = bone_remap(free_sk, ped_sk)
    ped_names, free_names = [n for n, _ in ped_sk], [n for n, _ in free_sk]
    deltas = retarget_matrices(free_sk, bind_pose(free_yft), ped_sk, bind_pose(ped_yft), remap)
    print(f"skeleton: freemode {len(free_sk)} bones -> {ped} {len(ped_sk)} bones "
          f"({sum(1 for i, j in enumerate(remap) if free_names[i] != ped_names[j])} folded into parents)")

    tree = ET.parse(ped_xml / f"{ped}.ydd.xml")
    root = tree.getroot()
    ped_uppr, ped_lowr = find_drawable(root, "uppr"), find_drawable(root, "lowr")
    mod_uppr = ET.parse(mod_xml / f"{args.uppr}.ydd.xml").getroot().find("Item")
    mod_lowr = ET.parse(mod_xml / f"{args.lowr}.ydd.xml").getroot().find("Item")

    # Where can the ped's feet texture go? Map which texels the mod's high-LOD lowr actually uses.
    lowr_occ = uv_coverage(parse_model(mod_lowr.find("DrawableModelsHigh/Item"))[0])
    print(f"mod lowr texture usage: {lowr_occ.mean():.0%} of texels")

    feet_box = None  # (u0, v0, u1, v1) of feet in the ped's own lowr texture, from high LOD
    feet_xform = None

    # Pre-pass per LOD: cut hands, retarget both body parts, then smooth the retarget jointly.
    prepared, notes = {}, {}
    for lod in LODS:
        parts = {}
        for slot, mod_d in (("uppr", mod_uppr), ("lowr", mod_lowr)):
            mod_model = mod_d.find(f"{lod}/Item")
            if mod_model is None:
                continue
            mg, mod_ids = parse_model(mod_model)
            if slot == "uppr":
                # Drop only triangles lying entirely on the hand, so the forearm slightly overlaps the
                # ped's own hands (in its head mesh) instead of leaving a gap at the wrist.
                keep = [share(mg.weights(v, mod_ids), free_names, HAND_KEYS) < 0.5 for v in mg.verts]
                before = len(mg.verts)
                mg.keep_triangles(keep)
                notes[(slot, lod)] = f"hands removed ({before} -> {len(mg.verts)} verts)"
            orig = np.array([[float(x) for x in v["Position"]] for v in mg.verts])
            free = np.array([share(mg.weights(v, mod_ids), free_names, TORSO_KEYS) > 0.95 for v in mg.verts])
            if not args.no_retarget:
                for v in mg.verts:
                    retarget_vertex(v, mod_ids, deltas)   # freemode rest pose -> ped rest pose
            parts[slot] = (mg, mod_ids, orig, free)
        if not args.no_retarget and args.smooth > 0 and len(parts) == 2:
            moved = smooth_retarget([p[0] for p in parts.values()], [p[2] for p in parts.values()],
                                    [p[3] for p in parts.values()], args.smooth)
            print(f"  {lod[14:]}: retarget smoothed over torso/hips ({args.smooth} passes, max shift {moved * 1000:.0f} mm)")
        for slot, (mg, mod_ids, _, _) in parts.items():
            prepared[(slot, lod)] = (mg, mod_ids)

    final_geoms = {"uppr": [], "lowr": []}   # per slot, every LOD as written (for texture edge padding)
    for slot, ped_d, mod_d in (("uppr", ped_uppr, mod_uppr), ("lowr", ped_lowr, mod_lowr)):
        positions = []
        for lod in LODS:
            ped_model = ped_d.find(f"{lod}/Item")
            if ped_model is None or (slot, lod) not in prepared:
                continue
            mg, mod_ids = prepared[(slot, lod)]
            note = notes.get((slot, lod), "")
            for v in mg.verts:
                remap_weights(v, mod_ids, remap)

            if slot == "lowr":
                ped_g_el = ped_model.find("Geometries/Item")
                fg = Geom.parse(ped_g_el)
                ped_ids = fg.bone_ids(ped_g_el)
                want = [share(fg.weights(v, ped_ids), ped_names, FOOT_KEYS) > 0.5 for v in fg.verts]
                fg.select_triangles(want)
                uv = np.array([[float(x) for x in v["TexCoord0"]] for v in fg.verts])
                if feet_box is None:
                    pad = 2 / 512
                    feet_box = (max(uv[:, 0].min() - pad, 0), max(uv[:, 1].min() - pad, 0),
                                min(uv[:, 0].max() + pad, 1), min(uv[:, 1].max() + pad, 1))
                    bw, bh = feet_box[2] - feet_box[0], feet_box[3] - feet_box[1]
                    ru, rv, rw, rh, s = best_free_rect(lowr_occ, bw, bh)
                    if s <= 0:
                        raise SystemExit("no free space in the mod's lowr texture for the feet")
                    feet_xform = (s, ru - feet_box[0] * s, rv - feet_box[1] * s)
                    print(f"feet: uv box {tuple(round(float(x), 3) for x in feet_box)} -> free area "
                          f"u={ru:.2f}..{ru + bw * s:.2f} v={rv:.2f}..{rv + bh * s:.2f} (scale x{s:.2f})")
                s, du, dv = feet_xform
                base = len(mg.verts)
                for v in fg.verts:
                    u, vv = float(v["TexCoord0"][0]), float(v["TexCoord0"][1])
                    v["TexCoord0"] = [fmt(u * s + du), fmt(vv * s + dv)]
                    mg.verts.append(to_layout(v, mg.layout))
                mg.idx += [i + base for i in fg.idx]
                note = f"{len(fg.verts)} foot verts merged"

            # Write into the ped's own model (keeps its shader indices / flags)
            g_el = ped_model.find("Geometries/Item")
            for extra in ped_model.findall("Geometries/Item")[1:]:
                ped_model.find("Geometries").remove(extra)
            g_el.find("ShaderIndex").set("value", ped_model.find("Geometries/Item/ShaderIndex").get("value"))
            positions.append(write_geom(g_el, mg, len(ped_sk)))
            final_geoms[slot].append(mg)
            ped_model.find("Unknown1").set("value", str(len(ped_sk)))
            print(f"  {slot} {lod[14:]}: {len(mg.verts)} verts, {len(mg.idx) // 3} tris; {note}")
        set_bounds(ped_d, positions)

    if not args.keep_accs:
        for d in root.findall("Item"):
            name = d.findtext("Name").lower()
            diff = [p.findtext("Name") or "" for p in d.iter("Item") if p.get("name") == "DiffuseSampler"]
            if name.startswith("accs_") or any(x.lower().startswith("accs_diff") for x in diff):
                empty_drawable(d)
                print(f"  {d.findtext('Name')}: accessory hidden (use --keep-accs to keep it)")

    tree.write(out_xml / f"{ped}.ydd.xml", encoding="UTF-8", xml_declaration=True)

    # ------------------------------------------------------------ textures
    tex_in, tex_out = ped_xml / ped, out_xml / ped
    skin = {}
    for part in ("up", "lo"):
        for tone in SKIN_TONES:
            p = mod_xml / f"mp_fm_skin_f_{part}_{tone}" / f"mp_fm_skin_f_{part}_{tone}.dds"
            skin[(part, tone)] = Image.open(p).convert("RGBA")
    # Skin tone target per variant letter: the ped's own bare feet in its lowr texture.
    def feet_crop(path):
        orig = Image.open(path).convert("RGBA")
        W, H = orig.size
        return orig.crop((round(feet_box[0] * W), round(feet_box[1] * H),
                          round(feet_box[2] * W), round(feet_box[3] * H)))
    tone_target = {}
    for dds in sorted(tex_in.glob("lowr_diff_000_*.dds")):
        letter, tone = dds.stem.split("_")[3], dds.stem.split("_")[4]
        tone_target[letter] = skin_mean(feet_crop(dds))
        tone_target.setdefault(tone, tone_target[letter])

    coverage = {}
    def used_texels(slot, size):
        if (slot, size) not in coverage:
            m = np.zeros((size, size), bool)
            for g in final_geoms[slot]:
                m |= uv_coverage(g, size=size, pad=1)
            coverage[(slot, size)] = m
            print(f"  {slot}: {m.mean():.0%} of its {size}px texture is used; padding the rest")
        return coverage[(slot, size)]

    for dds in sorted(tex_in.glob("*_diff_000_*.dds")):
        name = dds.stem                      # e.g. lowr_diff_000_b_whi
        slot = name[:4]
        if slot not in ("uppr", "lowr"):
            continue
        letter, tone = name.split("_")[3], name.rsplit("_", 1)[-1]
        target = tone_target.get(letter, tone_target.get(tone))
        tone = tone if tone in SKIN_TONES else "whi"
        img = skin[("up" if slot == "uppr" else "lo", tone)].copy()
        if target is not None:
            img = match_tone(img, target)
        if slot == "lowr":
            crop = feet_crop(dds)
            s, du, dv = feet_xform
            tw, th = img.size
            dst = (round(du * tw + feet_box[0] * s * tw), round(dv * th + feet_box[1] * s * th))
            crop = crop.resize((max(1, round((feet_box[2] - feet_box[0]) * s * tw)),
                                max(1, round((feet_box[3] - feet_box[1]) * s * th))), Image.LANCZOS)
            img.paste(crop, dst)
        if img.size[0] == img.size[1]:
            img = edge_pad(img, used_texels(slot, img.size[0]))
        save_dds_rgba(img, tex_out / dds.name)
        print(f"  texture {dds.name} <- skin {tone}" + (" + own feet" if slot == "lowr" else ""))
    # Normal / spec maps: whatever the mod's main body shader references (embedded in its .ydd export)
    for slot, mod_d, stem in (("uppr", mod_uppr, args.uppr), ("lowr", mod_lowr, args.lowr)):
        shader = mod_d.find("ShaderGroup/Shaders/Item")
        for sampler, kind in (("BumpSampler", "normal"), ("SpecSampler", "spec")):
            name = next((p.findtext("Name") for p in shader.findall("Parameters/Item") if p.get("name") == sampler), None)
            src = mod_xml / stem / f"{name}.dds" if name else None
            dst = tex_out / f"{slot}_{kind}_000.dds"
            if src and src.exists() and dst.exists():
                shutil.copyfile(src, dst)
                print(f"  texture {dst.name} <- mod {src.name}")
    print(f"wrote {out_xml}")


if __name__ == "__main__":
    main()
