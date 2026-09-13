import { Buffer } from "node:buffer";
import { isIP } from "node:net";
import { PROTOCOL_VERSION, WEBSOCKET_PATH } from "./protocol.js";

export const CINDERLINE_PROTOCOL_VERSION = PROTOCOL_VERSION;
export const CINDERLINE_SERVICE_TYPE = "cinderline";
export const CINDERLINE_WEBSOCKET_PATH = WEBSOCKET_PATH;

export function bonjourInstanceName(serverName) {
  let instanceName = "";
  for (const character of String(serverName).replaceAll(".", "-")) {
    if (Buffer.byteLength(instanceName + character, "utf8") > 63) break;
    instanceName += character;
  }
  return instanceName || "Cinderline";
}

function addressFamily(record) {
  if (record?.family === "IPv4" || record?.family === 4) return 4;
  if (record?.family === "IPv6" || record?.family === 6) return 6;
  const bare = String(record?.address ?? "").split("%", 1)[0];
  return isIP(bare);
}

function isPrivateIPv4(address) {
  const octets = address.split(".").map(Number);
  if (octets.length !== 4 || octets.some((part) => !Number.isInteger(part) || part < 0 || part > 255)) return false;
  return octets[0] === 10
    || (octets[0] === 172 && octets[1] >= 16 && octets[1] <= 31)
    || (octets[0] === 192 && octets[1] === 168)
    || (octets[0] === 169 && octets[1] === 254);
}

function isPrivateIPv6(address) {
  const first = Number.parseInt(address.split(":", 1)[0], 16);
  return Number.isInteger(first) && ((first & 0xfe00) === 0xfc00 || (first & 0xffc0) === 0xfe80);
}

export function isUsableLanAddress(record) {
  if (!record || record.internal || typeof record.address !== "string") return false;
  const bare = record.address.split("%", 1)[0].toLowerCase();
  const family = addressFamily(record);
  if (isIP(bare) !== family) return false;
  if (family === 4) return isPrivateIPv4(bare);
  if (family === 6) return isPrivateIPv6(bare);
  return false;
}

export function collectLanAddresses(interfaces) {
  const addresses = [];
  const seen = new Set();
  for (const interfaceName of Object.keys(interfaces ?? {}).sort()) {
    for (const record of interfaces[interfaceName] ?? []) {
      if (!isUsableLanAddress(record)) continue;
      const family = addressFamily(record);
      const bare = record.address.split("%", 1)[0];
      let zone = record.address.includes("%") ? record.address.slice(record.address.indexOf("%") + 1) : "";
      if (family === 6 && isPrivateIPv6(bare) && (Number.parseInt(bare.split(":", 1)[0], 16) & 0xffc0) === 0xfe80 && !zone) {
        zone = interfaceName;
      }
      const key = `${family}:${bare.toLowerCase()}%${zone}`;
      if (seen.has(key)) continue;
      seen.add(key);
      addresses.push({ interfaceName, family, address: bare, zone });
    }
  }
  return addresses.sort((left, right) => left.family - right.family
    || left.interfaceName.localeCompare(right.interfaceName)
    || left.address.localeCompare(right.address));
}

export function formatWebSocketUrl(record, port, path = CINDERLINE_WEBSOCKET_PATH) {
  if (!record || (record.family !== 4 && record.family !== 6)) throw new Error("LAN address family must be IPv4 or IPv6.");
  if (isIP(record.address) !== record.family) throw new Error("LAN address does not match its address family.");
  if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error("LAN port must be between 1 and 65535.");
  if (typeof path !== "string" || !path.startsWith("/") || path.includes("?") || path.includes("#")) {
    throw new Error("LAN WebSocket path must be an absolute path without a query or fragment.");
  }
  if (record.family === 4) return `ws://${record.address}:${port}${path}`;
  const zone = record.zone ? `%25${encodeURIComponent(record.zone)}` : "";
  return `ws://[${record.address}${zone}]:${port}${path}`;
}

export function lanWebSocketUrls(interfaces, port, path = CINDERLINE_WEBSOCKET_PATH) {
  return collectLanAddresses(interfaces).map((record) => ({
    ...record,
    url: formatWebSocketUrl(record, port, path),
  }));
}

function parsePort(value) {
  if (value === undefined || value === "") return 8787;
  if (!/^\d{1,5}$/u.test(value)) throw new Error("PORT must be an integer between 0 and 65535.");
  const port = Number(value);
  if (port < 0 || port > 65535) throw new Error("PORT must be an integer between 0 and 65535.");
  return port;
}

function parseBoolean(value, name, fallback) {
  if (value === undefined || value === "") return fallback;
  if (["1", "true", "yes"].includes(value.toLowerCase())) return true;
  if (["0", "false", "no"].includes(value.toLowerCase())) return false;
  throw new Error(`${name} must be true or false.`);
}

function parseShutdownGrace(value) {
  if (value === undefined || value === "") return 0;
  if (!/^\d{1,5}$/u.test(value)) throw new Error("CINDERLINE_SHUTDOWN_GRACE_MS must be an integer between 0 and 30000.");
  const milliseconds = Number(value);
  if (milliseconds < 0 || milliseconds > 30_000) {
    throw new Error("CINDERLINE_SHUTDOWN_GRACE_MS must be an integer between 0 and 30000.");
  }
  return milliseconds;
}

function optionalInteger(env, environmentName, optionName, minimum, maximum) {
  const value = env[environmentName];
  if (value === undefined || value === "") return null;
  if (!/^\d+$/u.test(value)) throw new Error(`${environmentName} must be an integer between ${minimum} and ${maximum}.`);
  const parsed = Number(value);
  if (!Number.isSafeInteger(parsed) || parsed < minimum || parsed > maximum) {
    throw new Error(`${environmentName} must be an integer between ${minimum} and ${maximum}.`);
  }
  return [optionName, parsed];
}

function commaSeparated(value, environmentName, validator) {
  if (value === undefined) return null;
  if (value.trim() === "") return [];
  const entries = value.split(",").map((entry) => entry.trim());
  if (entries.some((entry) => !entry || !validator(entry))) {
    throw new Error(`${environmentName} contains an invalid entry.`);
  }
  return [...new Set(entries)];
}

function isExactOrigin(value) {
  try {
    const parsed = new URL(value);
    return (parsed.protocol === "http:" || parsed.protocol === "https:")
      && !parsed.username && !parsed.password
      && parsed.pathname === "/" && !parsed.search && !parsed.hash
      && parsed.origin === value;
  } catch {
    return false;
  }
}

function parseServerName(value) {
  const name = value === undefined ? "Cinderline" : value.trim();
  if (!name || name.length > 64 || /[\u0000-\u001f\u007f]/u.test(name)) {
    throw new Error("CINDERLINE_SERVER_NAME must contain 1 to 64 printable characters.");
  }
  return name;
}

export function readHostConfiguration({ env = process.env, argv = process.argv.slice(2) } = {}) {
  const flags = new Set(argv);
  for (const argument of flags) {
    if (argument !== "--lan" && argument !== "--online") throw new Error(`Unknown server argument: ${argument}`);
  }
  if (flags.has("--lan") && flags.has("--online")) throw new Error("Choose either --lan or --online, not both.");

  const requestedMode = flags.has("--lan") ? "lan"
    : flags.has("--online") ? "online"
      : env.CINDERLINE_NETWORK_MODE ?? "online";
  if (requestedMode !== "online" && requestedMode !== "lan") {
    throw new Error("CINDERLINE_NETWORK_MODE must be online or lan.");
  }

  const port = parsePort(env.PORT);
  const serverName = parseServerName(env.CINDERLINE_SERVER_NAME);
  const advertise = parseBoolean(env.CINDERLINE_LAN_ADVERTISE, "CINDERLINE_LAN_ADVERTISE", true);
  if (requestedMode === "online" && env.CINDERLINE_LAN_ADVERTISE !== undefined && advertise) {
    throw new Error("CINDERLINE_LAN_ADVERTISE can be enabled only in LAN mode.");
  }
  const configuredHost = env.HOST?.trim();
  if (requestedMode === "lan" && configuredHost && configuredHost !== "0.0.0.0") {
    throw new Error("LAN mode binds 0.0.0.0; remove HOST or set HOST=0.0.0.0.");
  }
  if (requestedMode === "online" && configuredHost !== undefined && configuredHost.length === 0) {
    throw new Error("HOST cannot be empty.");
  }

  const serverOptions = {};
  const integerOptions = [
    ["CINDERLINE_MAX_ROOMS", "maxRooms", 1, 10_000],
    ["CINDERLINE_MAX_CONNECTIONS", "maxConnections", 1, 100_000],
    ["CINDERLINE_MAX_ACTIVE_WORKERS", "maxActiveWorkers", 1, 1024],
    ["CINDERLINE_MAX_WORKER_COMMAND_QUEUE", "maxWorkerCommandQueue", 8, 4096],
    ["CINDERLINE_MAX_WORKER_BACKLOG_STEPS", "maxWorkerBacklogSteps", 5, 400],
    ["CINDERLINE_WORKER_PROGRESS_TIMEOUT_MS", "workerProgressTimeoutMs", 500, 300_000],
    ["CINDERLINE_SLOW_CLIENT_TIMEOUT_MS", "slowClientTimeoutMs", 500, 300_000],
    ["CINDERLINE_MAX_CONNECTIONS_PER_IP", "maxConnectionsPerIp", 1, 1000],
    ["CINDERLINE_ROOM_CREATE_LIMIT_COUNT", "roomCreateLimitCount", 1, 1000],
    ["CINDERLINE_ROOM_CREATE_LIMIT_WINDOW_MS", "roomCreateLimitWindowMs", 1000, 3_600_000],
  ];
  for (const definition of integerOptions) {
    const parsed = optionalInteger(env, ...definition);
    if (parsed) serverOptions[parsed[0]] = parsed[1];
  }
  const trustedProxyAddresses = commaSeparated(
    env.CINDERLINE_TRUSTED_PROXIES,
    "CINDERLINE_TRUSTED_PROXIES",
    (entry) => isIP(entry) !== 0,
  );
  if (trustedProxyAddresses !== null) serverOptions.trustedProxyAddresses = trustedProxyAddresses;
  const allowedOrigins = commaSeparated(
    env.CINDERLINE_ALLOWED_ORIGINS,
    "CINDERLINE_ALLOWED_ORIGINS",
    isExactOrigin,
  );
  if (allowedOrigins !== null) serverOptions.allowedOrigins = allowedOrigins;
  if (env.CINDERLINE_MATCH_WORKER !== undefined) {
    if (!env.CINDERLINE_MATCH_WORKER.trim() || /[\u0000-\u001f\u007f]/u.test(env.CINDERLINE_MATCH_WORKER)) {
      throw new Error("CINDERLINE_MATCH_WORKER must be a nonempty path.");
    }
    serverOptions.workerPath = env.CINDERLINE_MATCH_WORKER;
  }

  return {
    networkMode: requestedMode,
    serverName,
    host: requestedMode === "lan" ? "0.0.0.0" : configuredHost || "127.0.0.1",
    port,
    advertise: requestedMode === "lan" && advertise,
    shutdownGraceMs: parseShutdownGrace(env.CINDERLINE_SHUTDOWN_GRACE_MS),
    serverOptions,
  };
}

async function defaultBonjourFactory(onError) {
  const module = await import("bonjour-service");
  const Bonjour = module.Bonjour ?? module.default?.Bonjour ?? module.default;
  if (typeof Bonjour !== "function") throw new Error("bonjour-service did not export Bonjour.");
  return new Bonjour({}, onError);
}

function stopPublishedService(service, timeoutMs) {
  return new Promise((resolve) => {
    let complete = false;
    const finish = () => {
      if (complete) return;
      complete = true;
      clearTimeout(timer);
      resolve();
    };
    const timer = setTimeout(finish, timeoutMs);
    timer.unref?.();
    try {
      service?.stop?.(finish);
      if (!service?.stop) finish();
    } catch {
      finish();
    }
  });
}

export async function startLanAdvertisement({
  port,
  serverName,
  protocol = CINDERLINE_PROTOCOL_VERSION,
  path = CINDERLINE_WEBSOCKET_PATH,
  logger = console,
  bonjourFactory = defaultBonjourFactory,
  stopTimeoutMs = 1000,
} = {}) {
  let bonjour;
  let service;
  const reportFailure = (error) => logger.warn?.(
    `Bonjour advertising failed; connect with a printed LAN URL instead. ${error instanceof Error ? error.message : String(error)}`,
  );
  try {
    bonjour = await bonjourFactory(reportFailure);
    const instanceName = bonjourInstanceName(serverName);
    service = bonjour.publish({
      name: instanceName,
      type: CINDERLINE_SERVICE_TYPE,
      protocol: "tcp",
      port,
      disableIPv6: true,
      txt: { protocol: String(protocol), path, name: instanceName },
    });
    service?.on?.("error", reportFailure);
  } catch (error) {
    try { bonjour?.destroy?.(); } catch {}
    reportFailure(error);
    return { advertised: false, async stop() {} };
  }

  let stopped = false;
  return {
    advertised: true,
    serviceName: service?.name ?? bonjourInstanceName(serverName),
    async stop() {
      if (stopped) return;
      stopped = true;
      await stopPublishedService(service, stopTimeoutMs);
      bonjour.destroy?.();
    },
  };
}
