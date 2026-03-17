FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
	cmake g++ make libcurl4-openssl-dev ca-certificates \
	&& rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . /app

RUN cmake -S . -B build && cmake --build build -j

EXPOSE 8080
CMD ["/app/build/routing_hub"]
