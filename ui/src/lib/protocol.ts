// Messages échangés entre la page et l'hôte C++ des fenêtres (apps/ui). La
// page ne parle qu'à l'hôte ; l'hôte relaie vers le démon les seuls appels
// permis (docs/PROJECT.md § 4.6). Tout ce qui arrive est validé ici : un
// message mal formé est écarté, jamais interprété.

/// Appels que la page peut demander au démon, par l'hôte.
export const CALLS = [
  "session/open",
  "input",
  "session/describe",
  "settings/list",
  "journal/list",
  "models/list",
  "permissions/list",
  "modules/list",
  "conversations/list",
  "tabs/list",
  "tabs/content",
  "tabs/event",
] as const;
export type CallMethod = (typeof CALLS)[number];

/// Questions que le démon pose à l'interface.
export const QUESTIONS = ["ask", "ask_yes_no", "confirm", "ask_secret"] as const;
export type QuestionMethod = (typeof QUESTIONS)[number];

export type Json = null | boolean | number | string | Json[] | { [key: string]: Json };
export type JsonObject = { [key: string]: Json };

/// Page → hôte.
export type PageMessage =
  | { type: "call"; id: number; method: CallMethod; params: JsonObject }
  | { type: "cancel"; id: number }
  | { type: "answer"; id: number; result: Json }
  | { type: "resize"; height: number }
  | { type: "open_app" }
  | { type: "ready" };

/// Hôte → page.
export type HostMessage =
  | { type: "ui"; params: JsonObject }
  | { type: "question"; id: number; method: QuestionMethod; params: JsonObject }
  | { type: "reply"; id: number; result: JsonObject }
  | { type: "failure"; id: number; message: string }
  | { type: "dismiss"; id: number }
  | { type: "status"; connected: boolean; message: string };

export function isObject(value: unknown): value is JsonObject {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function isId(value: unknown): value is number {
  return typeof value === "number" && Number.isSafeInteger(value) && value >= 0;
}

/// Message de l'hôte validé, ou `null` s'il est mal formé.
export function parseHostMessage(raw: unknown): HostMessage | null {
  if (!isObject(raw)) {
    return null;
  }
  switch (raw["type"]) {
    case "ui":
      return isObject(raw["params"]) ? { type: "ui", params: raw["params"] } : null;
    case "question": {
      const method = raw["method"];
      if (
        !isId(raw["id"]) ||
        typeof method !== "string" ||
        !(QUESTIONS as readonly string[]).includes(method) ||
        !isObject(raw["params"])
      ) {
        return null;
      }
      return { type: "question", id: raw["id"], method: method as QuestionMethod, params: raw["params"] };
    }
    case "reply":
      return isId(raw["id"]) && isObject(raw["result"])
        ? { type: "reply", id: raw["id"], result: raw["result"] }
        : null;
    case "failure":
      return isId(raw["id"]) && typeof raw["message"] === "string"
        ? { type: "failure", id: raw["id"], message: raw["message"] }
        : null;
    case "dismiss":
      return isId(raw["id"]) ? { type: "dismiss", id: raw["id"] } : null;
    case "status":
      return typeof raw["connected"] === "boolean"
        ? {
            type: "status",
            connected: raw["connected"],
            message: typeof raw["message"] === "string" ? raw["message"] : "",
          }
        : null;
    default:
      return null;
  }
}
