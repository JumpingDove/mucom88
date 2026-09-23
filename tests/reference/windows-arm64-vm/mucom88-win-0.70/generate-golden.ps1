[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$RepositoryRoot,

    [Parameter(Mandatory = $true)]
    [string]$OutputRoot,

    [Parameter(Mandatory = $true)]
    [string]$Hypervisor,

    [Parameter(Mandatory = $true)]
    [string]$HypervisorVersion,

    [Parameter(Mandatory = $true)]
    [string]$SnapshotName,

    [Parameter(Mandatory = $true)]
    [string]$HostMacModel,

    [Parameter(Mandatory = $true)]
    [string]$HostSoc,

    [Parameter(Mandatory = $true)]
    [string]$HostMacOS,

    [switch]$AllowEnvironmentMismatch
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"

$BaselineId = "mucom88-win-0.70"
$BaselineSourceCommit = "535a65c472ac67abe5f9a852092ee755e4c202c6"
$RenderSeconds = 1
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$Ascii = [System.Text.Encoding]::ASCII

$ExpectedInputs = @(
    [ordered]@{
        path = "package/mucom88.exe"
        size = 329728
        sha256 = "f46ea25e733cf176d2a2c54fbc9a2451660c2a94f862f96d2c984dbdfff9876d"
    },
    [ordered]@{
        path = "package/sampl1.muc"
        size = 1940
        sha256 = "8194fd26cee9be5f60bc57b9c6c819ce89f774bee7f6640a2ba0e21b47f898e4"
    },
    [ordered]@{
        path = "package/sampl2.muc"
        size = 3968
        sha256 = "0e09a4a2a475408be30e65cc28c37ace52a7d591a8b7f49e00f36ba43afed409"
    },
    [ordered]@{
        path = "package/sampl3.muc"
        size = 1283
        sha256 = "ff1ae3b5e3dbe8a66a165ecd286d4f71f5c3d7e36ba6f92d2c84d61506e21617"
    },
    [ordered]@{
        path = "package/mucom88.mub"
        size = 1309
        sha256 = "aae62a128a1197fb66a30a3bc905b21bc177a280e4b245bc878718d0dadc0787"
    },
    [ordered]@{
        path = "package/mucompcm.bin"
        size = 63640
        sha256 = "29e3a31a38388eaa7cf93fb00af85e806f393a9ea5e26342f6996c8ab4af0609"
    },
    [ordered]@{
        path = "package/voice.dat"
        size = 8192
        sha256 = "5a1c7121804d3e486949357d122792cb2d9cb33d18e481ae0a1a283a367c5a0f"
    }
)

$Cases = @(
    [ordered]@{
        id = "sample1-auto"
        source = "sampl1.muc"
        driver = $null
        skip_pcm = $false
        render = $true
        reference_mub = $null
    },
    [ordered]@{
        id = "sample1-mucom88"
        source = "sampl1.muc"
        driver = "mucom88"
        skip_pcm = $false
        render = $true
        reference_mub = $null
    },
    [ordered]@{
        id = "sample1-mucom88e"
        source = "sampl1.muc"
        driver = "mucom88E"
        skip_pcm = $false
        render = $true
        reference_mub = $null
    },
    [ordered]@{
        id = "sample1-mucom88em"
        source = "sampl1.muc"
        driver = "mucom88EM"
        skip_pcm = $false
        render = $true
        reference_mub = $null
    },
    [ordered]@{
        id = "sample2-auto"
        source = "sampl2.muc"
        driver = $null
        skip_pcm = $true
        render = $false
        reference_mub = $null
    },
    [ordered]@{
        id = "sample3-auto"
        source = "sampl3.muc"
        driver = $null
        skip_pcm = $true
        render = $false
        reference_mub = $null
    },
    [ordered]@{
        id = "reference-mub"
        source = $null
        driver = $null
        skip_pcm = $false
        render = $true
        reference_mub = "mucom88.mub"
    }
)

function Write-Utf8Json {
    param(
        [Parameter(Mandatory = $true)]$Value,
        [Parameter(Mandatory = $true)][string]$Path
    )
    $json = $Value | ConvertTo-Json -Depth 16
    [System.IO.File]::WriteAllText($Path, $json + "`r`n", $Utf8NoBom)
}

function Write-AsciiText {
    param(
        [Parameter(Mandatory = $true)][string]$Value,
        [Parameter(Mandatory = $true)][string]$Path
    )
    [System.IO.File]::WriteAllText($Path, $Value, $Ascii)
}

function Get-Sha256 {
    param([Parameter(Mandatory = $true)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-ByteRangeSha256 {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][int]$Offset,
        [Parameter(Mandatory = $true)][int]$Length
    )
    if ($Offset -lt 0 -or $Length -lt 0 -or
        ([long]$Offset + [long]$Length) -gt $Bytes.LongLength) {
        throw "Byte range is outside the file: offset=$Offset length=$Length size=$($Bytes.LongLength)"
    }
    $slice = New-Object byte[] $Length
    if ($Length -gt 0) {
        [Array]::Copy($Bytes, $Offset, $slice, 0, $Length)
    }
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $digest = $sha.ComputeHash($slice)
        return [BitConverter]::ToString($digest).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $sha.Dispose()
    }
}

function Get-AsciiString {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][int]$Offset,
        [Parameter(Mandatory = $true)][int]$Length
    )
    return $Ascii.GetString($Bytes, $Offset, $Length)
}

function Get-U16 {
    param([byte[]]$Bytes, [int]$Offset)
    return [BitConverter]::ToUInt16($Bytes, $Offset)
}

function Get-U32 {
    param([byte[]]$Bytes, [int]$Offset)
    return [BitConverter]::ToUInt32($Bytes, $Offset)
}

function Assert-Range {
    param([byte[]]$Bytes, [int]$Offset, [int]$Length, [string]$Name)
    if ($Offset -lt 0 -or $Length -lt 0 -or
        ([long]$Offset + [long]$Length) -gt $Bytes.LongLength) {
        throw "$Name range is outside the file: offset=$Offset length=$Length size=$($Bytes.LongLength)"
    }
}

function Get-MubStructure {
    param([Parameter(Mandatory = $true)][string]$Path)
    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 80 -or (Get-AsciiString $bytes 0 4) -ne "MUB8") {
        throw "Invalid MUB8 file: $Path"
    }
    $dataOffset = [int](Get-U32 $bytes 4)
    $dataSize = [int](Get-U32 $bytes 8)
    $tagOffset = [int](Get-U32 $bytes 12)
    $tagSize = [int](Get-U32 $bytes 16)
    $pcmOffset = [int](Get-U32 $bytes 20)
    $pcmSize = [int](Get-U32 $bytes 24)
    Assert-Range $bytes $dataOffset $dataSize "MUB music data"
    if ($tagOffset -ne 0 -or $tagSize -ne 0) {
        Assert-Range $bytes $tagOffset $tagSize "MUB tag data"
    }
    if ($pcmOffset -ne 0 -or $pcmSize -ne 0) {
        Assert-Range $bytes $pcmOffset $pcmSize "MUB PCM data"
    }
    return [ordered]@{
        format = "MUB8"
        file_size = $bytes.Length
        header_size = $dataOffset
        data_offset = $dataOffset
        data_size = $dataSize
        data_sha256 = Get-ByteRangeSha256 $bytes $dataOffset $dataSize
        tag_offset = $tagOffset
        tag_size = $tagSize
        tag_sha256 = if ($tagSize -gt 0) { Get-ByteRangeSha256 $bytes $tagOffset $tagSize } else { $null }
        pcm_offset = $pcmOffset
        pcm_size = $pcmSize
        pcm_sha256 = if ($pcmSize -gt 0) { Get-ByteRangeSha256 $bytes $pcmOffset $pcmSize } else { $null }
        jump_count = Get-U16 $bytes 28
        jump_line = Get-U16 $bytes 30
        ext_flags = Get-U16 $bytes 32
        ext_system = $bytes[34]
        ext_target = $bytes[35]
        ext_channel_num = Get-U16 $bytes 36
        ext_fmvoice_num = Get-U16 $bytes 38
    }
}

function Get-WavStructure {
    param([Parameter(Mandatory = $true)][string]$Path)
    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 44 -or
        (Get-AsciiString $bytes 0 4) -ne "RIFF" -or
        (Get-AsciiString $bytes 8 4) -ne "WAVE" -or
        (Get-AsciiString $bytes 12 4) -ne "fmt " -or
        (Get-AsciiString $bytes 36 4) -ne "data") {
        throw "Unexpected WAV layout: $Path"
    }
    $dataSize = [int](Get-U32 $bytes 40)
    Assert-Range $bytes 44 $dataSize "WAV PCM data"
    $blockAlign = [int](Get-U16 $bytes 32)
    if ($blockAlign -le 0 -or ($dataSize % $blockAlign) -ne 0) {
        throw "Invalid WAV block alignment: $Path"
    }
    return [ordered]@{
        format = "RIFF/WAVE"
        file_size = $bytes.Length
        riff_size = Get-U32 $bytes 4
        audio_format = Get-U16 $bytes 20
        channels = Get-U16 $bytes 22
        sample_rate = Get-U32 $bytes 24
        byte_rate = Get-U32 $bytes 28
        block_align = $blockAlign
        bits_per_sample = Get-U16 $bytes 34
        data_offset = 44
        data_size = $dataSize
        sample_frames = [int64]($dataSize / $blockAlign)
        data_sha256 = Get-ByteRangeSha256 $bytes 44 $dataSize
    }
}

function Get-VgmStructure {
    param([Parameter(Mandatory = $true)][string]$Path)
    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 0x100 -or (Get-AsciiString $bytes 0 4) -ne "Vgm ") {
        throw "Invalid VGM file: $Path"
    }
    $relativeDataOffset = [int](Get-U32 $bytes 0x34)
    $dataOffset = if ($relativeDataOffset -eq 0) { 0x40 } else { 0x34 + $relativeDataOffset }
    Assert-Range $bytes $dataOffset ($bytes.Length - $dataOffset) "VGM command data"
    return [ordered]@{
        format = "VGM"
        file_size = $bytes.Length
        eof_offset = Get-U32 $bytes 0x04
        version = ("0x{0:x8}" -f (Get-U32 $bytes 0x08))
        total_samples = Get-U32 $bytes 0x18
        data_offset = $dataOffset
        ym2608_clock = Get-U32 $bytes 0x48
        command_size = $bytes.Length - $dataOffset
        command_sha256 = Get-ByteRangeSha256 $bytes $dataOffset ($bytes.Length - $dataOffset)
    }
}

function Get-S98Structure {
    param([Parameter(Mandatory = $true)][string]$Path)
    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 0x20 -or (Get-AsciiString $bytes 0 4) -ne "S983") {
        throw "Invalid S98 file: $Path"
    }
    $dumpOffset = [int](Get-U32 $bytes 0x14)
    Assert-Range $bytes $dumpOffset ($bytes.Length - $dumpOffset) "S98 command data"
    return [ordered]@{
        format = "S98v3"
        file_size = $bytes.Length
        timer_numerator = Get-U32 $bytes 0x04
        timer_denominator = Get-U32 $bytes 0x08
        tag_offset = Get-U32 $bytes 0x10
        dump_offset = $dumpOffset
        loop_offset = Get-U32 $bytes 0x18
        device_count = Get-U32 $bytes 0x1c
        command_size = $bytes.Length - $dumpOffset
        command_sha256 = Get-ByteRangeSha256 $bytes $dumpOffset ($bytes.Length - $dumpOffset)
    }
}

function Get-ArtifactStructure {
    param([Parameter(Mandatory = $true)][string]$Path)
    switch ([System.IO.Path]::GetExtension($Path).ToLowerInvariant()) {
        ".mub" { return Get-MubStructure $Path }
        ".wav" { return Get-WavStructure $Path }
        ".vgm" { return Get-VgmStructure $Path }
        ".s98" { return Get-S98Structure $Path }
        default { throw "Unsupported artifact extension: $Path" }
    }
}

function Assert-AsciiPathWithoutSpaces {
    param([Parameter(Mandatory = $true)][string]$Path, [string]$Name)
    if ($Path -match "\s" -or $Path -notmatch "^[\x20-\x7e]+$") {
        throw "$Name must be an ASCII path without spaces: $Path"
    }
}

function Assert-Pe32X86 {
    param([Parameter(Mandatory = $true)][string]$Path)
    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 0x40 -or (Get-AsciiString $bytes 0 2) -ne "MZ") {
        throw "Not a PE executable: $Path"
    }
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
    if ($peOffset -lt 0 -or $peOffset + 6 -gt $bytes.Length -or
        (Get-AsciiString $bytes $peOffset 4) -ne "PE`0`0") {
        throw "Invalid PE header: $Path"
    }
    $machine = Get-U16 $bytes ($peOffset + 4)
    if ($machine -ne 0x014c) {
        throw ("Expected PE32/x86 machine 0x014c, got 0x{0:x4}: {1}" -f $machine, $Path)
    }
}

function Invoke-Mucom {
    param(
        [string]$Stage,
        [string[]]$Arguments,
        [string]$CaseDirectory,
        [string]$Executable,
        [string]$WorkingDirectory
    )
    $stdoutPath = Join-Path $CaseDirectory ($Stage + ".stdout.bin")
    $stderrPath = Join-Path $CaseDirectory ($Stage + ".stderr.bin")
    $exitPath = Join-Path $CaseDirectory ($Stage + ".exit-code.txt")
    $commandPath = Join-Path $CaseDirectory ($Stage + ".command.txt")
    Write-AsciiText (("mucom88.exe " + ($Arguments -join " ")) + "`r`n") $commandPath

    $process = Start-Process -FilePath $Executable `
        -ArgumentList $Arguments `
        -WorkingDirectory $WorkingDirectory `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath `
        -NoNewWindow -Wait -PassThru
    Write-AsciiText (($process.ExitCode.ToString()) + "`r`n") $exitPath
    if ($process.ExitCode -ne 0) {
        throw "mucom88.exe failed: stage=$Stage exit=$($process.ExitCode) stderr=$stderrPath"
    }
}

function Invoke-GoldenRun {
    param(
        [Parameter(Mandatory = $true)][string]$RunRoot,
        [Parameter(Mandatory = $true)][string]$PackageDirectory,
        [Parameter(Mandatory = $true)][string]$Executable
    )
    New-Item -ItemType Directory -Path $RunRoot | Out-Null
    foreach ($case in $Cases) {
        $caseDirectory = Join-Path $RunRoot $case.id
        New-Item -ItemType Directory -Path $caseDirectory | Out-Null

        if ($null -eq $case.reference_mub) {
            $mubPath = Join-Path $caseDirectory "music.mub"
            $compileArguments = @("-g")
            if ($null -ne $case.driver) {
                $compileArguments += @("-f", [string]$case.driver)
            }
            if ($case.skip_pcm) {
                $compileArguments += "-k"
            }
            else {
                $compileArguments += @("-p", "mucompcm.bin")
            }
            $compileArguments += @("-v", "voice.dat", "-o", $mubPath, [string]$case.source)
            Invoke-Mucom "compile" $compileArguments $caseDirectory $Executable $PackageDirectory
            if (-not (Test-Path -LiteralPath $mubPath -PathType Leaf)) {
                throw "MUB was not created: $mubPath"
            }
        }
        else {
            $mubPath = Join-Path $PackageDirectory ([string]$case.reference_mub)
        }

        if ($case.render) {
            $wavPath = Join-Path $caseDirectory "audio.wav"
            $vgmPath = Join-Path $caseDirectory "log.vgm"
            $s98Path = Join-Path $caseDirectory "log.s98"
            Invoke-Mucom "wav" @("-x", "-l", "$RenderSeconds", "-w", $wavPath, $mubPath) `
                $caseDirectory $Executable $PackageDirectory
            Invoke-Mucom "vgm" @("-x", "-l", "$RenderSeconds", "-b", $vgmPath, $mubPath) `
                $caseDirectory $Executable $PackageDirectory
            Invoke-Mucom "s98" @("-x", "-l", "$RenderSeconds", "-b", $s98Path, $mubPath) `
                $caseDirectory $Executable $PackageDirectory
            foreach ($path in @($wavPath, $vgmPath, $s98Path)) {
                if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                    throw "Offline artifact was not created: $path"
                }
            }
        }
    }
}

$RepositoryRoot = [System.IO.Path]::GetFullPath($RepositoryRoot)
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
Assert-AsciiPathWithoutSpaces $RepositoryRoot "RepositoryRoot"
Assert-AsciiPathWithoutSpaces $OutputRoot "OutputRoot"
if (Test-Path -LiteralPath $OutputRoot) {
    throw "OutputRoot already exists. Use a new empty path: $OutputRoot"
}

$PackageDirectory = Join-Path $RepositoryRoot "package"
$Executable = Join-Path $PackageDirectory "mucom88.exe"
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Windows baseline executable was not found: $Executable"
}

$verifiedInputs = @()
foreach ($input in $ExpectedInputs) {
    $nativeRelativePath = $input.path.Replace("/", [System.IO.Path]::DirectorySeparatorChar)
    $path = Join-Path $RepositoryRoot $nativeRelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required input is missing: $path"
    }
    $file = Get-Item -LiteralPath $path
    $hash = Get-Sha256 $path
    if ($file.Length -ne $input.size -or $hash -ne $input.sha256) {
        throw "Input identity mismatch: $($input.path) size=$($file.Length) sha256=$hash"
    }
    $verifiedInputs += [ordered]@{
        path = $input.path
        size = $file.Length
        sha256 = $hash
    }
}
Assert-Pe32X86 $Executable

$systemLocale = (Get-WinSystemLocale).Name
$codePage = Get-ItemProperty -LiteralPath "HKLM:\SYSTEM\CurrentControlSet\Control\Nls\CodePage"
$windowsVersion = Get-ItemProperty -LiteralPath "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion"
$os = Get-CimInstance Win32_OperatingSystem
$computer = Get-CimInstance Win32_ComputerSystem
$processor = Get-CimInstance Win32_Processor | Select-Object -First 1
$environmentProblems = @()
if ($systemLocale -ne "ja-JP") {
    $environmentProblems += "system locale must be ja-JP (actual: $systemLocale)"
}
if ([string]$codePage.ACP -ne "932") {
    $environmentProblems += "ANSI code page must be 932 (actual: $($codePage.ACP))"
}
if ([string]$windowsVersion.DisplayVersion -ne "24H2") {
    $environmentProblems += "Windows display version must be 24H2 (actual: $($windowsVersion.DisplayVersion))"
}
if ([int]$processor.Architecture -ne 12) {
    $environmentProblems += "guest processor architecture must be ARM64/12 (actual: $($processor.Architecture))"
}
if ($environmentProblems.Count -gt 0 -and -not $AllowEnvironmentMismatch) {
    throw ($environmentProblems -join "; ")
}

& "$env:SystemRoot\System32\chcp.com" 932 | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw "Failed to set console code page 932"
}

$repositoryHead = $null
$repositoryStatus = $null
$git = Get-Command git.exe -ErrorAction SilentlyContinue
if ($null -ne $git -and (Test-Path -LiteralPath (Join-Path $RepositoryRoot ".git"))) {
    $repositoryHead = (& $git.Source -C $RepositoryRoot rev-parse HEAD).Trim()
    $repositoryStatus = ((& $git.Source -C $RepositoryRoot status --porcelain) -join "`n")
}

$environment = [ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString("o")
    baseline_id = $BaselineId
    baseline_source_commit = $BaselineSourceCommit
    repository_head = $repositoryHead
    repository_status = $repositoryStatus
    host_arch = "arm64"
    host_mac_model = $HostMacModel
    host_soc = $HostSoc
    host_macos = $HostMacOS
    hypervisor = $Hypervisor
    hypervisor_version = $HypervisorVersion
    snapshot_name = $SnapshotName
    guest_caption = $os.Caption
    guest_version = $os.Version
    guest_build = $os.BuildNumber
    guest_display_version = $windowsVersion.DisplayVersion
    guest_architecture = $os.OSArchitecture
    guest_manufacturer = $computer.Manufacturer
    guest_model = $computer.Model
    guest_processors = $computer.NumberOfLogicalProcessors
    guest_memory_bytes = [int64]$computer.TotalPhysicalMemory
    guest_processor_architecture_code = [int]$processor.Architecture
    process_architecture = [Environment]::GetEnvironmentVariable("PROCESSOR_ARCHITECTURE")
    process_architecture_w6432 = [Environment]::GetEnvironmentVariable("PROCESSOR_ARCHITEW6432")
    execution = "x86-emulated"
    system_locale = $systemLocale
    user_culture = (Get-Culture).Name
    ansi_code_page = [string]$codePage.ACP
    oem_code_page = [string]$codePage.OEMCP
    console_code_page = 932
    environment_mismatches = $environmentProblems
}

New-Item -ItemType Directory -Path $OutputRoot | Out-Null
$runA = Join-Path $OutputRoot "run-a"
$runB = Join-Path $OutputRoot "run-b"
Invoke-GoldenRun $runA $PackageDirectory $Executable
Invoke-GoldenRun $runB $PackageDirectory $Executable

$artifactExtensions = @(".mub", ".wav", ".vgm", ".s98")
$determinism = @()
$artifactsA = Get-ChildItem -LiteralPath $runA -Recurse -File | Where-Object {
    $artifactExtensions -contains $_.Extension.ToLowerInvariant()
} | Sort-Object FullName
$artifactsB = Get-ChildItem -LiteralPath $runB -Recurse -File | Where-Object {
    $artifactExtensions -contains $_.Extension.ToLowerInvariant()
}
if ($artifactsA.Count -ne $artifactsB.Count) {
    throw "Artifact count differs between run A and run B"
}

foreach ($artifactA in $artifactsA) {
    $relative = $artifactA.FullName.Substring($runA.Length).TrimStart([char]'\')
    $artifactBPath = Join-Path $runB $relative
    if (-not (Test-Path -LiteralPath $artifactBPath -PathType Leaf)) {
        throw "Run B artifact is missing: $relative"
    }
    $artifactB = Get-Item -LiteralPath $artifactBPath
    $hashA = Get-Sha256 $artifactA.FullName
    $hashB = Get-Sha256 $artifactB.FullName
    $match = $artifactA.Length -eq $artifactB.Length -and $hashA -eq $hashB
    $determinism += [ordered]@{
        path = $relative.Replace("\", "/")
        run_a_size = $artifactA.Length
        run_a_sha256 = $hashA
        run_b_size = $artifactB.Length
        run_b_sha256 = $hashB
        identical = $match
    }
}

Write-Utf8Json $determinism (Join-Path $OutputRoot "determinism.json")
if (($determinism | Where-Object { -not $_.identical }).Count -ne 0) {
    throw "At least one artifact is not deterministic; see determinism.json"
}

$candidate = Join-Path $OutputRoot "candidate"
$candidateCases = Join-Path $candidate "cases"
New-Item -ItemType Directory -Path $candidateCases -Force | Out-Null
Copy-Item -Path (Join-Path $runA "*") -Destination $candidateCases -Recurse
Write-Utf8Json $environment (Join-Path $candidate "environment.json")
Write-Utf8Json $verifiedInputs (Join-Path $candidate "inputs.json")
Write-Utf8Json $determinism (Join-Path $candidate "determinism.json")

$manifestArtifacts = @()
Get-ChildItem -LiteralPath $candidateCases -Recurse -File | Where-Object {
    $artifactExtensions -contains $_.Extension.ToLowerInvariant()
} | Sort-Object FullName | ForEach-Object {
    $relative = $_.FullName.Substring($candidate.Length).TrimStart([char]'\').Replace("\", "/")
    $manifestArtifacts += [ordered]@{
        path = $relative
        size = $_.Length
        sha256 = Get-Sha256 $_.FullName
        structure = Get-ArtifactStructure $_.FullName
    }
}

$manifest = [ordered]@{
    schema_version = 1
    baseline_id = $BaselineId
    windows_product_version = "0.70"
    mucom_core_version = "1.7d"
    baseline_source_commit = $BaselineSourceCommit
    render_seconds = $RenderSeconds
    render_rate = 44100
    cases = $Cases
    artifacts = $manifestArtifacts
}
Write-Utf8Json $manifest (Join-Path $candidate "manifest.json")

$manifestLines = @()
Get-ChildItem -LiteralPath $candidate -Recurse -File | Where-Object {
    $_.Name -ne "manifest.sha256"
} | Sort-Object FullName | ForEach-Object {
    $relative = $_.FullName.Substring($candidate.Length).TrimStart([char]'\').Replace("\", "/")
    $manifestLines += ((Get-Sha256 $_.FullName) + "  " + $relative)
}
[System.IO.File]::WriteAllLines(
    (Join-Path $candidate "manifest.sha256"),
    [string[]]$manifestLines,
    $Utf8NoBom)

Write-Host "Golden candidate generated and verified: $candidate"
Write-Host "Copy candidate contents to tests/reference/windows-arm64-vm/mucom88-win-0.70/ after review."
