{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};

      lektra = pkgs.stdenv.mkDerivation {
        pname = "lektra";
        version = "0.6.7";

        src = self;

        # The submodules are only present with `?submodules=1` in the flake URL.
        preConfigure = ''
          if [ -z "$(ls -A thirdparty/mupdf 2>/dev/null)" ]; then
            echo "lektra: submodules are missing. Use a flake URL with ?submodules=1:" >&2
            echo "  git+https://github.com/dheerajshenoy/lektra?submodules=1" >&2
            exit 1
          fi
        '';

        nativeBuildInputs = with pkgs; [
          cmake
          pkg-config
          gnumake
          python3
          unzip
          qt6.wrapQtAppsHook
          wrapGAppsHook3
        ];

        buildInputs = with pkgs; [
          qt6.qtbase
          qt6.qttools
          zlib
          djvulibre
          texlive.bin.core
          # Qt's gtk3 platform theme opens GTK's file chooser, which aborts
          # if its GSettings schemas are not on XDG_DATA_DIRS.
          gtk3
          gsettings-desktop-schemas
        ];

        # Merge the GApps environment into the single Qt wrapper instead of
        # double-wrapping the binary.
        dontWrapGApps = true;
        preFixup = ''
          qtWrapperArgs+=("''${gappsWrapperArgs[@]}")
        '';

        cmakeFlags = [
          "-DCMAKE_BUILD_TYPE=Release"
        ];

        meta = with pkgs.lib; {
          description = "High-performance PDF reader that prioritizes screen space and control";
          homepage = "https://codeberg.org/lektra/lektra";
          license = licenses.agpl3Only;
          platforms = [ "x86_64-linux" ];
          mainProgram = "lektra";
        };
      };
    in
    {
      packages.${system} = {
        default = lektra;
        inherit lektra;
      };

      devShells.${system}.default = pkgs.mkShell {
        inputsFrom = [ lektra ];

        packages = with pkgs; [
          gdb
          clang-tools
        ];
      };
    };
}
