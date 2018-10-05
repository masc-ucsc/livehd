#!/bin/bash

FROM google/cloud-sdk:alpine
#FROM node:6.9-alpine
MAINTAINER Tanvir Heer <tanvirheer@gmail.com>
#RUN gcloud config set account tanvirheer@gmail.com
#RUN gcloud config set project lgshell-cloud
WORKDIR /tmp
COPY . /tmp

#RUN gcloud init
#RUN gcloud auth activate-service-account --key-file application_default_credentials.json
VOLUME ["/application_default_credentials.json"]
RUN gcloud config set project lgshell-cloud
RUN gcloud config set account tanvirheer@gmail.com
RUN gcloud functions deploy helloworld --runtime nodejs6 --trigger-http --project lgshell-cloud

#RUN mkdir -p /usr/src/app
#WORKDIR /usr/src/app
#COPY . /usr/src/app


#RUN gcloud functions deploy helloGET --entry-point helloworld --runtime nodejs6 --trigger-http
