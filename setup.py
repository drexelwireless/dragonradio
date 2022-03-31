# Adapted from:
#   https://github.com/pybind/python_example
from pathlib import Path

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

TOPDIR = Path(__file__).parent

DEPENDENCIES = TOPDIR / 'extern'

ext_modules = [
    Pybind11Extension(
        '_dragonradio',
        sorted(['src/IQCompression.cc',
                'src/IQCompression/FLAC.cc',
                'src/Math.cc',
                'src/dsp/FIRDesign.cc',
                'src/dsp/FFTW.cc',
                'src/dsp/TableNCO.cc',
                'src/liquid/Filter.cc',
                'src/liquid/Modem.cc',
                'src/liquid/Mutex.cc',
                'src/liquid/OFDM.cc',
                'src/liquid/Resample.cc',
                'src/python/Channels.cc',
                'src/python/Filter.cc',
                'src/python/Header.cc',
                'src/python/IQBuffer.cc',
                'src/python/IQCompression.cc',
                'src/python/Liquid.cc',
                'src/python/Modem.cc',
                'src/python/NCO.cc',
                'src/python/Python.cc',
                'src/python/Resample.cc']),
        cxx_std=17,
        define_macros=[('NOUHD', '1'), ('PYMODULE', '1')],
        include_dirs=[
            DEPENDENCIES / 'xsimd/include',
            'src',
        ],
        libraries = ['liquid', 'FLAC', 'FLAC++', 'firpm'],
        library_dirs = ['/usr/local/lib'],
    ),
]

setup(ext_modules=ext_modules,
      use_scm_version = { 'relative_to': 'setup.py' })
