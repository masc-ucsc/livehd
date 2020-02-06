#!/bin/bash
git clone https://github.com/Xilinx/RapidWright.git
cd RapidWright
export RAPIDWRIGHT_PATH=$(pwd)
wget https://github.com/Xilinx/RapidWright/releases/download/v2019.2.0-beta/rapidwright_data.zip
unzip rapidwright_data.zip
wget https://github.com/Xilinx/RapidWright/releases/download/v2019.2.0-beta/rapidwright_jars.zip
unzip rapidwright_jars.zip

export CLASSPATH=$RAPIDWRIGHT_PATH/bin:$(echo $RAPIDWRIGHT_PATH/jars/*.jar | tr ' ' ':')
gradle build -p $RAPIDWRIGHT_PATH
