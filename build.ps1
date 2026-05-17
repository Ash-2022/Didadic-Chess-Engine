# Enhanced Chess Engine Build Script
param(
    [ValidateSet("Debug", "Release", "ReleaseMax", "Profile", "ProfileGprof", "ASAN", "TSAN", "NoMagics", "TestMagics")]
    [string]$Config = "Release",
    
    [switch]$Clean,
    [switch]$Benchmark,
    [switch]$Compare,
    [string]$CompareWith = "",
    [switch]$Profile,
    [switch]$Verbose,
    [switch]$Install
)

$ErrorActionPreference = "Stop"

# Preset mapping
$presetMap = @{
    "Debug"        = "clang-debug"
    "Release"      = "clang-release"
    "ReleaseMax"   = "clang-release-max"
    "Profile"      = "clang-release-profile"
    "ProfileGprof" = "clang-release-gprof"
    "ASAN"         = "clang-asan"
    "TSAN"         = "clang-tsan"
    "NoMagics"     = "clang-no-magics"
    "TestMagics"   = "clang-test-magics-seed"
}

$preset   = $presetMap[$Config]
$buildDir = "build/$preset"

Write-Host ""
Write-Host "=====================================" -ForegroundColor Cyan
Write-Host " Chess Engine Build System" -ForegroundColor Cyan
Write-Host "=====================================" -ForegroundColor Cyan
Write-Host "Configuration: $Config" -ForegroundColor White
Write-Host "Preset: $preset" -ForegroundColor White
Write-Host ""

# Kill any running processes
Get-Process ninja -ErrorAction SilentlyContinue | Stop-Process -Force
Get-Process chess_engine -ErrorAction SilentlyContinue | Stop-Process -Force

# =========================
# CLEAN
# =========================
if ($Clean) {
    Write-Host "[CLEAN] Removing build directory..." -ForegroundColor Yellow
    
    if (Test-Path $buildDir) {
        Remove-Item $buildDir -Recurse -Force -ErrorAction Stop
        Write-Host "[OK] Build directory cleaned" -ForegroundColor Green
    }
    
    if (Test-Path "$env:LOCALAPPDATA\CMakeBuild") {
        Remove-Item "$env:LOCALAPPDATA\CMakeBuild" -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# =========================
# CONFIGURE
# =========================
Write-Host "[CONFIGURE] Running CMake configuration..." -ForegroundColor Cyan

if ($Verbose) {
    cmake --preset $preset --fresh
} else {
    cmake --preset $preset --fresh | Out-Null
}

if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Configuration failed!" -ForegroundColor Red
    exit 1
}

Write-Host "[OK] Configuration successful" -ForegroundColor Green

# =========================
# BUILD
# =========================
Write-Host "[BUILD] Compiling..." -ForegroundColor Cyan

$buildStart = Get-Date

if ($Verbose) {
    cmake --build --preset $preset
} else {
    cmake --build --preset $preset 2>&1 | Where-Object { $_ -match "error" -or $_ -match "warning" }
}

if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Build failed!" -ForegroundColor Red
    exit 1
}

$buildTime = (Get-Date) - $buildStart
Write-Host "[OK] Build successful ($([math]::Round($buildTime.TotalSeconds, 2))s)" -ForegroundColor Green

# =========================
# INSTALL
# =========================
if ($Install) {
    Write-Host "[INSTALL] Installing to install/bin..." -ForegroundColor Cyan
    cmake --install $buildDir --prefix install | Out-Null
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[ERROR] Installation failed!" -ForegroundColor Red
        exit 1
    }
    
    Write-Host "[OK] Installation successful" -ForegroundColor Green
    $exePath = "install\bin\chess_engine.exe"
} else {
    $exePath = "$buildDir\chess_engine.exe"
}

# Verify executable exists
if (-not (Test-Path $exePath)) {
    Write-Host "[ERROR] Executable not found: $exePath" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "=====================================" -ForegroundColor Green
Write-Host " Build Summary" -ForegroundColor Green
Write-Host "=====================================" -ForegroundColor Green
Write-Host "Executable: $exePath" -ForegroundColor White
Write-Host "Build time: $([math]::Round($buildTime.TotalSeconds, 2))s" -ForegroundColor White

# Get file size
$fileSize = (Get-Item $exePath).Length / 1MB
Write-Host "Size: $([math]::Round($fileSize, 2)) MB" -ForegroundColor White

# =========================
# BENCHMARK
# =========================
if ($Benchmark) {
    Write-Host ""
    Write-Host "=====================================" -ForegroundColor Cyan
    Write-Host " Running Benchmark" -ForegroundColor Cyan
    Write-Host "=====================================" -ForegroundColor Cyan
    
    $output = & $exePath nouci 2>&1 | Out-String
    
    # Parse results
    if ($output -match "Depth 15.*Nodes (\d+).*NPS (\d+).*Time ([\d.e+-]+)s") {
        $nodes = [long]$matches[1]
        $nps = [long]$matches[2]
        $time = [double]$matches[3]
        
        Write-Host ""
        Write-Host "Results:" -ForegroundColor White
        Write-Host "  Nodes: " -NoNewline; Write-Host $nodes.ToString('N0') -ForegroundColor Cyan
        Write-Host "  NPS: " -NoNewline; Write-Host $nps.ToString('N0') -ForegroundColor Cyan
        Write-Host "  Time: " -NoNewline; Write-Host "${time}s" -ForegroundColor Cyan
        
        # Performance rating
        Write-Host ""
        Write-Host "Performance Rating: " -NoNewline
        if ($nps -gt 20000000) {
            Write-Host "Excellent (20M+ NPS)" -ForegroundColor Green
        } elseif ($nps -gt 10000000) {
            Write-Host "Very Good (10-20M NPS)" -ForegroundColor Green
        } elseif ($nps -gt 5000000) {
            Write-Host "Good (5-10M NPS)" -ForegroundColor Yellow
        } else {
            Write-Host "Needs Optimization (<5M NPS)" -ForegroundColor Red
        }
    }
}

# =========================
# COMPARE
# =========================
if ($Compare -and $CompareWith) {
    Write-Host ""
    Write-Host "=====================================" -ForegroundColor Cyan
    Write-Host " Performance Comparison" -ForegroundColor Cyan
    Write-Host "=====================================" -ForegroundColor Cyan
    
    if (-not (Test-Path $CompareWith)) {
        Write-Host "[ERROR] Comparison executable not found: $CompareWith" -ForegroundColor Red
        exit 1
    }
    
    # Run current build
    Write-Host "Running current build..." -ForegroundColor Yellow
    $output1 = & $exePath nouci 2>&1 | Out-String
    
    # Run comparison build
    Write-Host "Running comparison build..." -ForegroundColor Yellow
    $output2 = & $CompareWith nouci 2>&1 | Out-String
    
    # Parse results
    $output1 -match "Depth 15.*Nodes (\d+).*NPS (\d+).*Time ([\d.e+-]+)s" | Out-Null
    $nodes1 = [long]$matches[1]
    $nps1 = [long]$matches[2]
    $time1 = [double]$matches[3]
    
    $output2 -match "Depth 15.*Nodes (\d+).*NPS (\d+).*Time ([\d.e+-]+)s" | Out-Null
    $nodes2 = [long]$matches[1]
    $nps2 = [long]$matches[2]
    $time2 = [double]$matches[3]
    
    # Calculate improvements
    $speedup = $time2 / $time1
    $npsImprovement = (($nps1 / $nps2) - 1) * 100
    
    Write-Host ""
    Write-Host "Metric           Current         Comparison      Change" -ForegroundColor White
    Write-Host "------           -------         ----------      ------" -ForegroundColor Gray
    
    $timeChange = if ($speedup -gt 1.0) { 
        "$([math]::Round(($speedup - 1) * 100, 1))% faster" 
    } else { 
        "$([math]::Round((1 - $speedup) * 100, 1))% slower" 
    }
    $timeColor = if ($speedup -gt 1.0) { "Green" } else { "Red" }
    
    Write-Host ("Time             {0,-15} {1,-15} " -f 
        "$([math]::Round($time1, 3))s",
        "$([math]::Round($time2, 3))s") -NoNewline
    Write-Host $timeChange -ForegroundColor $timeColor
    
    Write-Host ("NPS              {0,-15} {1,-15} {2:+0.0;-0.0}%" -f 
        $nps1.ToString('N0'),
        $nps2.ToString('N0'),
        $npsImprovement)
    
    Write-Host ("Nodes            {0,-15} {1,-15}" -f 
        $nodes1.ToString('N0'),
        $nodes2.ToString('N0'))
    
    Write-Host ""
    Write-Host "Overall: " -NoNewline
    if ($speedup -gt 1.1) {
        Write-Host "Significant improvement!" -ForegroundColor Green
    } elseif ($speedup -gt 1.0) {
        Write-Host "Improvement" -ForegroundColor Green
    } elseif ($speedup -gt 0.95) {
        Write-Host "Roughly equivalent" -ForegroundColor Yellow
    } else {
        Write-Host "Regression" -ForegroundColor Red
    }
}

# =========================
# PROFILING
# =========================
if ($Profile) {
    Write-Host ""
    Write-Host "=====================================" -ForegroundColor Cyan
    Write-Host " Profiling" -ForegroundColor Cyan
    Write-Host "=====================================" -ForegroundColor Cyan
    
    if ($Config -eq "Profile") {
        # LLVM profiling
        Write-Host "Running with LLVM profiling..." -ForegroundColor Yellow
        
        $env:LLVM_PROFILE_FILE = "chess.profraw"
        & $exePath nouci | Out-Null
        Remove-Item Env:\LLVM_PROFILE_FILE
        
        Write-Host "Merging profile data..." -ForegroundColor Yellow
        llvm-profdata merge -output chess.profdata chess.profraw
        
        Write-Host "Generating HTML report..." -ForegroundColor Yellow
        llvm-cov show $exePath -instr-profile chess.profdata -format html -output-dir prof-report
        
        Write-Host "[OK] Profile report generated: prof-report/index.html" -ForegroundColor Green
        Start-Process "prof-report/index.html"
        
    } elseif ($Config -eq "ProfileGprof") {
        # gprof profiling
        Write-Host "Running with gprof..." -ForegroundColor Yellow
        
        & $exePath nouci | Out-Null
        gprof $exePath gmon.out > analysis.txt
        
        Write-Host "[OK] Profile report generated: analysis.txt" -ForegroundColor Green
        
    } else {
        Write-Host "[WARNING] Profiling requires Profile or ProfileGprof configuration" -ForegroundColor Yellow
    }
}

# =========================
# MAGIC SEED TESTING
# =========================
if ($Config -eq "TestMagics") {
    Write-Host ""
    Write-Host "=====================================" -ForegroundColor Cyan
    Write-Host " Magic Number Analysis" -ForegroundColor Cyan
    Write-Host "=====================================" -ForegroundColor Cyan
    
    Write-Host "Testing magic number generation with different seeds..." -ForegroundColor Yellow
    
    # Test with current seed
    $output = & $exePath nouci 2>&1
    
    # Show initialization time
    if ($output -match "Magic Bitboards initialized") {
        Write-Host "[OK] Magic bitboards initialized successfully" -ForegroundColor Green
    }
    
    # Run benchmark to test performance
    if ($output -match "Depth 15.*NPS (\d+)") {
        $nps = [long]$matches[1]
        Write-Host "Performance with current seed: " -NoNewline
        Write-Host $nps.ToString('N0') -ForegroundColor Cyan -NoNewline
        Write-Host " NPS"
    }
}

Write-Host ""
Write-Host "=====================================" -ForegroundColor Green
Write-Host " Build Complete!" -ForegroundColor Green
Write-Host "=====================================" -ForegroundColor Green
Write-Host ""
Write-Host "Next steps:" -ForegroundColor White
Write-Host "  - Run executable: $exePath" -ForegroundColor Gray
Write-Host "  - Benchmark: .\build.ps1 -Config ReleaseMax -Benchmark" -ForegroundColor Gray
Write-Host "  - Compare: .\build.ps1 -Compare -CompareWith path\to\old.exe" -ForegroundColor Gray
Write-Host "  - Profile: .\build.ps1 -Config Profile -Profile" -ForegroundColor Gray
Write-Host ""
