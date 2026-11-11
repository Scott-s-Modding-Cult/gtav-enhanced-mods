# Converts a folder of CodeWalker XML (*.ydd.xml / *.ytd.xml, each with a texture folder named after the
# file stem) to Enhanced game files and packs them as an overlay DLC for a *streamed* ped
# (e.g. mp_f_freemode_01), i.e. dlc.rpf\x64\models\cdimages\streamedpeds_mp.rpf\<PedFolder>\<files>.
#   & make-streamed-dlc.ps1 -XmlDir E:\GTAV-Modding\build\mpf_sfm_normal\xml -PackName mpf_sfm_body
param(
    [Parameter(Mandatory)] [string] $XmlDir,
    [Parameter(Mandatory)] [string] $PackName,
    [string] $PedFolder = 'mp_f_freemode_01',
    [string] $RpfName = 'streamedpeds_mp.rpf',
    [int] $Order = 1000,
    [string] $OutRoot = 'E:\GTAV-Modding\build\dlcpacks',
    [string] $GameDir = 'E:\SteamLibrary\steamapps\common\Grand Theft Auto V Enhanced',
    [string] $CodeWalkerDir = 'D:\Development\CodeWalker30_dev48\CodeWalker30_dev48'
)
$ErrorActionPreference = 'Stop'
Get-ChildItem $CodeWalkerDir -Filter 'SharpDX*.dll' | ForEach-Object { [void][Reflection.Assembly]::LoadFrom($_.FullName) }
[void][Reflection.Assembly]::LoadFrom((Join-Path $CodeWalkerDir 'CodeWalker.Core.dll'))
[CodeWalker.GameFiles.GTA5Keys]::LoadFromPath($GameDir, $true, $null)
[CodeWalker.GameFiles.RpfManager]::IsGen9 = $true
Push-Location $CodeWalkerDir
try {
    $files = [ordered]@{}
    foreach ($x in Get-ChildItem $XmlDir -Filter '*.xml' | Sort-Object Name) {
        $name = $x.Name -replace '\.xml$', ''                  # e.g. jbib_000_u.ydd
        $stem = [IO.Path]::GetFileNameWithoutExtension($name)  # e.g. jbib_000_u
        $texDir = Join-Path $XmlDir $stem
        $text = [IO.File]::ReadAllText($x.FullName)
        switch ([IO.Path]::GetExtension($name)) {
            '.ydd' { $obj = [CodeWalker.GameFiles.XmlYdd]::GetYdd($text, $texDir); $chk = New-Object CodeWalker.GameFiles.YddFile }
            '.ytd' { $obj = [CodeWalker.GameFiles.XmlYtd]::GetYtd($text, $texDir); $chk = New-Object CodeWalker.GameFiles.YtdFile }
            default { continue }
        }
        $bytes = $obj.Save()
        $chk.Load($bytes)
        $info = if ($chk -is [CodeWalker.GameFiles.YddFile]) {
            $d = $chk.Drawables[0]; $v = 0; foreach ($m in $d.DrawableModels.High) { foreach ($g in $m.Geometries) { $v += $g.VerticesCount } }
            "$v verts high LOD, $($d.ShaderGroup.Shaders.data_items.Count) shaders, embedded textures: " + $(if ($d.ShaderGroup.TextureDictionary) { $d.ShaderGroup.TextureDictionary.Textures.data_items.Count } else { 0 })
        } else { ($chk.TextureDict.Textures.data_items | ForEach-Object { "$($_.Width)x$($_.Height) $($_.Format) m$($_.Levels)" }) -join ', ' }
        Write-Host ("{0,-28} {1,10:N0} B  v{2}  {3}" -f $name, $bytes.Length, [BitConverter]::ToUInt32($bytes, 4), $info)
        $files[$name] = $bytes
    }

    $packDir = Join-Path $OutRoot $PackName
    if (Test-Path $packDir) { Remove-Item $packDir -Recurse -Force }
    New-Item -ItemType Directory -Force $packDir | Out-Null
    $dlc = [CodeWalker.GameFiles.RpfFile]::CreateNew($OutRoot, "$PackName\dlc.rpf", [CodeWalker.GameFiles.RpfEncryption]::OPEN)
    $cd = [CodeWalker.GameFiles.RpfFile]::CreateDirectory([CodeWalker.GameFiles.RpfFile]::CreateDirectory([CodeWalker.GameFiles.RpfFile]::CreateDirectory($dlc.Root, 'x64'), 'models'), 'cdimages')
    $inner = [CodeWalker.GameFiles.RpfFile]::CreateNew($cd, $RpfName, [CodeWalker.GameFiles.RpfEncryption]::OPEN)
    $pedDir = [CodeWalker.GameFiles.RpfFile]::CreateDirectory($inner.Root, $PedFolder)
    foreach ($k in $files.Keys) { [void][CodeWalker.GameFiles.RpfFile]::CreateFile($pedDir, $k, $files[$k]) }

    $device = "dlc_$PackName"
    $path = "${device}:/%PLATFORM%/models/cdimages/$RpfName"
    $content = @"
<?xml version="1.0" encoding="utf-8"?>
<CDataFileMgr__ContentsOfDataFileXml>
  <disabledFiles />
  <includedXmlFiles />
  <dataFiles>
    <Item>
      <filename>$path</filename>
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
        <Item>$path</Item>
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
  <order value="$Order" />
</SSetupData>
"@
    $utf8 = New-Object Text.UTF8Encoding $false
    [void][CodeWalker.GameFiles.RpfFile]::CreateFile($dlc.Root, 'content.xml', $utf8.GetBytes($content))
    [void][CodeWalker.GameFiles.RpfFile]::CreateFile($dlc.Root, 'setup2.xml', $utf8.GetBytes($setup))
    Write-Host ("Packed {0} files -> {1}\dlc.rpf ({2:N1} MB)" -f $files.Count, $packDir, ((Get-Item "$packDir\dlc.rpf").Length / 1MB))
} finally { Pop-Location }
