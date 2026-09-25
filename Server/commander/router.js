// Runtime commander. The older pipeline.js remains a proposal-only experiment.
export const COMMANDER_ACTIONS = Object.freeze({
  select_units: "Select one complete listed friendly unit group.",
  focus_location: "Focus the camera on one listed known location.",
  move: "Move a listed friendly group to a listed location.",
  attack_move: "Advance a listed friendly group toward a listed location, engaging enemies.",
  attack: "Attack one currently visible listed enemy target.",
  hold: "Hold a listed friendly group's current position.",
  stop: "Stop a listed friendly group's current orders.",
  defend: "Defend a listed location with a listed friendly group.",
  patrol: "Patrol from the listed group's position to a listed location.",
  escort: "Escort one currently visible listed friendly target.",
});

const DESTINATION_ACTIONS = new Set(["focus_location", "move", "attack_move", "defend", "patrol"]);
const TARGET_ACTIONS = new Set(["attack", "escort"]);
const QUEUE_ACTIONS = new Set(["move", "attack_move", "patrol"]);
const RECEIPT_STATUSES = new Set(["accepted", "rejected", "pending", "cancelled", "uncertain"]);
const MAX_TEXT = 4096;
const MAX_HISTORY = 12;

function fail(code) {
  const error = new Error("The commander could not safely resolve that request.");
  error.code = code;
  return error;
}

export function throwIfAborted(signal) {
  if (signal?.aborted) throw new DOMException("Commander operation cancelled.", "AbortError");
}

function record(value) { return value !== null && typeof value === "object" && !Array.isArray(value); }
function identifier(value) { return typeof value === "string" && value.length > 0 && value.length <= 128; }
function clone(value) { return structuredClone(value); }
function historyOf(value = []) {
  if (!Array.isArray(value)) throw fail("invalid_history");
  return value.slice(-MAX_HISTORY).filter((item) => ["user", "assistant"].includes(item?.role) && typeof item?.text === "string")
    .map((item) => ({ role: item.role, text: item.text.slice(0, MAX_TEXT) }));
}

export function checkedObservation(value) {
  if (!record(value) || !identifier(value.contextId) || !["string", "number"].includes(typeof value.generation)
    || !String(value.generation).length || (typeof value.generation === "number" && !Number.isFinite(value.generation))) {
    throw fail("invalid_observation");
  }
  for (const field of ["unit_groups", "locations", "targets"]) {
    if (!Array.isArray(value[field]) || value[field].length > 254) throw fail("invalid_candidates");
    const ids = new Set();
    for (const candidate of value[field]) {
      if (!record(candidate) || !identifier(candidate.id) || candidate.id === "no_match" || ids.has(candidate.id)) throw fail("invalid_candidates");
      ids.add(candidate.id);
    }
  }
  if (JSON.stringify(value).length > 65536) throw fail("observation_too_large");
  return clone(value);
}

export function validateCommanderProposal(proposal, observation) {
  if (!record(proposal) || Object.keys(proposal).some((key) => !["action", "unitGroup", "destination", "target", "queueMode"].includes(key))) return false;
  if (!Object.hasOwn(COMMANDER_ACTIONS, proposal.action) || !["replace", "append"].includes(proposal.queueMode)) return false;
  if (proposal.queueMode === "append" && !QUEUE_ACTIONS.has(proposal.action)) return false;
  if (proposal.action !== "focus_location") {
    const group = observation.unit_groups.find((item) => item.id === proposal.unitGroup);
    if (!group || group.commandable !== true || !Array.isArray(group.units) || group.units.length === 0) return false;
  } else if (proposal.unitGroup != null) return false;
  if (DESTINATION_ACTIONS.has(proposal.action)) {
    const location = observation.locations.find((item) => item.id === proposal.destination);
    if (!location || location.commandable !== true || (proposal.action === "attack_move" && location.explored !== true)) return false;
  } else if (proposal.destination != null) return false;
  if (TARGET_ACTIONS.has(proposal.action)) {
    const target = observation.targets.find((item) => item.id === proposal.target);
    if (!target || target.visible !== true || target.commandable === false) return false;
    if (target.relationship !== (proposal.action === "attack" ? "enemy" : "friendly")) return false;
  } else if (proposal.target != null) return false;
  return true;
}

function choice(answers, id, threshold) {
  const answer = answers?.[id];
  if (answer?.type !== "choice" || !identifier(answer.choice) || !Number.isFinite(answer.confidence)
    || answer.confidence < threshold || answer.confidence > 1) return null;
  return answer.choice;
}

// Missing and uncertain used fields cause a handoff, never a Jev-authored question.
export function jevProposal(response, observation, threshold) {
  const answers = response?.answers;
  if (choice(answers, "route", threshold) !== "handle") return null;
  const action = choice(answers, "action", threshold);
  if (!Object.hasOwn(COMMANDER_ACTIONS, action)) return null;
  const proposal = { action, queueMode: "replace" };
  if (action !== "focus_location") proposal.unitGroup = choice(answers, "unit_group", threshold);
  if (DESTINATION_ACTIONS.has(action)) proposal.destination = choice(answers, "destination", threshold);
  if (TARGET_ACTIONS.has(action)) proposal.target = choice(answers, "target", threshold);
  if (QUEUE_ACTIONS.has(action)) proposal.queueMode = choice(answers, "queue_mode", threshold);
  return validateCommanderProposal(proposal, observation) ? proposal : null;
}

function toolProposal(call) {
  if (!identifier(call?.id) || !record(call.arguments)) throw fail("invalid_tool_call");
  const args = call.arguments;
  if (call.name === "select_units" && Object.keys(args).length === 1) {
    return { action: "select_units", unitGroup: args.unitGroup, queueMode: "replace" };
  }
  if (call.name === "focus_location" && Object.keys(args).length === 1) {
    return { action: "focus_location", destination: args.destination, queueMode: "replace" };
  }
  if (call.name === "issue_order" && !["select_units", "focus_location"].includes(args.action)) {
    // Null represents an unused strict-schema argument, never a default candidate.
    return Object.fromEntries(Object.entries(args).filter(([, value]) => value !== null));
  }
  throw fail("unsupported_tool");
}

function checkedReceipt(result) {
  if (!record(result) || !RECEIPT_STATUSES.has(result.status) || typeof result.message !== "string") {
    // A command may already have reached the authority: malformed receipts are uncertain, not retryable.
    return { status: "uncertain", message: "The order outcome is unknown. Check your units before repeating it." };
  }
  return { status: result.status, message: result.message.slice(0, 1000), ...(Number.isSafeInteger(result.sequence) ? { sequence: result.sequence } : {}) };
}

function completed(route, receipts, remainder = "") {
  return { status: "completed", route, message: [...receipts.map((receipt) => receipt.message), remainder].filter(Boolean).join(" "), receipts };
}

/**
 * jev(input) returns TypeSafe's {answers}; luna(input) returns a structured
 * decision or {status:'tools',toolCalls,output?}. `output` carries stateless
 * Responses continuation items. execute must await the authoritative receipt;
 * if it returns pending/uncertain, this router stops without retrying.
 */
export function createCommanderRouter({ jev, luna, minimumConfidence = 0.72, maxToolRounds = 6, maxMutations = 4, pendingTtlMs = 60000, now = Date.now }) {
  if ((jev !== null && typeof jev !== "function") || typeof luna !== "function") throw new TypeError("A Luna provider and an explicit Jev provider or null are required.");
  if (!Number.isFinite(minimumConfidence) || minimumConfidence < 0 || minimumConfidence > 1) throw new TypeError("Invalid commander confidence threshold.");
  for (const limit of [maxToolRounds, maxMutations]) if (!Number.isSafeInteger(limit) || limit < 1 || limit > 16) throw new TypeError("Invalid commander tool limit.");

  return {
    async run({ text, observation, history = [], pending, signal, operationId }, { getObservation, execute }) {
      throwIfAborted(signal);
      if (typeof text !== "string" || !text.trim() || text.length > MAX_TEXT || !identifier(operationId)) throw fail("invalid_request");
      if (typeof execute !== "function" || typeof getObservation !== "function") throw fail("missing_adapter");
      const current = checkedObservation(observation);
      let captured = current;
      let originalRequest = text;
      let conversation = historyOf(history);
      let pendingExpiresAt = now() + pendingTtlMs;
      if (pending) {
        if (pending.version !== 1 || !identifier(pending.operationId) || typeof pending.originalRequest !== "string"
          || !Number.isFinite(pending.expiresAt) || pending.expiresAt <= now()) {
          return { status: "answered", route: "luna", message: "That pending request expired. Please give the order again." };
        }
        captured = checkedObservation(pending.observation);
        if (captured.generation !== current.generation) return { status: "answered", route: "luna", message: "The match changed. Please give the order again." };
        operationId = pending.operationId;
        originalRequest = pending.originalRequest;
        conversation = historyOf(pending.history);
        pendingExpiresAt = pending.expiresAt;
      }
      conversation = historyOf([...conversation, { role: "user", text }]);
      const receipts = [];
      const executed = new Map();
      let route = pending || jev === null ? "luna" : "jev";
      async function submit(proposal) {
        throwIfAborted(signal);
        if (!validateCommanderProposal(proposal, captured)) return { status: "rejected", message: "That order does not match the captured units, locations or visible targets." };
        const fingerprint = JSON.stringify([proposal.action, proposal.unitGroup, proposal.destination, proposal.target, proposal.queueMode]);
        if (executed.has(fingerprint)) return executed.get(fingerprint);
        if (receipts.length >= maxMutations) return { status: "rejected", message: "The request reached its order limit." };
        const mutationId = `${operationId}:${receipts.length}`;
        let receipt;
        try {
          receipt = checkedReceipt(await execute(clone(proposal), clone(captured), { operationId: mutationId, signal }));
        } catch (error) {
          throwIfAborted(signal);
          receipt = { status: "uncertain", message: "The order outcome is unknown. Check your units before repeating it." };
        }
        throwIfAborted(signal);
        const result = { ...receipt, operationId: mutationId };
        receipts.push(result);
        executed.set(fingerprint, result);
        return result;
      }

      try {
        if (!pending && jev !== null) {
          let interpretation;
          try { interpretation = await jev({ text, observation: clone(captured), history: clone(conversation), signal }); }
          catch { throwIfAborted(signal); /* Provider failure takes the same bounded fallback. */ }
          throwIfAborted(signal);
          const proposal = jevProposal(interpretation, captured, minimumConfidence);
          if (proposal) {
            await submit(proposal);
            return completed("jev", receipts);
          }
        }
        route = "luna";
        let fresh = current;
        let canStartNewRequest = Boolean(pending);
        const exchange = [];
        const seenCalls = new Map();
        for (let round = 0; round < maxToolRounds; round++) {
          throwIfAborted(signal);
          const decision = await luna({ text, originalRequest, capturedObservation: clone(captured), observation: clone(fresh),
            latestUtteranceObservation: clone(current), canStartNewRequest, history: clone(conversation), exchange: clone(exchange), signal });
          throwIfAborted(signal);
          if (["needs_clarification", "answered"].includes(decision?.status)) {
            if (typeof decision.message !== "string" || !decision.message.trim() || decision.message.length > 2000) throw fail("invalid_luna_decision");
            // Only verified receipt text is allowed to claim that an order happened.
            if (receipts.length) return completed(route, receipts, decision.status === "needs_clarification"
              ? "I stopped before the remaining steps. Please give a new request for those steps." : "");
            if (decision.status === "answered") return { status: "answered", route, message: decision.message };
            return {
              status: "needs_clarification", route, message: decision.message,
              pending: { version: 1, operationId, originalRequest, observation: clone(captured), expiresAt: pendingExpiresAt,
                history: historyOf([...conversation, { role: "assistant", text: decision.message }]) },
            };
          }
          if (decision?.status !== "tools" || !Array.isArray(decision.toolCalls) || decision.toolCalls.length !== 1) throw fail("invalid_luna_tools");
          const call = decision.toolCalls[0];
          if (!identifier(call?.id) || !record(call.arguments)) throw fail("invalid_tool_call");
          const callFingerprint = JSON.stringify([call.name, call.arguments]);
          let result;
          if (seenCalls.has(call.id)) {
            if (seenCalls.get(call.id).fingerprint !== callFingerprint) throw fail("reused_tool_id");
            result = seenCalls.get(call.id).result;
          } else if (call.name === "start_new_request") {
            if (!canStartNewRequest || receipts.length || Object.keys(call.arguments).length) throw fail("invalid_request_restart");
            // Only Luna determines that the latest utterance explicitly abandons
            // the pending intent. Rebase to that utterance, never a later UI read.
            canStartNewRequest = false;
            captured = clone(current);
            fresh = clone(current);
            originalRequest = text;
            conversation = [{ role: "user", text }];
            pendingExpiresAt = now() + pendingTtlMs;
            result = { status: "ready", message: "The pending intent was abandoned. Resolve the latest utterance using its captured observation.", observation: clone(captured) };
          } else if (call.name === "get_observation") {
            if (Object.keys(call.arguments).length) throw fail("invalid_tool_arguments");
            fresh = checkedObservation(await getObservation({ signal }));
            throwIfAborted(signal);
            if (fresh.generation !== captured.generation) return { status: "answered", route, message: "The match changed. Please give the order again." };
            result = fresh;
          } else {
            canStartNewRequest = false;
            result = await submit(toolProposal(call));
            if (["pending", "uncertain", "cancelled", "rejected"].includes(result.status)) {
              return receipts.length ? completed(route, receipts, receipts.includes(result) ? "" : result.message) : { status: "answered", route, message: result.message };
            }
          }
          seenCalls.set(call.id, { fingerprint: callFingerprint, result });
          const output = Array.isArray(decision.output) ? decision.output : [{ type: "function_call", call_id: call.id, name: call.name, arguments: JSON.stringify(call.arguments) }];
          exchange.push(...clone(output), { type: "function_call_output", call_id: call.id, output: JSON.stringify(result) });
          if (JSON.stringify(exchange).length > 131072) throw fail("tool_context_limit");
        }
        return receipts.length ? completed(route, receipts, "I stopped the remaining steps at the command limit.") : { status: "answered", route, message: "I could not finish that request within the command limit." };
      } catch (error) {
        throwIfAborted(signal);
        if (error?.name === "AbortError") throw error;
        return receipts.length ? completed(route, receipts, "The remaining steps could not be completed.") : { status: "answered", route, message: "The commander could not resolve that request. Please try again." };
      }
    },
  };
}
