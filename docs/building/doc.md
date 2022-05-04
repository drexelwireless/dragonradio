# Building the Documentation

The documentation can be built as follows:

```shell-session
$ source venv/bin/activate
$ cd docs
$ sudo apt install doxygen
$ pip install -Ur requirements.txt
$ make html
```
