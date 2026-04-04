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
    extra_compile_args=["-O2", "-funroll-loops", "-fopenmp", "-Wall"],
    extra_link_args=["-fopenmp"],
)
 
setup(ext_modules=[ext])