// Mise en forme des valeurs des onglets « Activité » et « Système ».

/// Durée lisible : « 45 s », « 12 min », « 3 h 05 », « 2 j 4 h ».
export function formatDuration(seconds: number): string {
  const s = Math.max(0, Math.floor(seconds));
  if (s < 60) {
    return `${s} s`;
  }
  if (s < 3600) {
    return `${Math.floor(s / 60)} min`;
  }
  if (s < 86400) {
    return `${Math.floor(s / 3600)} h ${String(Math.floor((s % 3600) / 60)).padStart(2, "0")}`;
  }
  return `${Math.floor(s / 86400)} j ${Math.floor((s % 86400) / 3600)} h`;
}

/// Nombre de jetons : « 850 », « 12,3 k », « 1,2 M ».
export function formatTokens(count: number): string {
  const n = Math.max(0, count);
  if (n < 1000) {
    return String(Math.round(n));
  }
  const [value, unit] = n < 1_000_000 ? [n / 1000, "k"] : [n / 1_000_000, "M"];
  return `${value.toFixed(1).replace(".", ",")} ${unit}`;
}

/// Coût indicatif en dollars : « 0,0042 $ », « 1,25 $ ».
export function formatCost(dollars: number): string {
  return `${dollars < 0.1 ? dollars.toFixed(4).replace(".", ",") : dollars.toFixed(2).replace(".", ",")} $`;
}
