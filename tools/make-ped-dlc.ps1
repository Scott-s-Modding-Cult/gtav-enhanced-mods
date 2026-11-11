# Converts edited ped XML (CodeWalker format) back to Enhanced game files and packs them as a DLC pack
# that overrides the ped's original model and textures.
#   & make-ped-dlc.ps1 -XmlDir E:\GTAV-Modding\build\a_f_y_beach_01_nude\xml -Ped a_f_y_beach_01 -PackName nude_npcs
# Output: <OutRoot>\<PackName>\dlc.rpf   (copy to <game>\mods\update\x64\dlcpacks\<PackName>\)
#         <OutRoot>\raw\<ped>.ydd/.ytd    (the converted files, for inspection)
param(
    [Parameter(Mandatory)] [string[]] $XmlDir,     # one folder per ped
    [Parameter(Mandatory)] [string[]] $Ped,        # matching ped names
    [string] $PackName = 'nude_npcs',
    [string] $OutRoot = 'E:\GTAV-Modding\build\dlcpacks',
    [string] $GameDir = 'E:\SteamLibrary\steamapps\common\Grand Theft Auto V Enhanced',
    [string] $CodeWalkerDir = 'D:\Development\CodeWalker30_dev48\CodeWalker30_dev48'
)
$ErrorActionPreference = 'Stop'
Get-ChildItem $CodeWalkerDir -Filter 'SharpDX*.dll' | ForEach-Object { [void][Reflection.Assembly]::LoadFrom($_.FullName) }
[void][Reflection.Assembly]::LoadFrom((Join-Path $CodeWalkerDir 'CodeWalker.Core.dll'))
[CodeWalker.GameFiles.GTA5Keys]::LoadFromPath($GameDir, $true, $null)
[CodeWalker.GameFiles.RpfManager]::IsGen9 = $true
Push-Location $CodeWalkerDir   # CodeWalker looks for ShadersGen9Conversion.xml next to itself
try {
    $rawDir = Join-Path $OutRoot 'raw'
    New-Item -ItemType Directory -Force $rawDir | Out-Null
    $files = @{}
    for ($i = 0; $i -lt $Ped.Count; $i++) {
        $dir, $name = $XmlDir[$i], $Ped[$i]
        $texDir = Join-Path $dir $name

        $ydd = [CodeWalker.GameFiles.XmlYdd]::GetYdd([IO.File]::ReadAllText((Join-Path $dir "$name.ydd.xml")), $texDir)
        $yddBytes = $ydd.Save()
        $ytd = [CodeWalker.GameFiles.XmlYtd]::GetYtd([IO.File]::ReadAllText((Join-Path $dir "$name.ytd.xml")), $texDir)
        $ytdBytes = $ytd.Save()

        foreach ($pair in @(@("$name.ydd", $yddBytes, [CodeWalker.GameFiles.YddFile]), @("$name.ytd", $ytdBytes, [CodeWalker.GameFiles.YtdFile]))) {
            $fn, $bytes, $type = $pair
            [IO.File]::WriteAllBytes((Join-Path $rawDir $fn), $bytes)
            # Round-trip check: the saved file must load again
            $chk = New-Object $type
            $chk.Load($bytes)
            $count = if ($chk -is [CodeWalker.GameFiles.YddFile]) { "$($chk.Drawables.Length) drawables" } else { "$($chk.TextureDict.Textures.Count) textures" }
            Write-Host ("{0}: {1:N0} bytes, version {2}, reloads OK ({3})" -f $fn, $bytes.Length, [BitConverter]::ToUInt32($bytes, 4), $count)
            $files[$fn] = $bytes
        }
    }

    # ---- DLC pack
    $packDir = Join-Path $OutRoot $PackName
    if (Test-Path $packDir) { Remove-Item $packDir -Recurse -Force }
    New-Item -ItemType Directory -Force $packDir | Out-Null
    $dlc = [CodeWalker.GameFiles.RpfFile]::CreateNew($OutRoot, "$PackName\dlc.rpf", [CodeWalker.GameFiles.RpfEncryption]::OPEN)
    $device = "dlc_$PackName"
    $content = @"
<?xml version="1.0" encoding="utf-8"?>
<CDataFileMgr__ContentsOfDataFileXml>
  <disabledFiles />
  <includedXmlFiles />
  <dataFiles>
    <Item>
      <filename>${device}:/%PLATFORM%/models/cdimages/$PackName.rpf</filename>
      <fileType>RPF_FILE</fileType>
      <locked value="true"/>
      <disabled value="true"/>
      <persistent value="true"/>
      <overlay value="true"/>
    </Item>
  </dataFiles>
  <contentChangeSets>
    <Item>
      <changeSetName>${PackName}_STREAMING</changeSetName>
      <filesToEnable>
        <Item>${device}:/%PLATFORM%/models/cdimages/$PackName.rpf</Item>
      </filesToEnable>
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
  <nameHash>$PackName</nameHash>
  <contentChangeSetGroups>
    <Item>
      <NameHash>GROUP_STARTUP</NameHash>
      <ContentChangeSets>
        <Item>${PackName}_STREAMING</Item>
      </ContentChangeSets>
    </Item>
  </contentChangeSetGroups>
  <type>EXTRACONTENT_COMPAT_PACK</type>
  <order value="40" />
</SSetupData>
"@
    $utf8 = New-Object Text.UTF8Encoding $false
    [void][CodeWalker.GameFiles.RpfFile]::CreateFile($dlc.Root, 'content.xml', $utf8.GetBytes($content))
    [void][CodeWalker.GameFiles.RpfFile]::CreateFile($dlc.Root, 'setup2.xml', $utf8.GetBytes($setup))
    $x64 = [CodeWalker.GameFiles.RpfFile]::CreateDirectory($dlc.Root, 'x64')
    $models = [CodeWalker.GameFiles.RpfFile]::CreateDirectory($x64, 'models')
    $cd = [CodeWalker.GameFiles.RpfFile]::CreateDirectory($models, 'cdimages')
    $inner = [CodeWalker.GameFiles.RpfFile]::CreateNew($cd, "$PackName.rpf", [CodeWalker.GameFiles.RpfEncryption]::OPEN)
    foreach ($fn in $files.Keys) { [void][CodeWalker.GameFiles.RpfFile]::CreateFile($inner.Root, $fn, $files[$fn]) }
    Write-Host "Packed $($files.Count) files into $packDir\dlc.rpf"
} finally { Pop-Location }
