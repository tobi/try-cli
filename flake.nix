{
  description = "try - fresh directories for every vibe (C implementation)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-parts.url = "github:hercules-ci/flake-parts";
  };

  outputs = inputs@{ flake-parts, ... }:
    flake-parts.lib.mkFlake { inherit inputs; } {
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];

      flake = {
        homeModules.default = { config, lib, pkgs, ... }:
          with lib;
          let
            cfg = config.programs.try;
          in
          {
            options.programs.try = {
              enable = mkEnableOption "try - fresh directories for every vibe";

              package = mkOption {
                type = types.package;
                default = inputs.self.packages.${pkgs.stdenv.hostPlatform.system}.default;
                defaultText = literalExpression "inputs.self.packages.\${pkgs.stdenv.hostPlatform.system}.default";
                description = "The try package to use.";
              };

              path = mkOption {
                type = types.str;
                default = "~/src/tries";
                description = "Path where try directories will be stored.";
              };
            };

            config = mkIf cfg.enable {
              programs.bash.initExtra = mkIf config.programs.bash.enable ''
                eval "$(${cfg.package}/bin/try init ${cfg.path})"
              '';

              programs.zsh.initContent = mkIf config.programs.zsh.enable ''
                eval "$(${cfg.package}/bin/try init ${cfg.path})"
              '';

              programs.fish.shellInit = mkIf config.programs.fish.enable ''
                eval (${cfg.package}/bin/try init ${cfg.path} | string collect)
              '';
            };
          };

        # Backwards compatibility - deprecated
        homeManagerModules.default = builtins.trace
          "WARNING: homeManagerModules is deprecated and will be removed in a future version. Please use homeModules instead."
          inputs.self.homeModules.default;
      };

      perSystem = { config, self', inputs', pkgs, system, ... }: {
        packages = {
          default = pkgs.stdenv.mkDerivation {
            pname = "try";
            version = builtins.replaceStrings ["\n"] [""] (builtins.readFile ./VERSION);

            src = inputs.self;

            nativeBuildInputs = with pkgs; [
              gcc
              gnumake
            ];

            buildPhase = ''
              make
            '';

            installPhase = ''
              mkdir -p $out/bin
              cp dist/try $out/bin/
            '';

            meta = with pkgs.lib; {
              description = "Fresh directories for every vibe - C implementation";
              homepage = "https://github.com/tobi/try-cli";
              license = licenses.mit;
              maintainers = [ "tobi@shopify.com" ];
              platforms = platforms.unix;
            };
          };

          # Cross-compilation packages (statically linked with musl for portability)
          x86_64-linux = pkgs.pkgsStatic.stdenv.mkDerivation {
            pname = "try";
            version = builtins.replaceStrings ["\n"] [""] (builtins.readFile ./VERSION);

            src = inputs.self;

            nativeBuildInputs = with pkgs.pkgsStatic; [
              gcc
              gnumake
            ];

            # Force static linking for portable binary
            buildPhase = ''
              make LDFLAGS="-static"
            '';

            installPhase = ''
              mkdir -p $out/bin
              cp dist/try $out/bin/
            '';

            meta = with pkgs.lib; {
              description = "Fresh directories for every vibe - C implementation (x86_64-linux)";
              homepage = "https://github.com/tobi/try-cli";
              license = licenses.mit;
              maintainers = [ "tobi@shopify.com" ];
              platforms = [ "x86_64-linux" ];
            };
          };

          aarch64-linux = pkgs.pkgsCross.aarch64-multiplatform-musl.stdenv.mkDerivation {
            pname = "try";
            version = builtins.replaceStrings ["\n"] [""] (builtins.readFile ./VERSION);

            src = inputs.self;

            nativeBuildInputs = with pkgs.pkgsCross.aarch64-multiplatform-musl; [
              gcc
              gnumake
            ];

            # Force static linking for portable binary
            buildPhase = ''
              make LDFLAGS="-static"
            '';

            installPhase = ''
              mkdir -p $out/bin
              cp dist/try $out/bin/
            '';

            meta = with pkgs.lib; {
              description = "Fresh directories for every vibe - C implementation (aarch64-linux)";
              homepage = "https://github.com/tobi/try-cli";
              license = licenses.mit;
              maintainers = [ "tobi@shopify.com" ];
              platforms = [ "aarch64-linux" ];
            };
          };

          x86_64-darwin = pkgs.pkgsCross.x86_64-darwin.stdenv.mkDerivation {
            pname = "try";
            version = builtins.replaceStrings ["\n"] [""] (builtins.readFile ./VERSION);

            src = inputs.self;

            nativeBuildInputs = with pkgs.pkgsCross.x86_64-darwin; [
              gcc
              gnumake
            ];

            buildPhase = ''
              make
            '';

            installPhase = ''
              mkdir -p $out/bin
              cp dist/try $out/bin/
            '';

            meta = with pkgs.lib; {
              description = "Fresh directories for every vibe - C implementation (x86_64-darwin)";
              homepage = "https://github.com/tobi/try-cli";
              license = licenses.mit;
              maintainers = [ "tobi@shopify.com" ];
              platforms = [ "x86_64-darwin" ];
            };
          };

          aarch64-darwin = pkgs.pkgsCross.aarch64-darwin.stdenv.mkDerivation {
            pname = "try";
            version = builtins.replaceStrings ["\n"] [""] (builtins.readFile ./VERSION);

            src = inputs.self;

            nativeBuildInputs = with pkgs.pkgsCross.aarch64-darwin; [
              gcc
              gnumake
            ];

            buildPhase = ''
              make
            '';

            installPhase = ''
              mkdir -p $out/bin
              cp dist/try $out/bin/
            '';

            meta = with pkgs.lib; {
              description = "Fresh directories for every vibe - C implementation (aarch64-darwin)";
              homepage = "https://github.com/tobi/try-cli";
              license = licenses.mit;
              maintainers = [ "tobi@shopify.com" ];
              platforms = [ "aarch64-darwin" ];
            };
          };
        };

        apps.default = {
          type = "app";
          program = "${self'.packages.default}/bin/try";
        };

        devShells.default = pkgs.mkShell {
          buildInputs = with pkgs; [
            gcc
            gnumake
            valgrind
            gdb
          ];

          shellHook = ''
            echo "try development environment"
            echo "Run 'make' to build"
            echo "Run 'make test' to run tests"
          '';
        };
      };
    };
}
