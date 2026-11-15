// Hôte simulé, pour travailler l'interface dans un navigateur (`npm run dev`)
// sans démon : il rejoue une courte session, confirmation comprise.

import type { PageMessage } from "./protocol";
import type { Transport } from "./bridge";

export function mockTransport(): Transport {
  let receive: (raw: unknown) => void = () => undefined;
  const later = (delay: number, message: unknown): void => {
    setTimeout(() => receive(message), delay);
  };
  const ui = (delay: number, params: Record<string, unknown>): void =>
    later(delay, { type: "ui", params });
  let question = 1000;
  return {
    moduleUrl: () => null,
    listen(callback) {
      receive = callback;
    },
    send(message: PageMessage) {
      if (message.type === "ready") {
        later(0, { type: "status", connected: true, message: "hôte simulé" });
        return;
      }
      if (message.type !== "call") {
        return;
      }
      if (message.method === "session/open") {
        ui(50, { op: "banner", llm: "gemma4 : gemma4:26b-a4b, local", tools: 23, conversation: "« Essai » (n° 1)" });
        ui(60, { op: "info", text: "Hôte simulé : aucune action n'est réellement faite." });
        later(80, { type: "reply", id: message.id, result: { conversation: "« Essai » (n° 1)" } });
        return;
      }
      if (message.method === "input") {
        const line = String(message.params["line"] ?? "");
        ui(0, { op: "user", text: line });
        ui(50, { op: "start_thinking" });
        if (line.includes("éteins")) {
          later(400, {
            type: "question",
            id: question++,
            method: "confirm",
            params: {
              spec: { name: "power", description: "Met en veille, éteint ou redémarre l'ordinateur.", risk: "sensitive" },
              call: { id: "1", name: "power", arguments: { action: "shutdown" } },
              choices: { always: true, never: true },
            },
          });
        } else {
          ui(300, { op: "tool_calls", calls: [{ id: "1", name: "current_time", arguments: {} }] });
          ui(700, { op: "tool_result", message: { role: "tool_result", call_id: "1", content: "ok", is_error: false } });
        }
        for (const [index, word] of "Voici une réponse simulée, écrite au fil de l'eau.".split(" ").entries()) {
          ui(900 + index * 80, { op: "stream_text", text: word + " " });
        }
        ui(1800, { op: "stream_end", kept: true });
        ui(1820, { op: "stop_thinking" });
        later(1850, { type: "reply", id: message.id, result: { quit: false } });
        return;
      }
      later(10, { type: "failure", id: message.id, message: "non simulé" });
    },
  };
}
