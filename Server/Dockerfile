FROM node:22-bookworm-slim AS native-build
RUN apt-get update \
    && apt-get install -y --no-install-recommends cmake g++ ninja-build \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY Source/Cinderline/Public/Sim ./Source/Cinderline/Public/Sim
COPY Source/Cinderline/Private/Sim ./Source/Cinderline/Private/Sim
COPY Server/MatchWorker.cpp ./Server/MatchWorker.cpp
RUN cmake -S . -B /build -G Ninja \
      -DCINDERLINE_BUILD_NATIVE=OFF \
      -DBUILD_TESTING=OFF \
      -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /build --target CinderlineMatchWorker --parallel 2

FROM node:22-bookworm-slim AS node-dependencies
WORKDIR /app/Server
COPY Server/package.json Server/package-lock.json ./
RUN npm ci --omit=dev --ignore-scripts

FROM node:22-bookworm-slim AS runtime
ENV NODE_ENV=production \
    HOST=0.0.0.0 \
    PORT=8787 \
    CINDERLINE_MATCH_WORKER=/app/CinderlineMatchWorker
WORKDIR /app
COPY --from=node-dependencies /app/Server/node_modules ./Server/node_modules
COPY Server/package.json Server/index.js Server/server.js ./Server/
COPY --from=native-build /build/CinderlineMatchWorker ./CinderlineMatchWorker
USER node
EXPOSE 8787
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s --retries=3 \
  CMD node -e "fetch('http://127.0.0.1:8787/healthz').then(r=>{if(!r.ok)process.exit(1)}).catch(()=>process.exit(1))"
CMD ["node", "Server/index.js"]
