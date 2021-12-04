# Adapted from:
#   https://github.com/pybind/python_example

# Available at setup time due to pyproject.toml
from pybind11.setup_helpers import Pybind11Extension
from setuptools import setup

# See:
#   https://github.com/pypa/setuptools_scm/issues/190
try:
    import setuptools_scm.integration
    setuptools_scm.integration.find_files = lambda _: []
except ImportError:
    pass

ext_modules = [
    Pybind11Extension(
        '_dragonradio_tools_mgen',
        sorted([ 'src/main.cpp'
               , 'src/mgen.cpp'
               ]),
    ),
]

setup(
    ext_modules=ext_modules,
    use_scm_version = {
        'root': '../..',
        'relative_to': 'setup.py',
        'write_to': 'python/dragonradio/dragonradio/radio/version.py'
    })
