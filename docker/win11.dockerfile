# escape=`

FROM mcr.microsoft.com/dotnet/framework/runtime:4.8.1-windowsservercore-ltsc2022

SHELL ["cmd", "/S", "/C"]

# Download separately so we can verify Docker can commit a simple layer.
RUN curl.exe -SL `
    --output C:\vs_buildtools.exe `
    https://aka.ms/vs/17/release/vs_buildtools.exe `
    && dir C:\vs_buildtools.exe

# Install Build Tools.
RUN (start /w C:\vs_buildtools.exe `
        --quiet `
        --wait `
        --norestart `
        --nocache `
        --installPath C:\BuildTools `
        --add Microsoft.VisualStudio.Workload.VCTools `
        --includeRecommended `
        --add Microsoft.VisualStudio.Component.Windows11SDK.26100 `
        --remove Microsoft.VisualStudio.Component.Windows10SDK.10240 `
        --remove Microsoft.VisualStudio.Component.Windows10SDK.10586 `
        --remove Microsoft.VisualStudio.Component.Windows10SDK.14393 `
        --remove Microsoft.VisualStudio.Component.Windows81SDK `
        || IF "%ERRORLEVEL%"=="3010" EXIT 0) `
    && echo Visual Studio Build Tools installer returned successfully.

RUN del /q C:\vs_buildtools.exe


# ---------------------------------------------------------------------------
# Command-line development tools
# ---------------------------------------------------------------------------

SHELL ["powershell.exe", "-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command"]

# CMake 3.28.6
RUN $ErrorActionPreference = 'Stop'; `
    $ProgressPreference = 'SilentlyContinue'; `
    Invoke-WebRequest `
        -UseBasicParsing `
        -Uri 'https://github.com/Kitware/CMake/releases/download/v3.28.6/cmake-3.28.6-windows-x86_64.zip' `
        -OutFile 'C:\cmake.zip'; `
    $expected = 'A8F2E684EAD94A64FD3517A38857A5B3F7F8D68D15C49CA1143D18797EAF9CAC'; `
    $actual = (Get-FileHash 'C:\cmake.zip' -Algorithm SHA256).Hash; `
    if ($actual -ne $expected) { `
        throw "CMake SHA256 mismatch. Expected $expected, got $actual"; `
    }; `
    New-Item -ItemType Directory -Force -Path 'C:\Tools' | Out-Null; `
    Expand-Archive -Path 'C:\cmake.zip' -DestinationPath 'C:\Tools'; `
    Move-Item 'C:\Tools\cmake-3.28.6-windows-x86_64' 'C:\Tools\CMake'; `
    Remove-Item 'C:\cmake.zip'


# MinGit 2.55.0.3
RUN $ErrorActionPreference = 'Stop'; `
    $ProgressPreference = 'SilentlyContinue'; `
    Invoke-WebRequest `
        -UseBasicParsing `
        -Uri 'https://github.com/git-for-windows/git/releases/download/v2.55.0.windows.3/MinGit-2.55.0.3-64-bit.zip' `
        -OutFile 'C:\mingit.zip'; `
    $expected = 'F48E2D2DC74A24454ADC6D8FD0AC25BF9C2386F19CFB06202B9465AAAD4F9F05'; `
    $actual = (Get-FileHash 'C:\mingit.zip' -Algorithm SHA256).Hash; `
    if ($actual -ne $expected) { `
        throw "MinGit SHA256 mismatch. Expected $expected, got $actual"; `
    }; `
    New-Item -ItemType Directory -Force -Path 'C:\Tools\MinGit' | Out-Null; `
    Expand-Archive -Path 'C:\mingit.zip' -DestinationPath 'C:\Tools\MinGit'; `
    Remove-Item 'C:\mingit.zip'


# Vim 9.2.1036, signed x64 portable archive
RUN $ErrorActionPreference = 'Stop'; `
    $ProgressPreference = 'SilentlyContinue'; `
    Invoke-WebRequest `
        -UseBasicParsing `
        -Uri 'https://github.com/vim/vim-win32-installer/releases/download/v9.2.1036/gvim_9.2.1036_x64_signed.zip' `
        -OutFile 'C:\vim.zip'; `
    $expected = '4E039C59980943593FDA8729902A04F96543342DF6D2DAF73E4D5378BB16F9AD'; `
    $actual = (Get-FileHash 'C:\vim.zip' -Algorithm SHA256).Hash; `
    if ($actual -ne $expected) { `
        throw "Vim SHA256 mismatch. Expected $expected, got $actual"; `
    }; `
    New-Item -ItemType Directory -Force -Path 'C:\Tools\Vim' | Out-Null; `
    Expand-Archive -Path 'C:\vim.zip' -DestinationPath 'C:\Tools\Vim'; `
    Remove-Item 'C:\vim.zip'; `
    $vimExe = Get-ChildItem 'C:\Tools\Vim' -Filter 'vim.exe' -File -Recurse | Select-Object -First 1; `
    if ($null -eq $vimExe) { throw 'vim.exe was not found after extraction'; }; `
    New-Item -ItemType Junction -Path 'C:\Tools\Vim\current' -Target $vimExe.Directory.FullName | Out-Null


# ---------------------------------------------------------------------------
# Codex CLI
# ---------------------------------------------------------------------------

# Install the executable/package into the immutable image.
# Runtime user state will use C:\codex-data instead and will be mounted from
# a Docker named volume.
RUN $ErrorActionPreference = 'Stop'; `
    $ProgressPreference = 'SilentlyContinue'; `
    New-Item -ItemType Directory -Force -Path 'C:\Tools\Codex' | Out-Null; `
    $env:CODEX_HOME = 'C:\Tools\CodexPackage'; `
    $env:CODEX_INSTALL_DIR = 'C:\Tools\Codex\bin'; `
    Invoke-WebRequest `
        -UseBasicParsing `
        -Uri 'https://chatgpt.com/codex/install.ps1' `
        -OutFile 'C:\codex-install.ps1'; `
    & 'C:\codex-install.ps1'; `
    Remove-Item 'C:\codex-install.ps1'; `
    & 'C:\Tools\Codex\bin\codex.exe' --version


# ---------------------------------------------------------------------------
# Environment
# ---------------------------------------------------------------------------

ENV PATH="C:\Tools\Codex\bin;C:\Tools\CMake\bin;C:\Tools\MinGit\cmd;C:\Tools\Vim\current;${PATH}"
ENV CODEX_HOME=C:\codex-data

RUN New-Item -ItemType Directory -Force -Path 'C:\workspace' | Out-Null; `
    New-Item -ItemType Directory -Force -Path 'C:\codex-data' | Out-Null


# ---------------------------------------------------------------------------
# MSVC development-shell launcher
#
# docker exec starts a new process and therefore does not inherit environment
# variables from a previously initialized VsDevCmd shell. Always enter the
# workspace through this launcher when compiling.
# ---------------------------------------------------------------------------

RUN Set-Content `
        -Path 'C:\Tools\Enter-DevShell.cmd' `
        -Encoding ASCII `
        -Value `
            '@echo off', `
            'call C:\BuildTools\Common7\Tools\VsDevCmd.bat -arch=amd64 -host_arch=amd64', `
            'if errorlevel 1 exit /b %errorlevel%', `
            'cd /d C:\workspace', `
            'powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass'


WORKDIR C:\workspace


# Keep the development container alive in detached mode.
# Interactive development sessions are created with docker exec.
CMD ["powershell.exe", "-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", "while ($true) { Start-Sleep -Seconds 3600 }"]
