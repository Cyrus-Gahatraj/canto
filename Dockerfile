FROM debian:12

RUN apt-get update && apt-get install -y \
	gperf \
	gcc \
	g++ \
	bear \
	llvm \
	curl \
	vim \
	&& rm -rf /var/lib/apt/lists/*

RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y

ENV PATH="/root/.cargo/bin:${PATH}"

# Force debian to link with C++ std library
ENV RUSTFLAGS="-C link-arg=-lstdc++" 

WORKDIR /canto
COPY . .

