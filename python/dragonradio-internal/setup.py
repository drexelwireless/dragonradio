# Adapted from:
#   https://github.com/pybind/python_example

import os

# Available at setup time due to pyproject.toml
from pybind11.setup_helpers import Pybind11Extension
from setuptools import setup

ROOT = os.path.realpath(os.environ['PWD'])
if not os.path.exists(os.path.join(ROOT, 'python', 'dragonradio', 'setup.py')):
    ROOT = os.path.realpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))

SRC = os.path.realpath(os.path.join(ROOT, 'src'))
DEPENDENCIES = os.path.realpath(os.path.join(ROOT, 'dependencies'))

ext_modules = [
    Pybind11Extension(
        '_dragonradio',
        [os.path.join(SRC, 'IQCompression.cc'),
         os.path.join(SRC, 'IQCompression/FLAC.cc'),
         os.path.join(SRC, 'Math.cc'),
         os.path.join(SRC, 'dsp/FIRDesign.cc'),
         os.path.join(SRC, 'dsp/FFTW.cc'),
         os.path.join(SRC, 'dsp/TableNCO.cc'),
         os.path.join(SRC, 'liquid/Filter.cc'),
         os.path.join(SRC, 'liquid/Modem.cc'),
         os.path.join(SRC, 'liquid/Mutex.cc'),
         os.path.join(SRC, 'liquid/OFDM.cc'),
         os.path.join(SRC, 'liquid/Resample.cc'),
         os.path.join(SRC, 'python/Channels.cc'),
         os.path.join(SRC, 'python/Filter.cc'),
         os.path.join(SRC, 'python/Header.cc'),
         os.path.join(SRC, 'python/IQBuffer.cc'),
         os.path.join(SRC, 'python/IQCompression.cc'),
         os.path.join(SRC, 'python/Liquid.cc'),
         os.path.join(SRC, 'python/Modem.cc'),
         os.path.join(SRC, 'python/NCO.cc'),
         os.path.join(SRC, 'python/Python.cc'),
         os.path.join(SRC, 'python/Resample.cc')],
        cxx_std=17,
        define_macros=[('NOUHD', '1'), ('PYMODULE', '1')],
        include_dirs=[
            os.path.join(DEPENDENCIES, 'xsimd/include'),
            SRC,
        ],
        libraries = ['liquid', 'FLAC', 'FLAC++', 'firpm'],
        library_dirs = ['/usr/local/lib'],
    ),
]

setup(
    name='dragonradio-internal',
    author='Geoffrey Mainland',
    author_email='mainland@drexel.edu',
    url='https://github.com/mainland/dragonradio',
    description='Python interface to DragonRadio',
    long_description='',
    ext_modules=ext_modules,
    use_scm_version = {
        'root': ROOT
    },
    zip_safe=False,
    python_requires=">=3.6",
)
