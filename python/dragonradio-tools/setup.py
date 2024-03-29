from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
import sys
import setuptools

class get_pybind_include(object):
    """Helper class to determine the pybind11 include path

    The purpose of this class is to postpone importing pybind11
    until it is actually installed, so that the ``get_include()``
    method can be invoked. """

    def __str__(self):
        import pybind11
        return pybind11.get_include()

# cf http://bugs.python.org/issue26689
def has_flag(compiler, flagname):
    """Return a boolean indicating whether a flag name is supported on
    the specified compiler.
    """
    import tempfile
    import os
    with tempfile.NamedTemporaryFile('w', suffix='.cpp', delete=False) as f:
        f.write('int main (int argc, char **argv) { return 0; }')
        fname = f.name
    try:
        compiler.compile([fname], extra_postargs=[flagname])
    except setuptools.distutils.errors.CompileError:
        return False
    finally:
        try:
            os.remove(fname)
        except OSError:
            pass
    return True

def cpp_flag(compiler):
    """Return the -std=c++17 compiler flag."""
    flags = ['-std=c++17']

    for flag in flags:
        if has_flag(compiler, flag):
            return flag

    raise RuntimeError('Unsupported compiler -- at least C++11 support '
                       'is needed!')

class BuildExt(build_ext):
    """A custom build extension for adding compiler-specific options."""
    c_opts = {
        'msvc': ['/EHsc'],
        'unix': [],
    }
    l_opts = {
        'msvc': [],
        'unix': [],
    }

    if sys.platform == 'darwin':
        darwin_opts = ['-stdlib=libc++', '-mmacosx-version-min=10.7']
        c_opts['unix'] += darwin_opts
        l_opts['unix'] += darwin_opts

    def build_extensions(self):
        ct = self.compiler.compiler_type
        opts = self.c_opts.get(ct, [])
        link_opts = self.l_opts.get(ct, [])
        if ct == 'unix':
            opts.append(cpp_flag(self.compiler))
            if has_flag(self.compiler, '-fvisibility=hidden'):
                opts.append('-fvisibility=hidden')

        for ext in self.extensions:
            ext.define_macros = [('VERSION_INFO', '"{}"'.format(self.distribution.get_version()))]
            ext.extra_compile_args = opts
            ext.extra_link_args = link_opts
        build_ext.build_extensions(self)

__version__ = '0.0.1'

ext_modules = [
    Extension(
        '_dragonradio_tools_mgen',
        # Sort input source files to ensure bit-for-bit reproducible builds
        # (https://github.com/pybind/python_example/pull/53)
        sorted([ 'src/main.cpp'
               , 'src/mgen.cpp'
               ]),
        include_dirs=[
            # Path to pybind11 headers
            get_pybind_include(),
        ],
        language='c++'
    ),
]

setup(
    name='dragonradio-tools',
    version=__version__,
    author='Geoffrey Mainland',
    author_email='mainland@drexel.edu',
    url='https://github.com/drexelwireless/dragonradio',
    description='DragonRadio data collection and processing tools',
    long_description='',
    ext_modules=ext_modules,
    packages=['dragonradio'],
    setup_requires=['pybind11>=2.5.0'],
    cmdclass={'build_ext': BuildExt},
    install_requires=['fastparquet==0.7.1'
                     ,'h5py==3.4.0'
                     ,'importlib_resources==5.3.0'
                     ,'matplotlib==3.4.3'
                     ,'numpy==1.21.2'
                     ,'pandas==1.3.4'
                     ,'pycairo==1.20.1'
                     ,'PyGObject==3.42.0'
                     ,'pyparsing<3'
                     ,'python-snappy==0.6.0'
                     ,'requests==2.26.0'
                     ,'scipy==1.7.1'
                     ],
    entry_points = {
        'console_scripts':
            [ 'plot-mgen-metric=dragonradio.tools.plot.mgen.command_line:plot_mgen_metric'
            , 'plot-score=dragonradio.tools.plot.colosseum.command_line:plot_score'
            , 'dumpevents=dragonradio.tools.plot.radio.command_line:dump_events'
            , 'plot-events=dragonradio.tools.plot.radio.command_line:plot_events'
            , 'plot-traffic=dragonradio.tools.plot.radio.command_line:plot_traffic'
            , 'plot-radio-metric=dragonradio.tools.plot.radio.command_line:plot_radio_metric'
            , 'drgui=dragonradio.tools.plot.radio.drgui:drgui'
            ],
    },
    zip_safe=False,
)
