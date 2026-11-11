"""Turns an Alan13 SFM base body into mp_f_freemode_01's default (nude) body.

Usage:
  python sfm_to_freemode.py <pack_root> <BodyName> <freemode_default_xml_dir> <freemode_yft_xml> <out_xml_dir> [--preview out.html]

  pack_root                 SFM addon root (models\\, materials\\)
  freemode_default_xml_dir  CodeWalker export of her default pieces (uppr/lowr/feet/accs/jbib _000 + textures)
  out_xml_dir               receives XML + textures for: jbib_000_u (the body), jbib_diff_000_[a-p]_uni (skins),
                            and emptied uppr_000_r / lowr_000_r / feet_000_r / accs_000_u

The body goes in her TOP slot (jbib, component 11) with plain `ped` shaders: torso/legs/feet are skin
slots that get the game's blended skin overlay (mapped to the standard body UVs), which would scramble a
custom mesh. Each SFM bone is re-posed from the SFM rest pose to her skeleton's rest pose (joint position +
bone direction; hands use two axes), then weights are remapped to her bones.
"""
import argparse
import base64
import copy
import io
import json
import shutil
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
import build_nude_ped as bnp  # noqa: E402
import ped_preview  # noqa: E402
import source_mdl as sm  # noqa: E402

INCH = 0.0254
ATLAS = 2048
PAD = 8 / ATLAS
QUAD = {"torso": (0.0, 0.0), "arms": (0.5, 0.0), "legs": (0.0, 0.5), "gens": (0.5, 0.5)}
SIDES = (("L", "l"), ("R", "r"))


# ------------------------------------------------------------------ bone mapping
def bone_tables():
    weight_map = {"hip": "SKEL_Pelvis", "pelvis": "SKEL_Pelvis", "Genitals": "SKEL_Pelvis", "Anus": "SKEL_Pelvis",
                  "abdomenLower": "SKEL_Spine0", "abdomenUpper": "SKEL_Spine1", "chestLower": "SKEL_Spine2",
                  "chestUpper": "SKEL_Spine3", "lPectoral": "SKEL_Spine3", "rPectoral": "SKEL_Spine3",
                  "neckLower": "SKEL_Neck_1", "neckUpper": "SKEL_Neck_1"}
    canon = {"SKEL_Pelvis": "pelvis", "SKEL_Spine0": "abdomenLower", "SKEL_Spine1": "abdomenUpper",
             "SKEL_Spine2": "chestLower", "SKEL_Spine3": "chestUpper", "SKEL_Neck_1": "neckLower"}
    child = {"SKEL_Spine0": "SKEL_Spine1", "SKEL_Spine1": "SKEL_Spine2", "SKEL_Spine2": "SKEL_Spine3",
             "SKEL_Spine3": "SKEL_Neck_1"}
    segment = {}
    for S, s in SIDES:
        vb = f"ValveBiped.Bip01_{S}_"
        weight_map.update({f"{s}Collar": f"SKEL_{S}_Clavicle", f"{s}ShldrBend": f"SKEL_{S}_UpperArm",
                           f"{s}ShldrTwist": f"RB_{S}_ArmRoll", f"{s}ForearmBend": f"SKEL_{S}_Forearm",
                           f"{s}ForearmTwist": f"RB_{S}_ForeArmRoll", f"{vb}Hand": f"SKEL_{S}_Hand",
                           f"{s}ThighBend": f"SKEL_{S}_Thigh", f"{s}ThighTwist": f"RB_{S}_ThighRoll",
                           f"{s}Shin": f"SKEL_{S}_Calf", f"{s}Foot": f"SKEL_{S}_Foot", f"{s}Heel": f"SKEL_{S}_Foot",
                           f"{s}Metatarsals": f"SKEL_{S}_Toe0", f"{s}Toe": f"SKEL_{S}_Toe0"})
        for k in range(1, 5):
            weight_map[f"{s}Carpal{k}"] = f"SKEL_{S}_Hand"
        for n in range(5):
            for k, suf in enumerate(("", "1", "2")):
                weight_map[f"{vb}Finger{n}{suf}"] = f"SKEL_{S}_Finger{n}{k}"
                canon[f"SKEL_{S}_Finger{n}{k}"] = f"{vb}Finger{n}{suf}"
                if k < 2:
                    child[f"SKEL_{S}_Finger{n}{k}"] = f"SKEL_{S}_Finger{n}{k + 1}"
        canon.update({f"SKEL_{S}_Clavicle": f"{s}Collar", f"SKEL_{S}_UpperArm": f"{s}ShldrBend",
                      f"SKEL_{S}_Forearm": f"{s}ForearmBend", f"SKEL_{S}_Hand": f"{vb}Hand",
                      f"SKEL_{S}_Thigh": f"{s}ThighBend", f"SKEL_{S}_Calf": f"{s}Shin",
                      f"SKEL_{S}_Foot": f"{s}Foot", f"SKEL_{S}_Toe0": f"{s}Toe"})
        child.update({f"SKEL_{S}_Clavicle": f"SKEL_{S}_UpperArm", f"SKEL_{S}_UpperArm": f"SKEL_{S}_Forearm",
                      f"SKEL_{S}_Forearm": f"SKEL_{S}_Hand", f"SKEL_{S}_Hand": f"SKEL_{S}_Finger20",
                      f"SKEL_{S}_Thigh": f"SKEL_{S}_Calf", f"SKEL_{S}_Calf": f"SKEL_{S}_Foot",
                      f"SKEL_{S}_Foot": f"SKEL_{S}_Toe0"})
        segment.update({f"RB_{S}_ArmRoll": f"SKEL_{S}_UpperArm", f"RB_{S}_ForeArmRoll": f"SKEL_{S}_Forearm",
                        f"RB_{S}_ThighRoll": f"SKEL_{S}_Thigh"})
    return weight_map, canon, child, segment


def rot_between(a, b):
    a, b = a / np.linalg.norm(a), b / np.linalg.norm(b)
    v, c = np.cross(a, b), float(np.dot(a, b))
    if c < -0.9999:
        axis = np.cross(a, [1, 0, 0])
        if np.linalg.norm(axis) < 1e-6:
            axis = np.cross(a, [0, 1, 0])
        axis /= np.linalg.norm(axis)
        return 2 * np.outer(axis, axis) - np.eye(3)
    vx = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    return np.eye(3) + vx + vx @ vx / (1 + c)


def frame(primary, secondary):
    x = primary / np.linalg.norm(primary)
    y = secondary - np.dot(secondary, x) * x
    y /= np.linalg.norm(y)
    return np.stack([x, y, np.cross(x, y)], 1)


def pose_matrices(sfm, gta_names, gta_world):
    """Per GTA bone: 4x4 matrix moving SFM rest-pose geometry onto her rest pose."""
    weight_map, canon, child, segment = bone_tables()
    sp = {b["name"]: b["bind"][:3, 3] * INCH for b in sfm["bones"]}
    gp = {n: gta_world[i][:3, 3] for i, n in enumerate(gta_names)}
    rot = {"SKEL_Pelvis": np.eye(3)}
    order = ["SKEL_Pelvis", "SKEL_Spine0", "SKEL_Spine1", "SKEL_Spine2", "SKEL_Spine3", "SKEL_Neck_1"]
    for S, _ in SIDES:
        order += [f"SKEL_{S}_{b}" for b in ("Clavicle", "UpperArm", "Forearm", "Hand")]
        order += [f"SKEL_{S}_Finger{n}{k}" for n in range(5) for k in range(3)]
        order += [f"SKEL_{S}_{b}" for b in ("Thigh", "Calf", "Foot", "Toe0")]
    parent_of = {}
    for g, c in child.items():
        parent_of.setdefault(c, g)
    for g in order:
        if g in rot:
            continue
        c = child.get(g)
        src_child = "neckUpper" if g == "SKEL_Neck_1" else canon.get(c)
        if c and src_child in sp and c in gp:
            ds, dt = sp[src_child] - sp[canon[g]], gp[c] - gp[g]
            if g.endswith("_Hand"):
                S = g[5]
                vb = f"ValveBiped.Bip01_{S}_"
                ss = sp[f"{vb}Finger4"] - sp[f"{vb}Finger1"]
                st = gp[f"SKEL_{S}_Finger40"] - gp[f"SKEL_{S}_Finger10"]
                rot[g] = frame(dt, st) @ frame(ds, ss).T
            else:
                rot[g] = rot_between(ds, dt)
        else:   # end bones inherit their parent's rotation
            p = parent_of.get(g) or ("SKEL_Spine3" if "Clavicle" in g else "SKEL_Pelvis")
            rot[g] = rot.get(p, np.eye(3))
    mats = {}
    for g in order:
        m = np.eye(4)
        m[:3, :3] = rot[g]
        m[:3, 3] = gp[g] - rot[g] @ sp[canon[g]]
        mats[g] = m
    for roll, seg in segment.items():
        mats[roll] = mats[seg]
    return mats, weight_map


# ------------------------------------------------------------------ textures
def vmt_textures(root, cddirs, material):
    for d in cddirs:
        vmt = root / "materials" / d.replace("\\", "/") / f"{material}.vmt"
        if vmt.exists():
            out = {}
            for line in vmt.read_text(errors="ignore").splitlines():
                parts = line.replace('"', " ").split()
                if len(parts) >= 2 and parts[0].lower() in ("$basetexture", "$bumpmap"):
                    out[parts[0].lower()] = root / "materials" / (" ".join(parts[1:]).replace("\\", "/") + ".vtf")
            return out
    return {}


def make_atlas(images, mode="RGB", fill=(0, 0, 0)):
    q = ATLAS // 2
    inner = q - 2 * int(PAD * ATLAS)
    atlas = Image.new(mode, (ATLAS, ATLAS), fill)
    for name, img in images.items():
        qx, qy = QUAD[name]
        tile = img.convert(mode).resize((inner, inner), Image.LANCZOS)
        # edge-extend the tile into its padding so mip levels don't bleed between quadrants
        padded = Image.new(mode, (q, q))
        p = int(PAD * ATLAS)
        padded.paste(tile.resize((q, q), Image.LANCZOS))
        padded.paste(tile, (p, p))
        atlas.paste(padded, (int(qx * ATLAS), int(qy * ATLAS)))
    return atlas


def save_dds(img, path, fourcc):
    """DXT1/DXT5 DDS with a full mip chain (Pillow encodes each level)."""
    levels, w, h = [], img.size[0], img.size[1]
    cur = img
    while True:
        b = io.BytesIO()
        (cur.convert("RGB") if fourcc == "DXT1" else cur.convert("RGBA")).save(b, "DDS", pixel_format=fourcc)
        levels.append(b.getvalue()[128:])
        if w == 1 and h == 1:
            break
        w, h = max(1, w // 2), max(1, h // 2)
        cur = img.resize((w, h), Image.LANCZOS)
    W, H = img.size
    hdr = struct.pack("<4s7I44x", b"DDS ", 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000, H, W,
                      len(levels[0]), 0, len(levels))
    pf = struct.pack("<2I4s5I", 32, 0x4, fourcc.encode(), 0, 0, 0, 0, 0)
    caps = struct.pack("<5I", 0x1000 | 0x8 | 0x400000, 0, 0, 0, 0)
    Path(path).write_bytes(hdr + pf + caps + b"".join(levels))
    return len(levels)


def set_tex_item(item, name, w, h, mips, fmt):
    item.find("Name").text = name
    item.find("FileName").text = f"{name}.dds"
    item.find("Width").set("value", str(w))
    item.find("Height").set("value", str(h))
    item.find("MipLevels").set("value", str(mips))
    item.find("Format").text = fmt


# ------------------------------------------------------------------ main
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("root", type=Path)
    ap.add_argument("body")
    ap.add_argument("defaults", type=Path)
    ap.add_argument("yft", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--preview", type=Path)
    a = ap.parse_args()

    sfm = sm.load_model(a.root / "models" / "Alan13" / "SFM" / "Bases" / f"{a.body}.mdl")
    gta_sk = bnp.load_skeleton(a.yft)
    gta_names = [n for n, _ in gta_sk]
    gta_world = bnp.bind_pose(a.yft)
    mats, weight_map = pose_matrices(sfm, gta_names, gta_world)
    sfm_names = [b["name"] for b in sfm["bones"]]
    sfm_parent = [b["parent"] for b in sfm["bones"]]

    def gta_for(i):
        while i >= 0 and sfm_names[i] not in weight_map:
            i = sfm_parent[i]
        return weight_map[sfm_names[i]] if i >= 0 else "SKEL_Pelvis"
    sfm_to_gta = [gta_for(i) for i in range(len(sfm_names))]
    seg_of = {g: g for g in mats}
    unmapped = sorted({sfm_names[i] for i in range(len(sfm_names)) if sfm_names[i] not in weight_map})
    print(f"{a.body}: {len(sfm_names)} SFM bones -> {len(set(sfm_to_gta))} GTA bones "
          f"({len(unmapped)} folded into parents: {', '.join(unmapped[:8])}{'…' if len(unmapped) > 8 else ''})")

    # ---- deform + merge into one vertex list
    pos, nrm, uv, wts, idx = [], [], [], [], []
    for mesh in sfm["meshes"]:
        mat = mesh["material"]
        if mat not in QUAD:
            continue
        base = len(pos)
        P = mesh["positions"] * INCH
        for k in range(len(P)):
            M = np.zeros((4, 4))
            acc = {}
            tot = 0.0
            for bi, w in zip(mesh["bone_ids"][k][:mesh["numbones"][k]], mesh["weights"][k][:mesh["numbones"][k]]):
                if w <= 0:
                    continue
                g = sfm_to_gta[int(bi)]
                M += w * mats[seg_of.get(g, "SKEL_Pelvis")]
                acc[g] = acc.get(g, 0.0) + w
                tot += w
            M /= tot
            p = M @ np.append(P[k], 1.0)
            n = M[:3, :3] @ mesh["normals"][k]
            pos.append(p[:3])
            nrm.append(n / np.linalg.norm(n))
            u, v = mesh["uvs"][k]
            qx, qy = QUAD[mat]
            uv.append((qx + PAD + u * (0.5 - 2 * PAD), qy + PAD + v * (0.5 - 2 * PAD)))
            items = sorted(acc.items(), key=lambda x: -x[1])[:4]
            wts.append(items)
        idx.extend((mesh["tris"] + base).ravel().tolist())
    pos, nrm, uv = np.array(pos), np.array(nrm), np.array(uv)
    tris = np.array(idx).reshape(-1, 3)
    print(f"body: {len(pos)} verts, {len(tris)} tris; height {pos[:, 2].min():.2f}..{pos[:, 2].max():.2f} m "
          f"(her feet {gta_world[gta_names.index('SKEL_L_Foot')][2, 3]:.2f}, neck {gta_world[gta_names.index('SKEL_Neck_1')][2, 3]:.2f})")

    # ---- neck blend: widen the SFM neck to the head mesh's bottom rim (all freemode heads share it)
    hd = ET.parse(a.defaults / "head_000_r.ydd.xml").getroot().find("Item")
    hgeom, _ = bnp.parse_model(hd.find("DrawableModelsHigh/Item"))
    hpos = np.array([[float(x) for x in v["Position"]] for v in hgeom.verts])
    # open boundary edges of the welded head mesh; the neck rim is the boundary loop lowest on the mesh
    key = {}
    rep = np.array([key.setdefault(tuple(np.round(p, 4)), len(key)) for p in hpos])
    ht = rep[np.array(hgeom.idx).reshape(-1, 3)]
    edges = np.sort(np.vstack([ht[:, [0, 1]], ht[:, [1, 2]], ht[:, [2, 0]]]), axis=1)
    uniq, counts = np.unique(edges, axis=0, return_counts=True)
    bverts = np.unique(uniq[counts == 1])
    rep_pos = np.zeros((len(key), 3)); rep_pos[rep] = hpos
    bpos = rep_pos[bverts]
    rim = bpos[bpos[:, 2] < bpos[:, 2].min() + 0.08]           # bottom loop (slanted), not eyes/mouth
    centre = rim[:, :2].mean(0)
    bins = 48
    ang = np.arctan2(rim[:, 1] - centre[1], rim[:, 0] - centre[0])
    b_of = ((ang + np.pi) / (2 * np.pi) * bins).astype(int) % bins
    rim_r = np.full(bins, np.nan); rim_z = np.full(bins, np.nan)
    for b_ in range(bins):
        m = b_of == b_
        if m.any():
            rim_r[b_] = np.linalg.norm(rim[m, :2] - centre, axis=1).mean()
            rim_z[b_] = rim[m, 2].mean()
    good = ~np.isnan(rim_r)
    xs = np.arange(bins)
    rim_r = np.interp(xs, xs[good], rim_r[good], period=bins)
    rim_z = np.interp(xs, xs[good], rim_z[good], period=bins)
    # Head neck surface radius by (angle, height above rim), so the body can wrap just outside it like the
    # vanilla torso does (covering the head's rim) instead of ending inside it (which exposes the rim edge).
    COVER, OUTSET = 0.035, 0.0025              # body covers 3.5 cm of the head's neck, 2.5 mm outside it
    hrel = np.arctan2(hpos[:, 1] - centre[1], hpos[:, 0] - centre[0])
    hb = ((hrel + np.pi) / (2 * np.pi) * bins).astype(int) % bins
    hr = np.linalg.norm(hpos[:, :2] - centre, axis=1)
    hz = hpos[:, 2] - rim_z[hb]                # height above the rim at that angle
    steps = np.arange(0.0, COVER + 0.011, 0.005)
    surf = np.full((bins, len(steps)), np.nan)
    for b_ in range(bins):
        for j, s in enumerate(steps):
            m = (hb == b_) & (np.abs(hz - s) < 0.006) & (hr < 0.12)
            if m.any():
                surf[b_, j] = hr[m].max()
    for b_ in range(bins):                      # fill gaps along height, then around the neck
        row = surf[b_]
        ok = ~np.isnan(row)
        surf[b_] = np.interp(steps, steps[ok], row[ok]) if ok.any() else np.nan
    for j in range(len(steps)):
        col = surf[:, j]
        ok = ~np.isnan(col)
        surf[:, j] = np.interp(xs, xs[ok], col[ok], period=bins)

    # Exact head surface: ray from the neck axis outward, intersected with the head's actual triangles
    # (its neck is low-poly/faceted, so a smooth per-angle radius lets facets poke through).
    htri = np.array(hgeom.idx).reshape(-1, 3)
    tz = hpos[htri][:, :, 2]
    tr = np.linalg.norm(hpos[htri][:, :, :2] - centre, axis=2)
    near = (tz.max(1) > rim_z.min() - 0.02) & (tz.min(1) < rim_z.max() + COVER + 0.03) & (tr.min(1) < 0.15)
    T0, T1, T2 = hpos[htri[near, 0]], hpos[htri[near, 1]], hpos[htri[near, 2]]
    E1, E2 = T1 - T0, T2 - T0

    def head_hit(direction2d, z):
        """Distance from the neck axis (at height z) to the outermost head surface along direction2d."""
        o = np.array([centre[0], centre[1], z])
        dvec = np.array([direction2d[0], direction2d[1], 0.0])
        pv = np.cross(dvec, E2)
        det = np.einsum("ij,ij->i", E1, pv)
        ok = np.abs(det) > 1e-12
        inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
        tv = o - T0
        u = np.einsum("ij,ij->i", tv, pv) * inv
        qv = np.cross(tv, E1)
        v = (qv @ dvec) * inv
        t = np.einsum("ij,ij->i", E2, qv) * inv
        hit = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 0)
        return t[hit].max() if hit.any() else None

    def head_radius(x, above, direction2d=None, z=None):
        if direction2d is not None:
            h = head_hit(direction2d, z)
            if h is not None:
                return h
        j = np.clip(above / 0.005, 0, len(steps) - 1)
        r0 = np.interp(x, xs, surf[:, int(np.floor(j))], period=bins)
        r1 = np.interp(x, xs, surf[:, int(np.ceil(j))], period=bins)
        return r0 + (r1 - r0) * (j - np.floor(j))

    moved = 0
    above_top = np.zeros(len(pos), bool)
    for k in range(len(pos)):
        if not any(g in ("SKEL_Neck_1", "SKEL_Spine3", "SKEL_L_Clavicle", "SKEL_R_Clavicle") for g, _ in wts[k]):
            continue
        d = pos[k, :2] - centre
        r = np.linalg.norm(d)
        # neck column only: full effect within 10 cm of the axis, fading out by 13 cm (no folds at the shoulders)
        wr = np.clip((0.13 - r) / 0.03, 0, 1)
        if r < 1e-6 or wr <= 0:
            continue
        wr = wr * wr * (3 - 2 * wr)
        x = (np.arctan2(d[1], d[0]) + np.pi) / (2 * np.pi) * bins - 0.5
        zr = np.interp(x, xs, rim_z, period=bins)
        above = pos[k, 2] - zr
        if above < -0.06:
            continue
        if above > COVER and wr > 0.99:   # above the covered band: its triangles get clipped/removed below
            above_top[k] = True
            continue
        target = head_radius(x, min(max(above, 0.0), COVER), d / r, zr + min(max(above, 0.0), COVER)) + OUTSET
        t = np.clip((above + 0.06) / 0.06, 0, 1)
        t = t * t * (3 - 2 * t) * wr  # smoothstep: body shape below, head-hugging from the rim up
        pos[k, :2] = centre + d / r * (r + (target - r) * t)
        moved += 1
    # Clip triangles crossing the band's top line exactly at the line, so the body ends in a clean edge.
    def line_z(p):
        ang_ = np.arctan2(p[1] - centre[1], p[0] - centre[0])
        return np.interp((ang_ + np.pi) / (2 * np.pi) * bins - 0.5, xs, rim_z, period=bins) + COVER

    pos_l, nrm_l, uv_l = list(pos), list(nrm), list(uv)
    cut_cache = {}

    def cut(a_, b_):                       # a_ below the line, b_ above
        key_ = (a_, b_)
        if key_ in cut_cache:
            return cut_cache[key_]
        lo_, hi_ = 0.0, 1.0
        for _ in range(20):                # bisection on z(t) - line(t)
            mid = (lo_ + hi_) / 2
            p_ = pos_l[a_] + (pos_l[b_] - pos_l[a_]) * mid
            lo_, hi_ = (mid, hi_) if p_[2] < line_z(p_) else (lo_, mid)
        t_ = (lo_ + hi_) / 2
        p_ = pos_l[a_] + (pos_l[b_] - pos_l[a_]) * t_
        d_ = p_[:2] - centre
        r_ = np.linalg.norm(d_)
        h_ = head_hit(d_ / r_, p_[2])
        if h_ is not None:
            p_[:2] = centre + d_ / r_ * (h_ + OUTSET)
        n_ = nrm_l[a_] + (nrm_l[b_] - nrm_l[a_]) * t_
        pos_l.append(p_); nrm_l.append(n_ / np.linalg.norm(n_)); uv_l.append(uv_l[a_] + (uv_l[b_] - uv_l[a_]) * t_)
        wts.append(wts[a_])
        cut_cache[key_] = len(pos_l) - 1
        return cut_cache[key_]

    new_tris, dropped, clipped = [], 0, 0
    for tri in tris:
        up = above_top[tri]
        if not up.any():
            new_tris.append(tri); continue
        if up.all():
            dropped += 1; continue
        clipped += 1
        i0 = int(np.argmax(up != up[[1, 2, 0]]))       # rotate so the pattern is consistent, winding kept
        tri = np.roll(tri, -i0); up = np.roll(up, -i0)
        a_, b_, c_ = (int(x) for x in tri)
        if up.sum() == 2:                  # one vertex below
            below = [i for i in range(3) if not up[i]][0]
            t3 = np.roll(np.array([a_, b_, c_]), -below)
            new_tris.append([t3[0], cut(t3[0], t3[1]), cut(t3[0], t3[2])])
        else:                              # two vertices below
            above_i = [i for i in range(3) if up[i]][0]
            t3 = np.roll(np.array([a_, b_, c_]), -above_i)   # t3[0] above, t3[1], t3[2] below
            p1, p2 = cut(t3[1], t3[0]), cut(t3[2], t3[0])
            new_tris.append([t3[1], t3[2], p2])
            new_tris.append([t3[1], p2, p1])
    pos, nrm, uv = np.array(pos_l), np.array(nrm_l), np.array(uv_l)
    tris = np.array(new_tris, dtype=np.int64)
    print(f"neck: body wraps {COVER * 100:.1f} cm over the head's neck; clipped {clipped} triangles at a clean edge, "
          f"removed {dropped} above it")
    print(f"neck: blended {moved} verts to the head rim ({len(rim)} rim verts, z {rim_z.min():.3f}..{rim_z.max():.3f}, "
          f"radius {rim_r.min() * 100:.1f}..{rim_r.max() * 100:.1f} cm)")

    # ---- tangents from UVs
    tan = np.zeros((len(pos), 3)); bit = np.zeros((len(pos), 3))
    p0, p1, p2 = pos[tris[:, 0]], pos[tris[:, 1]], pos[tris[:, 2]]
    w0, w1, w2 = uv[tris[:, 0]], uv[tris[:, 1]], uv[tris[:, 2]]
    e1, e2, d1, d2 = p1 - p0, p2 - p0, w1 - w0, w2 - w0
    r = d1[:, 0] * d2[:, 1] - d2[:, 0] * d1[:, 1]
    r = np.where(np.abs(r) < 1e-12, 1e-12, r)
    t = (e1 * d2[:, 1:2] - e2 * d1[:, 1:2]) / r[:, None]
    bt = (e2 * d1[:, 0:1] - e1 * d2[:, 0:1]) / r[:, None]
    for c in range(3):
        np.add.at(tan, tris[:, c], t)
        np.add.at(bit, tris[:, c], bt)
    tan -= nrm * np.einsum("ij,ij->i", tan, nrm)[:, None]
    tan /= np.maximum(np.linalg.norm(tan, axis=1, keepdims=True), 1e-9)
    sign = np.where(np.einsum("ij,ij->i", np.cross(nrm, tan), bit) < 0, -1.0, 1.0)

    # ---- vertex dicts in her high-LOD layout
    f = bnp.fmt
    verts = []
    for k in range(len(pos)):
        items = wts[k]
        total = sum(w for _, w in items)
        bw = [round(w * 255 / total) for _, w in items]
        bw[0] += 255 - sum(bw)
        bi = [gta_names.index(g) for g, _ in items]
        while len(bw) < 4:
            bw.append(0); bi.append(0)
        verts.append({"Position": [f(x) for x in pos[k]], "BlendWeights": [str(x) for x in bw],
                      "BlendIndices": [str(x) for x in bi], "Normal": [f(x) for x in nrm[k]],
                      "Colour0": ["255", "128", "0", "255"], "Colour1": ["0", "0", "0", "0"],
                      "TexCoord0": [f(uv[k, 0]), f(uv[k, 1])], "TexCoord1": [f(uv[k, 0]), f(uv[k, 1])],
                      "Tangent": [f(x) for x in tan[k]] + [f(sign[k])]})

    # ---- output folder: start from her default pieces
    if a.out.exists():
        shutil.rmtree(a.out)
    a.out.mkdir(parents=True)
    uppr_t = ET.parse(a.defaults / "uppr_000_r.ydd.xml")
    jbib_t = ET.parse(a.defaults / "jbib_000_u.ydd.xml")
    jd = jbib_t.getroot().find("Item")
    ud = uppr_t.getroot().find("Item")
    jd.remove(jd.find("ShaderGroup"))
    sg = copy.deepcopy(ud.find("ShaderGroup"))
    jd.insert([c.tag for c in ud].index("ShaderGroup"), sg)
    rename = {"DiffuseSampler": "jbib_diff_000_a_uni", "BumpSampler": "jbib_normal_000", "SpecSampler": "jbib_spec_000"}
    for p in sg.iter("Item"):
        if p.get("name") in rename and p.find("Name") is not None:
            p.find("Name").text = rename[p.get("name")]
    for lod in bnp.LODS:
        old = jd.find(lod)
        i = list(jd).index(old)
        jd.remove(old)
        jd.insert(i, copy.deepcopy(ud.find(lod)))
    positions = []
    for lod in bnp.LODS:
        model = jd.find(f"{lod}/Item")
        g_el = model.find("Geometries/Item")
        layout = [c.tag for c in g_el.find("VertexBuffer/Layout")]
        geom = bnp.Geom(layout, [bnp.to_layout(v, layout) for v in verts], tris.ravel().tolist())
        positions.append(bnp.write_geom(g_el, geom, len(gta_names)))
    bnp.set_bounds(jd, positions)
    jd.find("Name").text = "jbib_000_u"

    # ---- textures
    tex_dir = a.out / "jbib_000_u"
    tex_dir.mkdir()
    default = {m: vmt_textures(a.root, sfm["cddirs"], m) for m in QUAD}
    normal = make_atlas({m: sm.load_vtf(t["$bumpmap"]) for m, t in default.items()}, fill=(128, 128, 255))
    nm = save_dds(normal, tex_dir / "jbib_normal_000.dds", "DXT1")
    spec_src = Image.open(a.defaults / "uppr_000_r" / "uppr_spec_000.dds").convert("RGBA")
    spec_col = tuple(int(x) for x in np.asarray(spec_src).reshape(-1, 4).mean(0))
    sp_m = save_dds(Image.new("RGBA", (256, 256), spec_col), tex_dir / "jbib_spec_000.dds", "DXT5")
    for item in sg.findall("TextureDictionary/Item"):
        if item.find("Usage").text == "NORMAL":
            set_tex_item(item, "jbib_normal_000", ATLAS, ATLAS, nm, "D3DFMT_DXT1")
        else:
            set_tex_item(item, "jbib_spec_000", 256, 256, sp_m, "D3DFMT_DXT5")
    jbib_t.write(a.out / "jbib_000_u.ydd.xml", encoding="UTF-8", xml_declaration=True)

    skins = [""] + [str(k) for k in range(2, 11)]
    letters = "abcdefghijklmnop"
    ytd_t = a.defaults / "jbib_diff_000_a_uni.ytd.xml"
    preview_atlas = None
    made = {}
    for i, letter in enumerate(letters):
        skin = skins[i % len(skins)]
        name = f"jbib_diff_000_{letter}_uni"
        d = a.out / name
        d.mkdir()
        if skin not in made:
            imgs = {}
            for m in QUAD:
                tex = vmt_textures(a.root, sfm["cddirs"], m + skin if m != "gens" else m) or default[m]
                imgs[m] = sm.load_vtf(tex["$basetexture"]).convert("RGB")
            atlas = make_atlas(imgs).convert("RGBA")
            made[skin] = d / f"{name}.dds"
            mips = save_dds(atlas, made[skin], "DXT5")
            if preview_atlas is None:
                preview_atlas = atlas
        else:
            shutil.copyfile(made[skin], d / f"{name}.dds")
        t = ET.parse(ytd_t)
        set_tex_item(t.getroot().find("Item"), name, ATLAS, ATLAS, mips, "D3DFMT_DXT5")
        t.write(a.out / f"{name}.ytd.xml", encoding="UTF-8", xml_declaration=True)
    print(f"textures: {len(made)} skin atlases ({ATLAS}px) over 16 variants, normal atlas, spec")

    # ---- empty her default torso / legs / shoes / undershirt
    for n in ("uppr_000_r", "lowr_000_r", "feet_000_r", "accs_000_u"):
        t = ET.parse(a.defaults / f"{n}.ydd.xml")
        for d in t.getroot().findall("Item"):
            bnp.empty_drawable(d)
        t.write(a.out / f"{n}.ydd.xml", encoding="UTF-8", xml_declaration=True)
        if (a.defaults / n).exists():
            shutil.copytree(a.defaults / n, a.out / n)
    print("emptied: uppr_000_r, lowr_000_r, feet_000_r, accs_000_u")
    print(f"wrote {a.out}")

    if a.preview:
        buf = io.BytesIO()
        preview_atlas.convert("RGB").resize((1024, 1024)).save(buf, "JPEG", quality=85)
        body = {"slot": "body", "pos": pos.ravel().round(5).tolist(), "nrm": nrm.ravel().round(4).tolist(),
                "uv": uv.ravel().round(5).tolist(), "idx": tris.ravel().tolist(),
                "tex": "data:image/jpeg;base64," + base64.b64encode(buf.getvalue()).decode(), "alpha": False}
        parts = [body]
        hd = ET.parse(a.defaults / "head_000_r.ydd.xml").getroot().find("Item")
        hg = hd.find("DrawableModelsHigh/Item/Geometries/Item")
        hp, hn, hu, hi = ped_preview.parse_geometry(hg)
        uri, _ = ped_preview.texture_data_uri(a.defaults / "head_diff_000_a_whi", "head_diff_000_a_whi", {})
        parts.append({"slot": "head", "pos": hp, "nrm": hn, "uv": hu, "idx": hi, "tex": uri, "alpha": False})
        orig = []
        for n in ("uppr_000_r", "lowr_000_r", "feet_000_r", "jbib_000_u", "accs_000_u", "head_000_r"):
            dd = ET.parse(a.defaults / f"{n}.ydd.xml").getroot().find("Item")
            g = dd.find("DrawableModelsHigh/Item/Geometries/Item")
            p_, n_, u_, i_ = ped_preview.parse_geometry(g)
            tex = next((x.findtext("Name") for x in dd.iter("Item") if x.get("name") == "DiffuseSampler"), None)
            uri2 = None
            if tex and (a.defaults / tex).exists():
                uri2, _ = ped_preview.texture_data_uri(a.defaults / tex, tex, {})
            orig.append({"slot": n, "pos": p_, "nrm": n_, "uv": u_, "idx": i_, "tex": uri2, "alpha": False})
        peds = [{"name": f"default (vanilla)", "parts": orig}, {"name": f"{a.body} body", "parts": parts}]
        a.preview.write_text(ped_preview.PAGE.replace("__DATA__", json.dumps(peds, separators=(",", ":"))), encoding="utf-8")
        print(f"preview: {a.preview}")


if __name__ == "__main__":
    main()
