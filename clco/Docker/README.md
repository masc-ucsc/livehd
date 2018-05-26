# Python-Wrapper Package via Docker.

Running Python-Wrapper C++ file via docker image (Unbuntu). This is done on Local Machine for now.

## Prerequisites

Have script.py and Dockerfile inside folder python_example2/.

## Getting Started
Add Dockerfile to path:
```
$ ~/pyth/python_example2
```
Execute:
```
$ sudo docker build -t pythWrapper .
$ sudo docker run pythWrapper
```

### Debugging

Any issues can be checked via running psudo terminal to check folder/package paths.

```
$ docker images
$ docker run -it [Image ID] /bin/bash

```
