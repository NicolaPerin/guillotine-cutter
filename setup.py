from setuptools import setup, Extension
import numpy as np

ext = Extension(
    name="guillotine.core._solver",
    sources=[
        "src/guillotine/core/_solver.c",
        "src/guillotine/core/solver_core.c",
    ],
    include_dirs=[
        np.get_include(),
        "src/guillotine/core",
    ],
    extra_objects=['build/gpu_solver.o'],
    library_dirs=['/usr/local/cuda/lib64'],
    libraries=['cudart'],
    runtime_library_dirs=['/usr/local/cuda/lib64'],
    extra_compile_args=["-O3", "-march=native", "-fopenmp", "-Wall", "-Wextra"],
    extra_link_args=["-fopenmp"],
)

setup(
    package_dir={"": "src"},
    ext_modules=[ext],
)