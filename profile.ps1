# ===============================
# LLVM PROFILING SCRIPT
# ===============================

$ExePath  = "build/clang-release-profile\chess_engine.exe"
$ExeArgs  = "nouci"
$Runs     = 5
$RawDir   = ".\profraw"
$ProfData = ".\engine.profdata"
$HtmlDir  = ".\prof-report"

Write-Host "=== LLVM Profiling Started ==="

# Safety check
if (!(Test-Path $ExePath)) {
    Write-Error "Executable not found: $ExePath"
    exit 1
}

# Clean old data
Remove-Item -Recurse -Force $RawDir, $ProfData, $HtmlDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $RawDir | Out-Null

# Run executable N times
for ($i = 1; $i -le $Runs; $i++) {
    $env:LLVM_PROFILE_FILE = "$RawDir\engine_run_${i}.profraw"
    Write-Host "Run $i / $Runs"
    & $ExePath $ExeArgs
}

# Clear env var
Remove-Item Env:\LLVM_PROFILE_FILE

# Merge profraw → profdata
Write-Host "Merging profile data..."
llvm-profdata merge $RawDir\*.profraw -o $ProfData

if (!(Test-Path $ProfData)) {
    Write-Error "Failed to generate engine.profdata"
    exit 1
}

# Generate HTML report
Write-Host "Generating HTML report..."
llvm-cov show $ExePath `
    -instr-profile $ProfData `
    -format html `
    -output-dir $HtmlDir

if (!(Test-Path "$HtmlDir\index.html")) {
    Write-Error "HTML report generation failed"
    exit 1
}

# Open report
Write-Host "Opening report..."
Start-Process "$HtmlDir\index.html"

Write-Host "=== LLVM Profiling Complete ==="
