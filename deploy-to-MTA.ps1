# Deploy or revert the mtasa-blue build against the MTA-SA1.6 install.
#
# Usage:
#   .\deploy-to-MTA.ps1 backup     # back up originals only (no copy)
#   .\deploy-to-MTA.ps1 install    # back up originals if missing, copy built DLLs in
#   .\deploy-to-MTA.ps1 revert     # restore originals from backup
#   .\deploy-to-MTA.ps1 status     # show current state and whether a backup exists
#
# Originals are stashed next to the install as `.orig`. The backup is only
# created once and never overwritten, so repeated install/revert cycles always
# recover the pristine pre-modification DLL.

param(
    [Parameter(Position=0)]
    [ValidateSet('backup','install','revert','status')]
    [string]$Action = 'status',

    # When set, operate on the Debug build's _d.dll / _d.exe variants. These
    # live alongside the Release files at different filenames, so no backup is
    # needed on install and revert simply deletes the debug files.
    [switch]$Debug,

    # When set (Release only), mirror the entire Bin\ tree into the install —
    # every matching .dll/.exe under the root, mta\, and mods\deathmatch\ gets
    # replaced, with `.orig` backups created for any file being overwritten
    # for the first time. Revert walks the same dirs and restores every .orig.
    # Use this when ABI drift between our build and the installed release
    # causes unexplained crashes — a full mirror guarantees a matched set.
    [switch]$Full
)

$BuildRoot   = 'Z:\More games\GTA San Andreas\mtasa-blue\Bin'
$InstallRoot = 'Z:\More games\GTA San Andreas\MTA-SA1.6'

if ($Debug) {
    # Debug variants install alongside the Release files under *_d.* names.
    # Launch `Multi Theft Auto_d.exe` to run the debug build.
    $Targets = @(
        @{ Name='launcher_d.exe'; Src="$BuildRoot\Multi Theft Auto_d.exe";             Dst="$InstallRoot\Multi Theft Auto_d.exe" },
        @{ Name='loader_d.dll';   Src="$BuildRoot\mta\loader_d.dll";                   Dst="$InstallRoot\mta\loader_d.dll" },
        @{ Name='core_d.dll';     Src="$BuildRoot\mta\core_d.dll";                     Dst="$InstallRoot\mta\core_d.dll" },
        @{ Name='netc_d.dll';     Src="$BuildRoot\mta\netc_d.dll";                     Dst="$InstallRoot\mta\netc_d.dll" },
        @{ Name='client_d.dll';   Src="$BuildRoot\mods\deathmatch\client_d.dll";       Dst="$InstallRoot\mods\deathmatch\client_d.dll" }
    )
    $SymbolTargets = @(
        @{ Name='launcher_d.pdb'; Src="$BuildRoot\Multi Theft Auto_d.pdb";             Dst="$InstallRoot\Multi Theft Auto_d.pdb" },
        @{ Name='loader_d.pdb';   Src="$BuildRoot\mta\loader_d.pdb";                   Dst="$InstallRoot\mta\loader_d.pdb" },
        @{ Name='core_d.pdb';     Src="$BuildRoot\mta\core_d.pdb";                     Dst="$InstallRoot\mta\core_d.pdb" },
        @{ Name='client_d.pdb';   Src="$BuildRoot\mods\deathmatch\client_d.pdb";       Dst="$InstallRoot\mods\deathmatch\client_d.pdb" }
    )
} else {
    $Targets = @(
        # Launcher + loader together bypass the CL29 integrity check. The released
        # loader.dll verifies core.dll against a pinned MD5; our dev loader doesn't.
        @{ Name='launcher.exe'; Src="$BuildRoot\Multi Theft Auto.exe";             Dst="$InstallRoot\Multi Theft Auto.exe" },
        @{ Name='loader.dll';   Src="$BuildRoot\mta\loader.dll";                   Dst="$InstallRoot\mta\loader.dll" },
        @{ Name='core.dll';     Src="$BuildRoot\mta\core.dll";                     Dst="$InstallRoot\mta\core.dll" },
        # Network module: core.dll is compiled against a specific netc.dll ABI
        # ("Expected 0xNNNN" error). The prebuilt sits in our build tree from
        # win-install-data.bat; just mirror that version over.
        @{ Name='netc.dll';     Src="$BuildRoot\mta\netc.dll";                     Dst="$InstallRoot\mta\netc.dll" },
        @{ Name='client.dll';   Src="$BuildRoot\mods\deathmatch\client.dll";       Dst="$InstallRoot\mods\deathmatch\client.dll" }
    )

    # PDBs are optional - copied when present so DebugView / WinDbg resolve
    # symbols. They're not required for the build to run.
    $SymbolTargets = @(
        @{ Name='launcher.pdb'; Src="$BuildRoot\Multi Theft Auto.pdb";             Dst="$InstallRoot\Multi Theft Auto.pdb" },
        @{ Name='loader.pdb';   Src="$BuildRoot\mta\loader.pdb";                   Dst="$InstallRoot\mta\loader.pdb" },
        @{ Name='core.pdb';     Src="$BuildRoot\mta\core.pdb";                     Dst="$InstallRoot\mta\core.pdb" },
        @{ Name='client.pdb';   Src="$BuildRoot\mods\deathmatch\client.pdb";       Dst="$InstallRoot\mods\deathmatch\client.pdb" }
    )
}

function Assert-ClientClosed {
    $running = Get-Process -Name 'Multi Theft Auto' -ErrorAction SilentlyContinue
    if ($running) {
        Write-Error ("MTA client is running (PID " + $running.Id + "). Close it first - the DLLs are locked while it's live.")
        exit 1
    }
}

function Get-FileHashSafe($path) {
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
}

# Directories the full mirror walks. Non-recursive, pattern-filtered so we only
# touch runtime files — never PDBs/LIBs/EXPs/logs, never user data, never the
# GTA data files that live alongside them.
$MirrorDirs = @(
    @{ Src = "$BuildRoot";                 Dst = "$InstallRoot";                    Patterns = @('*.exe') },
    @{ Src = "$BuildRoot\mta";             Dst = "$InstallRoot\mta";                Patterns = @('*.dll', '*.exe') },
    @{ Src = "$BuildRoot\mods\deathmatch"; Dst = "$InstallRoot\mods\deathmatch";    Patterns = @('*.dll') }
)

# Mirror one directory (non-recursive, pattern-filtered). Backs up every
# destination file being overwritten if no `.orig` exists yet.
function Mirror-Dir($entry) {
    if (-not (Test-Path -LiteralPath $entry.Src)) {
        Write-Host ("Skip dir     " + $entry.Src + " (source dir missing)")
        return
    }
    if (-not (Test-Path -LiteralPath $entry.Dst)) {
        Write-Host ("Skip dir     " + $entry.Dst + " (install dir missing)")
        return
    }

    $files = Get-ChildItem -LiteralPath $entry.Src -File
    foreach ($pattern in $entry.Patterns) {
        $matched = $files | Where-Object { $_.Name -like $pattern -and $_.Name -notlike '*.orig' }
        foreach ($f in $matched) {
            $dstFile = Join-Path $entry.Dst $f.Name
            $backup  = "$dstFile.orig"

            if (Test-Path -LiteralPath $dstFile) {
                if (-not (Test-Path -LiteralPath $backup)) {
                    Copy-Item -LiteralPath $dstFile -Destination $backup -Force
                    Write-Host ("Backed up    " + $f.Name + " -> " + $backup)
                }
            }
            Copy-Item -LiteralPath $f.FullName -Destination $dstFile -Force
            Write-Host ("Installed    " + $f.Name + " -> " + $dstFile)
        }
    }
}

# Walk all mirror dirs and restore every `.orig` sidecar we find, then delete
# the sidecars. Files that were newly added by install (no prior .orig) are
# left in place — user can delete them manually if needed.
function Revert-Full {
    foreach ($entry in $MirrorDirs) {
        if (-not (Test-Path -LiteralPath $entry.Dst)) { continue }
        $origs = Get-ChildItem -LiteralPath $entry.Dst -Filter "*.orig" -File -ErrorAction SilentlyContinue
        foreach ($orig in $origs) {
            $dstFile = $orig.FullName.Substring(0, $orig.FullName.Length - 5)
            Copy-Item -LiteralPath $orig.FullName -Destination $dstFile -Force
            Remove-Item -LiteralPath $orig.FullName -Force
            Write-Host ("Restored     " + (Split-Path $dstFile -Leaf) + " in " + $entry.Dst)
        }
    }
}

# Stash the currently-installed file to `<Dst>.orig` if no backup exists yet.
# Idempotent: once a backup is present, leaves it alone.
function Save-Backup($target) {
    $backup = "$($target.Dst).orig"
    if (-not (Test-Path -LiteralPath $target.Dst)) {
        Write-Host ("Skip backup  " + $target.Name + " (nothing installed at " + $target.Dst + ")")
        return
    }
    if (Test-Path -LiteralPath $backup) {
        Write-Host ("Skip backup  " + $target.Name + " (already at " + $backup + ")")
        return
    }
    Copy-Item -LiteralPath $target.Dst -Destination $backup -Force
    Write-Host ("Backed up    " + $target.Name + " -> " + $backup)
}

switch ($Action) {
    'backup' {
        if ($Debug) {
            Write-Host "Debug-mode files are add-only (no collision with Release); nothing to back up."
            return
        }
        Assert-ClientClosed
        foreach ($t in $Targets) {
            Save-Backup $t
        }
    }

    'install' {
        Assert-ClientClosed
        if ($Full) {
            if ($Debug) {
                Write-Error "-Full is only supported for Release mode (drop -Debug)."
                exit 1
            }
            foreach ($entry in $MirrorDirs) { Mirror-Dir $entry }
            Write-Host ""
            Write-Host "Full mirror installed. Launch: $InstallRoot\Multi Theft Auto.exe"
            return
        }
        foreach ($t in $Targets) {
            if (-not (Test-Path -LiteralPath $t.Src)) {
                Write-Error ("Built artifact not found: " + $t.Src + ". Did the build finish?")
                exit 1
            }

            # Debug: add-only, never back up (the _d files don't exist in the
            # pristine install, so there's nothing the user would want restored).
            if (-not $Debug) {
                Save-Backup $t
            }

            Copy-Item -LiteralPath $t.Src -Destination $t.Dst -Force
            Write-Host ("Installed    " + $t.Name + " from " + $t.Src)
        }
        foreach ($s in $SymbolTargets) {
            if (Test-Path -LiteralPath $s.Src) {
                Copy-Item -LiteralPath $s.Src -Destination $s.Dst -Force
                Write-Host "Installed   $($s.Name) (symbols)"
            }
        }
        Write-Host ""
        Write-Host "Done. Launch: $InstallRoot\Multi Theft Auto.exe"
    }

    'revert' {
        Assert-ClientClosed
        if ($Full) {
            if ($Debug) {
                Write-Error "-Full is only supported for Release mode (drop -Debug)."
                exit 1
            }
            Revert-Full
            return
        }
        if ($Debug) {
            # Debug files are add-only. Revert = delete them.
            foreach ($t in $Targets) {
                if (Test-Path -LiteralPath $t.Dst) {
                    Remove-Item -LiteralPath $t.Dst -Force
                    Write-Host ("Removed     " + $t.Name)
                }
            }
            foreach ($s in $SymbolTargets) {
                if (Test-Path -LiteralPath $s.Dst) {
                    Remove-Item -LiteralPath $s.Dst -Force
                    Write-Host ("Removed     " + $s.Name)
                }
            }
            return
        }
        foreach ($t in $Targets) {
            $backup = "$($t.Dst).orig"
            if (Test-Path -LiteralPath $backup) {
                Copy-Item -LiteralPath $backup -Destination $t.Dst -Force
                Remove-Item -LiteralPath $backup -Force
                Write-Host "Restored    $($t.Name) from $backup"
            } else {
                Write-Host "No backup for $($t.Name) (nothing to revert)"
            }
        }
        # PDBs weren't in the pristine install — just remove the ones we dropped.
        foreach ($s in $SymbolTargets) {
            if (Test-Path -LiteralPath $s.Dst) {
                Remove-Item -LiteralPath $s.Dst -Force
                Write-Host "Removed     $($s.Name) (build-only symbol file)"
            }
        }
    }

    'status' {
        Write-Host ('file'.PadRight(12) + ' ' + 'installed'.PadRight(24) + ' ' + 'backup')
        Write-Host ('----'.PadRight(12) + ' ' + '---------'.PadRight(24) + ' ' + '------')
        foreach ($t in $Targets) {
            $installed  = Get-FileHashSafe $t.Dst
            $built      = Get-FileHashSafe $t.Src
            $backupPath = "$($t.Dst).orig"
            $hasBackup  = Test-Path -LiteralPath $backupPath

            if (-not $installed) {
                $installedLabel = 'MISSING'
            } elseif ($installed -eq $built) {
                $installedLabel = 'modified (this build)'
            } else {
                $installedLabel = 'unknown / original'
            }

            if ($hasBackup) {
                $backupLabel = 'present'
            } else {
                $backupLabel = 'none'
            }

            Write-Host ($t.Name.PadRight(12) + ' ' + $installedLabel.PadRight(24) + ' ' + $backupLabel)
        }
    }
}
