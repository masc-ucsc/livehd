#!/bin/bash

# Download pre-build prometheus and alertmanager binaries
# Check the latest version(https://prometheus.io/download/)
: ${PROMETHEUS_VERSION:=2.23.0}
: ${ALERTMANAGER_VERSION:=0.21.0}

wget https://github.com/prometheus/prometheus/releases/download/v$PROMETHEUS_VERSION/prometheus-$PROMETHEUS_VERSION.linux-amd64.tar.gz
tar -xvzf prometheus-$PROMETHEUS_VERSION.linux-amd64.tar.gz
mv prometheus-$PROMETHEUS_VERSION.linux-amd64 prometheus

wget https://github.com/prometheus/alertmanager/releases/download/v$ALERTMANAGER_VERSION/alertmanager-$ALERTMANAGER_VERSION.linux-amd64.tar.gz
tar -xvzf alertmanager-$ALERTMANAGER_VERSION.linux-amd64.tar.gz
mv alertmanager-$ALERTMANAGER_VERSION.linux-amd64 alertmanager

# Copy config files for the livehd exporter
cp prometheus.yml ./prometheus
cp alert.rules.yml ./prometheus
./prometheus/promtool check rules ./prometheus/alert.rules.yml
cp alert.tmpl ./prometheus
cp alertmanager.yml ./alertmanager
