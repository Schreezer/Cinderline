import { liveCases, sampleBattlefield } from "./fixtures.js";
import { createCommanderPipeline, createTypeSafeInterpreter } from "./pipeline.js";
import { createOpenAIClarifier, createTypeSafeHttpEvaluator } from "./providers.js";

const typesafeModel = process.env.CINDERLINE_TYPESAFE_MODEL;
const llmModel = process.env.CINDERLINE_COMMANDER_LLM_MODEL;
const evaluate = createTypeSafeHttpEvaluator({ apiKey: process.env.TYPESAFE_API_KEY });
const interpreter = createTypeSafeInterpreter({ evaluate, model: typesafeModel });
const withoutLlm = createCommanderPipeline({ interpreter });
const withLlm = process.env.OPENAI_API_KEY && llmModel
  ? createCommanderPipeline({
      interpreter,
      clarifier: createOpenAIClarifier({ apiKey: process.env.OPENAI_API_KEY, model: llmModel }),
    })
  : null;

const results = [];
for (const testCase of liveCases) {
  const input = { ...testCase, battlefield: sampleBattlefield };
  const direct = await withoutLlm(input);
  const clarified = withLlm ? await withLlm(input) : { status: "skipped", reason: "OpenAI key or configured model missing" };
  results.push({ id: testCase.id, typesafeOnly: direct, llmThenTypesafe: clarified });
}

console.log(JSON.stringify({
  warning: "Proposal-only evaluation. No battlefield command was executed.",
  results,
}, null, 2));
