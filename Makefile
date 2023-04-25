.PHONY: stubs
stubs:
	./venv/bin/pip install "pybind11-stubgen==2.2.2"
	./venv/bin/dragonpython ./venv/bin/pybind11-stubgen \
		--numpy-array-remove-parameters \
		--enum-class-locations Strategy:dragonradiocpp.signal.firpm \
		-o python/dragonradiocpp-stubs/src \
		dragonradiocpp
