# Build a speedup table from bench_simd's Google Benchmark JSON.
# One row per (kernel, size); columns are speedups vs the scalar baseline.
#
#   powershell -ExecutionPolicy Bypass -File scripts/report.ps1
#   powershell -ExecutionPolicy Bypass -File scripts/report.ps1 -Json build/other.json -KernelFilter BM_Dot
param(
    [string]$Json = "build/bench.json",
    [string]$KernelFilter = ".*"
)

if (-not (Test-Path $Json)) {
    Write-Error "missing $Json ; run: bench_simd --benchmark_format=json --benchmark_out=build/bench.json"
    exit 1
}

$unit = @{ ns = 1.0; us = 1e3; ms = 1e6; s = 1e9 }
$data = Get-Content $Json -Raw | ConvertFrom-Json
$iterations = $data.benchmarks | Where-Object { $_.run_type -eq 'iteration' }

$samples = @{}
foreach ($b in $iterations) {
    if ($b.name -notmatch '^(?<k>[^/]+)/(?<v>[^/]+)/(?<a>.+)$') { continue }
    $key = "$($Matches.k)|$($Matches.a)|$($Matches.v)"
    if (-not $samples.ContainsKey($key)) { $samples[$key] = New-Object System.Collections.Generic.List[double] }
    $samples[$key].Add([double]$b.real_time * $unit[$b.time_unit])
}

function Get-Median([System.Collections.Generic.List[double]]$xs) {
    $arr = @($xs | Sort-Object)
    $n = $arr.Count
    if ($n -eq 0) { return [double]::NaN }
    if ($n % 2 -eq 1) { return [double]$arr[[int](($n - 1) / 2)] }
    return ([double]$arr[[int]($n / 2 - 1)] + [double]$arr[[int]($n / 2)]) / 2.0
}

$rows = @{}
$variants = New-Object System.Collections.Generic.List[string]
foreach ($key in $samples.Keys) {
    $parts = $key -split '\|'
    $kernel = $parts[0]; $args = $parts[1]; $variant = $parts[2]
    if ($kernel -notmatch $KernelFilter) { continue }
    if (-not $rows.ContainsKey("$kernel|$args")) { $rows["$kernel|$args"] = @{} }
    $rows["$kernel|$args"][$variant] = Get-Median $samples[$key]
    if (-not $variants.Contains($variant)) { $variants.Add($variant) }
}

$order = @('scalar', 'auto_sse2', 'auto_native', 'sse41', 'avx2', 'avx512')
$variants = @($variants | Sort-Object { $i = [array]::IndexOf($order, $_); if ($i -lt 0) { 99 } else { $i } })

$keys = @($rows.Keys | Sort-Object {
    $p = $_ -split '\|'
    $num = [int64]([regex]::Match($p[1], '(\d+)\s*$').Groups[1].Value)
    '{0}|{1:D18}' -f $p[0], $num
})

$hdr = '{0,-16} {1,-22} {2,10}' -f 'kernel', 'args', 'scalar(ns)'
foreach ($v in $variants) { if ($v -ne 'scalar') { $hdr += '{0,12}' -f $v } }
Write-Host $hdr
Write-Host ('-' * $hdr.Length)

foreach ($key in $keys) {
    $p = $key -split '\|'
    $kernel = $p[0]; $args = $p[1]
    if (-not $rows[$key].ContainsKey('scalar')) { continue }
    $base = $rows[$key]['scalar']
    $line = '{0,-16} {1,-22} {2,10:N1}' -f $kernel, $args, $base
    foreach ($v in $variants) {
        if ($v -eq 'scalar') { continue }
        if ($rows[$key].ContainsKey($v) -and $base -gt 0) {
            $line += '{0,11:N2}x' -f ($base / $rows[$key][$v])
        } else {
            $line += '{0,12}' -f '-'
        }
    }
    Write-Host $line
}

Write-Host ''
Write-Host 'speedup = scalar median / variant median; >1 means faster.'
