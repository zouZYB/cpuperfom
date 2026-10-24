# Build a "ns per element" table from bench_branch's Google Benchmark JSON.
# One row per scenario; speedup is relative to the fastest case of the same
# (group, kernel), i.e. "how much slower than the most predictable variant".
#
#   powershell -ExecutionPolicy Bypass -File scripts/report.ps1
#   powershell -ExecutionPolicy Bypass -File scripts/report.ps1 -Json build/bench.json -GroupFilter pattern
param(
    [string]$Json = "build/bench.json",
    [string]$GroupFilter = ".*"
)

if (-not (Test-Path $Json)) {
    Write-Error "missing $Json ; run: bench_branch --benchmark_format=json --benchmark_out=build/bench.json"
    exit 1
}

$unit = @{ ns = 1.0; us = 1e3; ms = 1e6; s = 1e9 }
$data = Get-Content $Json -Raw | ConvertFrom-Json
$mhzPerCpu = 0.0
if ($data.context -and $data.context.mhz_per_cpu) { $mhzPerCpu = [double]$data.context.mhz_per_cpu }

$agg = $data.benchmarks | Where-Object { $_.run_type -eq 'aggregate' -and $_.name -match '_median$' }
$iters = $data.benchmarks | Where-Object { $_.run_type -eq 'iteration' }

function Parse-N([string]$label, [string]$name) {
    if ($label -and $label -match 'n=(\d+)') { return [int64]$Matches[1] }
    if ($name -and $name -match 'n=(\d+)') { return [int64]$Matches[1] }
    return 0
}

$samples = [ordered]@{}
foreach ($b in $iters) {
    if ($b.name -notmatch '^(?<g>[^/]+)/(?<k>[^/]+)/(?<v>.+)$') { continue }
    $key = $b.name
    if (-not $samples.Contains($key)) {
        $samples[$key] = [pscustomobject]@{
            Name = $b.name; Group = $Matches.g; Kernel = $Matches.k; Variant = $Matches.v
            N = (Parse-N $b.label $b.name); Ns = New-Object System.Collections.Generic.List[double]
        }
    }
    $samples[$key].Ns.Add([double]$b.real_time * $unit[$b.time_unit])
}

$rows = New-Object System.Collections.Generic.List[object]
foreach ($k in $samples.Keys) {
    $s = $samples[$k]
    $vals = @($s.Ns | Sort-Object)
    $med = $vals[[int](($vals.Count - 1) / 2)]
    $n = $s.N
    if ($n -le 0) { continue }
    $rows.Add([pscustomobject]@{
        Name = $s.Name; Group = $s.Group; Kernel = $s.Kernel; Variant = $s.Variant
        N = $n; MedianNs = $med; NsPerElem = $med / $n
    })
}

$groups = $rows | Where-Object { $_.Group -match $GroupFilter } | Group-Object Group
$order = @('data', 'store', 'pattern', 'corr', 'density', 'indirect', 'loop', 'branchless', 'hint', 'call', 'random', 'align')
$groups = $groups | Sort-Object { $i = [array]::IndexOf($order, $_.Name); if ($i -lt 0) { 99 } else { $i } }

Write-Host ("Google Benchmark JSON: {0}" -f $Json)
if ($mhzPerCpu -gt 0) { Write-Host ("CPU {0:N0} MHz, cases={1}" -f $mhzPerCpu, $rows.Count) }
Write-Host ''

foreach ($g in $groups) {
    $items = @($g.Group)
    $basis = @{}
    foreach ($it in $items) {
        $key = "$($it.Kernel)"
        if (-not $basis.ContainsKey($key) -or $it.NsPerElem -lt $basis[$key]) { $basis[$key] = $it.NsPerElem }
    }
    Write-Host ("## {0}  ({1} cases)" -f $g.Name, $items.Count)
    $hdr = '{0,-46} {1,8} {2,10} {3,9} {4,9}' -f 'case', 'n', 'ns/elem', 'cycle/el', 'vs best'
    Write-Host $hdr
    Write-Host ('-' * $hdr.Length)
    foreach ($it in @($items | Sort-Object NsPerElem -Descending)) {
        $cyc = if ($mhzPerCpu -gt 0) { $it.NsPerElem * $mhzPerCpu / 1000.0 } else { 0 }
        $relative = $it.NsPerElem / $basis[$it.Kernel]
        $line = '{0,-46} {1,8} {2,10:N3} {3,9:N2} {4,8:N2}x' -f $it.Name, $it.N, $it.NsPerElem, $cyc, $relative
        Write-Host $line
    }
    Write-Host ''
}

Write-Host 'ns/elem  = median wall time per call / n elements'
Write-Host 'cycle/el = ns/elem x CPU MHz / 1000 (nominal frequency, not measured core clock)'
Write-Host 'vs best  = ns/elem / fastest case of the same kernel (larger = less predictable)'
