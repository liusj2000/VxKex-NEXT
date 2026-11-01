$ErrorActionPreference='Stop'
$command=Get-Command opencode -ErrorAction Stop
$version=(opencode --version | Out-String).Trim()
$pipelineCode=$LASTEXITCODE
opencode --version > 'D:\project\VxKex-NEXT\docx\opencode-installed-version-20261101.txt'
$redirectCode=$LASTEXITCODE
$info=@{CommandType=[string]$command.CommandType;Version=$version;PipelineExit=$pipelineCode;RedirectExit=$redirectCode;Profile=$PROFILE.CurrentUserAllHosts}
$info | ConvertTo-Json | Set-Content -LiteralPath 'D:\project\VxKex-NEXT\docx\opencode-installed-profile-20261101.json' -Encoding UTF8
if($command.CommandType -ne 'Function' -or $pipelineCode -ne 0 -or $redirectCode -ne 0 -or $version -ne 'opencode v2.2.5') { exit 2 }
opencode
