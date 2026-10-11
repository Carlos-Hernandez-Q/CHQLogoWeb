"""Compila y ejecuta el C++ real con adaptadores Arduino/socket de prueba.

No instala dependencias. Usa CXX, g++/clang++ en PATH o MinGW de esta PC.
Los ejecutables se crean en una carpeta temporal fuera del proyecto.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", help="Ruta al compilador g++/clang++")
    args = parser.parse_args()
    compiler = args.compiler or os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not compiler and Path("C:/mingw32/bin/g++.exe").is_file():
        compiler = "C:/mingw32/bin/g++.exe"
    if not compiler:
        parser.error("Instala g++/clang++ o indica --compiler. No se sustituyen los tests por simulación Python.")
    root = Path(__file__).resolve().parents[1]
    sources = root / "lib/CHQLogoWeb/src"
    with tempfile.TemporaryDirectory(prefix="chqlogo-native-") as directory:
        binary = Path(directory) / ("native_tests.exe" if os.name == "nt" else "native_tests")
        command = [compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O1",
                   "-I", str(root / "pruebas/native/stubs"), "-I", str(sources),
                   str(root / "pruebas/native/test_library.cpp"),
                   str(sources / "CHQLogoProtocol.cpp"), str(sources / "CHQLogoWeb.cpp"),
                   str(sources / "CHQLogoEsp32Transport.cpp"), "-o", str(binary)]
        environment = os.environ.copy()
        environment["PATH"] = str(Path(compiler).resolve().parent) + os.pathsep + environment.get("PATH", "")
        subprocess.run(command, check=True, env=environment, timeout=120)
        subprocess.run([str(binary)], check=True, env=environment, timeout=30)


if __name__ == "__main__":
    main()
