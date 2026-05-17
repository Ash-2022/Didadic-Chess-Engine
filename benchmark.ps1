# Chess Engine Benchmark Script
param(
    [int]$Runs = 3,
    [string]$Executable = "install\bin\chess_engine.exe",
    [switch]$Verbose,
    [switch]$Compare,
    [string]$CompareWith = "",
    [string]$OutputCsv = ""
)

$ErrorActionPreference = "Stop"

function Write-Step {
    param([string]$Message)
    Write-Host "`n[$Message]" -ForegroundColor Cyan
}

function Write-Metric {
    param([string]$Name, [string]$Value, [string]$Color = "Gray")
    $padding = " " * (15 - $Name.Length)
    Write-Host "  $Name$padding : " -NoNewline -ForegroundColor Gray
    Write-Host $Value -ForegroundColor $Color
}

function Parse-EngineOutput {
    param([string]$Output)
    
    $depth10 = $Output | Select-String "Depth 10.*" | Select-Object -Last 1
    $finalMove = $Output | Select-String "Final best move: (\S+)"
    
    $result = @{
        Nodes = 0
        NPS = 0
        Score = 0
        BestMove = "?"
        Depth10Time = 0
    }
    
    if ($depth10) {
        if ($depth10 -match "Nodes (\d+)") { 
            $result.Nodes = [long]$matches[1] 
        }
        if ($depth10 -match "NPS (\d+)") { 
            $result.NPS = [long]$matches[1] 
        }
        if ($depth10 -match "Score (-?\d+)") { 
            $result.Score = [int]$matches[1] 
        }
    }
    
    if ($finalMove -and $finalMove -match "Final best move: (\S+)") {
        $result.BestMove = $matches[1]
    }
    
    return $result
}

function Run-Benchmark {
    param(
        [string]$ExePath,
        [string]$Name = "Current"
    )
    
    Write-Step "Benchmarking: $Name"
    Write-Metric "Executable" $ExePath
    Write-Metric "Runs" $Runs
    
    $results = @()
    
    for ($i = 1; $i -le $Runs; $i++) {
        Write-Host "`n  Run $i/$Runs..." -ForegroundColor Yellow
        
        $output = ""
        $time = Measure-Command {
            $output = & $ExePath 2>&1 | Out-String
        }
        
        if ($Verbose) {
            Write-Host $output -ForegroundColor DarkGray
        }
        
        $parsed = Parse-EngineOutput $output
        
        $result = [PSCustomObject]@{
            Run = $i
            Time = [math]::Round($time.TotalSeconds, 2)
            Nodes = $parsed.Nodes
            NPS = $parsed.NPS
            Score = $parsed.Score
            BestMove = $parsed.BestMove
        }
        
        $results += $result
        
        Write-Metric "Time" "$($result.Time)s" "White"
        Write-Metric "Nodes" $result.Nodes.ToString('N0') "White"
        Write-Metric "NPS" $result.NPS.ToString('N0') "White"
        Write-Metric "Best Move" $result.BestMove "White"
    }
    
    # Calculate statistics
    $avgTime = ($results.Time | Measure-Object -Average).Average
    $minTime = ($results.Time | Measure-Object -Minimum).Minimum
    $maxTime = ($results.Time | Measure-Object -Maximum).Maximum
    $stdDevTime = [math]::Sqrt((($results.Time | ForEach-Object { [math]::Pow($_ - $avgTime, 2) } | Measure-Object -Sum).Sum / $Runs))
    
    $avgNodes = ($results.Nodes | Measure-Object -Average).Average
    $avgNPS = ($results.NPS | Measure-Object -Average).Average
    
    Write-Host "`n  Statistics:" -ForegroundColor Cyan
    Write-Metric "Avg Time" "$([math]::Round($avgTime, 2))s" "Green"
    Write-Metric "Min Time" "$([math]::Round($minTime, 2))s" "Green"
    Write-Metric "Max Time" "$([math]::Round($maxTime, 2))s" "Green"
    Write-Metric "Std Dev" "$([math]::Round($stdDevTime, 2))s" "Green"
    Write-Metric "Avg Nodes" $avgNodes.ToString('N0') "Green"
    Write-Metric "Avg NPS" $avgNPS.ToString('N0') "Green"
    
    return @{
        Name = $Name
        Results = $results
        AvgTime = $avgTime
        MinTime = $minTime
        MaxTime = $maxTime
        StdDev = $stdDevTime
        AvgNodes = $avgNodes
        AvgNPS = $avgNPS
    }
}

# Main execution
Write-Host "`n=====================================" -ForegroundColor Cyan
Write-Host " Chess Engine Benchmark" -ForegroundColor Cyan
Write-Host "=====================================" -ForegroundColor Cyan

# Check if executable exists
if (-not (Test-Path $Executable)) {
    Write-Host "`n✗ Error: Executable not found!" -ForegroundColor Red
    Write-Host "  Path: $Executable" -ForegroundColor Yellow
    Write-Host "  Run: .\build.ps1 first" -ForegroundColor Yellow
    exit 1
}

# Run main benchmark
$mainResults = Run-Benchmark -ExePath $Executable -Name "Current Build"

# Compare mode
if ($Compare -and $CompareWith) {
    if (-not (Test-Path $CompareWith)) {
        Write-Host "`n✗ Error: Comparison executable not found!" -ForegroundColor Red
        Write-Host "  Path: $CompareWith" -ForegroundColor Yellow
        exit 1
    }
    
    $compareResults = Run-Benchmark -ExePath $CompareWith -Name "Comparison Build"
    
    # Show comparison
    Write-Host "`n=====================================" -ForegroundColor Cyan
    Write-Host " Comparison" -ForegroundColor Cyan
    Write-Host "=====================================" -ForegroundColor Cyan
    
    $speedup = $compareResults.AvgTime / $mainResults.AvgTime
    $nodeReduction = (1 - ($mainResults.AvgNodes / $compareResults.AvgNodes)) * 100
    $npsImprovement = (($mainResults.AvgNPS / $compareResults.AvgNPS) - 1) * 100
    
    Write-Host "`n  Metric                Current         Comparison      Change" -ForegroundColor White
    Write-Host "  ────────────────────────────────────────────────────────────────" -ForegroundColor Gray
    
    $timeChange = if ($speedup > 1.0) { 
        "$([math]::Round(($speedup - 1) * 100, 1))% faster" 
    } else { 
        "$([math]::Round((1 - $speedup) * 100, 1))% slower" 
    }
    $timeColor = if ($speedup > 1.0) { "Green" } else { "Red" }
    
    Write-Host ("  Time (avg)        {0,-15} {1,-15} " -f 
        "$([math]::Round($mainResults.AvgTime, 2))s",
        "$([math]::Round($compareResults.AvgTime, 2))s") -NoNewline
    Write-Host $timeChange -ForegroundColor $timeColor
    
    Write-Host ("  Nodes             {0,-15} {1,-15} {2}" -f 
        $mainResults.AvgNodes.ToString('N0'),
        $compareResults.AvgNodes.ToString('N0'),
        "$([math]::Round($nodeReduction, 1))%")
    
    Write-Host ("  NPS               {0,-15} {1,-15} {2}" -f 
        $mainResults.AvgNPS.ToString('N0'),
        $compareResults.AvgNPS.ToString('N0'),
        "$([math]::Round($npsImprovement, 1))%")
    
    Write-Host "`n  Overall: " -NoNewline -ForegroundColor Cyan
    if ($speedup > 1.05) {
        Write-Host "Current build is significantly faster! 🚀" -ForegroundColor Green
    } elseif ($speedup > 1.0) {
        Write-Host "Current build is slightly faster" -ForegroundColor Green
    } elseif ($speedup > 0.95) {
        Write-Host "Performance is roughly equivalent" -ForegroundColor Yellow
    } else {
        Write-Host "Comparison build is faster" -ForegroundColor Red
    }
}

# Save results to CSV
if ($OutputCsv) {
    $mainResults.Results | Export-Csv -Path $OutputCsv -NoTypeInformation
    Write-Host "`n✓ Results saved to: $OutputCsv" -ForegroundColor Green
}

# Performance rating
Write-Host "`n=====================================" -ForegroundColor Cyan
Write-Host " Performance Rating" -ForegroundColor Cyan
Write-Host "=====================================" -ForegroundColor Cyan

$nps = $mainResults.AvgNPS

if ($nps -gt 10000000) {
    Write-Host "  ⭐⭐⭐⭐⭐ Excellent! (10M+ NPS)" -ForegroundColor Green
} elseif ($nps -gt 7000000) {
    Write-Host "  ⭐⭐⭐⭐ Very Good (7-10M NPS)" -ForegroundColor Green
} elseif ($nps -gt 5000000) {
    Write-Host "  ⭐⭐⭐ Good (5-7M NPS)" -ForegroundColor Yellow
} elseif ($nps -gt 3000000) {
    Write-Host "  ⭐⭐ Fair (3-5M NPS)" -ForegroundColor Yellow
} else {
    Write-Host "  ⭐ Needs Optimization (<3M NPS)" -ForegroundColor Red
}

Write-Host "`nBest move consistency: " -NoNewline
$uniqueMoves = ($mainResults.Results.BestMove | Select-Object -Unique).Count
if ($uniqueMoves -eq 1) {
    Write-Host "Perfect (all runs agree)" -ForegroundColor Green
} elseif ($uniqueMoves -eq 2) {
    Write-Host "Good (minor variation)" -ForegroundColor Yellow
} else {
    Write-Host "Unstable (multiple different moves)" -ForegroundColor Red
}

Write-Host "`n=====================================" -ForegroundColor Cyan
