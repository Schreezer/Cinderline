import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, extname, relative, resolve, sep } from "node:path";
import test from "node:test";

const repositoryRoot = resolve(import.meta.dirname, "../..");
const serverRoot = resolve(repositoryRoot, "Server");

function repositoryPath(path) {
  return relative(repositoryRoot, path).split(sep).join("/");
}

function relativeModuleSpecifiers(source) {
  const specifiers = [];
  const staticImport = /\b(?:import|export)\s+(?:[^"']*?\s+from\s*)?["'](\.[^"']+)["']/gu;
  const dynamicImport = /\bimport\s*\(\s*["'](\.[^"']+)["']\s*\)/gu;
  for (const pattern of [staticImport, dynamicImport]) {
    for (const match of source.matchAll(pattern)) specifiers.push(match[1]);
  }
  return specifiers;
}

function runtimeJavaScriptClosure(entry) {
  const discovered = new Set();
  const pending = [entry];
  while (pending.length > 0) {
    const modulePath = pending.pop();
    const canonical = resolve(modulePath);
    if (discovered.has(canonical)) continue;
    assert.equal(canonical.startsWith(`${serverRoot}${sep}`), true, `runtime module escapes Server/: ${canonical}`);
    discovered.add(canonical);
    const source = readFileSync(canonical, "utf8");
    for (const specifier of relativeModuleSpecifiers(source)) {
      let dependency = resolve(dirname(canonical), specifier);
      if (!extname(dependency)) dependency += ".js";
      pending.push(dependency);
    }
  }
  return [...discovered].map(repositoryPath).sort();
}

function runtimeCopySources(dockerfile) {
  const normalized = dockerfile.replace(/\\\r?\n/gu, " ");
  const marker = /^FROM\s+[^\r\n]+\s+AS\s+runtime\s*$/imu.exec(normalized);
  assert(marker, "Dockerfile has a runtime stage");
  const following = normalized.slice(marker.index + marker[0].length);
  const nextStage = following.search(/^FROM\s/imu);
  const runtime = nextStage < 0 ? following : following.slice(0, nextStage);
  const copied = new Set();
  for (const line of runtime.split(/\r?\n/u)) {
    const tokens = line.trim().split(/\s+/u);
    if (tokens[0]?.toUpperCase() !== "COPY" || tokens.some((token) => token.startsWith("--from="))) continue;
    for (const source of tokens.slice(1, -1)) copied.add(source);
  }
  return copied;
}

test("Docker runtime packaging includes the complete index.js module closure", () => {
  const modules = runtimeJavaScriptClosure(resolve(serverRoot, "index.js"));
  const dockerfile = readFileSync(resolve(serverRoot, "Dockerfile"), "utf8");
  const copied = runtimeCopySources(dockerfile);
  const allowlist = new Set(
    readFileSync(resolve(serverRoot, "Dockerfile.dockerignore"), "utf8")
      .split(/\r?\n/u)
      .map((line) => line.trim())
      .filter((line) => line.startsWith("!") && !line.endsWith("/"))
      .map((line) => line.slice(1)),
  );

  assert.deepEqual(modules, [
    "Server/index.js",
    "Server/lan.js",
    "Server/protocol.js",
    "Server/server.js",
    "Server/worker-bridge.js",
  ]);
  assert.deepEqual(modules.filter((module) => !copied.has(module)), [], "runtime COPY omits an imported JavaScript module");
  assert.deepEqual(modules.filter((module) => !allowlist.has(module)), [], "Docker ignore allowlist omits an imported JavaScript module");
});
