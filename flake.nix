{
  description = "ghostling";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/release-25.11";
    flake-utils.url = "github:numtide/flake-utils";
    zig = {
      url = "github:mitchellh/zig-overlay";
      inputs.nixpkgs.follows = "nixpkgs";
    };
    raylib-src = {
      url = "github:raysan5/raylib/5.5";
      flake = false;
    };
    ghostty-src = {
      url = "github:ghostty-org/ghostty/ecc55b94c803789762682065ab68f227447909c5";
      flake = false;
    };
  };

  outputs = {
    nixpkgs,
    flake-utils,
    zig,
    raylib-src,
    ghostty-src,
    ...
  }:
    flake-utils.lib.eachDefaultSystem (
      system: let
        pkgs = nixpkgs.legacyPackages.${system};
        zigPkg = zig.packages.${system}."0.15.2";

        ghosttyZigDeps = pkgs.callPackage "${ghostty-src}/build.zig.zon.nix" {
          name = "ghostty-vt-deps";
          zig_0_15 = zigPkg;
        };
      in {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "ghostling";
          version = "0.1.0";
          src = ./.;

          nativeBuildInputs = [
            zigPkg
            pkgs.cmake
            pkgs.ninja
          ];

          buildInputs =
            pkgs.lib.optionals pkgs.stdenv.hostPlatform.isLinux [
              pkgs.xorg.libX11
              pkgs.xorg.libXcursor
              pkgs.xorg.libXrandr
              pkgs.xorg.libXinerama
              pkgs.xorg.libXi
              pkgs.libGL
              pkgs.libxkbcommon
              pkgs.wayland
            ];

          preConfigure = ''
            cp -rL ${ghostty-src} ghostty-local
            chmod -R u+w ghostty-local

            cp -rL ${ghosttyZigDeps} zig-deps
            chmod -R u+w zig-deps

            cmakeFlagsArray+=(
              "-DFETCHCONTENT_SOURCE_DIR_RAYLIB=${raylib-src}"
              "-DFETCHCONTENT_SOURCE_DIR_GHOSTTY=$PWD/ghostty-local"
              "-DGHOSTTY_ZIG_BUILD_FLAGS=--system;$PWD/zig-deps"
            )
            export ZIG_GLOBAL_CACHE_DIR=$TMPDIR/zig-global-cache
          '' + pkgs.lib.optionalString pkgs.stdenv.hostPlatform.isDarwin ''
            printf '#!/bin/sh\necho ok\n' > $TMPDIR/xcode-select
            printf '#!/bin/sh\necho %s\n' "$SDKROOT" > $TMPDIR/xcrun
            chmod +x $TMPDIR/xcode-select $TMPDIR/xcrun
            export PATH="$TMPDIR:$PATH"
          '';

          installPhase = ''
            mkdir -p $out/bin $out/lib
            cp ghostling $out/bin/
            cp ../ghostty-local/zig-out/lib/libghostty-vt*.dylib $out/lib/ 2>/dev/null \
              || cp ../ghostty-local/zig-out/lib/libghostty-vt*.so* $out/lib/
          '' + pkgs.lib.optionalString pkgs.stdenv.hostPlatform.isDarwin ''
            install_name_tool -change @rpath/libghostty-vt.dylib $out/lib/libghostty-vt.0.1.0.dylib $out/bin/ghostling
          '' + pkgs.lib.optionalString pkgs.stdenv.hostPlatform.isLinux ''
            patchelf --set-rpath $out/lib $out/bin/ghostling
          '';
        };

        devShells.default = pkgs.mkShell {
          packages =
            [
              zigPkg
              pkgs.cmake
              pkgs.ninja
              pkgs.pinact
              pkgs.scc
            ]
            ++ pkgs.lib.optionals pkgs.stdenv.hostPlatform.isLinux [
              pkgs.xorg.libX11
              pkgs.xorg.libXcursor
              pkgs.xorg.libXrandr
              pkgs.xorg.libXinerama
              pkgs.xorg.libXi
              pkgs.libGL
              pkgs.libxkbcommon
              pkgs.wayland
            ];

          shellHook = ''
            unset SDKROOT
            unset DEVELOPER_DIR
            export PATH=$(echo "$PATH" | tr ':' '\n' | grep -v -e xcbuild -e apple-sdk | tr '\n' ':')
          '';
        };
      }
    );
}
