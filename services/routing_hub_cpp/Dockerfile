FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
	cmake g++ make libcurl4-openssl-dev ca-certificates curl \
	&& rm -rf /var/lib/apt/lists/*

RUN curl -sL https://raw.githubusercontent.com/metacall/install/master/install.sh | sh

RUN METACALL_LIB=$(find /gnu/store -name "libmetacall.so" -print -quit 2>/dev/null | xargs dirname) \
	&& echo "METACALL_LIB=${METACALL_LIB}" \
	&& ln -sf "${METACALL_LIB}/libmetacall.so" /usr/local/lib/libmetacall.so \
	&& find "${METACALL_LIB}" -name "*.so" -exec ln -sf {} /usr/local/lib/ \; \
	&& METACALL_INC=$(find /gnu/store -path "*/include/metacall/metacall.h" -print -quit 2>/dev/null | xargs dirname | xargs dirname) \
	&& ln -sf "${METACALL_INC}/metacall" /usr/local/include/metacall \
	&& ldconfig

ENV LOADER_LIBRARY_PATH=/usr/local/lib
ENV SERIAL_LIBRARY_PATH=/usr/local/lib

WORKDIR /app
COPY . /app

RUN cmake -S . -B build && cmake --build build -j

EXPOSE 8080
CMD ["/app/build/routing_hub"]
