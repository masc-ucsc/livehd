#!/bin/bash

#Download ubuntu from the docker hub

FROM ubuntu:latest

#Download python3.6, git, pip3

RUN apt-get update && apt-get install -y \
    software-properties-common
RUN add-apt-repository universe
RUN apt-get update && apt-get install -y \
    python3.6 \
    git \
    python3-pip

#FROM gcc:4.9
#FROM fenicsproject/pybind11-testing
#FROM vaeum/ubuntu-python3-pip3
#FROM brumbrum/python3-develop
#FROM deifwpt/build-essential
#FROM rikorose/gcc-cmake

RUN git clone https://github.com/pybind/cmake_example


#COPY CMakeLists.txt /
#RUN mkdir -p /build
#WORKDIR build
#RUN cd /build
#RUN cmake ..
#RUN make check -j 4

COPY . /cmake_example

WORKDIR /cmake_example

RUN pip3 install /cmake_example

ADD script.py /

CMD ["python3", "./script.py"]
# Run the application inside the image

#RUN g++ -o pythTest /cmake_example/src/main.cpp
