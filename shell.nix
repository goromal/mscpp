let
  pkgs = import (fetchTarball
    # ("https://github.com/goromal/anixpkgs/archive/refs/tags/v6.5.9.tar.gz"))
    ("https://github.com/goromal/anixpkgs/archive/refs/heads/dev/cpp-sanitizers.tar.gz"))
    { };
in with pkgs;
mkShell {
  nativeBuildInputs = [ cpp-helper cmake ];
  buildInputs = [
    boost
  ];
  shellHook = ''
    cpp-helper vscode
  '';
}
