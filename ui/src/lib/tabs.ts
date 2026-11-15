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

/// Argument de commande saisi dans un formulaire d'onglet : sur une ligne,
/// sans caractère de contrôle, 100 caractères au plus, espaces aux bords
/// retirés. La commande passe ensuite par la saisie habituelle (permissions,
/// confirmations, audit).
export function commandArgument(value: string): string {
  // eslint-disable-next-line no-control-regex
  return Array.from(value.replace(/[\u0000-\u001f\u007f-\u009f‎‏‪-‮⁦-⁩]/g, " "))
    .slice(0, 100)
    .join("")
    .trim();
}
