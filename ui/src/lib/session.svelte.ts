// Session de l'interface : état réactif (runes de Svelte) tenu à jour par le
// pont, et actions de l'utilisateur.

import { Bridge, nativeTransport, type WebKitWindow } from "./bridge";
import { mockTransport } from "./mock";
import type { CallMethod, Json, JsonObject, QuestionMethod } from "./protocol";
import { initialState, reduce, type Entry, type ViewState } from "./state";

export interface Question {
  id: number;
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
  /// Messages d'Avenoir pendant la dernière requête (retour des actions de
  /// l'application : « réglage changé », refus…).
  lastNotes = $state<Extract<Entry, { kind: "note" }>[]>([]);
  /// Onglets déclarés par les modules lancés (décision 73).
  tabs = $state<{ module: string; id: string; title: string; kind: string }[]>([]);
  /// Requêtes dont des actions peuvent encore être défaites (`/annuler`).
  undoable = $state(0);

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
      dismiss: (id) => {
        if (this.question?.id === id) {
          this.question = null;
        }
      },
      question: (id, method, params) =>
        new Promise<Json>((resolve) => {
          this.question = {
            id,
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
      return;
    }
    try {
      const result = await this.bridge.call("tabs/list", {}).result;
      const list = Array.isArray(result["tabs"]) ? result["tabs"] : [];
      this.tabs = list.flatMap((tab) =>
        typeof tab === "object" && tab !== null && !Array.isArray(tab) &&
        typeof tab["module"] === "string" && typeof tab["id"] === "string" &&
        typeof tab["title"] === "string" && typeof tab["kind"] === "string"
          ? [{ module: tab["module"], id: tab["id"], title: tab["title"], kind: tab["kind"] }]
          : [],
      );
    } catch {
      this.tabs = []; // démon plus ancien : pas d'onglets
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
    const before = this.view.entries.length;
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
      this.lastNotes = this.view.entries
        .slice(Math.min(before, this.view.entries.length))
        .filter((entry): entry is Extract<Entry, { kind: "note" }> => entry.kind === "note");
      void this.refreshUndo();
    }
  }

  /// Relit le nombre de requêtes encore annulables.
  async refreshUndo(): Promise<void> {
    try {
      const result = await this.request("undo/list");
      this.undoable = Array.isArray(result["requests"]) ? result["requests"].length : 0;
    } catch {
      this.undoable = 0;
    }
  }

  /// Données en lecture seule du démon (journal, permissions, modèles…).
  async request(method: Exclude<CallMethod, "input" | "session/open">, params: JsonObject = {}): Promise<JsonObject> {
    return this.bridge.call(method, params).result;
  }

  /// Adresse de l'onglet web du module `id`, ou `null`.
  moduleUrl(id: string): string | null {
    return this.bridge.moduleUrl(id);
  }

  /// Ouvre la fenêtre de l'application principale.
  openApp(): void {
    this.bridge.openApp();
  }

  /// Hauteur de la page (barre pliée ou dépliée), donnée à l'hôte.
  resize(height: number): void {
    this.bridge.resize(height);
  }

  /// Arrête l'action en cours (équivalent de Ctrl+C).
  stop(): void {
    if (this.running !== null) {
      this.bridge.cancel(this.running);
    }
  }
}

export const session = new Session();
