"""Setup script for maxcomp Python bindings."""
import os
import subprocess
import sys
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext

class CMakeBuild(build_ext):
    """Build the C library via CMake, then package the shared lib."""

    def build_extension(self, ext):
        source_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
        build_dir = os.path.join(source_dir, "build")
        os.makedirs(build_dir, exist_ok=True)

        cmake_args = [
            f"-DCMAKE_BUILD_TYPE=Release",
            f"-DBUILD_SHARED_LIBS=ON",
        ]
        subprocess.check_call(["cmake", "-S", source_dir, "-B", build_dir] + cmake_args)
        subprocess.check_call(["cmake", "--build", build_dir, "--config", "Release"])

        # Find the built shared library
        import glob
        patterns = [
            os.path.join(build_dir, "lib", "libmaxcomp.so*"),
            os.path.join(build_dir, "lib", "Release", "maxcomp.dll"),
            os.path.join(build_dir, "lib", "libmaxcomp.dylib"),
            os.path.join(build_dir, "bin", "libmaxcomp.so*"),
        ]
        for pattern in patterns:
            for lib in glob.glob(pattern):
                if os.path.isfile(lib):
                    self.copy_file(lib, os.path.join(self.build_lib, "maxcomp", os.path.basename(lib)))
                    return

        raise RuntimeError("Could not find built maxcomp shared library")


setup(
    name="maxcomp",
    version="2.1.1",
    description="High-ratio lossless data compression library",
    long_description=open(os.path.join(os.path.dirname(__file__), "..", "..", "README.md")).read(),
    long_description_content_type="text/markdown",
    author="Dreams-Makers Studio",
    author_email="contact@dreams-makers.com",
    url="https://github.com/SamDreamsMaker/Max-Compression",
    license="GPL-3.0",
    packages=["maxcomp"],
    package_dir={"maxcomp": "."},
    python_requires=">=3.7",
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: GNU General Public License v3 (GPLv3)",
        "Programming Language :: Python :: 3",
        "Programming Language :: C",
        "Topic :: System :: Archiving :: Compression",
    ],
    ext_modules=[Extension("maxcomp._native", sources=[])],
    cmdclass={"build_ext": CMakeBuild},
)
