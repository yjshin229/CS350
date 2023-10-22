# Using ubuntu base image
FROM --platform=linux/amd64 ubuntu:20.04
# set environment to non-interactive:
ENV DEBIAN_FRONTEND=non-interactive


# Install necessary packages
RUN apt-get update && apt-get install -y build-essential cmake \
    make

RUN apt-get install -y time

# Set working directory
WORKDIR /workspace

# Command to run when the container starts
CMD ["/bin/bash"]
