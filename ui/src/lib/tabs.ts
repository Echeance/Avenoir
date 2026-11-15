// Messages d'un onglet web de module (iframe isolée, directive 12) : la page
// ne relaie que des événements bien formés venus de cette iframe ; tout le
// reste est écarté.

import { isObject, type Json } from "./protocol";

/// Taille maximale d'un événement (sérialisé).
export const MAX_EVENT_BYTES = 16 * 1024;

export interface TabEvent {
  name: string;
  payload: Json;
}

/// Événement `{type: "avenoir/event", name, payload}` validé, ou `null`.
export function parseTabEvent(data: unknown): TabEvent | null {
  if (!isObject(data) || data["type"] !== "avenoir/event") {
    return null;
  }
  const name = data["name"];
  if (typeof name !== "string" || !/^[a-z0-9_-]{1,64}$/.test(name)) {
    return null;
  }
  const payload = data["payload"] ?? null;
  let size = 0;
  try {
    size = new TextEncoder().encode(JSON.stringify(payload)).length;
  } catch {
    return null;
  }
  return size <= MAX_EVENT_BYTES ? { name, payload } : null;
}
