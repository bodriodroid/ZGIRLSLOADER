#!/bin/sh
# UNTESTED RECIPE. This sandbox cannot reach NuGet, so none of this has been run.
#
# Goal: an arm64 *Android (bionic)* build of SteamRE/DepotDownloader that can be executed from the app's
# nativeLibraryDir as libdepotdownloader.so.  The loader (src/core/steam_proc.cpp) only needs an executable
# that speaks DepotDownloader's command line and prompts; any build that does works.
#
#   * .NET 8 SDK on a Linux x64 machine
#   * git clone https://github.com/SteamRE/DepotDownloader && cd DepotDownloader
#   * check `dotnet --info` / NuGet for the runtime pack "Microsoft.NETCore.App.Runtime.linux-bionic-arm64"
#     (and, for NativeAOT, "runtime.linux-bionic-arm64.Microsoft.DotNet.ILCompiler"). If either is missing the
#     bionic RID is not usable and the alternatives are: a Termux-built dotnet, or running the downloader on a PC.
set -eu
OUT="${1:-out}"
dotnet publish DepotDownloader/DepotDownloader.csproj -c Release -r linux-bionic-arm64 --self-contained true \
    -p:PublishSingleFile=true -p:InvariantGlobalization=true -o "$OUT"
cp "$OUT/DepotDownloader" "$(dirname "$0")/../android/app/src/main/jniLibs/arm64-v8a/libdepotdownloader.so"
