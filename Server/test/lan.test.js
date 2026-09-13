import assert from "node:assert/strict";
import { Buffer } from "node:buffer";
import { EventEmitter } from "node:events";
import test from "node:test";
import { startConfiguredServer } from "../index.js";
import {
  CINDERLINE_PROTOCOL_VERSION,
  bonjourInstanceName,
  collectLanAddresses,
  formatWebSocketUrl,
  lanWebSocketUrls,
  readHostConfiguration,
  startLanAdvertisement,
} from "../lan.js";

const fixtureInterfaces = {
  lo0: [
    { address: "127.0.0.1", family: "IPv4", internal: true },
    { address: "::1", family: "IPv6", internal: true },
  ],
  en1: [
    { address: "203.0.113.8", family: "IPv4", internal: false },
    { address: "2001:db8::5", family: "IPv6", internal: false },
    { address: "fd12:3456::5", family: "IPv6", internal: false },
  ],
  en0: [
    { address: "192.168.1.25", family: "IPv4", internal: false },
    { address: "10.0.0.4", family: 4, internal: false },
    { address: "fe80::aede:48ff:fe00:1122", family: 6, internal: false },
    { address: "fe80::aede:48ff:fe00:1122%en0", family: "IPv6", internal: false },
  ],
};

test("LAN address selection keeps only reachable private addresses and formats WebSocket URLs", () => {
  assert.deepEqual(collectLanAddresses(fixtureInterfaces), [
    { interfaceName: "en0", family: 4, address: "10.0.0.4", zone: "" },
    { interfaceName: "en0", family: 4, address: "192.168.1.25", zone: "" },
    { interfaceName: "en0", family: 6, address: "fe80::aede:48ff:fe00:1122", zone: "en0" },
    { interfaceName: "en1", family: 6, address: "fd12:3456::5", zone: "" },
  ]);
  assert.deepEqual(lanWebSocketUrls(fixtureInterfaces, 8787).map(({ url }) => url), [
    "ws://10.0.0.4:8787/play",
    "ws://192.168.1.25:8787/play",
    "ws://[fe80::aede:48ff:fe00:1122%25en0]:8787/play",
    "ws://[fd12:3456::5]:8787/play",
  ]);
  assert.equal(formatWebSocketUrl({ family: 6, address: "fd00::7", zone: "" }, 9000), "ws://[fd00::7]:9000/play");
  assert.throws(() => formatWebSocketUrl({ family: 4, address: "10.0.0.2" }, 0), /between 1 and 65535/u);
});

test("host configuration makes LAN binding explicit and rejects ambiguous environment values", () => {
  assert.deepEqual(readHostConfiguration({ env: {}, argv: [] }), {
    networkMode: "online", serverName: "Cinderline", host: "127.0.0.1", port: 8787, advertise: false,
    shutdownGraceMs: 0,
    serverOptions: {},
  });
  assert.deepEqual(readHostConfiguration({
    env: { PORT: "0", CINDERLINE_SERVER_NAME: "  Ember Room  ", CINDERLINE_LAN_ADVERTISE: "no" },
    argv: ["--lan"],
  }), {
    networkMode: "lan", serverName: "Ember Room", host: "0.0.0.0", port: 0, advertise: false,
    shutdownGraceMs: 0,
    serverOptions: {},
  });
  assert.throws(() => readHostConfiguration({ env: { PORT: "87x" }, argv: [] }), /PORT must be/u);
  assert.throws(() => readHostConfiguration({ env: { HOST: "127.0.0.1" }, argv: ["--lan"] }), /LAN mode binds 0.0.0.0/u);
  assert.throws(() => readHostConfiguration({ env: { CINDERLINE_NETWORK_MODE: "public" }, argv: [] }), /online or lan/u);
  assert.throws(() => readHostConfiguration({ env: { CINDERLINE_LAN_ADVERTISE: "true" }, argv: [] }), /only in LAN mode/u);
  assert.throws(() => readHostConfiguration({ env: { CINDERLINE_SHUTDOWN_GRACE_MS: "30001" }, argv: [] }), /between 0 and 30000/u);
  assert.throws(() => readHostConfiguration({ env: { CINDERLINE_MAX_ACTIVE_WORKERS: "0" }, argv: [] }), /between 1 and 1024/u);
  assert.throws(() => readHostConfiguration({ env: { CINDERLINE_TRUSTED_PROXIES: "127.0.0.1,not-an-ip" }, argv: [] }), /invalid entry/u);
  assert.throws(() => readHostConfiguration({ env: { CINDERLINE_ALLOWED_ORIGINS: "https://play.example/path" }, argv: [] }), /invalid entry/u);

  const tuned = readHostConfiguration({
    env: {
      CINDERLINE_MAX_ROOMS: "12",
      CINDERLINE_MAX_CONNECTIONS: "48",
      CINDERLINE_MAX_ACTIVE_WORKERS: "4",
      CINDERLINE_MAX_WORKER_COMMAND_QUEUE: "64",
      CINDERLINE_MAX_WORKER_BACKLOG_STEPS: "20",
      CINDERLINE_WORKER_PROGRESS_TIMEOUT_MS: "5000",
      CINDERLINE_SLOW_CLIENT_TIMEOUT_MS: "2500",
      CINDERLINE_MAX_CONNECTIONS_PER_IP: "6",
      CINDERLINE_ROOM_CREATE_LIMIT_COUNT: "3",
      CINDERLINE_ROOM_CREATE_LIMIT_WINDOW_MS: "10000",
      CINDERLINE_TRUSTED_PROXIES: "127.0.0.1,::1,127.0.0.1",
      CINDERLINE_ALLOWED_ORIGINS: "https://play.example,http://192.168.1.2:8080",
      CINDERLINE_MATCH_WORKER: "/srv/CinderlineMatchWorker",
    },
    argv: [],
  });
  assert.deepEqual(tuned.serverOptions, {
    maxRooms: 12,
    maxConnections: 48,
    maxActiveWorkers: 4,
    maxWorkerCommandQueue: 64,
    maxWorkerBacklogSteps: 20,
    workerProgressTimeoutMs: 5000,
    slowClientTimeoutMs: 2500,
    maxConnectionsPerIp: 6,
    roomCreateLimitCount: 3,
    roomCreateLimitWindowMs: 10000,
    trustedProxyAddresses: ["127.0.0.1", "::1"],
    allowedOrigins: ["https://play.example", "http://192.168.1.2:8080"],
    workerPath: "/srv/CinderlineMatchWorker",
  });
});

test("Bonjour instance labels stay within the 63-byte DNS label limit", () => {
  assert.equal(bonjourInstanceName("a".repeat(63)), "a".repeat(63));
  assert.equal(bonjourInstanceName("a".repeat(64)), "a".repeat(63));
  assert.equal(bonjourInstanceName("🔥".repeat(16)), "🔥".repeat(15));
  assert.equal(Buffer.byteLength(bonjourInstanceName("é".repeat(32)), "utf8"), 62);
  assert.equal(bonjourInstanceName("Cinder.Line"), "Cinder-Line");
});

test("Bonjour advertises the actual bound port and stops the service before destroying its socket", async () => {
  const events = [];
  const service = new EventEmitter();
  service.stop = (done) => { events.push("service.stop"); done(); };
  const bonjour = {
    publish(options) { events.push(["publish", options]); return service; },
    destroy() { events.push("bonjour.destroy"); },
  };
  const advertisement = await startLanAdvertisement({
    port: 43123,
    serverName: "Cinderline Test",
    bonjourFactory: async () => bonjour,
    logger: { warn: (message) => events.push(["warn", message]) },
  });
  assert.equal(advertisement.advertised, true);
  assert.equal(advertisement.serviceName, "Cinderline Test");
  assert.deepEqual(events[0], ["publish", {
    name: "Cinderline Test",
    type: "cinderline",
    protocol: "tcp",
    port: 43123,
    disableIPv6: true,
    txt: { protocol: String(CINDERLINE_PROTOCOL_VERSION), path: "/play", name: "Cinderline Test" },
  }]);
  await advertisement.stop();
  await advertisement.stop();
  assert.deepEqual(events.slice(1), ["service.stop", "bonjour.destroy"]);

  events.length = 0;
  const publicName = "🔥".repeat(32);
  const discoveryName = "🔥".repeat(15);
  const unicodeAdvertisement = await startLanAdvertisement({
    port: 43124,
    serverName: publicName,
    bonjourFactory: async () => bonjour,
    logger: { warn: (message) => events.push(["warn", message]) },
  });
  assert.equal(Buffer.byteLength(events[0][1].name, "utf8"), 60);
  assert.equal(Buffer.byteLength(events[0][1].txt.name, "utf8"), 60);
  assert.deepEqual(events[0][1], {
    name: discoveryName,
    type: "cinderline",
    protocol: "tcp",
    port: 43124,
    disableIPv6: true,
    txt: { protocol: String(CINDERLINE_PROTOCOL_VERSION), path: "/play", name: discoveryName },
  });
  await unicodeAdvertisement.stop();
});

test("Bonjour failure leaves manual LAN URLs available", async () => {
  const warnings = [];
  const urls = lanWebSocketUrls(fixtureInterfaces, 8787);
  const advertisement = await startLanAdvertisement({
    port: 8787,
    serverName: "Cinderline",
    bonjourFactory: async () => { throw new Error("multicast socket unavailable"); },
    logger: { warn: (message) => warnings.push(message) },
  });
  assert.equal(advertisement.advertised, false);
  assert.equal(urls[0].url, "ws://10.0.0.4:8787/play");
  assert.match(warnings[0], /printed LAN URL instead.*multicast socket unavailable/u);
  await advertisement.stop();
});

test("configured startup forwards mode and name, advertises only LAN, and closes once", async () => {
  const forwarded = [];
  const lifecycle = [];
  const serverFactory = (options) => {
    forwarded.push(options);
    return {
      async listen() { lifecycle.push("listen"); return { address: "0.0.0.0", family: "IPv4", port: 45678 }; },
      drain() { lifecycle.push("server.drain"); },
      async close() { lifecycle.push("server.close"); },
    };
  };
  const advertisementFactory = async (options) => {
    lifecycle.push(["advertise", options.port, options.serverName]);
    return { advertised: true, async stop() { lifecycle.push("advertisement.stop"); } };
  };
  const messages = [];
  const logger = { log: (message) => messages.push(message), warn: (message) => messages.push(message) };
  const runtime = await startConfiguredServer({
    env: { PORT: "0", CINDERLINE_SERVER_NAME: "Coal Room" },
    argv: ["--lan"],
    interfaces: fixtureInterfaces,
    logger,
    serverFactory,
    advertisementFactory,
  });
  assert.deepEqual(forwarded[0], { host: "0.0.0.0", port: 0, serverName: "Coal Room", networkMode: "lan" });
  assert.deepEqual(lifecycle.slice(0, 2), ["listen", ["advertise", 45678, "Coal Room"]]);
  assert.ok(messages.some((message) => message.includes("ws://192.168.1.25:45678/play")));
  assert.ok(messages.every((message) => !message.includes("%25en0") && !message.includes("fd12:3456")));
  await runtime.close();
  await runtime.close();
  assert.deepEqual(lifecycle.slice(2), ["advertisement.stop", "server.drain", "server.close"]);

  let onlineAdvertised = false;
  const online = await startConfiguredServer({
    env: {}, argv: [], interfaces: fixtureInterfaces, logger,
    serverFactory,
    advertisementFactory: async () => { onlineAdvertised = true; throw new Error("must not run"); },
  });
  assert.equal(online.config.networkMode, "online");
  assert.equal(onlineAdvertised, false);
  await online.close();
});
