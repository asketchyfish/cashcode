export type CompileHead = { title?: string; meta?: Array<Record<string, string>> };
export type CompileResult = { html: string; head: CompileHead };

function escapeHtml(value: unknown): string {
  const str = String(value ?? "");
  return str
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#39;");
}

function evalInContext(expr: string, context: Record<string, unknown>): unknown {
  const argNames = Object.keys(context);
  const argValues = Object.values(context);
  // eslint-disable-next-line no-new-func
  const fn = new Function(...argNames, `return (${expr});`);
  return fn(...argValues);
}

function extractHead(source: string): { head: CompileHead; rest: string } {
  const m = source.match(/\$head\s*{([\s\S]*?)}/);
  if (!m) return { head: {}, rest: source };
  let head: CompileHead = {};
  try {
    // eslint-disable-next-line no-new-func
    const obj = Function(`return ({${m[1]}});`)();
    head = obj as CompileHead;
  } catch {
    head = {};
  }
  const rest = source.replace(m[0], "");
  return { head, rest };
}

function stripRoute(source: string): string {
  return source.replace(/\$route\s+["']([\s\S]*?)["']\s*/g, "");
}

function applyIfs(html: string, context: Record<string, unknown>): string {
  // Matches elements with $if attribute and conditionally renders content
  const pattern = /<([a-zA-Z0-9-]+)([^>]*)\s\$if=\"([^\"]+)\"([^>]*)>([\s\S]*?)<\/\1>/g;
  return html.replace(pattern, (_m, tag, preAttrs, expr, postAttrs, inner) => {
    let show = false;
    try {
      const normalized = expr.replace(/\$([A-Za-z_][\w]*)/g, '$1');
      show = Boolean(evalInContext(normalized, context));
    } catch {
      show = false;
    }
    if (!show) return "";
    const cleanedAttrs = `${preAttrs}${postAttrs}`.replace(/\s\$if=\"([^\"]*)\"/, "");
    const processedInner = applyIfs(inner, context);
    return `<${tag}${cleanedAttrs}>${processedInner}</${tag}>`;
  });
}

function expandForLoops(html: string, context: Record<string, unknown>): string {
  // <tag ... $for="$x in items">inner</tag>
  return html.replace(
    /<([a-zA-Z0-9-]+)([^>]*)\s\$for=\"([^\"]+)\"([^>]*)>([\s\S]*?)<\/\1>/g,
    (_m, tag, preAttrs, expr, postAttrs, inner) => {
      const attrs = `${preAttrs}${postAttrs}`;
      const forMatch = expr.match(/^\s*\$([a-zA-Z_][\w]*)\s+in\s+([\s\S]+)$/);
      if (!forMatch) return _m; // leave as-is if not matched
      const varName = forMatch[1];
      const listExpr = forMatch[2].replace(/\$([A-Za-z_][\w]*)/g, '$1');
      let list: unknown[] = [];
      try {
        const evaluated = evalInContext(listExpr, context);
        if (Array.isArray(evaluated)) list = evaluated;
        else if (evaluated && typeof (evaluated as any)[Symbol.iterator] === "function")
          list = Array.from(evaluated as any);
      } catch {
        // ignore
      }
      const pieces: string[] = [];
      for (const item of list) {
        const loopCtx = { ...context, [varName]: item };
        const afterIf = applyIfs(inner, loopCtx);
        const renderedInner = renderInterpolations(afterIf, loopCtx);
        pieces.push(`<${tag}${attrs}>${renderedInner}</${tag}>`);
      }
      return pieces.join("");
    }
  );
}

function renderInterpolations(html: string, context: Record<string, unknown>): string {
  return html.replace(/\{\$([^}]+)}/g, (_m, expr) => {
    try {
      const value = evalInContext(String(expr).trim(), context);
      return escapeHtml(value);
    } catch {
      return "";
    }
  });
}

export function renderCash(source: string, context: Record<string, unknown> = {}): CompileResult {
  const { head, rest } = extractHead(source);
  let html = stripRoute(rest);
  // Rewrite minimal $action on forms to a concrete POST endpoint
  html = html.replace(/<form([^>]*)\s\$action=\"([^\"]+)\"([^>]*)>/g, (_m, pre, name, post) => {
    const cleaned = `${pre}${post}`.replace(/\s\$action=\"([^\"]+)\"/, "");
    return `<form${cleaned} action=\"/__action/${name}\" method=\"post\">`;
  });
  html = expandForLoops(html, context);
  html = applyIfs(html, context);
  html = renderInterpolations(html, context);
  return { html, head };
}


