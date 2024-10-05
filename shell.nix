let
  pkgs = import (fetchTarball
    ("https://github.com/goromal/anixpkgs/archive/refs/tags/v6.5.10.tar.gz"))
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
