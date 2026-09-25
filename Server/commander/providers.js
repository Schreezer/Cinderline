const OPENAI_RESPONSES_URL = "https://api.openai.com/v1/responses";
const TYPESAFE_SYSTEM_ONE_URL = "https://api.typesafe.ai/v1/systemone";

async function checkedJson(response, provider) {
  const body = await response.json().catch(() => null);
  if (!response.ok) {
    const message = body?.error?.message ?? body?.message ?? `${response.status} ${response.statusText}`;
    throw new Error(`${provider} request failed: ${message}`);
  }
  return body;
}

export function createTypeSafeHttpEvaluator({ apiKey, fetchImpl = fetch }) {
  if (!apiKey) throw new Error("TYPESAFE_API_KEY is required for live TypeSafe evaluation.");
  return async (request) => checkedJson(await fetchImpl(TYPESAFE_SYSTEM_ONE_URL, {
    method: "POST",
    headers: {
      authorization: `Bearer ${apiKey}`,
      "content-type": "application/json",
    },
    body: JSON.stringify(request),
  }), "TypeSafe");
}

function outputText(response) {
  if (typeof response.output_text === "string") return response.output_text;
  for (const item of response.output ?? []) {
    for (const content of item.content ?? []) {
      if (content.type === "output_text" && typeof content.text === "string") return content.text;
    }
  }
  throw new Error("OpenAI response did not contain output text.");
}

export function createOpenAIClarifier({ apiKey, model, fetchImpl = fetch }) {
  if (!apiKey) throw new Error("OPENAI_API_KEY is required for live LLM clarification.");
  if (!model) throw new Error("CINDERLINE_COMMANDER_LLM_MODEL must select the LLM model.");

  return async ({ transcript, conversation, battlefield }) => {
    const response = await checkedJson(await fetchImpl(OPENAI_RESPONSES_URL, {
      method: "POST",
      headers: {
        authorization: `Bearer ${apiKey}`,
        "content-type": "application/json",
      },
      body: JSON.stringify({
        model,
        store: false,
        instructions: [
          "You are a clarification layer for a real-time strategy game commander.",
          "You cannot call tools or execute orders.",
          "Return ready only when the player's intended action and referents are supported by the supplied battlefield state or conversation.",
          "A normalized request may resolve pronouns from explicit context, but must not invent entity ids, locations, visibility, or facts.",
          "When material ambiguity remains, return one short spoken clarification question.",
        ].join(" "),
        input: JSON.stringify({ transcript, conversation, battlefield }),
        text: {
          format: {
            type: "json_schema",
            name: "cinderline_commander_clarification",
            strict: true,
            schema: {
              type: "object",
              additionalProperties: false,
              properties: {
                status: { type: "string", enum: ["ready", "clarify"] },
                normalizedRequest: { type: "string" },
                question: { type: "string" },
                reason: { type: "string" },
              },
              required: ["status", "normalizedRequest", "question", "reason"],
            },
          },
        },
      }),
    }), "OpenAI");

    const parsed = JSON.parse(outputText(response));
    return parsed.status === "ready"
      ? { status: "ready", normalizedRequest: parsed.normalizedRequest, reason: parsed.reason }
      : { status: "clarify", question: parsed.question, reason: parsed.reason };
  };
}
