FROM debian:latest

WORKDIR /root
RUN apt-get update && apt-get install unzip
ADD build/hlcpp /root
ADD dockserv.sh /root

CMD ./dockserv.sh

