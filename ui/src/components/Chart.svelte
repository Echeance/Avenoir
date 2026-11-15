<script lang="ts">
  // Graphique d'un onglet déclaratif, dessiné en SVG par Avenoir.
  import { CHART, layout, type Series } from "../lib/chart";

  let {
    kind,
    labels,
    series,
    title = "",
    unit = "",
  }: { kind: string; labels: string[]; series: Series[]; title?: string; unit?: string } = $props();

  const shape = $derived(layout(kind, labels, series));
  // Étiquettes de l'axe : une sur deux ou plus quand elles sont nombreuses.
  const every = $derived(Math.max(1, Math.ceil(labels.length / 12)));
</script>

<figure>
  {#if title !== "" || unit !== ""}
    <figcaption>{title}{unit !== "" ? ` (${unit})` : ""}</figcaption>
  {/if}
  <svg viewBox="0 0 {CHART.width} {CHART.height}" role="img" aria-label={title}>
    {#each shape.ticks as tick (tick.y)}
      <line class="grid" x1={CHART.left} x2={CHART.width - CHART.right} y1={tick.y} y2={tick.y} />
      <text class="axis" x={CHART.left - 4} y={tick.y + 3} text-anchor="end">{tick.label}</text>
    {/each}
    {#each shape.bars as bar, index (index)}
      <rect class="s{bar.series}" x={bar.x} y={bar.y} width={bar.width} height={bar.height}>
        <title>{series[bar.series]?.name ?? ""} {bar.value}</title>
      </rect>
    {/each}
    {#each shape.lines as line, index (index)}
      <polyline class="line s{line.series}" points={line.points} />
    {/each}
    {#each shape.labels as label, index (index)}
      {#if index % every === 0}
        <text class="axis" x={label.x} y={CHART.height - 6} text-anchor="middle">{label.text}</text>
      {/if}
    {/each}
  </svg>
  {#if series.length > 1}
    <p class="legend">
      {#each series as one, index (index)}<span class="key s{index}">■</span> {one.name}&nbsp;&nbsp;{/each}
    </p>
  {/if}
</figure>

<style>
  figure {
    margin: 0;
    width: 100%;
  }
  figcaption {
    color: var(--muted);
    font-size: 12px;
  }
  svg {
    width: 100%;
    height: auto;
    display: block;
  }
  .grid {
    stroke: var(--line);
    stroke-width: 1;
  }
  .axis {
    fill: var(--muted);
    font-size: 10px;
  }
  rect.s0,
  .key.s0 {
    fill: var(--azure);
    color: var(--azure);
  }
  rect.s1,
  .key.s1 {
    fill: var(--green);
    color: var(--green);
  }
  rect.s2,
  .key.s2 {
    fill: var(--yellow);
    color: var(--yellow);
  }
  rect.s3,
  .key.s3 {
    fill: var(--red);
    color: var(--red);
  }
  .line {
    fill: none;
    stroke-width: 2;
  }
  .line.s0 {
    stroke: var(--azure);
  }
  .line.s1 {
    stroke: var(--green);
  }
  .line.s2 {
    stroke: var(--yellow);
  }
  .line.s3 {
    stroke: var(--red);
  }
  .legend {
    margin: 0;
    font-size: 12px;
    color: var(--muted);
  }
</style>
