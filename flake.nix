{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.11";

  outputs = { self, nixpkgs }:
    let
      forAllSystems = nixpkgs.lib.genAttrs [
        "x86_64-linux" "aarch64-linux"
        "x86_64-darwin" "aarch64-darwin"
      ];
    in {
      devShells = forAllSystems (system:
        let pkgs = nixpkgs.legacyPackages.${system};
        in {
          default = pkgs.mkShell {
            buildInputs = with pkgs; [
              rustup
              clang_18
              llvm_18
              libclang
              gperf
              bear
              stdenv.cc.cc.lib
            ];

            shellHook = ''
              export CXX="clang++"
              export LLVM_CONFIG_PATH="${pkgs.llvm_18}/bin/llvm-config"
              export LIBCLANG_PATH="${pkgs.libclang.lib}/lib"
              export LD_LIBRARY_PATH="${pkgs.stdenv.cc.cc.lib}/lib:$LD_LIBRARY_PATH"
            '';
          };
        });
    };
}

