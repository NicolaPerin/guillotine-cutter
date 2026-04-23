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
        "src/guillotine/core",    # for #include "solver_core.h"
    ],
    extra_objects=['build/gpu_solver.o'],
    
    # --- ADDED CUDA LINKING ARGS ---
    library_dirs=['/usr/local/cuda/lib64'],          # Build-time path to libcudart.so
    libraries=['cudart'],                            # Tells the linker to use libcudart
    runtime_library_dirs=['/usr/local/cuda/lib64'],  # Bakes the path into the .so for runtime (RPATH)
    # -------------------------------
    
    extra_compile_args=["-O3", "-march=native", "-fopenmp", "-Wall", "-Wextra"],
    extra_link_args=["-fopenmp"],
)

setup(ext_modules=[ext])