$ErrorActionPreference = 'SilentlyContinue'

function Get-DirSize($path) {
    $total = 0L
    foreach ($f in [System.IO.Directory]::EnumerateFiles($path, "*", [System.IO.SearchOption]::AllDirectories)) {
        try { $total += ([System.IO.FileInfo]::new($f)).Length } catch {}
    }
    return $total
}

# Scan top-level user folders
$root = "C:\Users\mslag"
$folders = Get-ChildItem $root -Directory -Force -ErrorAction SilentlyContinue
$results = @()
foreach ($d in $folders) {
    $size = Get-DirSize $d.FullName
    if ($size -gt 100MB) {
        $results += [PSCustomObject]@{Folder=$d.Name; GB=[math]::Round($size/1GB,2)}
    }
}
Write-Output "=== User Profile Folders > 100 MB ==="
$results | Sort-Object GB -Descending | Format-Table -AutoSize

# Also check some common system space hogs
Write-Output ""
Write-Output "=== Common System Space Hogs ==="

$systemPaths = @(
    "C:\Windows\Temp",
    "C:\Windows\SoftwareDistribution",
    "C:\ProgramData\Package Cache",
    "$env:LOCALAPPDATA\Temp",
    "$env:LOCALAPPDATA\Microsoft\WindowsApps",
    "$env:LOCALAPPDATA\CrashDumps",
    "C:\`$Recycle.Bin"
)

foreach ($p in $systemPaths) {
    if (Test-Path $p) {
        $size = Get-DirSize $p
        if ($size -gt 50MB) {
            Write-Output "$p : $([math]::Round($size/1GB,2)) GB"
        }
    }
}

# Check for large AppData subfolders
Write-Output ""
Write-Output "=== AppData\Local subfolders > 500 MB ==="
$localDirs = Get-ChildItem "$env:LOCALAPPDATA" -Directory -Force -ErrorAction SilentlyContinue
foreach ($d in $localDirs) {
    $size = Get-DirSize $d.FullName
    if ($size -gt 500MB) {
        Write-Output "$($d.Name): $([math]::Round($size/1GB,2)) GB"
    }
}

Write-Output ""
Write-Output "=== AppData\Roaming subfolders > 500 MB ==="
$roamingDirs = Get-ChildItem "$env:APPDATA" -Directory -Force -ErrorAction SilentlyContinue
foreach ($d in $roamingDirs) {
    $size = Get-DirSize $d.FullName
    if ($size -gt 500MB) {
        Write-Output "$($d.Name): $([math]::Round($size/1GB,2)) GB"
    }
}

Write-Output ""
Write-Output "=== Top-level C:\ folders > 1 GB ==="
$cDirs = Get-ChildItem "C:\" -Directory -Force -ErrorAction SilentlyContinue | Where-Object { $_.Name -notin @('Windows','Program Files','Program Files (x86)','Users','PerfLogs','Recovery') }
foreach ($d in $cDirs) {
    $size = Get-DirSize $d.FullName
    if ($size -gt 1GB) {
        Write-Output "$($d.Name): $([math]::Round($size/1GB,2)) GB"
    }
}

Write-Output ""
Write-Output "=== Done ==="
