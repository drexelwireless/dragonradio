from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
import sys
import setuptools

__version__ = '0.0.1'

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

ext_modules = [
    Extension(
        'dragonradio',
        ['src/main.cpp',
         '../../../src/dsp/NCO.cc',
         '../../../src/liquid/Filter.cc',
         '../../../src/liquid/Resample.cc',
         '../../../src/python/Filter.cc',
         '../../../src/python/LiquidEnum.cc',
         '../../../src/python/MCS.cc',
         '../../../src/python/NCO.cc',
         '../../../src/python/Resample.cc',
         'src/PHY.cc',
         'src/FlexFrame.cc',
         'src/OFDM.cc'],
        define_macros=[('NOUHD', '1')],
        include_dirs=[
            # Path to pybind11 headers
            get_pybind_include(),
            get_pybind_include(user=True),
            'src',
            '../../../src',
        ],
        libraries = ['liquid'],
        library_dirs = ['/usr/local/lib'],
        language='c++'
    ),
]

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

class BuildExt(build_ext):
    """A custom build extension for adding compiler-specific options."""
    c_opts = {
        'msvc': ['/EHsc'],
        'unix': [],
    }

    if sys.platform == 'darwin':
        c_opts['unix'] += ['-stdlib=libc++', '-mmacosx-version-min=10.7']

    def build_extensions(self):
        ct = self.compiler.compiler_type
        opts = self.c_opts.get(ct, [])
        if ct == 'unix':
            opts.append('-DVERSION_INFO="%s"' % self.distribution.get_version())
            opts.append(cpp_flag(self.compiler))
            if has_flag(self.compiler, '-fvisibility=hidden'):
                opts.append('-fvisibility=hidden')
        elif ct == 'msvc':
            opts.append('/DVERSION_INFO=\\"%s\\"' % self.distribution.get_version())
        for ext in self.extensions:
            ext.extra_compile_args = opts
        build_ext.build_extensions(self)

setup(
    name='dragonradio',
    version=__version__,
    author='Geoffrey Mainland',
    author_email='mainland@drexel.edu',
    url='https://github.com/mainland/dragonradio',
    description='Provide access to DragonRadio primitives from Python',
    long_description='',
    ext_modules=ext_modules,
    setup_requires=['pybind11>=2.2'],
    cmdclass={'build_ext': BuildExt},
    zip_safe=False,
)