#!/usr/bin/env node
import { networkInterfaces } from "node:os";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { createGameServer } from "./server.js";
import { lanWebSocketUrls, readHostConfiguration, startLanAdvertisement } from "./lan.js";

export async function startConfiguredServer({
  env = process.env,
  argv = process.argv.slice(2),
  interfaces = networkInterfaces(),
  logger = console,
  serverFactory = createGameServer,
  advertisementFactory = startLanAdvertisement,
} = {}) {
  const config = readHostConfiguration({ env, argv });
  const gameServer = serverFactory({
    host: config.host,
    port: config.port,
    serverName: config.serverName,
    networkMode: config.networkMode,
    ...config.serverOptions,
  });
  const address = await gameServer.listen();
  const port = typeof address === "object" && address ? address.port : config.port;
  let advertisement = { advertised: false, async stop() {} };

  if (config.networkMode === "lan") {
    const urls = lanWebSocketUrls(interfaces, port).filter((entry) => entry.family === 4);
    logger.log?.(`Cinderline LAN server "${config.serverName}" is listening on port ${port}.`);
    if (urls.length === 0) {
      logger.warn?.("No private IPv4 LAN address was found. Join a private IPv4 network or configure the host address manually.");
    } else {
      for (const entry of urls) logger.log?.(`LAN WebSocket (${entry.interfaceName}): ${entry.url}`);
    }
    if (config.advertise) {
      try {
        advertisement = await advertisementFactory({ port, serverName: config.serverName, logger });
        if (advertisement.advertised) {
          logger.log?.(`Bonjour: ${advertisement.serviceName ?? config.serverName}._cinderline._tcp.local`);
        }
      } catch (error) {
        logger.warn?.(`Bonjour advertising failed; connect with a printed LAN URL instead. ${error instanceof Error ? error.message : String(error)}`);
      }
    } else {
      logger.log?.("Bonjour advertising is disabled; use one of the printed LAN URLs.");
    }
  } else {
    const shownHost = typeof address === "object" && address ? address.address : config.host;
    logger.log?.(`Cinderline game server listening on ${shownHost}:${port}`);
  }

  let closed = false;
  let drained = false;
  const drain = async () => {
    if (drained) return;
    drained = true;
    try {
      await advertisement.stop();
    } catch (error) {
      logger.warn?.(`Bonjour shutdown failed; server shutdown will continue. ${error instanceof Error ? error.message : String(error)}`);
    }
    try {
      gameServer.drain?.();
    } catch (error) {
      logger.warn?.(`Server drain failed; shutdown will continue. ${error instanceof Error ? error.message : String(error)}`);
    }
  };
  const close = async () => {
    if (closed) return;
    closed = true;
    await drain();
    await gameServer.close();
  };
  return {
    address,
    config,
    gameServer,
    advertisement,
    drain,
    close,
  };
}

export async function main(options = {}) {
  const logger = options.logger ?? console;
  let runtime;
  try {
    runtime = await startConfiguredServer({ ...options, logger });
  } catch (error) {
    logger.error?.(error instanceof Error ? error.message : String(error));
    process.exitCode = 1;
    return null;
  }

  let shuttingDown = false;
  const shutdown = async (signal) => {
    if (shuttingDown) return;
    shuttingDown = true;
    try {
      await runtime.drain();
      if (runtime.config.shutdownGraceMs > 0) {
        await new Promise((resolveDelay) => setTimeout(resolveDelay, runtime.config.shutdownGraceMs));
      }
      await runtime.close();
      logger.log?.(`Cinderline game server stopped after ${signal}.`);
    } catch (error) {
      logger.error?.(`Cinderline shutdown failed: ${error instanceof Error ? error.message : String(error)}`);
      process.exitCode = 1;
    }
  };
  for (const signal of ["SIGINT", "SIGTERM"]) process.once(signal, () => void shutdown(signal));
  return runtime;
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) await main();
