#!/bin/bash
echo Checking that no uncommitted work exists...

lines=`git diff | wc -l`
if [ $lines -ne 0 ]; then
    echo Uncommitted changes are present. Hermetic Docker builds are supported
    echo only based on committed git state. Please commit and try again.
    exit 1
fi

echo Building Docker image...
dockerimg=`cd buildbase && docker build . | grep "Successfully built" | awk '{print $3}'`
if [ $? -ne 0 ]; then
    echo Error.
    exit 1
fi
echo Built Docker image: $dockerimg

echo Building autopiper...
rm -rf binaries
mkdir binaries
docker run -v `pwd`/binaries:/binaries -v `pwd`/../.git:/git $dockerimg || (echo "Error." && exit 1)

echo Produced binaries in ./binaries.

git=`git rev-parse HEAD`

echo Stamping build output with git rev $git and docker image $dockerimg.
echo "git $git"                >  binaries/versions.txt
echo "docker-build $dockerimg" >> binaries/versions.txt
