FROM ubuntu
MAINTAINER Chris Fallin <cfallin@c1f.net>

RUN apt-get update
RUN apt-get install -y g++ libboost-dev cmake
RUN apt-get install -y git
RUN apt-get install -y libgmp-dev

ENV SOURCEREPO /git
ENV DESTPATH /binaries

VOLUME ["/git"]
VOLUME ["/binaries"]

ADD build.sh /build.sh
ENTRYPOINT ["bash", "/build.sh"]
