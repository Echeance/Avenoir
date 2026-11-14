import { describe, expect, it } from "vitest";

import { parseHostMessage } from "../src/lib/protocol";
import { initialState, MAX_ENTRIES, reduce, type ViewState } from "../src/lib/state";
import { plainText } from "../src/lib/text";

function apply(...ops: Record<string, unknown>[]): ViewState {
  return ops.reduce<ViewState>((state, op) => reduce(state, op as never), initialState());
}

describe("texte du démon", () => {
  it("retire les couleurs et les caractères de contrôle", () => {
    expect(plainText("\x1b[1;33m▲ Attention\x1b[0m\x07 fin")).toBe("▲ Attention fin");
    expect(plainText("\x1b]52;c;QQ==\x07collé")).toBe("collé");
    expect(plainText("ligne 1\nligne 2\t.")).toBe("ligne 1\nligne 2\t.");
  });
});

describe("messages de l'hôte", () => {
  it("accepte les messages bien formés", () => {
    expect(parseHostMessage({ type: "ui", params: { op: "info" } })).not.toBeNull();
    expect(
      parseHostMessage({ type: "question", id: 3, method: "confirm", params: {} }),
    ).toMatchObject({ id: 3, method: "confirm" });
  });
  it("écarte les messages mal formés ou inconnus", () => {
    expect(parseHostMessage(null)).toBeNull();
    expect(parseHostMessage({ type: "ui", params: [] })).toBeNull();
    expect(parseHostMessage({ type: "question", id: 1, method: "run", params: {} })).toBeNull();
    expect(parseHostMessage({ type: "reply", id: -1, result: {} })).toBeNull();
    expect(parseHostMessage({ type: "eval", code: "alert(1)" })).toBeNull();
  });
});

describe("état de l'interface", () => {
  it("suit une requête : outil, flux, réponse finale", () => {
    const state = apply(
      { op: "user", text: "quelle heure ?" },
      { op: "start_thinking" },
      { op: "tool_calls", calls: [{ id: "a", name: "current_time", arguments: {} }] },
      { op: "tool_result", message: { role: "tool_result", call_id: "a", is_error: false } },
      { op: "stream_text", text: "Il est " },
      { op: "stream_text", text: "15 h." },
      { op: "stream_end", kept: true },
      { op: "stop_thinking" },
    );
    expect(state.thinking).toBe(false);
    expect(state.entries).toEqual([
      { kind: "user", text: "quelle heure ?" },
      { kind: "tool", id: "a", name: "current_time", summary: "", done: true, failed: false },
      { kind: "assistant", text: "Il est 15 h.", streaming: false },
    ]);
  });
  it("retire une réponse écartée par Avenoir", () => {
    const state = apply({ op: "stream_text", text: "J'ai éteint." }, { op: "stream_end", kept: false });
    expect(state.entries).toEqual([]);
  });
  it("marque un outil en échec, et ignore une opération inconnue", () => {
    const state = apply(
      { op: "tool_calls", calls: [{ id: "x", name: "open_url", arguments: { url: "https://a.example" } }] },
      { op: "tool_result", message: { role: "tool_result", call_id: "x", is_error: true } },
      { op: "inconnue", text: "?" },
    );
    expect(state.entries).toEqual([
      { kind: "tool", id: "x", name: "open_url", summary: "https://a.example", done: true, failed: true },
    ]);
  });
  it("recharge une conversation et borne l'historique", () => {
    const reloaded = apply(
      { op: "info", text: "ancien" },
      {
        op: "messages",
        messages: [
          { role: "user", content: "bonjour" },
          { role: "assistant", text: "Bonjour !", tool_calls: [] },
        ],
      },
    );
    expect(reloaded.entries.map((entry) => entry.kind)).toEqual(["user", "assistant"]);
    let state = initialState();
    for (let index = 0; index < MAX_ENTRIES + 20; index += 1) {
      state = reduce(state, { op: "user", text: String(index) });
    }
    expect(state.entries).toHaveLength(MAX_ENTRIES);
  });
});
