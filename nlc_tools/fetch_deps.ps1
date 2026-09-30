# NLC: fetch the third-party sources into 3rd_party\Src at pinned commits.
#
# Replaces upstream Update_Components.cmd for reproducible builds: that script
# clones moving branch heads, which drift away from what the project files in a
# given OGSR revision expect (e.g. mimalloc removed src\arena-meta.c on
# 2026-07-27 while the 3.525 project still compiles it).
#
# The pins below match upstream OGSR main 2021123 (2026-09-27), merged into
# branch `nlc`: tags as Update_Components.cmd names them, branch heads as of
# that commit date. After merging a newer upstream, re-pin and rebuild.
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
    @('DirectXMath\DirectXMath',                        'https://github.com/microsoft/DirectXMath.git',    'b758b4a5669aca3542b4bf2b0cbc334696edd302'), # main 2026-09-21
    @('concurrentqueue\concurrentqueue',                'https://github.com/cameron314/concurrentqueue.git','683b9e31ea15eb69f1b81cc1defc7850d5f20b71'), # master 2026-07-11
    @('libsquashfs\squashfs-tools-ng',                  'https://github.com/AgentD/squashfs-tools-ng.git', 'e3dcf1770fd77a0babcca422dcbe7b2cc7b8ab90'), # master
    @('lz4\lz4',                                        'https://github.com/lz4/lz4.git',                  '0774d05537f9762f838f7ab541b7765f1a729cb5'), # dev 2026-06-01
    @('zstd\zstd',                                      'https://github.com/facebook/zstd.git',            '01b7154f1172432f8abe9b3bb9909e14a1176b7d'), # dev 2026-09-18
    @('mimalloc\mimalloc',                              'https://github.com/microsoft/mimalloc.git',       '34fbd7e7cd4627424490afe19b20f8066bfc537d'), # tag v3.5.1
    @('NVIDIA_DLSS\DLSS',                               'https://github.com/NVIDIA/DLSS.git',              'a291cc7d2cc642a51566f3dfd5376f635cd1b284'), # tag v310.7.0
    @('cpputils\cpputils',                              'https://github.com/tzcnt/cpputils.git',           '1627271be1fc3a75b9398a17c2ef36c4f21d4eb3'), # main
    @('DiscordRPC\DiscordRPC',                          'https://github.com/OGSR/discord-rpc.git',         'd9fbcddc13bb51d58298c0e08b19a42a2e791975'), # master
    @('DiscordRPC\DiscordRPC\thirdparty\rapidjson-1.1.0','https://github.com/Tencent/rapidjson.git',       'f54b0e47a08782a6131cc3d60f94d038fa6e0a51'), # tag v1.1.0
    @('FidelityFX-SDK\FidelityFX-SDK',                  'https://github.com/OGSR/FidelityFX-SDK.git',      '8adb7cf1650120987d84fcc8be7e73ad8a6f9945')  # release-FSR3-3.1.2-DX11-Native-API
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
