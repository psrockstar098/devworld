# Scala 3 toolchain. No trustworthy official Scala image exists, so we
# install via Coursier (the official Scala installer) onto a Temurin JDK.
FROM eclipse-temurin:21-jdk-jammy
ARG SCALA_VERSION=3.7.1
RUN apt-get update \
 && apt-get install -y --no-install-recommends curl ca-certificates \
 && curl -fsSL https://github.com/coursier/launchers/raw/master/cs-x86_64-pc-linux.gz \
      | gzip -d > /usr/local/bin/cs \
 && chmod +x /usr/local/bin/cs \
 && cs install --install-dir /usr/local/bin "scala:${SCALA_VERSION}" "scalac:${SCALA_VERSION}" \
 && scala --version \
 && rm -rf /root/.cache/coursier \
 && apt-get purge -y curl \
 && apt-get autoremove -y \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /work
