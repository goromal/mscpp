let
  pkgs = import (fetchTarball
    ("https://github.com/goromal/anixpkgs/archive/refs/tags/v7.7.0.tar.gz"))
    { };
in with pkgs;
mkShell {
  nativeBuildInputs = [ cmake ];
  buildInputs = [
    spdlog
    catch2
  ];
}
