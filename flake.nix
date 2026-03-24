{
  description = "ghostling";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/release-25.11";
    flake-utils.url = "github:numtide/flake-utils";
    zig = {
      url = "github:mitchellh/zig-overlay";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs = {
    nixpkgs,
    flake-utils,
    zig,
    ...
  }:
    flake-utils.lib.eachDefaultSystem (
      system: let
        pkgs = nixpkgs.legacyPackages.${system};
      in {
        devShells.default = pkgs.mkShell {
          packages = [
            zig.packages.${system}."0.15.2"
            pkgs.cmake
            pkgs.ninja
            pkgs.pinact
            pkgs.scc
          ] ++ pkgs.lib.optionals pkgs.stdenv.hostPlatform.isLinux [
            pkgs.xorg.libX11
            pkgs.xorg.libXcursor
            pkgs.xorg.libXrandr
            pkgs.xorg.libXinerama
            pkgs.xorg.libXi
            pkgs.libGL
            pkgs.libxkbcommon
            pkgs.wayland
          ];

          # Unset Nix Darwin SDK env vars and remove the xcbuild
          # xcrun wrapper so Zig's SDK detection uses the real
          # system xcrun/xcode-select.
          shellHook = ''
            unset SDKROOT
            unset DEVELOPER_DIR
            export PATH=$(echo "$PATH" | tr ':' '\n' | grep -v -e xcbuild -e apple-sdk | tr '\n' ':')
          '';
        };
      }
    );
}
