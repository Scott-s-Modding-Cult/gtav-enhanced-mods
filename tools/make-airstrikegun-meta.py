"""Builds the meta files for the WEAPON_AIRSTRIKEGUN add-on weapon: a clone of the flare gun with
its own weapon name, slot, ammo type and aiming info, plus its animation entries.

    python make-airstrikegun-meta.py <weaponflaregun.meta> <mpheist weaponanimations.meta> <out dir>

The input is the patched flare gun definition (update.rpf\\dlc_patch\\mpheist\\common\\data\\ai\\
weaponflaregun.meta) and mpheist's weaponanimations.meta. NativeUiMod's airstrike code listens
for WEAPON_AIRSTRIKEGUN.
"""
import os
import re
import sys

src_meta, src_anims, out_dir = sys.argv[1:4]
os.makedirs(out_dir, exist_ok=True)

# ---- weapon info: rename everything that identifies the flare gun
meta = open(src_meta, encoding='utf-8-sig').read()
renames = [
    ('WEAPON_FLAREGUN', 'WEAPON_AIRSTRIKEGUN'),
    ('SLOT_FLAREGUN', 'SLOT_AIRSTRIKEGUN'),                 # its own weapon-wheel slot
    ('AMMO_FLAREGUN', 'AMMO_AIRSTRIKEFLARE'),               # its own ammo
    ('PISTOL_FLAREGUN_BASE_STRAFE', 'PISTOL_AIRSTRIKEGUN_BASE_STRAFE'),
    ('<HumanNameHash>WT_FLAREGUN</HumanNameHash>', '<HumanNameHash>PIM_AIRSTRIKE</HumanNameHash>'),  # "Airstrike"
    ('<Name>DLC - Flare Gun</Name>', '<Name>NativeUiMod - Airstrike Gun</Name>'),
]
for old, new in renames:
    if old not in meta:
        raise SystemExit(f'expected "{old}" in {src_meta}')
    meta = meta.replace(old, new)
# Pickups must name real pickup types (dropped on death), so keep the flare gun's.
meta = meta.replace('PICKUP_WEAPON_AIRSTRIKEGUN', 'PICKUP_WEAPON_FLAREGUN')
meta = meta.replace('PICKUP_AMMO_AIRSTRIKEFLARE', 'PICKUP_AMMO_FLAREGUN')
# The ammo's own model/fx names (w_pi_flaregun_shell, proj_flare_fuse ...) stay: we reuse the flare.
leftover = re.findall(r'(?<!PICKUP_)(?:WEAPON_FLAREGUN|SLOT_FLAREGUN|AMMO_FLAREGUN)', meta)
assert not leftover, leftover
open(os.path.join(out_dir, 'weapon_airstrikegun.meta'), 'w', encoding='utf-8', newline='\n').write(meta)

# ---- animations: every animation set's WEAPON_FLAREGUN entry, cloned under the new name
anims = open(src_anims, encoding='utf-8-sig').read().replace('\r\n', '\n')
sets = re.findall(r'(\n    <Item key="([^"]+)">\n(?:      <Fallback>[^<]*</Fallback>\n)?)      <WeaponAnimations>(.*?)\n\s*</WeaponAnimations>', anims, re.S)
out = ['<?xml version="1.0" encoding="UTF-8"?>', '<CWeaponAnimationsSets>', '  <WeaponAnimationsSets>']
count = 0
for head, set_name, body in sets:
    m = re.search(r'(        <Item key="WEAPON_FLAREGUN">.*?\n        </Item>)', body, re.S)
    if not m:
        continue
    fallback = re.search(r'<Fallback>([^<]*)</Fallback>', head)
    out.append(f'    <Item key="{set_name}">')
    if fallback:
        out.append(f'      <Fallback>{fallback.group(1)}</Fallback>')
    out.append('      <WeaponAnimations>')
    out.append(m.group(1).replace('WEAPON_FLAREGUN', 'WEAPON_AIRSTRIKEGUN'))
    out.append('      </WeaponAnimations>')
    out.append('    </Item>')
    count += 1
out += ['  </WeaponAnimationsSets>', '</CWeaponAnimationsSets>', '']
if count == 0:
    raise SystemExit('no WEAPON_FLAREGUN animation entries found')
open(os.path.join(out_dir, 'weaponanimations_airstrikegun.meta'), 'w', encoding='utf-8', newline='\n').write('\n'.join(out))
print(f'weapon_airstrikegun.meta: {len(meta)} chars; animation sets cloned: {count} '
      f'({", ".join(s for _, s, b in sets if "WEAPON_FLAREGUN" in b)})')
