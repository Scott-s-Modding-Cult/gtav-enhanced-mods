# Packs the WEAPON_AIRSTRIKEGUN add-on weapon (made by make-airstrikegun-meta.py) as an Enhanced DLC:
#   dlc.rpf\content.xml, setup2.xml, common\data\ai\weapon_airstrikegun.meta,
#   common\data\ai\weaponanimations_airstrikegun.meta
# then copies it to <game>\mods\update\x64\dlcpacks\airstrikegun\dlc.rpf (register with add-dlc-to-list.ps1).
#   & make-airstrikegun-dlc.ps1
param(
    [string] $MetaDir = 'E:\GTAV-Modding\build\airstrikegun\meta',
    [string] $PackName = 'airstrikegun',
    [int] $Order = 1001,                # after mpheist (10), which owns the flare gun's components/anims
    [string] $OutRoot = 'E:\GTAV-Modding\build\dlcpacks',
    [string] $GameDir = 'E:\SteamLibrary\steamapps\common\Grand Theft Auto V Enhanced',
    [string] $CodeWalkerDir = 'D:\Development\CodeWalker30_dev48\CodeWalker30_dev48',
    [switch] $NoInstall
)
$ErrorActionPreference = 'Stop'
Get-ChildItem $CodeWalkerDir -Filter 'SharpDX*.dll' | ForEach-Object { [void][Reflection.Assembly]::LoadFrom($_.FullName) }
[void][Reflection.Assembly]::LoadFrom((Join-Path $CodeWalkerDir 'CodeWalker.Core.dll'))
[CodeWalker.GameFiles.GTA5Keys]::LoadFromPath($GameDir, $true, $null)
[CodeWalker.GameFiles.RpfManager]::IsGen9 = $true

$device = "dlc_$PackName"
$weaponFile = 'weapon_airstrikegun.meta'
$animFile = 'weaponanimations_airstrikegun.meta'
$content = @"
<?xml version="1.0" encoding="utf-8"?>
<CDataFileMgr__ContentsOfDataFileXml>
  <disabledFiles />
  <includedXmlFiles />
  <includedDataFiles />
  <dataFiles>
    <Item>
      <filename>${device}:/common/data/ai/$animFile</filename>
      <fileType>WEAPON_ANIMATIONS_FILE</fileType>
      <overlay value="false" />
      <disabled value="true" />
      <persistent value="false" />
    </Item>
    <Item>
      <filename>${device}:/common/data/ai/$weaponFile</filename>
      <fileType>WEAPONINFO_FILE</fileType>
      <overlay value="false" />
      <disabled value="true" />
      <persistent value="false" />
    </Item>
  </dataFiles>
  <contentChangeSets>
    <Item>
      <changeSetName>${PackName}_AUTOGEN</changeSetName>
      <filesToDisable />
      <filesToEnable>
        <Item>${device}:/common/data/ai/$animFile</Item>
        <Item>${device}:/common/data/ai/$weaponFile</Item>
      </filesToEnable>
      <txdToLoad />
      <txdToUnload />
      <residentResources />
      <unregisterResources />
    </Item>
  </contentChangeSets>
  <patchFiles />
</CDataFileMgr__ContentsOfDataFileXml>
"@
$setup = @"
<?xml version="1.0" encoding="UTF-8"?>
<SSetupData>
  <deviceName>$device</deviceName>
  <datFile>content.xml</datFile>
  <timeStamp>11/11/2026 00:00:00</timeStamp>
  <nameHash>$PackName</nameHash>
  <contentChangeSetGroups>
    <Item>
      <NameHash>GROUP_STARTUP</NameHash>
      <ContentChangeSets>
        <Item>${PackName}_AUTOGEN</Item>
      </ContentChangeSets>
    </Item>
  </contentChangeSetGroups>
  <type>EXTRACONTENT_COMPAT_PACK</type>
  <order value="$Order" />
</SSetupData>
"@

$packDir = Join-Path $OutRoot $PackName
if (Test-Path $packDir) { Remove-Item $packDir -Recurse -Force }
New-Item -ItemType Directory -Force $packDir | Out-Null
$utf8 = New-Object Text.UTF8Encoding $false
$dlc = [CodeWalker.GameFiles.RpfFile]::CreateNew($OutRoot, "$PackName\dlc.rpf", [CodeWalker.GameFiles.RpfEncryption]::OPEN)
[void][CodeWalker.GameFiles.RpfFile]::CreateFile($dlc.Root, 'content.xml', $utf8.GetBytes($content))
[void][CodeWalker.GameFiles.RpfFile]::CreateFile($dlc.Root, 'setup2.xml', $utf8.GetBytes($setup))
$ai = [CodeWalker.GameFiles.RpfFile]::CreateDirectory([CodeWalker.GameFiles.RpfFile]::CreateDirectory([CodeWalker.GameFiles.RpfFile]::CreateDirectory($dlc.Root, 'common'), 'data'), 'ai')
foreach ($f in $weaponFile, $animFile) {
    [void][CodeWalker.GameFiles.RpfFile]::CreateFile($ai, $f, [IO.File]::ReadAllBytes((Join-Path $MetaDir $f)))
}

# Read it back to make sure the archive is well-formed.
$chk = New-Object CodeWalker.GameFiles.RpfFile((Join-Path $packDir 'dlc.rpf'), 'dlc.rpf')
$chk.ScanStructure($null, $null)
Write-Host ("Packed {0} ({1} entries, {2:N0} bytes, encryption {3}):" -f $chk.Path, $chk.AllEntries.Count, (Get-Item "$packDir\dlc.rpf").Length, $chk.Encryption)
$chk.AllEntries | Where-Object { $_ -is [CodeWalker.GameFiles.RpfFileEntry] } | ForEach-Object { Write-Host "   $($_.Path)" }

if (-not $NoInstall) {
    $dest = Join-Path $GameDir "mods\update\x64\dlcpacks\$PackName"
    New-Item -ItemType Directory -Force $dest | Out-Null
    Copy-Item "$packDir\dlc.rpf" "$dest\dlc.rpf" -Force
    Write-Host "Installed -> $dest\dlc.rpf"
}
