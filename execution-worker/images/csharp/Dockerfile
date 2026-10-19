# C# (.NET 9) toolchain. The SDK is official, but `dotnet run` needs network
# for NuGet restore, which sandboxed containers do not have. Instead we wrap
# the SDK-bundled Roslyn compiler (csc.dll) in a small shim script.
FROM mcr.microsoft.com/dotnet/sdk:9.0
ENV DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1
RUN CSC="$(find /usr/share/dotnet/sdk -name csc.dll | head -1)" \
 && test -n "$CSC" \
 && printf '#!/bin/sh\nexec dotnet "%s" "$@"\n' "$CSC" > /usr/local/bin/csc-shim \
 && chmod +x /usr/local/bin/csc-shim \
 && csc-shim -help >/dev/null \
 && echo "csc-shim ready: $CSC"
WORKDIR /work
