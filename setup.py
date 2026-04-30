from setuptools import setup, Extension
import numpy as np
import subprocess
import os
import shutil


def cuda_available():
    return shutil.which('nvcc') is not None


def get_gpu_arch():
    try:
        out = subprocess.check_output(
            ['nvidia-smi', '--query-gpu=compute_cap', '--format=csv,noheader'],
            stderr=subprocess.DEVNULL).decode().strip()
        # Handle multi-GPU systems — take the first result
        cap = out.splitlines()[0].strip()
        return 'sm_' + cap.replace('.', '')
    except Exception:
        return None


extra_objects  = []
define_macros  = []
library_dirs   = []
libraries      = []
runtime_dirs   = []

if cuda_available():
    arch    = get_gpu_arch()
    gpu_obj = 'build/gpu_solver.o'
    os.makedirs('build', exist_ok=True)

    cmd = [
        'nvcc', '-c', '-O3', '-Xcompiler', '-fPIC',
        'src/guillotine/core/gpu_solver.cu',
        '-o', gpu_obj,
    ]
    if arch:
        cmd.insert(3, f'-arch={arch}')

    subprocess.run(cmd, check=True)

    extra_objects.append(gpu_obj)
    define_macros.append(('HAVE_CUDA', '1'))
    library_dirs  = ['/usr/local/cuda/lib64']
    libraries     = ['cudart']
    runtime_dirs  = ['/usr/local/cuda/lib64']

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
    extra_objects=extra_objects,
    define_macros=define_macros,
    library_dirs=library_dirs,
    libraries=libraries,
    runtime_library_dirs=runtime_dirs,
    extra_compile_args=["-O3", "-march=native", "-fopenmp", "-Wall", "-Wextra"],
    extra_link_args=["-fopenmp"],
)

setup(
    package_dir={"": "src"},
    ext_modules=[ext],
)