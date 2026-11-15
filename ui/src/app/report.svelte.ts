// Données d'une vue de l'application, lues au démon (requêtes en lecture
// seule) et relues après chaque action.

import type { CallMethod, JsonObject } from "../lib/protocol";
import { session } from "../lib/session.svelte";

export class Report {
  data = $state<JsonObject | null>(null);
  error = $state("");
  loading = $state(false);

  constructor(
    private readonly method: Exclude<CallMethod, "input" | "session/open">,
    private readonly params: () => JsonObject = () => ({}),
  ) {}

  async load(): Promise<void> {
    if (!session.connected) {
      return;
    }
    this.loading = true;
    try {
      this.data = await session.request(this.method, this.params());
      this.error = "";
    } catch (error) {
      this.error = error instanceof Error ? error.message : "lecture impossible";
    } finally {
      this.loading = false;
    }
  }

  /// Commande d'Avenoir (avec ses confirmations), puis relecture.
  async run(command: string): Promise<void> {
    await session.send(command);
    await this.load();
  }
}

export function list(value: unknown): JsonObject[] {
  return Array.isArray(value) ? value.filter((item): item is JsonObject => typeof item === "object" && item !== null && !Array.isArray(item)) : [];
}

export function str(value: unknown): string {
  return typeof value === "string" ? value : typeof value === "number" ? String(value) : "";
}

export const RISK_LABELS: Record<string, string> = {
  read_only: "lecture seule",
  reversible: "réversible",
  sensitive: "sensible",
};
