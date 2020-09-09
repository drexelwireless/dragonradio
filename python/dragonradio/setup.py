import os
import setuptools
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
from setuptools.command.develop import develop
from setuptools.command.egg_info import egg_info
from setuptools.command.install import install
import subprocess
import sys

ROOT = os.path.realpath(os.environ['PWD'])
if not os.path.exists(os.path.join(ROOT, 'python', 'dragonradio', 'setup.py')):
    ROOT = os.path.realpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))

setup(
    name='dragonradio',
    author='Geoffrey Mainland',
    author_email='mainland@drexel.edu',
    url='https://github.com/mainland/dragonradio',
    description='Python support for DragonRadio',
    long_description='',
    packages=['dragonradio'
             ,'dragonradio.liquid'
             ,'sc2'
             ],
    scripts=['scripts/dragonradio-client'],
    use_scm_version = {
        'root': ROOT,
        'write_to': os.path.join(ROOT, 'python/dragonradio/dragonradio/version.py')
    },
    setup_requires=['setuptools_scm'],
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
    zip_safe=False,
)
