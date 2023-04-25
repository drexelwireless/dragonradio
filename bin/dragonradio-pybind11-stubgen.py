import pybind11_stubgen
import re

def ndarray_replacement(match: re.Match):
    type_name = match.group(1)
    return f'numpy.typing.NDArray[{type_name}]'

if __name__ == '__main__':
    pybind11_stubgen.StubsGenerator.GLOBAL_CLASSNAME_REPLACEMENTS[re.compile('buffer')] = 'typing_extensions.Buffer'
    pybind11_stubgen.StubsGenerator.GLOBAL_CLASSNAME_REPLACEMENTS[re.compile(r'numpy.ndarray\[([^]]*)\]')] = ndarray_replacement

    pybind11_stubgen.main()
