param([string[]]$BuildArguments, [string]$BuildLog)
$buildInfo = [Diagnostics.ProcessStartInfo]::new()
$buildInfo.FileName = (Get-Command cmake).Source
$buildInfo.UseShellExecute = $false
$buildInfo.RedirectStandardOutput = $true
$buildInfo.RedirectStandardError = $true
$buildInfo.Environment.Clear()
foreach ($buildEnvItem in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
  $buildInfo.Environment[([string]$buildEnvItem.Key).ToUpperInvariant()] = [string]$buildEnvItem.Value
}
$cleanBuildPath = $buildInfo.Environment["PATH"]
$buildInfo.Environment.Remove("PATH") | Out-Null
$buildInfo.Environment["Path"] = $cleanBuildPath
$buildInfo.Environment["MSBUILDDISABLENODEREUSE"] = "1"
foreach ($buildArgument in $BuildArguments) { $buildInfo.ArgumentList.Add($buildArgument) }
$buildProcess = [Diagnostics.Process]::Start($buildInfo)
$buildStdout = $buildProcess.StandardOutput.ReadToEndAsync()
$buildStderr = $buildProcess.StandardError.ReadToEndAsync()
$buildProcess.WaitForExit()
$buildOutput = $buildStdout.Result + $buildStderr.Result
[IO.File]::WriteAllText((Join-Path $PWD $BuildLog), $buildOutput, [Text.UTF8Encoding]::new($false))
$buildOutput -split '\r?\n' | Select-Object -Last 8
exit $buildProcess.ExitCode