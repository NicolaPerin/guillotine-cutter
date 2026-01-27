from setuptools import setup, find_packages
import os

# Read long description from README
long_description = ""
if os.path.exists("README.md"):
    with open("README.md", "r", encoding="utf-8") as f:
        long_description = f.read()

setup(
    name="guillotine-cutter",
    version="0.1.0",
    author="Nicola Perin",
    author_email="nicolaperin1998@gmail.com",
    description="Fast 2D guillotine cutting optimizer with defect handling",
    long_description=long_description,
    long_description_content_type="text/markdown",
    packages=find_packages(where="src"),
    package_dir={"": "src"},
    install_requires=[
        "numpy>=1.20.0",
        "matplotlib>=3.3.0",
    ],
    python_requires=">=3.8",
)
