export const sampleBattlefield = Object.freeze({
  revision: "match-17:tick-420",
  selectedGroup: "selected",
  unitGroups: [
    { id: "selected", label: "the currently selected units", description: "Two Strikers and one Mender currently selected by the player.", commandable: true },
    { id: "army", label: "the whole combat army", description: "All currently commandable combat units.", commandable: true },
    { id: "menders", label: "the Menders", description: "All commandable support Menders.", commandable: true },
    { id: "workers", label: "the Drudges", description: "All commandable worker units.", commandable: true },
  ],
  locations: [
    { id: "north_watch", label: "the northern watch point", description: "An explored defensive point north of the main base.", explored: true, commandable: true },
    { id: "east_expansion", label: "the eastern expansion", description: "An explored expansion location east of the main base.", explored: true, commandable: true },
  ],
  targets: [
    { id: "enemy_turret_31", label: "the visible enemy turret", description: "A visible enemy Turret near the eastern expansion.", relationship: "enemy", visible: true, commandable: true },
    { id: "frontline_striker_7", label: "the frontline Striker", description: "A visible friendly Striker leading the selected formation.", relationship: "friendly", visible: true, commandable: true },
  ],
});

export const liveCases = Object.freeze([
  {
    id: "clear_attack_move",
    transcript: "Send the whole army to attack-move toward the eastern expansion.",
    conversation: [],
  },
  {
    id: "ambiguous_destination",
    transcript: "Send them over there.",
    conversation: [],
  },
  {
    id: "contextual_pronouns",
    transcript: "Send them there.",
    conversation: [
      { role: "user", text: "Keep an eye on the northern watch point." },
      { role: "assistant", text: "The selected units are ready." },
    ],
  },
  {
    id: "unsupported_strategy",
    transcript: "Do whatever it takes to win the match for me.",
    conversation: [],
  },
]);
