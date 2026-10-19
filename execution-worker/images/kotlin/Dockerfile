# Kotlin 2.2 toolchain. JetBrains publishes no official Docker image, so we
# install the compiler from the official GitHub release onto a Temurin JDK.
FROM eclipse-temurin:21-jdk-jammy
ARG KOTLIN_VERSION=2.2.0
RUN apt-get update \
 && apt-get install -y --no-install-recommends curl unzip ca-certificates \
 && curl -fsSL -o /tmp/kotlin.zip \
      "https://github.com/JetBrains/kotlin/releases/download/v${KOTLIN_VERSION}/kotlin-compiler-${KOTLIN_VERSION}.zip" \
 && unzip -q /tmp/kotlin.zip -d /opt \
 && rm /tmp/kotlin.zip \
 && /opt/kotlinc/bin/kotlinc -version \
 && apt-get purge -y curl unzip \
 && apt-get autoremove -y \
 && rm -rf /var/lib/apt/lists/*
ENV PATH="/opt/kotlinc/bin:${PATH}"
WORKDIR /work
