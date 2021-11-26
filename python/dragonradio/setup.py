from setuptools import setup

setup(
    name='dragonradio',
    author='Geoffrey Mainland',
    author_email='mainland@drexel.edu',
    url='https://github.com/mainland/dragonradio',
    description='Python support for DragonRadio',
    long_description='',
    packages=['dragonradio'
             ,'dragonradio.liquid'
             ,'dragonradio.radio'
             ,'sc2'
             ],
    scripts=['scripts/dragonradio-client'],
    use_scm_version = {
        'root': '../..',
        'relative_to': 'setup.py',
        'write_to': 'python/dragonradio/dragonradio/radio/version.py'
    },
    setup_requires=['setuptools_scm'],
    install_requires=['ipython==7.28.0'
                     ,'libconf==2.0.1'
                     ,'netifaces==0.11.0'
                     ,'numpy==1.21.2'
                     ,'pandas==1.3.4'
                     ,'protobuf==3.13.0'
                     ,'psutil==5.8.0'
                     ,'python-daemon==2.3.0'
                     ,'pyzmq==19.0.2'
                     ,'scipy==1.7.1'
                     ],
    zip_safe=False,
)
