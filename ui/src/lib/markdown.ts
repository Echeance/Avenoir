// Markdown des réponses du modèle, analysé en blocs et en fragments de texte
// que les composants Svelte dessinent : aucun HTML n'est produit ni inséré,
// donc rien de ce que le modèle écrit ne peut devenir du code dans la page.
// Les liens restent du texte (l'adresse affichée à côté) : ouvrir une page
// passe par Avenoir (outil open_url, permissions).

export type Inline =
  | { kind: "text"; text: string }
  | { kind: "strong"; children: Inline[] }
  | { kind: "em"; children: Inline[] }
  | { kind: "code"; text: string }
  | { kind: "link"; children: Inline[]; url: string };

export type Block =
  | { kind: "paragraph"; inline: Inline[] }
  | { kind: "heading"; level: number; inline: Inline[] }
  | { kind: "list"; ordered: boolean; items: Inline[][] }
  | { kind: "quote"; inline: Inline[] }
  | { kind: "code"; language: string; text: string }
  | { kind: "rule" }
  | { kind: "table"; header: Inline[][]; rows: Inline[][][] };

/// Fragments d'une ligne : **gras**, *italique* / _italique_, `code`, [lien](url).
export function parseInline(text: string): Inline[] {
  const out: Inline[] = [];
  let plain = "";
  const flush = (): void => {
    if (plain !== "") {
      out.push({ kind: "text", text: plain });
      plain = "";
    }
  };
  let index = 0;
  while (index < text.length) {
    const rest = text.slice(index);
    const code = /^`([^`]+)`/.exec(rest);
    if (code?.[1] !== undefined) {
      flush();
      out.push({ kind: "code", text: code[1] });
      index += code[0].length;
      continue;
    }
    const strong = /^\*\*(.+?)\*\*/.exec(rest) ?? /^__(.+?)__/.exec(rest);
    if (strong?.[1] !== undefined) {
      flush();
      out.push({ kind: "strong", children: parseInline(strong[1]) });
      index += strong[0].length;
      continue;
    }
    const em = /^\*([^*\s][^*]*?)\*/.exec(rest) ?? /^_([^_\s][^_]*?)_(?![\p{L}\p{N}])/u.exec(rest);
    if (em?.[1] !== undefined && !/[\p{L}\p{N}]$/u.test(plain)) {
      flush();
      out.push({ kind: "em", children: parseInline(em[1]) });
      index += em[0].length;
      continue;
    }
    const link = /^\[([^\]]+)\]\(([^)\s]+)\)/.exec(rest);
    if (link?.[1] !== undefined && link[2] !== undefined) {
      flush();
      out.push({ kind: "link", children: parseInline(link[1]), url: link[2] });
      index += link[0].length;
      continue;
    }
    plain += text[index];
    index += 1;
  }
  flush();
  return out;
}

function cells(line: string): string[] {
  return line
    .trim()
    .replace(/^\|/, "")
    .replace(/\|$/, "")
    .split("|")
    .map((cell) => cell.trim());
}

/// Blocs d'un texte Markdown (titres, listes, citations, code, tableaux, filets).
export function parseMarkdown(source: string): Block[] {
  let lines = source.replace(/\r\n?/g, "\n").trim().split("\n");
  // Réponse entière enveloppée dans un bloc « markdown » : c'est du Markdown.
  if (
    lines.length >= 2 &&
    /^\s*```\s*(markdown|md)\s*$/i.test(lines[0] ?? "") &&
    /^\s*```\s*$/.test(lines.at(-1) ?? "")
  ) {
    lines = lines.slice(1, -1);
  }
  const blocks: Block[] = [];
  let paragraph: string[] = [];
  const endParagraph = (): void => {
    if (paragraph.length > 0) {
      blocks.push({ kind: "paragraph", inline: parseInline(paragraph.join("\n")) });
      paragraph = [];
    }
  };
  let index = 0;
  while (index < lines.length) {
    const line = lines[index] ?? "";
    const fence = /^\s*(```|~~~)\s*([\w+-]*)\s*$/.exec(line);
    if (fence?.[1] !== undefined) {
      endParagraph();
      const body: string[] = [];
      index += 1;
      // Fermé par le même signe, sans langage ; resté ouvert (réponse en
      // cours) : jusqu'à la fin.
      const closing = new RegExp(`^\\s*${fence[1]}\\s*$`);
      while (index < lines.length && !closing.test(lines[index] ?? "")) {
        body.push(lines[index] ?? "");
        index += 1;
      }
      blocks.push({ kind: "code", language: fence[2] ?? "", text: body.join("\n") });
      index += 1;
      continue;
    }
    if (line.trim() === "") {
      endParagraph();
      index += 1;
      continue;
    }
    const heading = /^(#{1,6})\s+(.*)$/.exec(line);
    if (heading?.[1] !== undefined && heading[2] !== undefined) {
      endParagraph();
      blocks.push({ kind: "heading", level: heading[1].length, inline: parseInline(heading[2]) });
      index += 1;
      continue;
    }
    if (/^\s*([-*_])(\s*\1){2,}\s*$/.test(line)) {
      endParagraph();
      blocks.push({ kind: "rule" });
      index += 1;
      continue;
    }
    if (/^\s*>/.test(line)) {
      endParagraph();
      const quoted: string[] = [];
      while (index < lines.length && /^\s*>/.test(lines[index] ?? "")) {
        quoted.push((lines[index] ?? "").replace(/^\s*>\s?/, ""));
        index += 1;
      }
      blocks.push({ kind: "quote", inline: parseInline(quoted.join("\n")) });
      continue;
    }
    const item = /^\s*([-*+]|\d+[.)])\s+(.*)$/.exec(line);
    if (item?.[1] !== undefined) {
      endParagraph();
      const ordered = /\d/.test(item[1]);
      const items: Inline[][] = [];
      while (index < lines.length) {
        const next = /^\s*([-*+]|\d+[.)])\s+(.*)$/.exec(lines[index] ?? "");
        if (next?.[2] === undefined || /\d/.test(next[1] ?? "") !== ordered) {
          break;
        }
        items.push(parseInline(next[2]));
        index += 1;
      }
      blocks.push({ kind: "list", ordered, items });
      continue;
    }
    const separator = lines[index + 1] ?? "";
    if (line.includes("|") && /^\s*\|?\s*:?-{3,}:?\s*(\|\s*:?-{3,}:?\s*)*\|?\s*$/.test(separator)) {
      endParagraph();
      const header = cells(line).map(parseInline);
      const rows: Inline[][][] = [];
      index += 2;
      while (index < lines.length && (lines[index] ?? "").includes("|")) {
        rows.push(cells(lines[index] ?? "").map(parseInline));
        index += 1;
      }
      blocks.push({ kind: "table", header, rows });
      continue;
    }
    paragraph.push(line);
    index += 1;
  }
  endParagraph();
  return blocks;
}
