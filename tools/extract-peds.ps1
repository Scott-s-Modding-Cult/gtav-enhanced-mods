# Extracts ped models from GTA V Enhanced using CodeWalker.Core.
# Usage (Windows PowerShell 5.1, needs .NET Framework):
#   powershell -ExecutionPolicy Bypass -Command "& extract-peds.ps1 -Peds 'a_f_y_beach_01','a_m_y_beach_01'"
#   powershell -ExecutionPolicy Bypass -Command "& extract-peds.ps1 -Prefix a_c_ -OutDir E:\GTAV-Modding\peds\animals"
# Output: <OutDir>\<ped>\raw  (game-format .yft/.ydd/.ytd/.ymt)
#         <OutDir>\<ped>\xml  (CodeWalker XML + DDS textures, importable with Sollumz)
param(
    [string[]] $Peds = @(),
    [string] $Prefix,          # export every ped whose model name starts with this (e.g. a_c_ = animals)
    [string] $GameDir = 'E:\SteamLibrary\steamapps\common\Grand Theft Auto V Enhanced',
    [string] $CodeWalkerDir = 'D:\Development\CodeWalker30_dev48\CodeWalker30_dev48',
    [string] $OutDir = 'E:\GTAV-Modding\peds'
)
$ErrorActionPreference = 'Stop'

Get-ChildItem $CodeWalkerDir -Filter 'SharpDX*.dll' | ForEach-Object { [void][Reflection.Assembly]::LoadFrom($_.FullName) }
[void][Reflection.Assembly]::LoadFrom((Join-Path $CodeWalkerDir 'CodeWalker.Core.dll'))

Write-Host 'Loading keys from game executable...'
[CodeWalker.GameFiles.GTA5Keys]::LoadFromPath($GameDir, $true, $null)

Write-Host 'Indexing archives (takes a minute or two)...'
$rpfman = New-Object CodeWalker.GameFiles.RpfManager
$errors = New-Object System.Collections.Generic.List[string]
$rpfman.Init($GameDir, $true, [Action[string]]{ param($s) }, [Action[string]]{ param($s) $errors.Add($s) }, $false, $true)
Write-Host ("Indexed {0} archives, {1} errors" -f $rpfman.AllRpfs.Count, $errors.Count)
$errors | Select-Object -First 5 | ForEach-Object { Write-Host "  err: $_" }

$wanted = '.yft', '.ydd', '.ytd', '.ymt'
$want = @{}; foreach ($p in $Peds) { $want[$p.ToLowerInvariant()] = $true }
$pre = if ($Prefix) { $Prefix.ToLowerInvariant() } else { $null }
# One pass over every archive: model-level files (<ped>.yft/.ydd/.ytd/.ymt) only, not per-item streamed files
$byPed = @{}
foreach ($rpf in $rpfman.AllRpfs) {
    if (-not $rpf.AllEntries) { continue }
    foreach ($e in $rpf.AllEntries) {
        if ($e -isnot [CodeWalker.GameFiles.RpfFileEntry]) { continue }
        $n = $e.Name.ToLowerInvariant()
        $ext = [IO.Path]::GetExtension($n)
        if ($wanted -notcontains $ext) { continue }
        $stem = [IO.Path]::GetFileNameWithoutExtension($n)
        # streamed peds keep their per-item meshes/textures in a folder named after the model
        $folder = if ($e.Parent) { $e.Parent.Name.ToLowerInvariant() } else { '' }
        $owner = if ($want.ContainsKey($stem) -or ($pre -and $stem.StartsWith($pre))) { $stem }
                 elseif ($want.ContainsKey($folder) -or ($pre -and $folder.StartsWith($pre))) { $folder }
                 else { $null }
        if ($owner) {
            if (-not $byPed.ContainsKey($owner)) { $byPed[$owner] = New-Object System.Collections.Generic.List[object] }
            $byPed[$owner].Add($e)
        }
    }
}
if ($pre) { Write-Host ("Prefix '{0}': {1} models found" -f $Prefix, $byPed.Count) }
$all = @($Peds | ForEach-Object { $_.ToLowerInvariant() }) + @($byPed.Keys | Sort-Object) | Select-Object -Unique
foreach ($ped in $all) {
    $found = $byPed[$ped]
    if (-not $found) { Write-Host "[$ped] NOT FOUND"; continue }

    $rawDir = Join-Path $OutDir "$ped\raw"
    $xmlDir = Join-Path $OutDir "$ped\xml"
    New-Item -ItemType Directory -Force $rawDir, $xmlDir | Out-Null

    # Keep the first copy of each file name (base game before patches/DLC duplicates)
    foreach ($e in ($found | Sort-Object { $_.Path.Length } | Group-Object { $_.Name.ToLowerInvariant() } | ForEach-Object { $_.Group[0] })) {
        Write-Host "[$ped] $($e.Path)"
        $data = $e.File.ExtractFile($e)
        if (-not $data) { Write-Host '   extract failed'; continue }

        $raw = $data
        if ($e -is [CodeWalker.GameFiles.RpfResourceFileEntry]) {
            $raw = [CodeWalker.GameFiles.ResourceBuilder]::AddResourceHeader($e, [CodeWalker.GameFiles.ResourceBuilder]::Compress($data))
        }
        [IO.File]::WriteAllBytes((Join-Path $rawDir $e.Name), $raw)

        try {
            $fn = ''
            $xml = [CodeWalker.GameFiles.MetaXml]::GetXml($e, $data, [ref]$fn, $xmlDir)
            if ($xml) {
                [IO.File]::WriteAllText((Join-Path $xmlDir $fn), $xml, (New-Object Text.UTF8Encoding $false))
            } else { Write-Host '   (no XML export for this type)' }
        } catch { Write-Host "   XML export failed: $($_.Exception.Message)" }
    }
}
Write-Host 'Done.'
