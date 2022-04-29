# Adapted from:
#   https://github.com/pybind/python_example

# Available at setup time due to pyproject.toml
from pybind11.setup_helpers import Pybind11Extension
from setuptools import setup

ext_modules = [
    Pybind11Extension(
        '_dragonradio_tools_mgen',
        sorted([ 'src/main.cpp'
               , 'src/mgen.cpp'
               ]),
    ),
]

setup(ext_modules=ext_modules)
