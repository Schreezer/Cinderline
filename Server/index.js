#!/usr/bin/env node
import { createGameServer } from "./server.js";

const gameServer = createGameServer();

try {
  const address = await gameServer.listen();
  console.log(`Cinderline game server listening on ${address.address}:${address.port}`);
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exitCode = 1;
}

for (const signal of ["SIGINT", "SIGTERM"]) {
  process.once(signal, async () => {
    await gameServer.close();
    process.exit(0);
  });
}
