# Converts a Legacy-format DLC pack (dlc.rpf) to GTA V Enhanced format.
# Every .ydd/.ytd/.yft/.ydr resource goes Legacy binary -> CodeWalker XML -> Enhanced binary;
# other files are copied. The nested-archive layout is kept, and content.xml/setup2.xml are
# regenerated as a clean overlay pack (all cdimages\*.rpf enabled at startup).
#   & convert-legacy-dlc.ps1 -Source <legacy dlc.rpf> -PackName mpnaked
# Output: <OutRoot>\<PackName>\dlc.rpf
param(
    [Parameter(Mandatory)] [string] $Source,
    [Parameter(Mandatory)] [string] $PackName,
    [string] $OutRoot = 'E:\GTAV-Modding\build\dlcpacks',
    [int] $Order = 999,
    [string] $GameDir = 'E:\SteamLibrary\steamapps\common\Grand Theft Auto V Enhanced',
    [string] $CodeWalkerDir = 'D:\Development\CodeWalker30_dev48\CodeWalker30_dev48'
)
$ErrorActionPreference = 'Stop'
Get-ChildItem $CodeWalkerDir -Filter 'SharpDX*.dll' | ForEach-Object { [void][Reflection.Assembly]::LoadFrom($_.FullName) }
[void][Reflection.Assembly]::LoadFrom((Join-Path $CodeWalkerDir 'CodeWalker.Core.dll'))
[CodeWalker.GameFiles.GTA5Keys]::LoadFromPath($GameDir, $true, $null)
$GF = 'CodeWalker.GameFiles'
Push-Location $CodeWalkerDir
try {
    [CodeWalker.GameFiles.RpfManager]::IsGen9 = $false
    $src = New-Object CodeWalker.GameFiles.RpfFile($Source, 'dlc.rpf')
    $src.ScanStructure([Action[string]]{ param($s) }, [Action[string]]{ param($s) Write-Host "scan error: $s" })

    $tmp = Join-Path $env:TEMP "cw_convert_$PackName"
    if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
    $packDir = Join-Path $OutRoot $PackName
    if (Test-Path $packDir) { Remove-Item $packDir -Recurse -Force }
    New-Item -ItemType Directory -Force $tmp, $packDir | Out-Null
    $dst = [CodeWalker.GameFiles.RpfFile]::CreateNew($OutRoot, "$PackName\dlc.rpf", [CodeWalker.GameFiles.RpfEncryption]::OPEN)

    $dirCache = @{}
    function Get-Dir($rpfRoot, $relDir) {
        $key = "$($rpfRoot.GetHashCode())|$relDir"
        if ($dirCache.ContainsKey($key)) { return $dirCache[$key] }
        $d = $rpfRoot
        if ($relDir) {
            foreach ($part in $relDir.Split('\')) {
                $next = $d.Directories | Where-Object { $_.Name -eq $part } | Select-Object -First 1
                if (-not $next) { $next = [CodeWalker.GameFiles.RpfFile]::CreateDirectory($d, $part) }
                $d = $next
            }
        }
        $dirCache[$key] = $d
        return $d
    }

    $types = @{ '.ydd' = 'Ydd'; '.ytd' = 'Ytd'; '.yft' = 'Yft'; '.ydr' = 'Ydr' }
    $stats = @{ converted = 0; copied = 0; failed = 0 }
    $innerRpfs = New-Object System.Collections.Generic.List[string]

    function Convert-Archive($srcRpf, $dstRoot) {
        foreach ($e in $srcRpf.AllEntries) {
            if ($e -isnot [CodeWalker.GameFiles.RpfFileEntry]) { continue }
            $rel = $e.Path.Substring($srcRpf.Path.Length + 1)          # path inside this archive
            $relDir = Split-Path $rel -Parent
            $ext = [IO.Path]::GetExtension($e.Name).ToLowerInvariant()
            if ($ext -eq '.rpf') {
                $child = $srcRpf.Children | Where-Object { $_.Path -eq $e.Path } | Select-Object -First 1
                $newChild = [CodeWalker.GameFiles.RpfFile]::CreateNew((Get-Dir $dstRoot $relDir), $e.Name, [CodeWalker.GameFiles.RpfEncryption]::OPEN)
                $innerRpfs.Add($rel)
                Convert-Archive $child $newChild.Root
                continue
            }
            if ($srcRpf.Path -eq 'dlc.rpf' -and $e.Name -in 'content.xml', 'setup2.xml') { continue }  # regenerated
            [CodeWalker.GameFiles.RpfManager]::IsGen9 = $false
            $data = $e.File.ExtractFile($e)
            if ($types.ContainsKey($ext)) {
                try {
                    $stem = [IO.Path]::GetFileNameWithoutExtension($e.Name)
                    $work = Join-Path $tmp ([guid]::NewGuid().ToString('N'))
                    New-Item -ItemType Directory $work | Out-Null
                    $fn = ''
                    $xml = [CodeWalker.GameFiles.MetaXml]::GetXml($e, $data, [ref]$fn, $work)
                    [CodeWalker.GameFiles.RpfManager]::IsGen9 = $true
                    $t = $types[$ext]
                    $obj = Invoke-Expression "[$GF.Xml$t]::Get$t(`$xml, (Join-Path `$work `$stem))"
                    $bytes = $obj.Save()
                    $chk = New-Object "$GF.$($t)File"
                    $chk.Load($bytes)                                  # must reload as Enhanced
                    [void][CodeWalker.GameFiles.RpfFile]::CreateFile((Get-Dir $dstRoot $relDir), $e.Name, $bytes)
                    Remove-Item $work -Recurse -Force
                    $stats.converted++
                } catch {
                    $stats.failed++
                    Write-Host "FAILED $($e.Path): $($_.Exception.Message)"
                }
            } else {
                [void][CodeWalker.GameFiles.RpfFile]::CreateFile((Get-Dir $dstRoot $relDir), $e.Name, $data)
                $stats.copied++
            }
        }
    }
    Convert-Archive $src $dst.Root

    # Clean content.xml / setup2.xml: every inner archive is an overlay enabled at startup.
    $device = "dlc_$PackName"
    $items = ($innerRpfs | ForEach-Object { $p = $_.Replace('\', '/').Replace('x64/', '%PLATFORM%/'); @"
    <Item>
      <filename>${device}:/$p</filename>
      <fileType>RPF_FILE</fileType>
      <locked value="true"/>
      <disabled value="true"/>
      <persistent value="true"/>
      <overlay value="true"/>
    </Item>
"@ }) -join "`n"
    $enable = ($innerRpfs | ForEach-Object { "        <Item>${device}:/" + $_.Replace('\', '/').Replace('x64/', '%PLATFORM%/') + "</Item>" }) -join "`n"
    $content = @"
<?xml version="1.0" encoding="utf-8"?>
<CDataFileMgr__ContentsOfDataFileXml>
  <disabledFiles />
  <includedXmlFiles />
  <dataFiles>
$items
  </dataFiles>
  <contentChangeSets>
    <Item>
      <changeSetName>${PackName}_STREAMING</changeSetName>
      <filesToEnable>
$enable
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
    [void][CodeWalker.GameFiles.RpfFile]::CreateFile($dst.Root, 'content.xml', $utf8.GetBytes($content))
    [void][CodeWalker.GameFiles.RpfFile]::CreateFile($dst.Root, 'setup2.xml', $utf8.GetBytes($setup))
    Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host ("Converted {0}, copied {1}, failed {2}; {3} inner archives -> {4}\dlc.rpf ({5:N1} MB)" -f `
        $stats.converted, $stats.copied, $stats.failed, $innerRpfs.Count, $packDir, ((Get-Item "$packDir\dlc.rpf").Length / 1MB))
} finally { Pop-Location }
