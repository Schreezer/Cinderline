import assert from "node:assert/strict";
import test from "node:test";
import { createCommanderRouter, validateCommanderProposal } from "../commander/router.js";
import { buildJevRequest, createJevProvider, createLunaProvider, lunaTools } from "../commander/runtime-providers.js";

function observation(overrides = {}) {
  return { contextId: "capture-1", generation: "match-1", revision: 42, capturedAt: 1, selected_group: "selected",
    unit_groups: [{ id: "selected", label: "Selected Drudges", units: [11, 12], commandable: true }, { id: "army", label: "Army", units: [11, 12, 13], commandable: true }],
    locations: [{ id: "west_ramp", label: "Western ramp", commandable: true, explored: true }, { id: "main_ramp", label: "Main ramp", commandable: true, explored: true }],
    targets: [{ id: "enemy-1", label: "Visible enemy", visible: true, relationship: "enemy" }, { id: "friend-1", label: "Friendly Skim", visible: true, relationship: "friendly" }], summary: { minerals: 431 }, ...overrides };
}
function choice(value, confidence = 0.98) { return { type: "choice", choice: value, confidence }; }
function judgments(overrides = {}) {
  return { answers: { route: choice("handle"), action: choice("move"), unit_group: choice("selected"), destination: choice("west_ramp"), target: choice("no_match"), queue_mode: choice("replace"), ...overrides } };
}
function input(overrides = {}) { return { text: "Move these to the western ramp", observation: observation(), operationId: "job-1", ...overrides }; }
function tool(name, args, id = "call-1") { return { status: "tools", toolCalls: [{ id, name, arguments: args }] }; }
function order(action = "move", extras = {}) { return { action, unitGroup: "selected", destination: action === "move" ? "west_ramp" : null, target: null, queueMode: "replace", ...extras }; }
function adapter(overrides = {}) {
  return { getObservation: async () => observation({ contextId: "fresh-2", revision: 43 }), execute: async () => ({ status: "accepted", message: "Order accepted.", sequence: 7 }), ...overrides };
}
const answer = () => ({ status: "answered", message: "Ready." });

test("Jev batches exactly handle/handoff capability and speculative choices; never clarification", () => {
  const request = buildJevRequest({ text: "Move then hold", observation: observation() });
  assert.equal(request.model, "jev-latest");
  assert.deepEqual(Object.keys(request.questions.route.criteria), ["handle", "handoff"]);
  assert.deepEqual(Object.keys(request.questions), ["route", "action", "unit_group", "destination", "target", "queue_mode"]);
  assert.match(request.questions.route.criteria.handoff, /multiple actions/i);
  assert.match(request.questions.route.criteria.handle, /count/);
  assert.ok(request.questions.destination.criteria.no_match);
  assert.equal(Object.keys(request.questions).some((key) => /clarif|question/.test(key)), false);
});

test("a simple Jev command bypasses Luna and uses a stable mutation ID", async () => {
  let lunaCalls = 0;
  let submitted;
  const router = createCommanderRouter({ jev: async () => judgments(), luna: async () => { lunaCalls++; return answer(); } });
  const result = await router.run(input(), adapter({ execute: async (...args) => { submitted = args; return { status: "accepted", message: "Moving to western ramp." }; } }));
  assert.equal(result.route, "jev"); assert.equal(result.status, "completed"); assert.equal(lunaCalls, 0);
  assert.deepEqual(submitted[0], { action: "move", unitGroup: "selected", destination: "west_ramp", queueMode: "replace" });
  assert.equal(submitted[1].contextId, "capture-1"); assert.equal(submitted[2].operationId, "job-1:0");
  assert.equal(result.message, "Moving to western ramp.");
});

test("low confidence on unused arguments does not defeat the fast path", async () => {
  const router = createCommanderRouter({ jev: async () => judgments({ action: choice("hold"), destination: choice("no_match", 0.01), target: choice("no_match", 0.01), queue_mode: choice("append", 0.01) }), luna: async () => assert.fail("Unused arguments must not call Luna") });
  const result = await router.run(input({ text: "Hold these units" }), adapter());
  assert.equal(result.route, "jev");
});

test("low confidence on a used argument hands off without fabricating a question", async () => {
  let lunaInput;
  const router = createCommanderRouter({ jev: async () => judgments({ destination: choice("west_ramp", 0.2) }), luna: async (value) => { lunaInput = value; return { status: "needs_clarification", message: "The western ramp or the main ramp?" }; } });
  const result = await router.run(input(), adapter({ execute: async () => assert.fail("Clarification cannot issue orders") }));
  assert.equal(result.status, "needs_clarification"); assert.equal(result.route, "luna");
  assert.equal(result.message, "The western ramp or the main ramp?");
  assert.equal(lunaInput.originalRequest, input().text); assert.equal("question" in lunaInput, false);
});

test("compound handoff runs sequential tools with actual results before the next step", async () => {
  let round = 0; const steps = []; const submittedIds = [];
  const router = createCommanderRouter({ jev: async () => judgments({ route: choice("handoff") }), luna: async ({ exchange }) => {
    steps.push(exchange);
    if (round++ === 0) return tool("issue_order", order());
    if (round === 2) return tool("issue_order", order("patrol", { destination: "main_ramp", queueMode: "append" }), "call-2");
    return { status: "answered", message: "I won the battle for you." };
  } });
  const result = await router.run(input({ text: "Move to west, then patrol to main" }), adapter({ execute: async (_p, _o, { operationId }) => { submittedIds.push(operationId); return { status: "accepted", message: `Accepted ${submittedIds.length}.` }; } }));
  assert.deepEqual(submittedIds, ["job-1:0", "job-1:1"]);
  assert.equal(JSON.parse(steps[1][1].output).status, "accepted");
  assert.equal(result.message, "Accepted 1. Accepted 2.");
  assert.equal(result.message.includes("won"), false);
});

test("a clarification reply goes directly to Luna and retains original selection across fresh reads", async () => {
  let jevCalls = 0; let phase = 0; let commandCapture; let pendingInput;
  const router = createCommanderRouter({ jev: async () => { jevCalls++; return judgments({ route: choice("handoff") }); }, luna: async (context) => {
    if (phase++ === 0) return { status: "needs_clarification", message: "Which ramp?" };
    pendingInput = context;
    if (phase === 2) return tool("get_observation", {});
    if (phase === 3) return tool("issue_order", order(), "call-2");
    return answer();
  } });
  const first = await router.run(input({ text: "Move them to the ramp" }), adapter({ execute: async () => assert.fail("Premature execution") }));
  const newObservation = observation({ contextId: "new-selection", revision: 55, unit_groups: [{ id: "selected", label: "New selection", units: [99], commandable: true }] });
  const second = await router.run(input({ text: "The western one", observation: newObservation, operationId: "later-delegation", pending: first.pending }), adapter({
    getObservation: async () => newObservation,
    execute: async (_proposal, frozen) => { commandCapture = frozen; return { status: "accepted", message: "Original units moving." }; },
  }));
  assert.equal(jevCalls, 1); assert.equal(second.route, "luna");
  assert.deepEqual(commandCapture.unit_groups[0].units, [11, 12]); assert.equal(commandCapture.contextId, "capture-1");
  assert.equal(pendingInput.originalRequest, "Move them to the ramp"); assert.equal(pendingInput.text, "The western one");
  assert.equal(second.receipts[0].operationId, "job-1:0");
  assert.equal(pendingInput.history.some((item) => item.text === "Which ramp?"), true);
});

test("expired clarification and changed match cannot execute or silently restart", async () => {
  let time = 1000; let calls = 0;
  const router = createCommanderRouter({ now: () => time, pendingTtlMs: 10, jev: async () => judgments({ route: choice("handoff") }), luna: async () => { calls++; return { status: "needs_clarification", message: "Which ramp?" }; } });
  const first = await router.run(input(), adapter());
  time = 1011;
  const expired = await router.run(input({ pending: first.pending }), adapter());
  assert.match(expired.message, /expired/); assert.equal(calls, 1);
  time = 1001;
  const changed = await router.run(input({ pending: first.pending, observation: observation({ generation: "match-2" }) }), adapter());
  assert.match(changed.message, /match changed/); assert.equal(calls, 1);
});

test("Luna can explicitly replace a pending intent using the new utterance selection, not a later UI read", async () => {
  let round = 0; let jevCalls = 0; let submitted; const contexts = [];
  const router = createCommanderRouter({ jev: async () => { jevCalls++; return judgments({ route: choice("handoff") }); }, luna: async (context) => {
    contexts.push(context);
    switch (round++) {
      case 0: return { status: "needs_clarification", message: "Which ramp?" };
      case 1: return tool("get_observation", {});
      case 2: return tool("start_new_request", {}, "restart-1");
      case 3: return tool("issue_order", order("hold"), "hold-1");
      default: return answer();
    }
  } });
  const first = await router.run(input(), adapter());
  const latest = observation({ contextId: "new-utterance", unit_groups: [{ id: "selected", label: "New selection", commandable: true, units: [99] }] });
  const laterUI = observation({ contextId: "later-ui", unit_groups: [{ id: "selected", label: "Even later selection", commandable: true, units: [88] }] });
  const result = await router.run(input({ text: "Forget that, hold these", observation: latest, pending: first.pending }), adapter({
    getObservation: async () => laterUI,
    execute: async (_proposal, captured) => { submitted = captured; return { status: "accepted", message: "Holding." }; },
  }));
  assert.equal(jevCalls, 1); assert.equal(result.route, "luna");
  assert.equal(contexts[1].canStartNewRequest, true); assert.equal(contexts[2].observation.contextId, "later-ui");
  assert.equal(contexts[3].canStartNewRequest, false); assert.equal(contexts[3].originalRequest, "Forget that, hold these");
  assert.deepEqual(submitted.unit_groups[0].units, [99]); assert.equal(submitted.contextId, "new-utterance");
  assert.deepEqual(lunaTools(observation(), { canStartNewRequest: true }).at(-1).parameters.required, []);
  assert.equal(lunaTools(observation()).some((item) => item.name === "start_new_request"), false);
});

test("Luna cannot rebase a normal request, restart twice, or restart after an accepted mutation", async () => {
  const ordinary = createCommanderRouter({ jev: null, luna: async () => tool("start_new_request", {}) });
  const failed = await ordinary.run(input(), adapter({ execute: async () => assert.fail("Unexpected execution") }));
  assert.equal(failed.status, "answered");
  for (const afterMutation of [false, true]) {
    let round = 0; let executions = 0;
    const router = createCommanderRouter({ jev: null, luna: async () => {
      if (round++ === 0) return { status: "needs_clarification", message: "Which ramp?" };
      if (round === 2) return afterMutation ? tool("issue_order", order()) : tool("start_new_request", {}, "restart-1");
      return tool("start_new_request", {}, "restart-2");
    } });
    const first = await router.run(input(), adapter());
    await router.run(input({ pending: first.pending }), adapter({ execute: async () => { executions++; return { status: "accepted", message: "Accepted." }; } }));
    assert.equal(executions, afterMutation ? 1 : 0);
  }
});

test("fresh reads cannot smuggle new command candidates into a frozen capture", async () => {
  let round = 0; let executions = 0;
  const router = createCommanderRouter({ jev: null, luna: async () => round++ === 0 ? tool("get_observation", {}) : tool("issue_order", order("attack", { target: "new-enemy" }), "call-2") });
  const result = await router.run(input(), adapter({ getObservation: async () => observation({ targets: [{ id: "new-enemy", visible: true, relationship: "enemy" }] }), execute: async () => { executions++; } }));
  assert.equal(executions, 0); assert.match(result.message, /captured/);
});

test("game-rule rejection is a receipt, never a clarification or Luna escalation", async () => {
  const router = createCommanderRouter({ jev: async () => judgments(), luna: async () => assert.fail("Do not bypass game rejection") });
  const result = await router.run(input(), adapter({ execute: async () => ({ status: "rejected", message: "Those units are no longer yours." }) }));
  assert.equal(result.status, "completed"); assert.equal(result.receipts[0].status, "rejected"); assert.equal(result.pending, undefined);
});

test("unknown or pending execution outcomes stop the loop without replay", async () => {
  for (const outcome of [{ status: "pending", message: "Awaiting server." }, { status: "uncertain", message: "Connection lost." }, { bad: true }]) {
    let calls = 0; let executions = 0;
    const router = createCommanderRouter({ jev: null, luna: async () => { calls++; return tool("issue_order", order()); } });
    const result = await router.run(input(), adapter({ execute: async () => { executions++; return outcome; } }));
    assert.equal(executions, 1); assert.equal(calls, 1);
    assert.equal(result.receipts[0].status, outcome.status ?? "uncertain");
  }
});

test("repeated proposal or tool call is not submitted twice", async () => {
  let calls = 0; let executions = 0;
  const router = createCommanderRouter({ jev: null, luna: async () => calls++ < 3 ? tool("issue_order", order(), calls === 3 ? "call-new" : "call-1") : answer() });
  const result = await router.run(input(), adapter({ execute: async () => { executions++; return { status: "accepted", message: "Moving." }; } }));
  assert.equal(executions, 1); assert.equal(result.receipts.length, 1);
});

test("cancellation during inference discards late proposals and never executes", async () => {
  const controller = new AbortController(); let executions = 0;
  const router = createCommanderRouter({ jev: async () => { controller.abort(); return judgments(); }, luna: async () => assert.fail("No fallback after cancellation") });
  await assert.rejects(router.run(input({ signal: controller.signal }), adapter({ execute: async () => { executions++; } })), { name: "AbortError" });
  assert.equal(executions, 0);
});

test("cancellation during fresh observation discards results before mutation", async () => {
  const controller = new AbortController();
  const router = createCommanderRouter({ jev: null, luna: async () => tool("get_observation", {}) });
  await assert.rejects(router.run(input({ signal: controller.signal }), adapter({ getObservation: async () => { controller.abort(); return observation(); }, execute: async () => assert.fail("No stale action") })), { name: "AbortError" });
});

test("malformed outputs and unsupported tools fail closed without leaking provider text", async () => {
  for (const decision of [null, { status: "clarify", question: "Bad Jev-style decision" }, tool("shell", { command: "bad" }), tool("issue_order", { ...order(), unexpected: "value" }), { status: "tools", toolCalls: [tool("issue_order", order()).toolCalls[0], tool("issue_order", order(), "two").toolCalls[0]] }]) {
    const router = createCommanderRouter({ jev: async () => ({ malformed: true }), luna: async () => decision });
    const result = await router.run(input(), adapter({ execute: async () => assert.fail("Invalid output executed") }));
    assert.equal(result.status, "answered"); assert.equal(result.pending, undefined);
  }
  const router = createCommanderRouter({ jev: async () => { throw new Error("private-provider-details"); }, luna: async () => { throw new Error("private-provider-details"); } });
  assert.equal(JSON.stringify(await router.run(input(), adapter())).includes("private-provider-details"), false);
});

test("bounded loops and mutation limits prevent an autonomous workflow", async () => {
  let reads = 0;
  const router = createCommanderRouter({ jev: null, maxToolRounds: 2, luna: async () => tool("get_observation", {}, `read-${reads}`) });
  const result = await router.run(input(), adapter({ getObservation: async () => { reads++; return observation(); } }));
  assert.equal(reads, 2); assert.match(result.message, /command limit/);
  let round = 0; let mutations = 0;
  const limited = createCommanderRouter({ jev: null, maxMutations: 1, luna: async () => tool("issue_order", order(round++ ? "hold" : "move"), `call-${round}`) });
  const partial = await limited.run(input(), adapter({ execute: async () => { mutations++; return { status: "accepted", message: "Accepted." }; } }));
  assert.equal(mutations, 1);
  assert.match(partial.message, /order limit/);
});

test("captured proposal validation rejects hidden, wrong-relationship, fabricated and nonqueueable orders", () => {
  const state = observation();
  assert.equal(validateCommanderProposal({ action: "attack", unitGroup: "selected", target: "friend-1", queueMode: "replace" }, state), false);
  assert.equal(validateCommanderProposal({ action: "hold", unitGroup: "selected", queueMode: "append" }, state), false);
  assert.equal(validateCommanderProposal({ action: "move", unitGroup: "selected", destination: "unlisted", queueMode: "replace" }, state), false);
  state.targets[0].visible = false;
  assert.equal(validateCommanderProposal({ action: "attack", unitGroup: "selected", target: "enemy-1", queueMode: "replace" }, state), false);
});

test("explicit direct-Luna configuration bypasses Jev; undefined Jev is a config error", async () => {
  assert.throws(() => createCommanderRouter({ luna: answer }), /explicit Jev/);
  const result = await createCommanderRouter({ jev: null, luna: async () => answer() }).run(input(), adapter());
  assert.equal(result.route, "luna");
});

function jsonResponse(value, status = 200) { return new Response(JSON.stringify(value), { status, headers: { "content-type": "application/json" } }); }
function decisionResponse(status = "answered", message = "Ready.") { return { status: "completed", output: [{ type: "message", role: "assistant", content: [{ type: "output_text", text: JSON.stringify({ status, message }) }] }] }; }
function lunaInput(overrides = {}) { return { text: "Move those", originalRequest: "Move those", capturedObservation: observation(), observation: observation(), ...overrides }; }

test("runtime providers use the current server-side endpoints, models and strict bounded tool schemas", async () => {
  const requests = [];
  const fetchImpl = async (url, options) => { requests.push({ url, body: JSON.parse(options.body), headers: options.headers }); return jsonResponse(url.includes("typesafe") ? judgments() : decisionResponse()); };
  const jev = createJevProvider({ apiKey: "fixture-jev", fetchImpl });
  const luna = createLunaProvider({ apiKey: "fixture-luna", fetchImpl });
  await jev(input()); await luna(lunaInput());
  assert.equal(requests[0].url, "https://api.typesafe.ai/v1/systemone"); assert.equal(requests[0].body.model, "jev-latest");
  const body = requests[1].body;
  assert.equal(requests[1].url, "https://api.openai.com/v1/responses"); assert.equal(body.model, "gpt-6-luna");
  assert.equal(body.store, false); assert.equal(body.parallel_tool_calls, false); assert.equal(body.reasoning.effort, "low");
  assert.deepEqual(body.include, ["reasoning.encrypted_content"]);
  assert.deepEqual(body.tools.map((item) => item.name), ["get_observation", "issue_order", "select_units", "focus_location"]);
  for (const item of body.tools) { assert.equal(item.strict, true); assert.equal(item.parameters.additionalProperties, false); assert.deepEqual(item.parameters.required, Object.keys(item.parameters.properties)); }
  assert.deepEqual(body.tools[1].parameters.properties.unitGroup.enum, ["selected", "army"]);
  assert.equal(body.text.format.strict, true);
});

test("Luna provider preserves encrypted reasoning and function outputs in a stateless tool loop", async () => {
  const output = [{ type: "reasoning", id: "reasoning-1", summary: [], encrypted_content: "opaque-fixture" }, { type: "function_call", call_id: "call-1", name: "get_observation", arguments: "{}" }];
  const requests = [];
  const provider = createLunaProvider({ apiKey: "fixture", fetchImpl: async (_url, request) => {
    requests.push(JSON.parse(request.body)); return jsonResponse(requests.length === 1 ? { status: "completed", output } : decisionResponse("needs_clarification", "Which ramp?"));
  } });
  const first = await provider(lunaInput());
  assert.deepEqual(first.toolCalls, [{ id: "call-1", name: "get_observation", arguments: {} }]);
  const exchange = [...first.output, { type: "function_call_output", call_id: "call-1", output: JSON.stringify(observation()) }];
  const second = await provider(lunaInput({ exchange }));
  assert.deepEqual(requests[1].input.slice(1), exchange); assert.equal(second.status, "needs_clarification");
});

test("provider HTTP, malformed, incomplete, and timeout errors contain no upstream body", async () => {
  for (const fake of [async () => jsonResponse({ error: { message: "sensitive fixture secret" } }, 401), async () => new Response("sensitive fixture secret"), async () => jsonResponse({ status: "incomplete", output: [] }), async () => jsonResponse({ status: "completed", output: [{ type: "function_call", call_id: "c", name: "issue_order", arguments: "sensitive fixture secret" }] })]) {
    const provider = createLunaProvider({ apiKey: "fixture", fetchImpl: fake });
    await assert.rejects(provider(lunaInput()), (error) => !error.message.includes("sensitive"));
  }
  const timeout = createJevProvider({ apiKey: "fixture", timeoutMs: 5, fetchImpl: async () => new Promise(() => {}) });
  await assert.rejects(timeout(input()), (error) => error.code === "provider_timeout");
});

test("provider cancellation aborts the network and sanitizes the abort reason", async () => {
  const controller = new AbortController(); let networkSignal;
  const provider = createJevProvider({ apiKey: "fixture", fetchImpl: async (_url, opts) => { networkSignal = opts.signal; queueMicrotask(() => controller.abort(new Error("sensitive fixture secret"))); return new Promise(() => {}); } });
  await assert.rejects(provider(input({ signal: controller.signal })), (error) => error.name === "AbortError" && !error.message.includes("sensitive"));
  assert.equal(networkSignal.aborted, true);
});

test("camera/selection actions require only their used typed arguments", async () => {
  const focused = createCommanderRouter({ jev: async () => judgments({ action: choice("focus_location"), unit_group: choice("no_match", 0.01), queue_mode: choice("replace", 0.01) }), luna: async () => assert.fail("Camera move should be bounded") });
  const result = await focused.run(input({ text: "Show the western ramp" }), adapter({ execute: async (proposal) => { assert.deepEqual(proposal, { action: "focus_location", destination: "west_ramp", queueMode: "replace" }); return { status: "accepted", message: "Camera focused." }; } }));
  assert.equal(result.route, "jev");
  assert.equal(lunaTools(observation())[2].name, "select_units");
});
