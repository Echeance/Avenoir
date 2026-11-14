// Session de l'interface : état réactif (runes de Svelte) tenu à jour par le
// pont, et actions de l'utilisateur.

import { Bridge, nativeTransport, type WebKitWindow } from "./bridge";
import { mockTransport } from "./mock";
import type { Json, JsonObject, QuestionMethod } from "./protocol";
import { initialState, reduce, type ViewState } from "./state";

export interface Question {
  method: QuestionMethod;
  params: JsonObject;
  answer(result: Json): void;
}

class Session {
  view = $state<ViewState>(initialState());
  connected = $state(false);
  status = $state("connexion…");
  question = $state<Question | null>(null);
  /// Appel « input » en cours (pour « Arrêter »).
  running = $state<number | null>(null);

  private readonly bridge: Bridge;

  constructor() {
    const transport = nativeTransport(window as unknown as WebKitWindow) ?? mockTransport();
    this.bridge = new Bridge(transport, {
      ui: (params) => {
        this.view = reduce(this.view, params);
      },
      status: (connected, message) => {
        const first = connected && !this.connected;
        this.connected = connected;
        this.status = message;
        if (first) {
          void this.open();
        }
      },
      question: (method, params) =>
        new Promise<Json>((resolve) => {
          this.question = {
            method,
            params,
            answer: (result) => {
              this.question = null;
              resolve(result);
            },
          };
        }),
    });
  }

  private async open(): Promise<void> {
    try {
      await this.bridge.call("session/open", { interactive: true }).result;
    } catch (error) {
      this.status = error instanceof Error ? error.message : "session refusée";
    }
  }

  /// Envoie une requête (ou une commande « / ») au démon.
  async send(line: string): Promise<void> {
    const text = line.trim();
    if (text === "" || this.running !== null || !this.connected) {
      return;
    }
    const call = this.bridge.call("input", { line: text });
    this.running = call.id;
    try {
      const result = await call.result;
      if (typeof result["llm"] === "string") {
        this.view = { ...this.view, llm: result["llm"] };
      }
      if (typeof result["conversation"] === "string") {
        this.view = { ...this.view, conversation: result["conversation"] };
      }
    } catch (error) {
      this.view = reduce(this.view, {
        op: "error",
        text: error instanceof Error ? error.message : "échec de la requête",
      });
    } finally {
      this.running = null;
    }
  }

  /// Arrête l'action en cours (équivalent de Ctrl+C).
  stop(): void {
    if (this.running !== null) {
      this.bridge.cancel(this.running);
    }
  }
}

export const session = new Session();
