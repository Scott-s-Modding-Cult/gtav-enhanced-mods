# Exports files matching a regex (on the entry path) from the game archives or a standalone .rpf.
# Each match is written raw (game format) and as CodeWalker XML (+ .dds textures) where supported.
#   -Source game              search the whole game install
#   -Source <path\to\x.rpf>   search one archive (and archives nested in it)
# Example:
#   & rpf-export.ps1 -Source game -Pattern 'mp_f_freemode_01\.yft$' -OutDir E:\GTAV-Modding\work\freemode
param(
    [Parameter(Mandatory)] [string] $Pattern,
    [Parameter(Mandatory)] [string] $OutDir,
    [string] $Source = 'game',
    [switch] $Legacy,          # set for archives in Legacy (pre-Enhanced) resource format
    [string] $GameDir = 'E:\SteamLibrary\steamapps\common\Grand Theft Auto V Enhanced',
    [string] $CodeWalkerDir = 'D:\Development\CodeWalker30_dev48\CodeWalker30_dev48'
)
$ErrorActionPreference = 'Stop'
Get-ChildItem $CodeWalkerDir -Filter 'SharpDX*.dll' | ForEach-Object { [void][Reflection.Assembly]::LoadFrom($_.FullName) }
[void][Reflection.Assembly]::LoadFrom((Join-Path $CodeWalkerDir 'CodeWalker.Core.dll'))
[CodeWalker.GameFiles.GTA5Keys]::LoadFromPath($GameDir, $true, $null)
[CodeWalker.GameFiles.RpfManager]::IsGen9 = -not $Legacy   # resource layout of standalone .rpf files

$rpfs = New-Object System.Collections.Generic.List[object]
if ($Source -eq 'game') {
    $rpfman = New-Object CodeWalker.GameFiles.RpfManager
    $rpfman.Init($GameDir, $true, [Action[string]]{ param($s) }, [Action[string]]{ param($s) }, $false, $true)
    $rpfman.AllRpfs | ForEach-Object { $rpfs.Add($_) }
} else {
    $f = New-Object CodeWalker.GameFiles.RpfFile($Source, [IO.Path]::GetFileName($Source))
    $f.ScanStructure([Action[string]]{ param($s) }, [Action[string]]{ param($s) })
    $stack = New-Object System.Collections.Stack; $stack.Push($f)
    while ($stack.Count) { $r = $stack.Pop(); $rpfs.Add($r); if ($r.Children) { $r.Children | ForEach-Object { $stack.Push($_) } } }
}

$rawDir = Join-Path $OutDir 'raw'; $xmlDir = Join-Path $OutDir 'xml'
New-Item -ItemType Directory -Force $rawDir, $xmlDir | Out-Null
$seen = @{}
foreach ($rpf in $rpfs) {
    if (-not $rpf.AllEntries) { continue }
    foreach ($e in $rpf.AllEntries) {
        if ($e -isnot [CodeWalker.GameFiles.RpfFileEntry]) { continue }
        if ($e.Path -notmatch $Pattern) { continue }
        $key = $e.Name.ToLowerInvariant()
        if ($seen.ContainsKey($key)) { Write-Host "  (skipping duplicate $($e.Path))"; continue }
        $seen[$key] = $true
        Write-Host $e.Path
        $data = $e.File.ExtractFile($e)
        $raw = $data
        if ($e -is [CodeWalker.GameFiles.RpfResourceFileEntry]) {
            $raw = [CodeWalker.GameFiles.ResourceBuilder]::AddResourceHeader($e, [CodeWalker.GameFiles.ResourceBuilder]::Compress($data))
        }
        [IO.File]::WriteAllBytes((Join-Path $rawDir $e.Name), $raw)
        try {
            $fn = ''
            $xml = [CodeWalker.GameFiles.MetaXml]::GetXml($e, $data, [ref]$fn, $xmlDir)
            if ($xml) { [IO.File]::WriteAllText((Join-Path $xmlDir $fn), $xml, (New-Object Text.UTF8Encoding $false)) }
        } catch { Write-Host "   XML export failed: $($_.Exception.Message)" }
    }
}
Write-Host "Done: $($seen.Count) files."
