# NLC: fetch the third-party sources into 3rd_party\Src at pinned commits.
#
# Replaces upstream Update_Components.cmd for reproducible builds: that script
# clones moving branch heads, which drift away from what the project files in a
# given OGSR revision expect (e.g. mimalloc removed src\arena-meta.c on
# 2026-07-27 while the 3.525 project still compiles it).
#
# The pins below match OGSR tag 3.525 (2026-05-26), the base of branch `nlc`.
# After merging a newer upstream, re-pin to that revision's date and rebuild.
#
# Usage (from any directory):
#   powershell -NoProfile -ExecutionPolicy Bypass -File nlc_tools\fetch_deps.ps1

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src = Join-Path $root '3rd_party\Src'

# relative dir under 3rd_party\Src, repository, commit
$pins = @(
    @('DirectXTex\DirectXTex',                          'https://github.com/microsoft/DirectXTex.git',     '92682bfa8a0d0cae18c0f4b89b7dfe68c4d085e2'), # tag mar2025
    @('DirectXMesh\DirectXMesh',                        'https://github.com/OGSR/DirectXMesh.git',         '4bd73ff11ad668216307a1f682a9be6304d1d90f'), # main
    @('DirectXMath\DirectXMath',                        'https://github.com/microsoft/DirectXMath.git',    '1abe1f758b76c2ab4ec7eb40138b254b4355815c'), # main
    @('concurrentqueue\concurrentqueue',                'https://github.com/cameron314/concurrentqueue.git','d655418bb644b7f85159d94c591d7d983949fb81'), # master
    @('libsquashfs\squashfs-tools-ng',                  'https://github.com/AgentD/squashfs-tools-ng.git', 'e3dcf1770fd77a0babcca422dcbe7b2cc7b8ab90'), # master
    @('lz4\lz4',                                        'https://github.com/lz4/lz4.git',                  '1b0fc692949cf474eb0d89db5f0dfa3698e9aa56'), # dev, 1.10.0
    @('zstd\zstd',                                      'https://github.com/facebook/zstd.git',            '5233c58e6ca0b1c4c6b353ad79649191ed195bdc'), # dev
    @('mimalloc\mimalloc',                              'https://github.com/microsoft/mimalloc.git',       '9ed924555009265c1faf373d70d5aa96040ea420'), # dev3
    @('NVIDIA_DLSS\DLSS',                               'https://github.com/NVIDIA/DLSS.git',              '9a6b48a79d5ae41bf1481d0c83d73859ec481bd2'), # tag v310.4.0
    @('cpputils\cpputils',                              'https://github.com/tzcnt/cpputils.git',           '1627271be1fc3a75b9398a17c2ef36c4f21d4eb3'), # main
    @('DiscordRPC\DiscordRPC',                          'https://github.com/OGSR/discord-rpc.git',         'd9fbcddc13bb51d58298c0e08b19a42a2e791975'), # master
    @('DiscordRPC\DiscordRPC\thirdparty\rapidjson-1.1.0','https://github.com/Tencent/rapidjson.git',       'f54b0e47a08782a6131cc3d60f94d038fa6e0a51')  # tag v1.1.0
)

foreach ($p in $pins) {
    $dir = Join-Path $src $p[0]; $url = $p[1]; $sha = $p[2]
    if (Test-Path (Join-Path $dir '.git')) {
        $have = (git -C $dir rev-parse HEAD).Trim()
        if ($have -eq $sha) { Write-Host "ok     $($p[0]) $($sha.Substring(0,9))"; continue }
    }
    # a nested pin (rapidjson inside DiscordRPC) is listed after its parent, so
    # re-fetching the parent here is followed by re-fetching the nested repo
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    git -C $dir init -q
    git -C $dir remote add origin $url
    git -C $dir fetch -q --depth 1 origin $sha
    if ($LASTEXITCODE -ne 0) { throw "fetch failed: $($p[0]) $sha" }
    git -C $dir checkout -q FETCH_HEAD
    if ($LASTEXITCODE -ne 0) { throw "checkout failed: $($p[0])" }
    Write-Host "fetch  $($p[0]) $($sha.Substring(0,9))"
}
Write-Host 'Dependencies are at the pinned commits.'
