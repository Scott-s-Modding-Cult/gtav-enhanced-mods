"""Minimal reader for compiled Source engine models (.mdl v44-49 + .vvd + .dx90.vtx) and .vtf textures.

load_model(path_to_mdl) -> dict:
  bones:  [ {name, parent, bind (4x4 model-space rest pose)} ]
  meshes: [ {material, positions (N,3), normals (N,3), uvs (N,2), bone_ids (N,3), weights (N,3), tris (M,3)} ]
          (LOD 0 only; one entry per MDL mesh, grouped by material)
  materials: [ material names (cdtexture dir + texture name) ]
load_vtf(path) -> PIL.Image (largest mip)

Coordinates are left in Source units/axes (inches, Z up).
"""
import struct
from pathlib import Path

import numpy as np


def _cstr(buf, off):
    end = buf.index(b"\0", off)
    return buf[off:end].decode("latin-1")


def _read_mdl(buf):
    if buf[:4] != b"IDST":
        raise ValueError("not an MDL file")
    version = struct.unpack_from("<i", buf, 4)[0]
    numbones, boneindex = struct.unpack_from("<2i", buf, 156)
    numtextures, textureindex, numcdtextures, cdtextureindex = struct.unpack_from("<4i", buf, 204)
    numbodyparts, bodypartindex = struct.unpack_from("<2i", buf, 232)

    bones = []
    for i in range(numbones):
        o = boneindex + i * 216
        nameidx, parent = struct.unpack_from("<2i", buf, o)
        pose_to_bone = np.array(struct.unpack_from("<12f", buf, o + 96)).reshape(3, 4)
        m = np.eye(4)
        m[:3, :4] = pose_to_bone
        bones.append({"name": _cstr(buf, o + nameidx), "parent": parent, "bind": np.linalg.inv(m)})

    textures = []
    for i in range(numtextures):
        o = textureindex + i * 64
        textures.append(_cstr(buf, o + struct.unpack_from("<i", buf, o)[0]))
    cddirs = [_cstr(buf, struct.unpack_from("<i", buf, cdtextureindex + 4 * i)[0]) for i in range(numcdtextures)]

    bodyparts = []
    for b in range(numbodyparts):
        bo = bodypartindex + b * 16
        nameidx, nummodels, base, modelindex = struct.unpack_from("<4i", buf, bo)
        models = []
        for mi in range(nummodels):
            mo = bo + modelindex + mi * 148
            name = buf[mo:mo + 64].split(b"\0")[0].decode("latin-1")
            nummeshes, meshindex, numvertices, vertexindex = struct.unpack_from("<4i", buf, mo + 72)
            meshes = []
            for k in range(nummeshes):
                me = mo + meshindex + k * 116
                material, _, mesh_numverts, vertexoffset = struct.unpack_from("<4i", buf, me)
                meshes.append({"material": material, "numverts": mesh_numverts, "vertexoffset": vertexoffset})
            models.append({"name": name, "numvertices": numvertices, "vertexindex": vertexindex, "meshes": meshes})
        bodyparts.append({"name": _cstr(buf, bo + nameidx), "models": models})
    return {"version": version, "bones": bones, "textures": textures, "cddirs": cddirs, "bodyparts": bodyparts}


def _read_vvd(buf):
    if buf[:4] != b"IDSV":
        raise ValueError("not a VVD file")
    numlods = struct.unpack_from("<i", buf, 12)[0]
    num_lod_verts = struct.unpack_from("<8i", buf, 16)
    numfixups, fixupstart, vertstart = struct.unpack_from("<3i", buf, 48)
    n_all = num_lod_verts[0]
    dt = np.dtype([("w", "<f4", 3), ("b", "u1", 3), ("nb", "u1"), ("pos", "<f4", 3), ("nrm", "<f4", 3), ("uv", "<f4", 2)])
    raw = np.frombuffer(buf, dt, count=n_all, offset=vertstart)
    if numfixups:
        idx = []
        for f in range(numfixups):
            lod, src, num = struct.unpack_from("<3i", buf, fixupstart + f * 12)
            if lod >= 0:   # LOD 0 uses every fixup
                idx.extend(range(src, src + num))
        raw = raw[np.array(idx)]
    return raw


def _read_vtx(buf, mdl):
    version, _, _, _, _, _, numlods, _, numbodyparts, bodypartoffset = struct.unpack_from("<i i HH i i i i i i".replace(" ", ""), buf, 0)
    out = []   # list per bodypart -> per model -> per mesh -> (vertex list of origMeshVertID, index list)

    def parse(sg_size):
        result = []
        for b in range(numbodyparts):
            bo = bodypartoffset + b * 8
            nummodels, modeloffset = struct.unpack_from("<2i", buf, bo)
            models = []
            for m in range(nummodels):
                mo = bo + modeloffset + m * 8
                _, lodoffset = struct.unpack_from("<2i", buf, mo)
                lo = mo + lodoffset                       # LOD 0
                nummeshes, meshoffset = struct.unpack_from("<2i", buf, lo)
                meshes = []
                for k in range(nummeshes):
                    me = lo + meshoffset + k * 9
                    numsg, sgoffset = struct.unpack_from("<2i", buf, me)
                    verts, idx = [], []
                    for s in range(numsg):
                        so = me + sgoffset + s * sg_size
                        nv, vo, ni, io = struct.unpack_from("<4i", buf, so)
                        if nv < 0 or ni < 0 or so + vo + nv * 9 > len(buf) or so + io + ni * 2 > len(buf):
                            raise ValueError("bad strip group")
                        base = len(verts)
                        for v in range(nv):
                            verts.append(struct.unpack_from("<H", buf, so + vo + v * 9 + 4)[0])
                        sidx = np.frombuffer(buf, "<u2", count=ni, offset=so + io).astype(np.int64) + base
                        if len(sidx) and sidx.max() >= len(verts):
                            raise ValueError("index out of range")
                        idx.extend(sidx.tolist())
                    meshes.append((verts, idx))
                models.append(meshes)
            result.append(models)
        return result

    for size in (25, 33):   # older / newer strip-group header layouts
        try:
            return parse(size)
        except (ValueError, struct.error):
            continue
    raise ValueError("could not parse VTX strip groups")


def load_model(mdl_path, bodypart_models=None):
    """bodypart_models: optional {bodypart name: model index} to pick sub-models (default: model 0 of each)."""
    mdl_path = Path(mdl_path)
    mdl = _read_mdl(mdl_path.read_bytes())
    vvd = _read_vvd(mdl_path.with_suffix(".vvd").read_bytes())
    vtx_path = mdl_path.with_name(mdl_path.stem + ".dx90.vtx")
    vtx = _read_vtx(vtx_path.read_bytes(), mdl)

    materials = []
    for t in mdl["textures"]:
        materials.append(t)
    meshes = []
    for b, bp in enumerate(mdl["bodyparts"]):
        choice = (bodypart_models or {}).get(bp["name"], 0)
        if choice is None or choice >= len(bp["models"]):
            continue
        model = bp["models"][choice]
        if not model["meshes"]:
            continue
        first = model["vertexindex"] // 48
        for k, mesh in enumerate(model["meshes"]):
            verts, idx = vtx[b][choice][k]
            if not idx:
                continue
            vids = first + mesh["vertexoffset"] + np.array(verts)
            v = vvd[vids]
            tris = np.array(idx).reshape(-1, 3)[:, ::-1]   # Source winding -> counter-clockwise
            meshes.append({
                "bodypart": bp["name"], "model": model["name"], "material": materials[mesh["material"]],
                "positions": v["pos"].astype(float), "normals": v["nrm"].astype(float), "uvs": v["uv"].astype(float),
                "bone_ids": v["b"].astype(int), "weights": v["w"].astype(float), "numbones": v["nb"].astype(int),
                "tris": tris,
            })
    return {"bones": mdl["bones"], "meshes": meshes, "materials": materials, "cddirs": mdl["cddirs"],
            "bodyparts": [(bp["name"], [m["name"] for m in bp["models"]]) for bp in mdl["bodyparts"]],
            "version": mdl["version"]}


VTF_FORMATS = {13: ("DXT1", 8), 14: ("DXT3", 16), 15: ("DXT5", 16)}


def load_vtf(path):
    """Largest mip of a DXT1/3/5 VTF as a PIL image (via an in-memory DDS)."""
    import io
    from PIL import Image
    buf = Path(path).read_bytes()
    if buf[:4] != b"VTF\0":
        raise ValueError("not a VTF file")
    major, minor, header_size = struct.unpack_from("<3I", buf, 4)
    w, h = struct.unpack_from("<2H", buf, 16)
    fmt = struct.unpack_from("<i", buf, 52)[0]
    lr_fmt = struct.unpack_from("<i", buf, 57)[0]
    lr_w, lr_h = buf[61], buf[62]
    if fmt not in VTF_FORMATS:
        raise ValueError(f"unsupported VTF format {fmt}")
    fourcc, block = VTF_FORMATS[fmt]
    size = max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * block
    if (major, minor) >= (7, 3):
        nres = struct.unpack_from("<I", buf, 68)[0]
        start = None
        for r in range(nres):
            tag = buf[80 + r * 8: 83 + r * 8]
            if tag == b"\x30\0\0":
                start = struct.unpack_from("<I", buf, 84 + r * 8)[0]
        hi_end = len(buf) if start is None else None
        data_end = (start if start is not None else 0)
        # image data runs from `start` to EOF with mips smallest->largest; largest is last
        chunk = buf[start:] if start is not None else buf[header_size:]
        data = chunk[len(chunk) - size:] if start is None else None
        if start is not None:
            # largest mip is at the end of the image resource (frames=1, faces=1)
            total = len(buf) - start
            data = buf[start + total - size: start + total]
    else:
        lr_size = 0
        if lr_fmt == 13 and lr_w and lr_h:
            lr_size = max(1, (lr_w + 3) // 4) * max(1, (lr_h + 3) // 4) * 8
        data = buf[len(buf) - size:]
    dds = struct.pack("<4s7I44x", b"DDS ", 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x80000, h, w, size, 0, 1)
    dds += struct.pack("<2I4s5I", 32, 0x4, fourcc.encode(), 0, 0, 0, 0, 0)
    dds += struct.pack("<5I", 0x1000, 0, 0, 0, 0)
    img = Image.open(io.BytesIO(dds + data))
    img.load()
    return img
