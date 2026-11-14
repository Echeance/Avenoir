// Le démon met en forme ses messages pour un terminal (couleurs ANSI,
// caractères de contrôle) : l'interface n'en garde que le texte.

// eslint-disable-next-line no-control-regex
const ANSI = /\x1b\[[0-9;?]*[A-Za-z]|\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)/g;
// eslint-disable-next-line no-control-regex
const CONTROL = /[\x00-\x08\x0b-\x1f\x7f]/g;

/// Texte sans séquences de terminal ni caractères de contrôle (sauts de
/// ligne et tabulations gardés).
export function plainText(text: string): string {
  return text.replace(ANSI, "").replace(CONTROL, "");
}
