param([int]$TargetPid)
# Retired: the experiment used invalid cross-bitness addresses, an incorrect
# calling convention and unverified handle enumeration. It never proved cleanup.
throw "DISABLED unsafe hook-stripping experiment. No process was opened or modified. Use run-mouse-rate-sampler-3cycles.ps1 for diagnostics."
