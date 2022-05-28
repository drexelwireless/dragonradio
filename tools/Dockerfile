FROM ubuntu:20.04 AS atf
ENV PYTHONDONTWRITEBYTECODE=1
ENV PYTHONUNBUFFERED=1
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

#
# Update
#
RUN apt-get update \
  && apt-get upgrade -y \
  && apt-get install -y \
  apt-utils \
  && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

#
# Install required packages
#
RUN apt-get update \
  && apt-get install -y \
    curl \
    git \
    netcat \
    rsync \
    sudo \
    vim \
    wget \
    # For protobuf
    autoconf \
    automake \
    libtool \
    curl \
    make \
    g++ \
    unzip \
    # For DragonRadio
    autoconf \
    automake \
    build-essential \
    cmake \
    libboost-date-time-dev \
    libboost-dev \
    libboost-filesystem-dev \
    libboost-program-options-dev \
    libboost-regex-dev \
    libboost-serialization-dev \
    libboost-system-dev \
    libboost-test-dev \
    libboost-thread-dev \
    libcap-dev \
    libeigen3-dev \
    libfftw3-dev \
    libflac8 \
    libflac++-dev \
    libflac-dev \
    libhdf5-dev \
    python3 \
    python3-dev \
    python3-distutils \
    python3-mako \
    python3-pip \
    python3-virtualenv \
    virtualenv \
    # For DragonRadio tools
    gir1.2-gtk-3.0 \
    libcairo2-dev pkg-config \
    libffi-dev \
    libgirepository1.0-dev \
    libsnappy-dev \
    python3.8-tk \
  && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

#
# Install protobuf
#
ARG PROTOBUF_VERSION=3.19.4
RUN wget https://github.com/google/protobuf/releases/download/v${PROTOBUF_VERSION}/protobuf-python-${PROTOBUF_VERSION}.tar.gz -O /tmp/protobuf.tar.gz \
 && cd /tmp \
 && tar xf protobuf.tar.gz \
 && cd protobuf-${PROTOBUF_VERSION} \
 && ./configure \
 && make -j10 \
 && make install \
 && ldconfig \
 && rm -rf /tmp/protobuf-${PROTOBUF_VERSION}

#
# Create dragonradio user
#
ARG USERNAME=dragonradio
ARG USER_UID=1000
ARG USER_GID=$USER_UID

RUN groupadd --gid $USER_GID $USERNAME \
  && useradd --uid $USER_UID --gid $USER_GID -m $USERNAME \
  && echo $USERNAME ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/$USERNAME \
  && chmod 0440 /etc/sudoers.d/$USERNAME

#
# Install dragonradio libraries
#
COPY . /dragonradio
RUN cd /dragonradio \
  && SETUPTOOLS_SCM_PRETEND_VERSION=1.0.0 ./build.sh \
  && rm -rf build venv \
  && virtualenv -p python3.8 /venv \
  && . /venv/bin/activate \
  && pip install --upgrade pip \
  && SETUPTOOLS_SCM_PRETEND_VERSION=1.0.0 pip install -r tools/requirements.txt \
       . \
       python/dragonradio \
       python/dragonradio-tools \
  && rm -rf /dragonradio \
  && chown -R $USER_UID:$USER_GID /venv

#
# Run everything from here on out as the atf user
#
USER $USERNAME

WORKDIR /venv

ENV PATH /venv/bin:$PATH
ENV XDG_CACHE_HOME /cache
ENV MPLCONFIGDIR /cache/mpl

VOLUME ["/cache", "/common_logs"]

CMD ["/bin/bash"]
