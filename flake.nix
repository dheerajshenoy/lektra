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

        nativeBuildInputs = with pkgs; [
          cmake
          pkg-config
          gnumake
          qt6.wrapQtAppsHook
        ];

        buildInputs = with pkgs; [
          qt6.qtbase
          qt6.qttools
          zlib
          djvulibre
          texlive.bin.core
        ];

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
