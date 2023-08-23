# Adapted from:
#   https://github.com/pybind/python_example

# Available at setup time due to pyproject.toml
from pybind11.setup_helpers import Pybind11Extension
from setuptools import setup

ext_modules = [
    Pybind11Extension(
        'dragonradiocpp_tools_mgen',
        sorted([ 'csrc/main.cpp'
               , 'csrc/mgen.cpp'
               ]),
    ),
]

setup(ext_modules=ext_modules)
