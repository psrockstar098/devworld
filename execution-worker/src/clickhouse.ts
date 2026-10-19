/**
 * Minimal ClickHouse HTTP client for the worker's event sink.
 *
 * Only what the sink needs: JSONEachRow inserts and a ping. Values are built by
 * the caller; identifiers are validated.
 */

export interface ClickHouseConfig {
  url: string;
  user: string;
  password: string;
  database: string;
}

export type FetchLike = (
  input: string,
  init?: { method?: string; headers?: Record<string, string>; body?: string },
) => Promise<Response>;

export class ClickHouseError extends Error {
  constructor(
    readonly status: number,
    message: string,
  ) {
    super(message);
    this.name = "ClickHouseError";
  }
}

/** Only `db.table` (or `table`) of alphanumerics/underscore is allowed. */
export function quoteTable(table: string): string {
  const parts = table.split(".");
  if (parts.length > 2 || parts.some((p) => !/^[A-Za-z_][A-Za-z0-9_]*$/.test(p))) {
    throw new ClickHouseError(400, `unsafe table name: ${table}`);
  }
  return parts.map((p) => `\`${p}\``).join(".");
}

export class ClickHouse {
  constructor(
    private readonly cfg: ClickHouseConfig,
    private readonly fetchImpl: FetchLike = fetch as unknown as FetchLike,
  ) {}

  private endpoint(): string {
    const u = new URL(this.cfg.url.replace(/\/$/, "") + "/");
    u.searchParams.set("database", this.cfg.database);
    u.searchParams.set("wait_end_of_query", "1");
    return u.toString();
  }

  async command(sql: string): Promise<string> {
    const res = await this.fetchImpl(this.endpoint(), {
      method: "POST",
      headers: {
        "X-ClickHouse-User": this.cfg.user,
        "X-ClickHouse-Key": this.cfg.password,
        "Content-Type": "text/plain; charset=utf-8",
      },
      body: sql,
    });
    const text = await res.text();
    if (!res.ok) throw new ClickHouseError(res.status, text.trim() || String(res.status));
    return text;
  }

  async insert(table: string, rows: Record<string, unknown>[]): Promise<number> {
    if (rows.length === 0) return 0;
    const body = rows.map((r) => JSON.stringify(r)).join("\n");
    await this.command(`INSERT INTO ${quoteTable(table)} FORMAT JSONEachRow\n${body}`);
    return rows.length;
  }

  async ping(): Promise<boolean> {
    try {
      await this.command("SELECT 1");
      return true;
    } catch {
      return false;
    }
  }
}
