from setuptools import setup

# See:
#   https://github.com/pypa/setuptools_scm/issues/190
try:
    import setuptools_scm.integration
    setuptools_scm.integration.find_files = lambda _: []
except ImportError:
    pass

setup(
    use_scm_version = {
        'root': '../..',
        'relative_to': 'setup.py',
        'write_to': 'python/dragonradio/dragonradio/radio/version.py'
    })
