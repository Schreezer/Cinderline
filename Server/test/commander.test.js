import assert from "node:assert/strict";
import test from "node:test";

import { sampleBattlefield } from "../commander/fixtures.js";
import {
  buildTypeSafeRequest,
  createCommanderPipeline,
  createTypeSafeInterpreter,
  validateProposal,
} from "../commander/pipeline.js";
import { createOpenAIClarifier, createTypeSafeHttpEvaluator } from "../commander/providers.js";

function choice(value, confidence = 0.95) {
  return { type: "choice", choice: value, confidence, probabilities: { [value]: 1 } };
}

function response({
  action = "move",
  unitGroup = "selected",
  destination = "north_watch",
  target = "no_match",
  queueMode = "replace",
  confidence = 0.95,
} = {}) {
  return {
    model: "fixture-model",
    answers: {
      action: choice(action, confidence),
      unit_group: choice(unitGroup, confidence),
      destination: choice(destination, confidence),
      target: choice(target, confidence),
      queue_mode: choice(queueMode, confidence),
    },
    usage: { input_tokens: 10, output_tokens: 5 },
  };
}

test("TypeSafe request contains bounded battlefield candidates and no execution tools", () => {
  const request = buildTypeSafeRequest({
    transcript: "Move the selected units north.",
    battlefield: sampleBattlefield,
    model: "fixture-model",
  });

  assert.equal(request.model, "fixture-model");
  assert.deepEqual(Object.keys(request.questions), ["action", "unit_group", "destination", "target", "queue_mode"]);
  assert.ok(request.questions.destination.criteria.north_watch);
  assert.ok(request.questions.destination.criteria.no_match);
  assert.equal("tools" in request, false);
});

test("without LLM: a clear request becomes a validated proposal", async () => {
  const interpreter = createTypeSafeInterpreter({
    model: "fixture-model",
    evaluate: async () => response({ action: "attack_move", unitGroup: "army", destination: "east_expansion" }),
  });
  const run = createCommanderPipeline({ interpreter });

  const result = await run({
    transcript: "Send the whole army to attack-move toward the eastern expansion.",
    battlefield: sampleBattlefield,
  });

  assert.equal(result.status, "ready");
  assert.equal(result.path, "typesafe_only");
  assert.deepEqual(result.proposal, {
    action: "attack_move",
    unitGroup: "army",
    destination: "east_expansion",
    queueMode: "replace",
  });
});

test("without LLM: a missing destination asks for clarification", async () => {
  const interpreter = createTypeSafeInterpreter({
    model: "fixture-model",
    evaluate: async () => response({ destination: "no_match", confidence: 0.42 }),
  });
  const run = createCommanderPipeline({ interpreter });
  const result = await run({ transcript: "Send them over there.", battlefield: sampleBattlefield });

  assert.equal(result.status, "clarify");
  assert.equal(result.path, "typesafe_only");
  assert.equal(result.reason, "destination_missing");
});

test("without LLM: low confidence on a complete proposal asks for clarification", async () => {
  const interpreter = createTypeSafeInterpreter({
    model: "fixture-model",
    evaluate: async () => response({ confidence: 0.42 }),
  });
  const run = createCommanderPipeline({ interpreter });
  const result = await run({ transcript: "Move the selected units north.", battlefield: sampleBattlefield });

  assert.equal(result.status, "clarify");
  assert.equal(result.reason, "low_confidence");
});

test("with LLM: ambiguous language is stopped before TypeSafe", async () => {
  let interpreterCalls = 0;
  const run = createCommanderPipeline({
    interpreter: async () => {
      interpreterCalls += 1;
      return { status: "proposal", proposal: {}, confidence: 1 };
    },
    clarifier: async () => ({
      status: "clarify",
      question: "Which location should the selected units move to?",
      reason: "destination_missing",
    }),
  });

  const result = await run({ transcript: "Send them over there.", battlefield: sampleBattlefield });
  assert.equal(result.status, "clarify");
  assert.equal(result.path, "llm_then_typesafe");
  assert.equal(interpreterCalls, 0);
});

test("with LLM: explicit conversation context can normalize pronouns before TypeSafe", async () => {
  let interpretedTranscript;
  const interpreter = createTypeSafeInterpreter({
    model: "fixture-model",
    evaluate: async (request) => {
      interpretedTranscript = request.state.transcript;
      return response({ action: "move", unitGroup: "selected", destination: "north_watch" });
    },
  });
  const run = createCommanderPipeline({
    interpreter,
    clarifier: async () => ({
      status: "ready",
      normalizedRequest: "Move the selected units to the northern watch point.",
      reason: "resolved_from_explicit_context",
    }),
  });

  const result = await run({
    transcript: "Send them there.",
    conversation: [
      { role: "user", text: "Keep an eye on the northern watch point." },
      { role: "assistant", text: "The selected units are ready." },
    ],
    battlefield: sampleBattlefield,
  });

  assert.equal(result.status, "ready");
  assert.equal(result.path, "llm_then_typesafe");
  assert.equal(interpretedTranscript, "Move the selected units to the northern watch point.");
});

test("deterministic validation rejects a hidden or invented target from either model path", () => {
  assert.deepEqual(validateProposal({
    action: "attack",
    unitGroup: "army",
    target: "hidden_enemy_base",
    queueMode: "replace",
  }, sampleBattlefield), { ok: false, reason: "invalid_or_hidden_target" });
});

test("live adapters send judgments and structured clarification without provider tool definitions", async () => {
  const requests = [];
  const fetchImpl = async (url, options) => {
    requests.push({ url, options, body: JSON.parse(options.body) });
    if (url.includes("typesafe")) {
      return { ok: true, json: async () => response() };
    }
    return {
      ok: true,
      json: async () => ({
        output_text: JSON.stringify({
          status: "clarify",
          normalizedRequest: "",
          question: "Where should they move?",
          reason: "destination_missing",
        }),
      }),
    };
  };

  const evaluate = createTypeSafeHttpEvaluator({ apiKey: "test-typesafe-key", fetchImpl });
  await evaluate(buildTypeSafeRequest({
    transcript: "Move them.",
    battlefield: sampleBattlefield,
    model: "fixture-model",
  }));
  const clarify = createOpenAIClarifier({ apiKey: "test-openai-key", model: "fixture-llm", fetchImpl });
  const result = await clarify({ transcript: "Move them.", conversation: [], battlefield: sampleBattlefield });

  assert.equal(result.status, "clarify");
  assert.equal(requests[0].body.model, "fixture-model");
  assert.equal("tools" in requests[0].body, false);
  assert.equal(requests[1].body.model, "fixture-llm");
  assert.equal(requests[1].body.store, false);
  assert.equal("tools" in requests[1].body, false);
  assert.equal(requests[1].body.text.format.type, "json_schema");
});
