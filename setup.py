from setuptools import setup, Extension
import numpy as np

ext = Extension(
    name="guillotine.core._solver",
    sources=["src/guillotine/core/_solver.c"],
    include_dirs=[np.get_include()],
    extra_compile_args=["-O2", "-funroll-loops", "-Wall"],
)

setup(ext_modules=[ext])