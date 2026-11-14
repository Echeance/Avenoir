// État de l'interface, construit à partir des messages « ui » du démon (les
// mêmes que ceux de la CLI). Fonctions pures : testées sans navigateur.

import { isObject, type Json, type JsonObject } from "./protocol";
import { plainText } from "./text";

export type Entry =
  | { kind: "user"; text: string }
  | { kind: "assistant"; text: string; streaming: boolean }
  | { kind: "tool"; id: string; name: string; summary: string; done: boolean; failed: boolean }
  | { kind: "note"; level: "info" | "success" | "warning" | "error"; text: string };

export interface ViewState {
  entries: Entry[];
  thinking: boolean;
  activity: string;
  llm: string;
  conversation: string;
  tools: number;
}

/// Entrées gardées au plus : l'interface montre la fin de la conversation.
export const MAX_ENTRIES = 500;

export function initialState(): ViewState {
  return { entries: [], thinking: false, activity: "", llm: "", conversation: "", tools: 0 };
}

function text(value: Json | undefined): string {
  return typeof value === "string" ? plainText(value) : "";
}

/// Arguments d'un appel d'outil, en une ligne courte.
function summarize(value: Json | undefined): string {
  if (!isObject(value)) {
    return "";
  }
  const values = Object.values(value);
  const only = values[0];
  if (values.length === 1 && typeof only === "string") {
    return plainText(only).slice(0, 120);
  }
  return values.length === 0 ? "" : plainText(JSON.stringify(value)).slice(0, 120);
}

function fromMessage(message: Json): Entry[] {
  if (!isObject(message)) {
    return [];
  }
  switch (message["role"]) {
    case "user":
      return [{ kind: "user", text: text(message["content"]) }];
    case "assistant": {
      const out: Entry[] = [];
      const body = text(message["text"]);
      if (body !== "") {
        out.push({ kind: "assistant", text: body, streaming: false });
      }
      const calls = message["tool_calls"];
      if (Array.isArray(calls)) {
        for (const call of calls) {
          if (isObject(call)) {
            out.push({
              kind: "tool",
              id: text(call["id"]),
              name: text(call["name"]),
              summary: summarize(call["arguments"]),
              done: true,
              failed: false,
            });
          }
        }
      }
      return out;
    }
    default:
      return [];
  }
}

function push(state: ViewState, ...added: Entry[]): ViewState {
  const entries = [...state.entries, ...added];
  return { ...state, entries: entries.slice(Math.max(0, entries.length - MAX_ENTRIES)) };
}

/// Fragment d'une réponse au fil de l'eau : ajouté à la réponse ouverte.
function stream(state: ViewState, fragment: string): ViewState {
  const last = state.entries.at(-1);
  if (last?.kind === "assistant" && last.streaming) {
    const entries = state.entries.slice(0, -1);
    return { ...state, entries: [...entries, { ...last, text: last.text + fragment }] };
  }
  return push(state, { kind: "assistant", text: fragment, streaming: true });
}

/// État après un message « ui » du démon. Une opération inconnue ne change rien.
export function reduce(state: ViewState, params: JsonObject): ViewState {
  const op = params["op"];
  const body = text(params["text"]);
  switch (op) {
    case "info":
    case "notify":
    case "plain":
      return body.trim() === "" ? state : push(state, { kind: "note", level: "info", text: body });
    case "success":
    case "warning":
    case "error":
      return push(state, { kind: "note", level: op, text: body });
    case "user":
      return push(state, { kind: "user", text: body });
    case "assistant": {
      // Réponse complète : elle remplace la version reçue au fil de l'eau.
      const last = state.entries.at(-1);
      const entries =
        last?.kind === "assistant" && last.streaming ? state.entries.slice(0, -1) : state.entries;
      return push({ ...state, entries }, { kind: "assistant", text: body, streaming: false });
    }
    case "tool_calls": {
      const calls = Array.isArray(params["calls"]) ? params["calls"] : [];
      const added: Entry[] = [];
      for (const call of calls) {
        if (isObject(call)) {
          added.push({
            kind: "tool",
            id: text(call["id"]),
            name: text(call["name"]),
            summary: summarize(call["arguments"]),
            done: false,
            failed: false,
          });
        }
      }
      return push(state, ...added);
    }
    case "tool_result": {
      const message = params["message"];
      if (!isObject(message)) {
        return state;
      }
      const id = text(message["call_id"]);
      const failed = message["is_error"] === true;
      return {
        ...state,
        entries: state.entries.map((entry) =>
          entry.kind === "tool" && entry.id === id && !entry.done
            ? { ...entry, done: true, failed }
            : entry,
        ),
      };
    }
    case "messages": {
      const list = Array.isArray(params["messages"]) ? params["messages"] : [];
      return push({ ...state, entries: [] }, ...list.flatMap(fromMessage));
    }
    case "clear":
      return { ...state, entries: [] };
    case "banner":
      return {
        ...state,
        llm: text(params["llm"]),
        conversation: text(params["conversation"]),
        tools: typeof params["tools"] === "number" ? params["tools"] : 0,
      };
    case "start_thinking":
      return { ...state, thinking: true, activity: "Réflexion" };
    case "activity":
      return { ...state, activity: body };
    case "stop_thinking":
      return { ...state, thinking: false, activity: "" };
    case "stream_text":
      return stream(state, body);
    case "stream_end": {
      const last = state.entries.at(-1);
      if (last?.kind !== "assistant" || !last.streaming) {
        return state;
      }
      const entries = state.entries.slice(0, -1);
      // Réponse écartée par Avenoir (action annoncée mais non faite) : retirée.
      return params["kept"] === false
        ? { ...state, entries }
        : { ...state, entries: [...entries, { ...last, streaming: false }] };
    }
    default:
      return state;
  }
}
