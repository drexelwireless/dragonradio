import os
import setuptools
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
from setuptools.command.develop import develop
from setuptools.command.egg_info import egg_info
from setuptools.command.install import install
import subprocess
import sys

SRC = os.path.realpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../src'))
DEPENDENCIES = os.path.realpath(os.path.join(SRC, '../dependencies'))

#
# This is taken from:
#   https://github.com/pybind/python_example
#

class get_pybind_include(object):
    """Helper class to determine the pybind11 include path

    The purpose of this class is to postpone importing pybind11
    until it is actually installed, so that the ``get_include()``
    method can be invoked. """

    def __init__(self, user=False):
        self.user = user

    def __str__(self):
        import pybind11
        return pybind11.get_include(self.user)

# As of Python 3.6, CCompiler has a `has_flag` method.
# cf http://bugs.python.org/issue26689
def has_flag(compiler, flagname):
    """Return a boolean indicating whether a flag name is supported on
    the specified compiler.
    """
    import tempfile
    with tempfile.NamedTemporaryFile('w', suffix='.cpp') as f:
        f.write('int main (int argc, char **argv) { return 0; }')
        try:
            compiler.compile([f.name], extra_postargs=[flagname])
        except setuptools.distutils.errors.CompileError:
            return False
    return True

def cpp_flag(compiler):
    """Return the -std=c++17 compiler flag.
    """
    if has_flag(compiler, '-std=c++17'):
        return '-std=c++17'
    else:
        raise RuntimeError('Unsupported compiler -- at least C++17 support '
                           'is needed!')

ext_modules = [
    Extension(
        '_dragonradio',
        ['src/main.cpp',
         os.path.join(SRC, 'IQCompression.cc'),
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
         os.path.join(SRC, 'python/Filter.cc'),
         os.path.join(SRC, 'python/Header.cc'),
         os.path.join(SRC, 'python/IQBuffer.cc'),
         os.path.join(SRC, 'python/IQCompression.cc'),
         os.path.join(SRC, 'python/Liquid.cc'),
         os.path.join(SRC, 'python/Modem.cc'),
         os.path.join(SRC, 'python/NCO.cc'),
         os.path.join(SRC, 'python/Resample.cc')],
        define_macros=[('NOUHD', '1')],
        include_dirs=[
            # Path to pybind11 headers
            get_pybind_include(),
            get_pybind_include(user=True),
            os.path.join(DEPENDENCIES, 'xsimd/include'),
            'src',
            SRC,
        ],
        libraries = ['liquid', 'FLAC', 'FLAC++', 'firpm'],
        library_dirs = ['/usr/local/lib'],
        language='c++'
    ),
]

# See:
#   https://stackoverflow.com/questions/18725137/how-to-obtain-arguments-passed-to-setup-py-from-pip-with-install-option

class CommandMixin(object):
    user_options = [
        ('embedded', None, 'install module for use with embedded version of dragonradio')
    ]

    def initialize_options(self):
        super().initialize_options()
        # Initialize options
        self.embedded = False

    def finalize_options(self):
        # Validate options
        super().finalize_options()

    def run(self):
        if self.embedded:
            self.distribution.ext_modules = []

        super().run()

class InstallCommand(CommandMixin, install):
    user_options = getattr(install, 'user_options', []) + CommandMixin.user_options

class DevelopCommand(CommandMixin, develop):
    user_options = getattr(develop, 'user_options', []) + CommandMixin.user_options

class BuildExt(build_ext):
    """A custom build extension for adding compiler-specific options."""
    def build_extensions(self):
        opts = []
        opts.append(cpp_flag(self.compiler))
        if has_flag(self.compiler, '-fvisibility=hidden'):
            opts.append('-fvisibility=hidden')

        for ext in self.extensions:
            ext.extra_compile_args = opts

        build_ext.build_extensions(self)

setup(
    name='dragonradio',
    author='Geoffrey Mainland',
    author_email='mainland@drexel.edu',
    url='https://github.com/mainland/dragonradio',
    description='Provide access to DragonRadio primitives from Python',
    long_description='',
    ext_modules=ext_modules,
    packages=['dragonradio'
             ,'dragonradio.liquid'
             ,'sc2'
             ],
    scripts=['scripts/dragonradio-client'],
    use_scm_version = {
        'root': '../..',
        'relative_to': __file__,
        'write_to': 'python/dragonradio/dragonradio/version.py'
    },
    setup_requires=['pybind11>=2.2'
                   ,'setuptools_scm'
                   ],
    install_requires=['ipython==7.9.0'
                     ,'libconf==2.0.1'
                     ,'matplotlib==3.0.3'
                     ,'netifaces==0.10.9'
                     ,'numpy==1.18.5'
                     ,'pandas==0.24.2'
                     ,'protobuf==3.6.1'
                     ,'psutil==5.7.2'
                     ,'python-daemon==2.2.4'
                     ,'pyzmq==19.0.2'
                     ,'scipy==1.4.1'
                     ],
    cmdclass={
        'build_ext': BuildExt,
        'develop': DevelopCommand,
        'install': InstallCommand,
    },
    zip_safe=False,
)
