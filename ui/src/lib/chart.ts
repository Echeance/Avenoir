// Géométrie des graphiques des onglets déclaratifs : calculée ici, dessinée
// en SVG par Chart.svelte (aucun code du module).

export interface Series {
  name: string;
  values: (number | null)[];
}

export interface Bar {
  x: number;
  y: number;
  width: number;
  height: number;
  series: number;
  value: number;
}

export interface Line {
  series: number;
  points: string; // « x,y x,y … », coupée aux valeurs absentes
}

export interface Layout {
  bars: Bar[];
  lines: Line[];
  ticks: { y: number; label: string }[];
  labels: { x: number; text: string }[];
}

export const CHART = { width: 520, height: 180, left: 40, right: 8, top: 8, bottom: 22 };

/// Pas rond (1, 2 ou 5 × 10^n) au moins égal à `raw`.
export function niceStep(raw: number): number {
  const power = 10 ** Math.floor(Math.log10(raw));
  const fraction = raw / power;
  return (fraction <= 1 ? 1 : fraction <= 2 ? 2 : fraction <= 5 ? 5 : 10) * power;
}

function format(value: number): string {
  const rounded = Math.round(value * 100) / 100;
  return Math.abs(rounded) >= 1000 ? `${Math.round(rounded / 100) / 10}k` : String(rounded);
}

/// Géométrie d'un graphique en barres (`bar`) ou en courbes (`line`).
export function layout(kind: string, labels: string[], series: Series[]): Layout {
  const { width, height, left, right, top, bottom } = CHART;
  const values = series.flatMap((one) => one.values.filter((v): v is number => v !== null && Number.isFinite(v)));
  // Les barres partent de zéro ; une courbe suit ses valeurs.
  const floor = kind === "line" ? Infinity : 0;
  const ceiling = kind === "line" ? -Infinity : 0;
  let low = Math.min(floor, ...values);
  let high = Math.max(ceiling, ...values);
  if (!Number.isFinite(low) || !Number.isFinite(high)) {
    low = 0;
    high = 1;
  }
  if (high === low) {
    high = low + 1;
  }
  // Graduations rondes : pas de 1, 2 ou 5 × 10^n, bornes arrondies au pas.
  const step = niceStep((high - low) / 3);
  low = Math.floor(low / step) * step;
  high = Math.ceil(high / step) * step;
  const plot = height - top - bottom;
  const y = (value: number): number => top + ((high - value) / (high - low)) * plot;
  const slot = (width - left - right) / Math.max(labels.length, 1);
  const center = (index: number): number => left + slot * (index + 0.5);

  const ticks: { y: number; label: string }[] = [];
  for (let value = low; value <= high + step / 2; value += step) {
    ticks.push({ y: y(value), label: format(value) });
  }
  const names = labels.map((text, index) => ({ x: center(index), text }));

  const bars: Bar[] = [];
  const lines: Line[] = [];
  if (kind === "line") {
    series.forEach((one, s) => {
      let points: string[] = [];
      one.values.forEach((value, index) => {
        if (value === null || !Number.isFinite(value)) {
          if (points.length > 0) {
            lines.push({ series: s, points: points.join(" ") });
          }
          points = [];
          return;
        }
        points.push(`${center(index).toFixed(1)},${y(value).toFixed(1)}`);
      });
      if (points.length > 0) {
        lines.push({ series: s, points: points.join(" ") });
      }
    });
  } else {
    const group = slot * 0.8;
    const each = group / Math.max(series.length, 1);
    series.forEach((one, s) => {
      one.values.forEach((value, index) => {
        if (value === null || !Number.isFinite(value)) {
          return;
        }
        const top0 = Math.min(y(value), y(0));
        bars.push({
          x: center(index) - group / 2 + each * s,
          y: top0,
          width: Math.max(each - 1, 1),
          height: Math.max(Math.abs(y(value) - y(0)), 1),
          series: s,
          value,
        });
      });
    });
  }
  return { bars, lines, ticks, labels: names };
}
