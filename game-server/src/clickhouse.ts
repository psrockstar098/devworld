// clickhouse.ts — a small ClickHouse HTTP client.
//
// ClickHouse's HTTP interface is simple enough to use directly with `fetch`, so
// this service carries no ClickHouse driver dependency. Values are bound as
// ClickHouse query parameters (`param_x=…` → `{x:String}`) rather than string
// interpolation; identifiers are quoted and validated.

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

  private endpoint(params: Record<string, string>): string {
    const u = new URL(this.cfg.url.replace(/\/$/, "") + "/");
    u.searchParams.set("database", this.cfg.database);
    for (const [k, v] of Object.entries(params)) u.searchParams.set(k, v);
    return u.toString();
  }

  private headers(): Record<string, string> {
    return {
      "X-ClickHouse-User": this.cfg.user,
      "X-ClickHouse-Key": this.cfg.password,
      "Content-Type": "text/plain; charset=utf-8",
    };
  }

  /** Run a statement, returning the raw response text. */
  async command(sql: string, params: Record<string, string> = {}): Promise<string> {
    const qp: Record<string, string> = { wait_end_of_query: "1" };
    for (const [k, v] of Object.entries(params)) qp[`param_${k}`] = v;
    const res = await this.fetchImpl(this.endpoint(qp), {
      method: "POST",
      headers: this.headers(),
      body: sql,
    });
    const text = await res.text();
    if (!res.ok) throw new ClickHouseError(res.status, text.trim() || String(res.status));
    return text;
  }

  /** SELECT as an array of row objects (JSONEachRow). */
  async query<T = Record<string, unknown>>(
    sql: string,
    params: Record<string, string> = {},
  ): Promise<T[]> {
    const text = await this.command(sql.trim().replace(/;\s*$/, "") + "\nFORMAT JSONEachRow", params);
    return text
      .split("\n")
      .filter((l) => l.trim().length > 0)
      .map((l) => JSON.parse(l) as T);
  }

  /** Insert row objects into a table (JSONEachRow). Returns the row count. */
  async insert(table: string, rows: Record<string, unknown>[]): Promise<number> {
    if (rows.length === 0) return 0;
    const body = rows.map((r) => JSON.stringify(r)).join("\n");
    await this.command(`INSERT INTO ${quoteTable(table)} FORMAT JSONEachRow\n${body}`);
    return rows.length;
  }

  /** First row, or undefined. */
  async queryRow<T = Record<string, unknown>>(
    sql: string,
    params: Record<string, string> = {},
  ): Promise<T | undefined> {
    const rows = await this.query<T>(sql, params);
    return rows[0];
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
