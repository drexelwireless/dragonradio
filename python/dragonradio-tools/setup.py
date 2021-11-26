# Adapted from:
#   https://github.com/pybind/python_example

# Available at setup time due to pyproject.toml
from pybind11.setup_helpers import Pybind11Extension
from setuptools import setup, find_namespace_packages

__version__ = '0.0.1'

ext_modules = [
    Pybind11Extension(
        '_dragonradio_tools_mgen',
        sorted([ 'src/main.cpp'
               , 'src/mgen.cpp'
               ]),
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
    packages=find_namespace_packages(include=['dragonradio.*']),include_package_data=True,
    setup_requires=['pybind11>=2.5.0'],
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
