const ACTIONS = Object.freeze({
  no_match: "The request is not a supported battlefield command.",
  move: "Move a friendly unit group to a known battlefield location.",
  attack_move: "Advance toward a known location and engage enemies along the way.",
  attack: "Attack one visible enemy target.",
  defend: "Defend a known battlefield location.",
  hold: "Hold the current position.",
  patrol: "Patrol between the unit group's current position and a known location.",
  escort: "Escort one visible friendly mobile target.",
});

const DESTINATION_ACTIONS = new Set(["move", "attack_move", "defend", "patrol"]);
const TARGET_ACTIONS = new Set(["attack", "escort"]);
const QUEUEABLE_ACTIONS = new Set(["move", "attack_move", "patrol"]);

function criteria(items, fallbackDescription) {
  return Object.fromEntries([
    ...items.map((item) => [item.id, item.description ?? item.label]),
    ["no_match", fallbackDescription],
  ]);
}

function choice(instructions, options) {
  return { type: "choice", instructions, criteria: options };
}

export function buildTypeSafeRequest({ transcript, conversation = [], battlefield, model }) {
  if (!model) throw new Error("A TypeSafe model must be supplied by configuration.");
  if (!battlefield?.revision) throw new Error("Battlefield state needs a revision.");

  return {
    model,
    state: {
      transcript,
      conversation,
      battlefield: {
        revision: battlefield.revision,
        selected_group: battlefield.selectedGroup,
        unit_groups: battlefield.unitGroups,
        locations: battlefield.locations,
        targets: battlefield.targets,
      },
    },
    questions: {
      action: choice(
        "Which single supported battlefield action is the player asking the commander to perform? Choose no_match when the request is informational, unsupported, or too ambiguous to identify an action.",
        ACTIONS,
      ),
      unit_group: choice(
        "Which friendly unit group does the player want to command? Use the current selection only when the request refers to selected units, 'these', or 'them' with clear conversational context.",
        criteria(battlefield.unitGroups, "No listed unit group is clearly intended."),
      ),
      destination: choice(
        "If the requested action needs a map destination, which known location is intended? Do not infer a location that is not listed.",
        criteria(battlefield.locations, "No listed destination is clearly intended or the action needs no destination."),
      ),
      target: choice(
        "If the requested action needs a unit or structure target, which listed target is intended? Respect the listed relationship and visibility; do not invent or reveal a hidden target.",
        criteria(battlefield.targets, "No listed target is clearly intended or the action needs no target."),
      ),
      queue_mode: choice(
        "Should this order replace the unit group's current orders or be appended after them? Append only when the player explicitly says after, then, queue, or next.",
        {
          replace: "Run this order now, replacing incompatible current and queued orders.",
          append: "Add this order after the group's existing compatible orders.",
        },
      ),
    },
  };
}

function readChoice(answers, id) {
  const answer = answers?.[id];
  if (!answer || answer.type !== "choice" || typeof answer.choice !== "string") {
    throw new Error(`TypeSafe response is missing the ${id} choice.`);
  }
  if (!Number.isFinite(answer.confidence)) {
    throw new Error(`TypeSafe response is missing ${id} confidence.`);
  }
  return answer;
}

function minimumConfidence(answers) {
  return Math.min(...answers.map((answer) => answer.confidence));
}

export function proposalFromTypeSafe(response) {
  const action = readChoice(response.answers, "action");
  const unitGroup = readChoice(response.answers, "unit_group");
  const destination = readChoice(response.answers, "destination");
  const target = readChoice(response.answers, "target");
  const queueMode = readChoice(response.answers, "queue_mode");

  if (action.choice === "no_match") {
    return {
      status: "clarify",
      question: "What would you like your units to do?",
      reason: "no_supported_action",
      confidence: action.confidence,
    };
  }

  if (unitGroup.choice === "no_match") {
    return {
      status: "clarify",
      question: "Which units should receive that order?",
      reason: "unit_group_missing",
      confidence: unitGroup.confidence,
    };
  }

  if (DESTINATION_ACTIONS.has(action.choice) && destination.choice === "no_match") {
    return {
      status: "clarify",
      question: "Which battlefield location do you mean?",
      reason: "destination_missing",
      confidence: destination.confidence,
    };
  }

  if (TARGET_ACTIONS.has(action.choice) && target.choice === "no_match") {
    return {
      status: "clarify",
      question: action.choice === "attack"
        ? "Which visible enemy should be attacked?"
        : "Which friendly unit should be escorted?",
      reason: "target_missing",
      confidence: target.confidence,
    };
  }

  const usedAnswers = [action, unitGroup];
  const proposal = {
    action: action.choice,
    unitGroup: unitGroup.choice,
    queueMode: QUEUEABLE_ACTIONS.has(action.choice) ? queueMode.choice : "replace",
  };

  if (DESTINATION_ACTIONS.has(action.choice)) {
    proposal.destination = destination.choice;
    usedAnswers.push(destination);
  }
  if (TARGET_ACTIONS.has(action.choice)) {
    proposal.target = target.choice;
    usedAnswers.push(target);
  }
  if (QUEUEABLE_ACTIONS.has(action.choice)) usedAnswers.push(queueMode);

  return {
    status: "proposal",
    proposal,
    confidence: minimumConfidence(usedAnswers),
    judgments: response.answers,
    usage: response.usage,
  };
}

export function validateProposal(proposal, battlefield) {
  if (!Object.hasOwn(ACTIONS, proposal.action) || proposal.action === "no_match") {
    return { ok: false, reason: "unsupported_action" };
  }

  const group = battlefield.unitGroups.find((item) => item.id === proposal.unitGroup);
  if (!group || group.commandable === false) return { ok: false, reason: "invalid_unit_group" };

  if (DESTINATION_ACTIONS.has(proposal.action)) {
    const location = battlefield.locations.find((item) => item.id === proposal.destination);
    if (!location || location.commandable === false) return { ok: false, reason: "invalid_destination" };
    if (proposal.action === "attack_move" && location.explored === false) {
      return { ok: false, reason: "unexplored_destination" };
    }
  }

  if (TARGET_ACTIONS.has(proposal.action)) {
    const target = battlefield.targets.find((item) => item.id === proposal.target);
    if (!target || target.visible === false || target.commandable === false) {
      return { ok: false, reason: "invalid_or_hidden_target" };
    }
    if (proposal.action === "attack" && target.relationship !== "enemy") {
      return { ok: false, reason: "attack_requires_enemy" };
    }
    if (proposal.action === "escort" && target.relationship !== "friendly") {
      return { ok: false, reason: "escort_requires_friendly" };
    }
  }

  if (proposal.queueMode !== "replace" && proposal.queueMode !== "append") {
    return { ok: false, reason: "invalid_queue_mode" };
  }
  if (proposal.queueMode === "append" && !QUEUEABLE_ACTIONS.has(proposal.action)) {
    return { ok: false, reason: "action_cannot_be_queued" };
  }

  return { ok: true };
}

export function createTypeSafeInterpreter({ evaluate, model }) {
  if (typeof evaluate !== "function") throw new Error("A TypeSafe evaluator is required.");
  return async ({ transcript, conversation, battlefield }) => {
    const request = buildTypeSafeRequest({ transcript, conversation, battlefield, model });
    return proposalFromTypeSafe(await evaluate(request));
  };
}

export function createCommanderPipeline({ interpreter, clarifier = null, minimumConfidence = 0.72 }) {
  if (typeof interpreter !== "function") throw new Error("An interpreter is required.");

  return async function run({ transcript, conversation = [], battlefield }) {
    let interpretedTranscript = transcript;
    let clarification = null;

    if (clarifier) {
      clarification = await clarifier({ transcript, conversation, battlefield });
      if (clarification.status === "clarify") {
        return {
          status: "clarify",
          path: "llm_then_typesafe",
          question: clarification.question,
          reason: clarification.reason,
        };
      }
      if (clarification.status !== "ready" || !clarification.normalizedRequest) {
        throw new Error("Clarifier returned an invalid result.");
      }
      interpretedTranscript = clarification.normalizedRequest;
    }

    const result = await interpreter({
      transcript: interpretedTranscript,
      conversation,
      battlefield,
    });
    const path = clarifier ? "llm_then_typesafe" : "typesafe_only";

    if (result.status === "clarify") return { ...result, path };
    if (result.confidence < minimumConfidence) {
      return {
        status: "clarify",
        path,
        question: "I am not certain which battlefield order you intended. Could you be more specific?",
        reason: "low_confidence",
        confidence: result.confidence,
      };
    }

    const validation = validateProposal(result.proposal, battlefield);
    if (!validation.ok) {
      return {
        status: "rejected",
        path,
        reason: validation.reason,
        proposal: result.proposal,
        confidence: result.confidence,
      };
    }

    return {
      status: "ready",
      path,
      proposal: result.proposal,
      confidence: result.confidence,
      normalizedRequest: clarifier ? interpretedTranscript : undefined,
      judgments: result.judgments,
      usage: result.usage,
    };
  };
}
