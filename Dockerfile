# NOTE: You might have to change this distribution or version depending on where the ac-display executable was compiled
FROM fedora:38

RUN dnf -y update && dnf clean all

COPY output/lib/libmicrohttpd*.so* /lib64/
ADD resources /root/ac-display/resources
COPY configuration.json server.crt server.key ac-display /root/ac-display/

EXPOSE 9997/udp
EXPOSE 8070/tcp

WORKDIR /root/ac-display/

ENTRYPOINT /root/ac-display/ac-display
